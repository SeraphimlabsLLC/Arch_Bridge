 /*   IMPORTANT:
 *  Some of the message formats used in this code are Copyright Digitrax, Inc.
 */

#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif
#ifndef ESP32_LOCONET_H
  #include "ESP32_Loconet.h"
#endif
#ifndef ESP32_DCCEX_H
  #include "ESP32_dccex.h"
#endif

//Constants that shouldn't be changed.
#define LN_BITLENGTH_US 60 
#define LN_COL_BACKOFF 20
#if LN_PRIORITY == MASTER 
  #define LN_MAX_PRIORITY 20
  #define LN_MIN_PRIORITY 0 
#endif
#if LN_PRIORITY == SENSOR
  #define LN_MAX_PRIORITY 6
  #define LN_MIN_PRIORITY 2 
#endif  
#if LN_PRIORITY == THROTTLE
  #define LN_MAX_PRIORITY 20
  #define LN_MIN_PRIORITY 6
#endif 

ESP_Uart LN_port; //Loconet uart object
LN_Class Loconet; //Loconet processing object

extern DCCEX_Class dccex_port;
extern uint64_t time_us; 

  /*Strategy: 
   * uart_rx() to move uart data to the rx ring
   * rx_scan() to Scan RX ring buffer for a byte > 0x80.
   * Read the command length bits, check if there is enough data in the buffer to match it
   * If yes, process it. If no, leave the RX read pointer at the opcode byte to try again later.
   * 
   * 
   * tx_encode() to validate an outgoing packet
   * tx_scan() to scan the rx data for the rx echo
   */

uint8_t LN_Class::uart_rx(){
  uint8_t read_size = 0;
  read_size = LN_port.uart_read(read_size); //read data into the ring buffer. read_size just because.
  if (read_size > 0){ //Data was actually moved, update the timer.
    rx_last_us = esp_timer_get_time();
  }
  return read_size;
}

