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

void ESP_LN_init(){//Initialize Loconet objects
  LN_UART //Initialize uart 
  return;
}

void LN_Class::loop_process(){
  time_us = esp_timer_get_time();
  uint8_t i = 0;
  if ((netstate == startup) || (netstate == disconnected)){
    //Wait for 250mS restart timer before accepting data. 
    if ((time_us - Loconet.rx_last_us) > 250000) { 
      LN_port.uart_rx_flush();
      LN_port.rx_flush();
      LN_port.rx_flush();
      Serial.printf("Loconet start \n");
      netstate = active;
    }
    return;
  }
  //Network should be ok to interact with: 
  //Serial.printf("Loconet scan cycle: \n");
  uart_rx(); //Read uart data into the RX ring
  rx_scan(); //Scan RX ring for an opcode
  //rx_decode() and tx_loopback(); are run automatically as needed by rx_scan() when an opcode is found.
  tx_queue(); //Try to send any queued packets
  return;
} 

uint8_t LN_Class::uart_rx(){
  uint8_t read_size = 0;
  read_size = LN_port.uart_read(read_size); //read data into the ring buffer. read_size just because.
  if (read_size > 0){ //Data was actually moved, update the timer.
    rx_last_us = esp_timer_get_time();
    Serial.printf("Read %u bytes \n", read_size);
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
    //Since we have the chance to while there is no data, reset both pointers to 0. The rollover has been inelegant.
    LN_port.rx_read_ptr = 0;
    LN_port.rx_write_ptr = 0;
    eod = true;
  }
  
 if (((rx_last_us - rx_opcode_last_us) > (10 * LN_BITLENGTH_US)) && (rx_opcode != 0x00)) { //Too long waiting for the rest of the packet. Move on. .
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

      last_rx_process = esp_timer_get_time(); //Probably redundant. 
      uint8_t netcontrol = ((last_rx_process - tx_last_us) < (LN_BITLENGTH_US * LN_COL_BACKOFF));
      Serial.printf("Last RX %llu, Last TX %llu, CD %u \n", last_rx_process, tx_last_us, (LN_BITLENGTH_US * LN_COL_BACKOFF) );
      if ((tx_pending >= 0) && ((last_rx_process - tx_last_us) < (LN_BITLENGTH_US * LN_COL_BACKOFF))) { 
        //rx_opcode == LN_port.tx_data[tx_opcode_ptr]) //replaced by tx_pending > 0
        //We have a packet pending and should still have control of the network. See if this is ours.  
        Serial.printf("Validating packet echo \n");
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
      }
      rx_opcode = 0x00; //Done processing the opcode. Clear it out. 
    } 
  if ((LN_port.rx_read_ptr == LN_port.rx_write_ptr)) { //End of file
    //Since we have the chance to while there is no data, reset both pointers to 0. The rollover has been inelegant.
    LN_port.rx_read_ptr = 0;
    LN_port.rx_write_ptr = 0;
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
      slot_move(LN_port.rx_data[LN_port.rx_read_ptr + 1], LN_port.rx_data[LN_port.rx_read_ptr + 2]); //Process slot data move
      slot_read(LN_port.rx_data[LN_port.rx_read_ptr + 2]); //Read the moved/modified data  back to the source
      break; 
    case 0xBB: //Request SLOT DATA/status block
      Serial.printf("Throttle requesting slot %u data \n", LN_port.rx_data[LN_port.rx_read_ptr + 1]);
      slot_read(LN_port.rx_data[LN_port.rx_read_ptr + 1]); //Read slot data out to Loconet
      Serial.printf("Response finished \n");   
      break;
    case 0xBC: //REQ state of SWITCH
      break;   
    case 0xBD: //REQ SWITCH WITH acknowledge function (not DT200
      break;
    case 0xBF: //;REQ loco AD
      //i = loco_select(LN_port.rx_data[LN_port.rx_read_ptr + 2]); 
      Serial.printf("Requested loco %u found/set in slot %u \n", LN_port.rx_read_ptr + 2, i); 
      //slot_read(i); //Read the slot back to the source. 
    
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

void LN_Class::tx_queue(){ //Try again to send queued packets on each loop cycle 
  uint8_t queue_size; //Count of how many packets are ready to send
  uint8_t i = 0;
  uint8_t priority = 0;
  time_us = esp_timer_get_time(); 
  if (tx_pending >= 0) {
    //Serial.printf("TXQueue packet %u is marked %u \n", tx_pending, tx_packets[tx_pending]->state); 
    if (tx_packets[tx_pending]->state == 2) { //Try to send packet in attempting status
      if ((time_us - tx_packets[tx_pending]->tx_last_start) > 15000) { //Packet has been attempting for more than 15mS. Mark it failed.
        Serial.printf("TXQueue packet %u window timeout \n", tx_pending); 
        tx_packets[tx_pending]->state == 4; //Failed packet
        tx_packets[tx_pending]->tx_attempts--; 
        if (tx_packets[tx_pending]->tx_attempts <= 0) {
          Serial.printf("Unable to transmit packet from TX queue index %u, dropping. \n", tx_pending);
          tx_packet_del(tx_pending);     
        } 
        tx_packets[tx_pending]->priority--; //Count down priority for next attempt. 
        if (tx_packets[tx_pending]->priority < LN_MIN_PRIORITY) {//Enforce priority floor 
          tx_packets[tx_pending]->priority = LN_MIN_PRIORITY;
        }
        tx_pending = -1; //Stop trying to send this packet. 
      }
      Serial.printf("WTF why are we sending a %u \n", tx_packets[tx_pending]->state);
      tx_send(tx_pending);
    }
    if (tx_packets[tx_pending]->state == 3) { //Clean up packets that should have been seen in loopback by now
      
    }
    return;
  }

//Todo: Sort packets to put lowest priority byte first, leaving slots 0 and 1 for new packets
//For now just scan the queue for each priority from 0-20 and send the first valid slot we find. 
  while (priority <= LN_MAX_PRIORITY){ //
    while (i < LN_TX_Q){
      if (tx_packets[i]) { //Exists, check it
        Serial.printf("Queue scan checking packet %d \n", i);
        if ((tx_packets[i]->priority == priority) && tx_packets[i]->state != 3) //Priority match and state is not sent
          Serial.printf("Sending packet from tx_packets[%d] \n", i);
          tx_send(i);
          return;  
        if (tx_packets[i]->tx_attempts <= 0) {
          Serial.printf("Unable to transmit packet from slot %u, dropping. \n", tx_pending);      
        } 
        if ((tx_packets[i]->state == 3) && ((time_us - tx_packets[i]->tx_last_start) > (tx_packets[i]->data_len * LN_BITLENGTH_US + 15000))){ 
          //Clean up packets that should have been seen in loopback by now
          Serial.printf("TXQueue packet %u was not verified before window expiry \n", i);  
          tx_packets[i]->state == 4; //Failed packet
          tx_packets[i]->tx_attempts--; 
          tx_packets[i]->priority--; //Count down priority for next attempt. 
          if (tx_packets[i]->priority < LN_MIN_PRIORITY) {//Enforce priority floor 
            tx_packets[i]->priority = LN_MIN_PRIORITY;
          }
          tx_pending = -1; //Stop trying to send this packet. 
          if (tx_packets[i]->tx_attempts <= 0) {
            Serial.printf("Unable to transmit packet from TX queue index %u, dropping. \n", i);
            tx_packet_del(tx_pending);     
          } 
        }           
      }
    i++;  
    }
    priority++;
  }
  //No packets to send. 
  return;
}

void LN_Class::tx_send(uint8_t txptr){
  //Note: tx_priority from LN_Class needs to be replaced by per-packet priority handling. 
  time_us = esp_timer_get_time();
  uint8_t i = 0;
  if ((time_us - tx_last_us) < (tx_pkt_len * LN_BITLENGTH_US * 8)) {//There is likely data already being sent. Don't add more.
    Serial.printf("Buffer should still have data, send wait \n");
    return;    
  }
   Serial.printf("Pending Packet %u has mark %u \n", txptr, tx_packets[txptr]->state);
  if ((tx_packets[txptr]->state == 1) || (tx_packets[txptr]->state == 4)) { //Packet is pending or failed. Mark attempting.
    tx_packets[txptr]->tx_last_start = time_us;
    tx_packets[txptr]->state = 2; //Attempting
    Serial.printf("Packet %u changed from 1 or 4 to  %u \n", txptr, tx_packets[txptr]->state);
  }
  
  if ((time_us - rx_last_us) > (LN_BITLENGTH_US * (tx_packets[txptr]->priority + LN_COL_BACKOFF))){
    //Serial.printf("Sending packet %u \n", txptr); 
    rx_last_us = time_us; //Force it to prevent looping transmissions
    tx_pending = txptr; //Save txpending for use in tx_loopback
   
    i = 0;
    tx_pkt_len = tx_packets[txptr]->data_len; //Track how much data is to be sent for overrun prevention.
    char writedata[tx_pkt_len]; 
    Serial.printf("Transmitting: ");
    while (i < tx_pkt_len){ 
      LN_port.tx_data[Loconet.LN_port.tx_write_ptr + i] = tx_packets[txptr]->data_ptr[i];
      Serial.printf("%x ", tx_packets[txptr]->data_ptr[i]);
     // LN_port.tx_data[tx_write_ptr + i] = tx_packets[txptr]->data_ptr[i]; 
      i++;
    }
    //LN_port.tx_write_ptr = tx_write_ptr + i;
    Serial.printf("\n ");
    LN_port.tx_write_ptr = Loconet.LN_port.tx_write_ptr + i;  
    tx_last_us = esp_timer_get_time();  
    tx_pkt_len = LN_port.uart_write(128); //Loconet has a 128 byte max packet size. Save how many were written in tx_pkt_len
    //tx_pkt_len= LN_port.uart_raw_write(writedata);
    tx_packets[txptr]->state = 3; //sent
    Serial.printf("Sent Packet %u marked %u \n", txptr, tx_packets[txptr]->state);
  } 
  return;
}

uint8_t LN_Class::tx_loopback(uint8_t packet_size){
  uint8_t delta = 0;
  uint8_t i;
 //  NEEDS FURTHER REWRITE FOR RX PACKET CLASS
  Serial.printf("Loopback: \n");  
  while (((LN_port.rx_read_ptr + i) != LN_port.rx_write_ptr) && (i != packet_size)) {
    Serial.printf("%x, %x \n", LN_port.rx_data[LN_port.rx_read_ptr] + i, tx_packets[tx_pending]->data_ptr[i]);
    if (LN_port.rx_data[LN_port.rx_read_ptr + i] != tx_packets[tx_pending]->data_ptr[i]){ //Compare rx data to tx data
      delta++;   
    }
  i++;  
  }
  Serial.printf("\n"); 
  if (delta == 0) {//Confirmed send of packet. Remove from queue.
    Serial.printf("Transmission confirmed \n");
    tx_packet_del(tx_pending);    
  } else { //Collision or corrupt packet. 
    Serial.printf("Collision or corrupt packet detected. \n");
    transmit_break();
    LN_port.rx_read_ptr = LN_port.rx_read_ptr + i;
    tx_packets[tx_pending]->tx_attempts--; 
    if (tx_packets[tx_pending]->tx_attempts <= 0) {
      Serial.printf("Unable to transmit packet from slot %u, dropping. \n", tx_pending);
      tx_packet_del(tx_pending);      
    }
  }
  tx_pending = -1;
  return delta;
}

void LN_Class::transmit_break(){
  //Write 15 bits low for BREAK on collision detection.  
  char txbreak[2] {char(0x00), char(0x01)};
  LN_port.uart_raw_write(txbreak);
  return;
}
bool LN_Class::receive_break(uint8_t break_ptr){ //Possible BREAK input at ptr. 
  bool rx_break = false;
  if (rx_break == true) {
    LN_port.rx_read_ptr = LN_port.rx_read_ptr =0; //Because this is a ring buffer, if you make both pointers the same all data will be overwritten instead of read.  
  }
  return rx_break;
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
  uint8_t index = 2;
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
  rx_packets[index]->state = 0; //Empty slot
  return;
}

void LN_Class::tx_packet_new(uint8_t index, uint8_t packetlen){ //Create new packet and initialize it
  if (!tx_packets[index]){ //Isn't initialized yet, fix this
    tx_packets[index] = new LN_Packet(packetlen);
  }
  if (!tx_packets[index]){ //We just initialized this. If it didn't work, warn.
    Serial.printf("WARNING: Could not initialize packet in tx_packets. \n");
  }
  tx_packets[index]->state = 0; //Empty 
  tx_packets[index]->priority = LN_MAX_PRIORITY; 
  tx_packets[index]->tx_attempts = 25; 
  return;
}
void LN_Class::tx_packet_del(uint8_t index){ //Delete a packet after confirmation of sending
  Serial.printf("Deleting tx packet %d \n", index);
  delete tx_packets[index]->data_ptr; //Deallocate the data packet
  delete tx_packets[index];
  Serial.printf("Deleting tx packet %d deleted. \n", index);
  return;
}
LN_Class::LN_Class(){ //Constructor, initializes some values.
  time_us = esp_timer_get_time();
  rx_pending = -1;
  tx_pending = -1;
  netstate = startup;
  rx_last_us = time_us;
  return;
}

/****************************************
 * Loconet Packet (LN_Packet) functions *
 ****************************************/

void LN_Packet::Make_Checksum(){
  char xsum = 0xFF;
  uint8_t i = 0;
  //Serial.printf("Make Checksum: ");
  while (i < data_len - 1){ //The -1 is because the last byte is where we write the xsum to
    xsum = xsum ^ data_ptr[i];
    //Serial.printf("%x ", xsum);
    i++; 
  }
  data_ptr[data_len - 1] = xsum;  
  //Serial.printf(" \n"); 
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
 state = 0; //Initialize to empty, change to new_packet after populating data
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

int8_t LN_Class::loco_select(uint8_t low_addr){ //Return the slot managing this locomotive, or assign one if new. 
  int8_t slotnum = -1; //Slot numbers < 0 indicate failures
  uint8_t freeslotnum = 0; //First empty slot, we can note this while scanning for the address to save time
  uint8_t i = 1; //Since slot 0 is system
  while (i < 120) { //Scan slots to see if this loco is known
    if (((slot_ptr[slotnum] -> slot_data[1] && 0x30) == 0) && (freeslotnum == 0)){
      //Find first slot in free status
      freeslotnum = i;        
    }
    if (slot_ptr[slotnum] -> slot_data[0] == low_addr) { //Found it, return result
      return slotnum;
    }
  i++;
  }
  slotnum = freeslotnum;
  Serial.printf("Assigning locomotive %u to new slot number %u \n", low_addr, slotnum);
  return slotnum;
}

void LN_Class::slot_read(int8_t slotnumber){ //Handle slot reads
  uint8_t i = 0;
  uint8_t tx_index = 0;
  uint8_t response_size = 0;
  slot_new(slotnumber); //Initialize slot if it isn't already. 
  if (slotnumber == 123) { //Update fast clock slot
    fastclock_update();
    Serial.printf("Processing fast clock update. \n");
  }
  if (slotnumber == 124) { //Programmer not supported.
    Serial.printf("Loconet Programmer not supported. \n"); 
    //Send <B4>,<7F>,<7F>,<chk> 
  }
  //Loconet response: 
  response_size = 14; //Packet is 14 bytes long, should be 0x0E
  tx_index = tx_packet_getempty();
  tx_packet_new(tx_index, response_size); 
  //Serial.printf("Preparing Loconet reply data \n");
  i = 0;
  while (i < response_size) {
   //Serial.printf("%x ", tx_packets[tx_index]->data_ptr[i]);
   i++;
  }
  //Serial.printf("\n");
  tx_packets[tx_index]->data_ptr[0] = 0xE7; //OPC_SL_RD_DATA
  tx_packets[tx_index]->data_ptr[1] = response_size; 
  tx_packets[tx_index]->data_ptr[2] = slotnumber;
  i = 3;
  while (i < response_size) {
    tx_packets[tx_index]->data_ptr[i] = slot_ptr[slotnumber]->slot_data[i-3]; //Copy values from slot_data
    i++;
  }
  i = 0;
  //Serial.printf("Calculating checksum \n");
  tx_packets[tx_index]->Make_Checksum(); //Populate checksum
  i = 0;
  /*
  Serial.printf("Response: \n");
  while (i < response_size) {
    Serial.printf("%x ", tx_packets[tx_index]->data_ptr[i]);
    //LN_port.tx_data[LN_port.tx_write_ptr + i] = tx_packets[tx_index]->data_ptr[i];
    i++;
  }
  Serial.printf(" \n");
  */
  //LN_port.tx_data[LN_port.tx_write_ptr] = LN_port.tx_data[LN_port.tx_write_ptr] + i;
  tx_packets[tx_index]->state = 1; //Mark as pending packet
  tx_send(tx_index);
  return;
}
void LN_Class::slot_write(int8_t slotnumber){ //Handle slot writes
  //uint8_t slot_number = LN_port.rx_data[LN_port.rx_read_ptr + 2]; //slot number
  uint8_t i = 0;
  uint8_t response_size = 4;
  uint8_t tx_index = 0;
  uint8_t ack = 0;
  if (slotnumber < 120) { 
    while (i < 10) {
      slot_ptr[slotnumber]->slot_data[i] = LN_port.rx_data[LN_port.rx_read_ptr + i + 3]; //Copy values from slot_data
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
  tx_packets[tx_index]->Make_Checksum(); //Populate checksum
  i = 0;
  while (i < response_size) {
    Serial.printf("%x ", tx_packets[tx_index]->data_ptr[i]);
    LN_port.tx_data[LN_port.tx_write_ptr + i] = tx_packets[tx_index]->data_ptr[i];
    i++;
  }
  LN_port.tx_data[LN_port.tx_write_ptr] = LN_port.tx_data[LN_port.tx_write_ptr] + i; 
  tx_packets[tx_index]->state = 1; //Mark as pending packet
  tx_send(tx_index);
  return;
}

void LN_Class::slot_move(int8_t slot_src, int8_t slot_dest){ //Handle slot moves
  uint8_t i = 0;
  if (slot_src == 0) {//When activated it sets all locomotives to speed 0 functions off
    Serial.printf("Requested slot 0, stop the train \n");
    i = 1; 
    while (i < 120){
      if (slot_ptr[i]) {
        slot_ptr[i]->slot_data[1] = (slot_ptr[i]->slot_data[1] & 0xDF) | 0x10  ; //Force slot to state common
        slot_ptr[i]->slot_data[2] = 0; //speed 0
        slot_ptr[i]->slot_data[3] = 0; //Direction and functions off
      }
      i++;
    }
    return;    
  }
  if (slot_src == slot_dest) { //NULL MOVE, a throttle is claiming this slot for use
    //Set status to IN_USE and read back the result.  
    slot_ptr[slot_src]->slot_data[1] | 0x30; //D4 and D5 to 11, IN_USE
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
    Serial.printf("Initializing slot %u \n", index);
    slot_ptr[index] = new LN_Slot_data; 
  }
  if (!(slot_ptr[index])){
    Serial.printf("Failure allocating slot %u \n", index);
    return 1; 
  }  
  //Implement special slot initialization
  if (index == 123) { //Fast Clock
      slot_ptr[123]->slot_data[0] = CLK_RATE; //Clock multiplier
      slot_ptr[123]->slot_data[3] = 256 - CLK_MINUTES;
      slot_ptr[123]->slot_data[4] = 256 - CLK_HOURS;
      slot_ptr[123]->slot_data[6] = 0; //Days since fast clock init
      slot_ptr[123]->slot_data[7] = 32; //Bit D6 = 1 for valid clock data
      slot_ptr[123]->last_refresh = esp_timer_get_time(); //Store when it was set last.   
   }
  return index;
}

uint8_t LN_Class::slot_del(uint8_t index) {
  delete slot_ptr[index];
  return 0; 
}


LN_Slot_data::LN_Slot_data(){ //Constructor
  uint8_t i = 0;
  while (i < 10) {
    slot_data[i] = 0; 
    i++;
  }
  last_refresh = esp_timer_get_time();
  
  return;
}

LN_Slot_data::~LN_Slot_data(){ //Destructor

  return;
}