void LN_Class::rx_scan(){ //Scan ring buffer data for an opcode and return its location
  bool eod = false;
  uint8_t i = 0;
  uint8_t packet_size;
  char xsum;
  last_rx_process = esp_timer_get_time();

 if ((LN_port.rx_read_ptr == LN_port.rx_write_ptr)) { //End of file
    eod = true;
  }
  
 if ((rx_last_us - rx_opcode_last_us) > (10 * LN_BITLENGTH_US) && (rx_opcode != 0x00)) { //Too long waiting for the rest of the packet. Move on. .
          LN_port.rx_read_ptr++;
          Serial.printf("Data timeout, discarding invalid packet \n");
          rx_opcode = 0x00;
          eod = true;
        }
  
  while (!(eod))  { //Scan until end of file is true
     
    if (LN_port.rx_data[LN_port.rx_read_ptr]) { //Check if this is a break and not actually data value 0.
      eod = receive_break(LN_port.rx_read_ptr); //receive_break returns true when resetting the buffer, which would make eod true as well.
      if (eod == true) {
        return;
      }
    }
    
    if ((LN_port.rx_data[LN_port.rx_read_ptr] & 0x80) && (LN_port.rx_data[LN_port.rx_read_ptr] != 255)) { //we found an opcode
      if (LN_port.rx_data[LN_port.rx_read_ptr] != rx_opcode){ //Only update rx_opcode_last if this is a new opcode
        rx_opcode_last_us = rx_last_us; 
        rx_opcode = LN_port.rx_data[LN_port.rx_read_ptr];
      }

      Serial.printf("Found opcode %x at position %u \n", rx_opcode, LN_port.rx_read_ptr); 
      
      packet_size= rx_opcode & 0x60; //D7 was 1, check D6 and D5 for packet length
      //Serial.printf("Packet Size %d \n", packet_size);
      i = packet_size>>4; //1 + packetsize >> 4 results in correct packet_size by bitshifting the masked bits
      packet_size = i + 2; //After shifting right 4 bytes, add 2
      if (packet_size >= 6) { //variable length packet, read size from 2nd byte.  
        if ((LN_port.rx_write_ptr - LN_port.rx_read_ptr) < 1){   //Check that there is enough data for the specified type and buffer it
          Serial.print("Variable length packet hasn't sent size yet, waiting for next byte \n");
          return;
        }        
          packet_size = LN_port.rx_data[(LN_port.rx_read_ptr + 1)]; //Size is in 2nd byte of packet.
      }
      if (packet_size > 127) { //Invalid packet
        Serial.printf("Invalid size of packet with opcode at %d and size %u \n", LN_port.rx_read_ptr, packet_size);
        LN_port.rx_data[LN_port.rx_read_ptr]= 0x00; //Remove the offending byte from the ring    
        packet_size = 2;   
      }
      Serial.printf("Packet Size %d \n", packet_size);
      if ((LN_port.rx_write_ptr - LN_port.rx_read_ptr) < packet_size ){   //Check that there is enough data for the specified type and buffer it
        //Serial.printf("Malfunction, need input. Have %u bytes, need %u bytes \n", (LN_port.rx_write_ptr - LN_port.rx_read_ptr), (packet_size - 1));
        return;
      }

      last_rx_process = esp_timer_get_time();
      if ((rx_opcode == LN_port.tx_data[tx_opcode_ptr]) && ((last_rx_process - tx_last_us) < (LN_BITLENGTH_US * LN_COL_BACKOFF))) { 
      //We just sent this code, and should still have control of the network. Check if it matches. 
        Serial.printf("Packet echo, validating \n");
        i = tx_loopback(packet_size);
        if (i == 0) {//It matched our packet.
          LN_port.rx_read_ptr = LN_port.rx_read_ptr + packet_size; 
          rx_opcode = 0x00;
          return; 
        }
            
      }
       
      i = 1; //reuse the temp int
      xsum = LN_port.rx_data[LN_port.rx_read_ptr];
      //Serial.printf("Packet size %u Checksum calculation: %x \n", packet_size, xsum);
      while (i < packet_size) {
        xsum = xsum ^  LN_port.rx_data[LN_port.rx_read_ptr + i];
          //Serial.printf("%x, %u \n", xsum, LN_port.rx_read_ptr + i);
        i++;
      }
      //Serial.printf(" \n");
      if (xsum == 0xff) { //Valid packet. Send to rx_decode
         rx_decode(); //Decode incoming opcode 
      } else {      
        Serial.printf("Ignoring invalid packet with opcode %x and checksum %x at %d \n", rx_opcode, xsum, LN_port.rx_read_ptr);
        LN_port.rx_data[LN_port.rx_read_ptr]= 0x00; //Remove the offending byte from the ring 
        rx_opcode = 0x00;
      }
    } 
  if ((LN_port.rx_read_ptr == LN_port.rx_write_ptr)) { //End of file
    eod = true;
  } else {
    LN_port.rx_read_ptr++; //Read pointer will increment until an opcode is found.
    }
  }
  return;
}  
  
void LN_Class::rx_decode(){  //Opcode was found. Lets use it.
  //Finally processing the packet. 
  Serial.printf("Processing received packet \n");
  char txsum;
  uint8_t i = 0;
  switch (rx_opcode) {
    
    //2 byte opcodes:
    case 0x81:  //Master Busy
      break;
    case 0x82: //Global power off
      Serial.print("Power off requested \n");
      //DCCEX.send: <0>
      break;
    case 0x83: //Global power on
      Serial.print("Power on requested \n");
      //DCCEX.send: <1>
      break;
    case 0x85: //Force idle, broadcast estop
      Serial.print("ESTOP! \n");
      break; 
      
    //4 byte opcodes: 
    case 0xA0: //Unknown
      break;
    case 0xA1: //Unknown
      break;
    case 0xB0: //REQ SWITCH function
      break;
    case 0xB1: //Turnout SENSOR state REPORT
      break;  
    case 0xB2: //General SENSOR Input codes
      break;       
    case 0xB4: //;Long acknowledge 
      break;         
    case 0xB5: //WRITE slot stat1
      break;   
    case 0xB6: //SET FUNC bits in a CONSIST uplink elemen
      break; 

    //0xB8 to 0xBF require replies
    case 0xB8: //UNLINK slot ARG1 from slot ARG2
      break;  
    case 0xB9: //LINK slot ARG1 to slot ARG
      break;  
    case 0xBA: //MOVE slot SRC to DEST
      Serial.printf("Slot move %u to %u \n", LN_port.rx_data[LN_port.rx_read_ptr + 1], LN_port.rx_data[LN_port.rx_read_ptr + 2]); 
      slot_move(); //Process slot data move
      slot_read(); //Read the moved data  back to the source
      break; 
    case 0xBB: //Request SLOT DATA/status block
      Serial.printf("Throttle requesting slot %u data \n", LN_port.rx_data[LN_port.rx_read_ptr + 1]);
      void slot_read(); //Read slot data out to Loconet   
      break;
    case 0xBC: //REQ state of SWITCH
      break;   
    case 0xBD: //REQ SWITCH WITH acknowledge function (not DT200
      break;
    case 0xBF: //;REQ loco AD
      break;                                   
    //6 byte opcodes, none known as of Oct 2023: 

    //Variable byte opcodes: 
    case 0xE7: //Slot read data
    
      break;
    case 0xEF: //Slot write data
      void slot_write(); //Accept slot write data and send Long Ack
      break; 
    
    default: 
    Serial.printf("No match for %x \n", rx_opcode);
    
  }
  rx_opcode = 0x00; //Clear it since we already processed it. 
  return;
}

void slot_move(uint8_t slot_src, uint8_t slot_dest){//Handle slot moves

  return; 
}

void LN_Class::tx_send(){
  //Note: tx_priority from LN_Class needs to be replaced by per-packet priority handling. 
  uint64_t tx_time = esp_timer_get_time();
  uint8_t tx_priority = 0; 
  //Enforce priority by device role: 
/* Needs to be rewritten to use the new Packet handling!
  if (tx_priority > LN_MAX_PRIORITY) {
    tx_priority = LN_MAX_PRIORITY; 
  }
  if (tx_priority < LN_MIN_PRIORITY) {
    tx_priority = LN_MIN_PRIORITY; 
  } */  
  if ((tx_time - rx_last_us) > (LN_BITLENGTH_US * (tx_priority + LN_COL_BACKOFF))){
    //tx_failure = 25; //default to 25
    //tx_opcode_ptr = LN_port.tx_write_ptr; //This isn't the right spot!
    LN_port.uart_write(128); //Loconet has a 128 byte max packet size
    tx_last_us = esp_timer_get_time();  
  }
  return;
}

uint8_t LN_Class::tx_loopback(uint8_t packet_size){
  uint8_t delta = 0;
  uint8_t i;
 /*  NEEDS REWRITE FOR PACKET CLASS
  *  
  while (((LN_port.rx_read_ptr + i) != LN_port.rx_write_ptr) && (i != packet_size)) {
    if (LN_port.rx_data[LN_port.rx_read_ptr + i] != LN_port.tx_data[tx_opcode_ptr + i]){ //Compare rx data to tx data
      delta++;   
    }
  i++;  
  }
  if (delta > 0) { //Collision or corrupt packet. 
    Serial.printf("Collision or corrupt packet detected. \n");
    transmit_break();
    LN_port.rx_read_ptr = LN_port.rx_read_ptr + i;
    tx_failure--;     
  }*/ 
  return delta;
}

void LN_Class::transmit_break(){
  //Write 15 bits low for BREAK on collision detection.  
  return;
}
bool LN_Class::receive_break(uint8_t break_ptr){ //Possible BREAK input at ptr. 
  bool rx_break = false;
  if (rx_break == true) {
    LN_port.rx_read_ptr = LN_port.rx_read_ptr =0; //Because this is a ring buffer, if you make both pointers the same all data will be overwritten instead of read.  
  }
  return rx_break;
}

void ESP_LN_init(){//Initialize Loconet objects
  LN_UART //Initialize uart
  return;
}

void LN_Class::loop_process(){
  uint64_t time_us = esp_timer_get_time();
  uart_rx(); //Read data from uart into rx ring 
  if ((time_us - last_rx_process) > (LN_BITLENGTH_US * 10)){ //Its been at least 10 bit times since the last rx read, check for new data and process what came in.
    rx_scan(); //Scan rx ring for opcodes
  }
  //tx_send(); //send what we can
return;
}

uint8_t LN_Class::rx_packet_getempty(){ //Scan rx_packets and return 1st empty slot
  uint8_t index = 0;
  while (index < 32) {//Iterate through possible arrays
    if (!tx_packets[index]) {//Doesn't exist yet, we can take this slot.
      Serial.printf("Found uninitialized slot tx_packets[%u]\n", index);
      return index;
    }
    index++;
  }
  Serial.printf("WARNING: Could not find space in rx_packets, consider larger queue \n");
  return index;
}

uint8_t LN_Class::tx_packet_getempty(){ //Scan tx_packets and return 1st empty slot
  uint8_t index = 0;
  while (index < 32) {//Iterate through possible arrays
    if (!tx_packets[index]) {//Doesn't exist yet, we can take this slot.
      Serial.printf("Found uninitialized slot tx_packets[%u]\n", index);
      return index;
    }
    index++;
  }
  Serial.printf("WARNING: Could not find space in tx_packets, consider larger queue \n");
  return index;
}

void LN_Class::rx_packet_new(uint8_t index, uint8_t packetlen){ //Create new packet and initialize it
  if (!rx_packets[index]){ //Isn't initialized yet, fix this
    rx_packets[index] = new LN_Packet(packetlen);
  }
  if (!tx_packets[index]){ //We just initialized this. If it didn't work, warn.
    Serial.printf("WARNING: Could not initialize packet in rx_packets. \n");
  }
  return;
}

void LN_Class::tx_packet_new(uint8_t index, uint8_t packetlen){ //Create new packet and initialize it
  if (!tx_packets[index]){ //Isn't initialized yet, fix this
    tx_packets[index] = new LN_Packet(packetlen);
  }
  if (!tx_packets[index]){ //We just initialized this. If it didn't work, warn.
    Serial.printf("WARNING: Could not initialize packet in tx_packets. \n");
  }
  return;
}
/****************************************
 * Loconet Packet (LN_Packet) functions *
 ****************************************/

void LN_Packet::Make_Checksum(){
  char xsum = 0xFF;
  uint8_t i = 0;
  Serial.printf("Make Checksum: ");
  while (i < data_len - 1){ //The -1 is because the last byte is where we write the xsum to
    xsum = xsum ^ data_ptr[i];
    Serial.printf("%x ", xsum);
    i++; 
  }
  data_ptr[data_len - 1] = xsum;  
  Serial.printf(" \n"); 
  return;
}

bool LN_Packet::Read_Checksum(){
  char xsum = 0;
  uint8_t i = 0;
  Serial.printf("Read Checksum: ");
  while (i < data_len){
    xsum = xsum ^ data_ptr[i];
    Serial.printf("%x ", xsum);
    i++;
  }
  Serial.printf("\n");
  
  if (xsum == 0xFF) {
    return true;
  }
  return false; 
}

LN_Packet::LN_Packet (uint8_t datalen){ //Constructor needs length of packet to create the char[] for it;
 uint8_t i = 0;
 priority = 20; 
 state = empty; //Initialize to empty, change to new_packet after populating data
 tx_attempts = 15; //15 attempts to send before failed status
 data_len = datalen; //Store the data length used to initialize. 
 data_ptr = new char[datalen]; //Allocate the data packet
 /*while (i < data_len){
  data_ptr[i] = 0;
  i++;
 }*/
 return;
}

LN_Packet::~LN_Packet(){ //Destructor to make sure data_ptr gets deallocated again
  delete data_ptr; //Deallocate the data packet
  return;
}


/*****************************************
 * Slot Manager (LN_Slot_data) Functions *
******************************************/

void LN_Class::slot_read(){ //Handle slot reads
  uint8_t i = 0;
  uint8_t slot_number = LN_port.rx_data[LN_port.rx_read_ptr + 1]; //slot number
  uint8_t tx_index = 0;
  uint8_t response_size = 0;
  slot_new(slot_number); //Initialize slot if it isn't already. 
  if (slot_number == 123) { //Update fast clock slot
    fastclock_update();
    Serial.printf("Processing fast clock update. \n");
  }
  if (slot_number == 124) { //Programmer not supported.
    Serial.printf("Loconet Programmer not supported. \n"); 
    //Send <B4>,<7F>,<7F>,<chk> 
  }
  //Loconet response: 
  response_size = 14; //Packet is 14 bytes long, should be 0x0E
  tx_index = tx_packet_getempty();
  tx_packet_new(tx_index, response_size); 
  Serial.printf("Preparing Loconet reply data \n");
  i = 0;
  while (i < response_size) {
   Serial.printf("%x ", tx_packets[tx_index]->data_ptr[i]);
   i++;
  }
  Serial.printf("\n");
  tx_packets[tx_index]->data_ptr[0] = 0xE7; //OPC_SL_RD_DATA
  tx_packets[tx_index]->data_ptr[1] = response_size; 
  tx_packets[tx_index]->data_ptr[2] = slot_number;
  i = 3;
  while (i < response_size) {
    tx_packets[tx_index]->data_ptr[i] = slot_ptr[slot_number]->slot_data[i-3]; //Copy values from slot_data
    i++;
  }
  i = 0;
  while (i < response_size) {
    Serial.printf("%x ", tx_packets[tx_index]->data_ptr[i]);
    i++;
  }
  Serial.printf("\n");
  Serial.printf("Calculating checksum \n");
  tx_packets[tx_index]->Make_Checksum(); //Populate checksum
  i = 0;
  while (i < response_size) {
    Serial.printf("%x ", tx_packets[tx_index]->data_ptr[i]);
    LN_port.tx_data[LN_port.tx_write_ptr + i] = tx_packets[tx_index]->data_ptr[i];
    i++;
  }
  LN_port.tx_data[LN_port.tx_write_ptr] = LN_port.tx_data[LN_port.tx_write_ptr] + i; 
  Serial.printf("Response complete \n");
  Loconet.tx_send();
  return;
}
void LN_Class::slot_write(){ //Handle slot writes
  uint8_t slot_number = LN_port.rx_data[LN_port.rx_read_ptr + 2]; //slot number
  uint8_t i = 0;
  uint8_t response_size = 4;
  uint8_t tx_index = 0;
  uint8_t ack = 0;
  if (slot_number < 120) { 
    while (i < 10) {
      slot_ptr[slot_number]->slot_data[i] = LN_port.rx_data[LN_port.rx_read_ptr + i + 3]; //Copy values from slot_data
      i++;
    }
    ack = 0xff; //Find out what the correct ACK code is
  }
  i = 0;  
  tx_index = tx_packet_getempty();
  tx_packet_new(tx_index, response_size);
  tx_packets[tx_index]->data_ptr[0] = 0xB4; //Long Acknowledge
  tx_packets[tx_index]->data_ptr[1] = LN_port.rx_data[LN_port.rx_read_ptr]; //Opcode being given LACK 
  tx_packets[tx_index]->data_ptr[2] = ack;// ACK1 byte
  return;
}

void LN_Class::slot_move(){ //Handle slot moves
  uint8_t slot_src = LN_port.rx_data[LN_port.rx_read_ptr + 1]; //slot src
  uint8_t slot_dest = LN_port.rx_data[LN_port.rx_read_ptr + 2]; //slot destination
  if (slot_src == 0) {//Read dispatch slot data
    return;    
  }
  if (slot_src == slot_dest) { //NULL MOVE, a throttle is claiming this slot for use
    //Set status to IN_USE and read back the result.  
    slot_ptr[slot_src]->slot_data || 0x30; //D4 and D5 to 11, IN_USE
    return;
  }

  return;
}

void LN_Class::fastclock_update(){
  uint64_t run_time;
  uint8_t days; 
  uint8_t hours; 
  uint8_t minutes;
  time_us = esp_timer_get_time(); 
  run_time = (time_us - fastclock_start) * slot_ptr[123]->slot_data[0]; //Elapsed time * clock multiplier

  //Todo: Human readable time conversion from run_time in uS + what it was set to last. 

  return; 
}

uint8_t LN_Class::slot_new(uint8_t index) { //Initialize empty slots
  if (!(slot_ptr[index])){
    slot_ptr[index] = new LN_Slot_data; 
  }
  if (!(slot_ptr[index])){
    Serial.printf("Failure allocating slot %u \n", index);
    return 1; 
  }  
  //Implement special slot initialization
  if (index == 123) { //Fast Clock
      slot_ptr[index]->slot_data[0] = CLK_RATE; //Clock multiplier
      slot_ptr[index]->slot_data[3] = 256 - CLK_MINUTES;
      slot_ptr[index]->slot_data[4] = 256 - CLK_HOURS;
      slot_ptr[index]->slot_data[6] = 0; //Days since fast clock init
      slot_ptr[index]->slot_data[7] = 32; //Bit D6 = 1 for valid clock data
      fastclock_start = esp_timer_get_time();     
   }
  
  return 0;
}

uint8_t LN_Class::slot_del(uint8_t index) {
  delete slot_ptr[index];
  return 0; 
}


LN_Slot_data::LN_Slot_data(){ //Constructor
  uint8_t i = 0;
  while (i < 10) {
    slot_data[i] = 0; 
  }
  last_refresh = esp_timer_get_time();
  
  return;
}

LN_Slot_data::~LN_Slot_data(){ //Destructor

  return;
}
