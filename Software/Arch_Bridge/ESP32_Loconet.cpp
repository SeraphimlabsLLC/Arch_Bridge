/*   IMPORTANT:
 *  Some of the message formats used in this code are Copyright Digitrax, Inc.
 */
#ifndef ESP32_LOCONET_H
  #include "ESP32_Loconet.h"
#endif
 
#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif
#ifndef ESP32_DCCEX_H
  #include "ESP32_dccex.h"
#endif

ESP_Uart LN_port; //Loconet uart object
LN_Class Loconet; //Loconet processing object
//extern DCCEX_Class dccex_port;
extern uint64_t time_us;

void LN_init(){//Initialize Loconet objects
  LN_UART //Initialize uart 
  return;
}

void LN_loop(){//reflector into LN_Class::loop_Process()
  Loconet.loop_process(); //Process and update Loconet
  return; 
}


void LN_Class::loop_process(){
  time_us = TIME_US;
  if (!((time_us - LN_loop_timer) > (LN_LOOP_DELAY_US))) { //Only scan at the interval specified in ESP32_Loconet.h
    return; 
  }
  LN_loop_timer = time_us; //Update last loop time
  uint8_t i = 0;
  if ((netstate == startup) || (netstate == disconnected)){
    //During startup interval, read the uart to keep it clear but don't process it. 
     uart_rx();
     LN_port.rx_read_processed = 255; //Mark as fully processed so it gets discarded.
    if ((time_us - Loconet.rx_last_us) > 250000) { 
      LN_port.rx_flush();
      LN_port.tx_flush();
      Serial.printf("Loconet start \n");
      netstate = active;
    }
    return;
  }
  //Network should be ok to interact with: 
  //Serial.printf("Loconet uart_rx cycle: \n");
  uart_rx(); //Read uart data into the RX ring
  //Serial.printf("Loconet rx_scan cycle: \n");
  rx_scan(); //Scan RX ring for an opcode
  //Serial.printf("Loconet rx_queue cycle: \n");
  rx_queue(); //Process queued RX packets and run rx_decode
  //Serial.printf("Loconet tx_queue cycle: \n");
  tx_queue(); //Try to send any queued packets
  //Serial.printf("Loconet loop complete \n");
  return;
} 

uint8_t LN_Class::uart_rx(){
  uint8_t read_size = 0;
  uint8_t i = 0;
  read_size = LN_port.uart_read(read_size); //populate rx_read_data and rx_data
  if (read_size > 0){ //Data was actually moved, update the timer.
    time_us = TIME_US;
    rx_last_us = time_us;
    Serial.printf("uart_rx has bytes: ");
    while (i< read_size) {
      Serial.printf("%x ", LN_port.rx_read_data[i]);
      i++;
    }
    Serial.printf("\n");  
  }
  return read_size;
}

void LN_Class::rx_scan(){ //Scan received data for a valid frame
  uint8_t i = 0;  
  uint8_t location; 
  time_us = TIME_US; //update time_us
  if (LN_port.rx_read_processed != 0) { //Data has already been processed. 
    return; 
  }
  last_rx_process = time_us; 
  //Serial.printf("RX_Scan: %u bytes read %u \n", LN_port.rx_read_len, i);
  while (i < LN_port.rx_read_len) {
    //Serial.printf("RX_Scan While, rxpending %u, rxbyte %x \n", rx_pending, LN_port.rx_read_data[i]);
    if (rx_pending < 0){ 
      //Serial.printf("No packet pending, i %u, byte %x \n", i, LN_port.rx_read_data[i]);
    }
    if ((LN_port.rx_read_data[i] & 0x80) && (LN_port.rx_read_data[i] != 255) && (rx_pending < 0)) { //we found an opcode 
      Serial.printf("Found opcode %x, ", LN_port.rx_read_data[i]);
      rx_pending = rx_packet_getempty(); //Get handle of next open packet
      
      Serial.printf("storing in rx slot %u \n", rx_pending);
      rx_packets[rx_pending]->state = 1; //Packet is pending data
      rx_packets[rx_pending]->rx_count = 0; //1st byte in packet. 
      rx_packets[rx_pending]->xsum = 0;
      rx_packets[rx_pending]->last_start_time = time_us; //Time this opcode was found
    }
    if (rx_pending >= 0) {//Packet is being processed.  
      rx_packets[rx_pending]->state = 2; //Packet being received.
      //Serial.printf("Processing packet %u. Time remaining (uS): %d \n", rx_pending, 15000 - (time_us - rx_packets[rx_pending]->last_start_time));
      if ((time_us - rx_packets[rx_pending]->last_start_time) > 15000) { //Packets must be fully received within 15mS of opcode detect
        Serial.printf("RX Timeout, dropping %u bytes from slot %u \n", rx_packets[rx_pending]->rx_count, rx_pending);
        rx_packets[rx_pending]->state = 4; //Set state to failed
        rx_packets[rx_pending]->last_start_time = time_us;
        rx_pending = -1; //Stop processing this packet.  
        i++;
        continue; //Skip the rest of this loop and start the next from while
      } 

      //Serial.printf("Checking for TX Loopback...");
      if ((tx_pending >= 0) && (rx_pending >= 0 )) { // && (time_us - tx_last_us) < (LN_BITLENGTH_US * LN_COL_BACKOFF))) {
        Serial.printf("rx_scan loopback test tx_pending %i, rx_pending %i \n", tx_pending, rx_pending);
        //We just sent a packet, this could be ours. 
        time_us = TIME_US;
        uint64_t started = tx_packets[tx_pending]->last_start_time;
        uint64_t elapsed = time_us - tx_packets[tx_pending]->last_start_time;
        uint64_t limit = LN_BITLENGTH_US * LN_COL_BACKOFF + LN_BITLENGTH_US * 8 * tx_packets[tx_pending]->data_len + TX_DELAY_US;
        Serial.printf("TX Time: started %u ", started);
        Serial.printf("elapsed %u ", elapsed);
        Serial.printf("limit %u \n", limit);
        if ((time_us - started) <= (limit)) {
          if (tx_loopback() == 0) { //Check if the data so far matches what we sent. Drop from both queues once complete or collision.  
            i++;
            continue;
          }
        }
      }
      location = rx_packets[rx_pending]->rx_count;
      //Serial.printf("Copying data %x position %u in packet %u \n", LN_port.rx_read_data[i], rx_packets[rx_pending]->rx_count, rx_pending);
      rx_packets[rx_pending]->data_ptr[location] = LN_port.rx_read_data[i]; //Copy the byte being read
      if ((rx_packets[rx_pending]->rx_count) == 1){ //Fix packet size now that both bytes 0 and 1 are present 
        rx_packets[rx_pending]->packet_size_check(); 
      }
      //Serial.printf("Checking current size %u against intended size %u \n", rx_packets[rx_pending]->rx_count + 1, rx_packets[rx_pending]->data_len);
      rx_packets[rx_pending]->rx_count++;
      if (!((rx_packets[rx_pending]->rx_count) < rx_packets[rx_pending]->data_len)) { 
        rx_packets[rx_pending]->state = 3; //Packet has been fully sent to this device.
        rx_packets[rx_pending]->last_start_time = time_us; //Time it was set to this state.
        //Serial.printf("RX slot %u completed packet \n", rx_pending);
        if (!(rx_packets[rx_pending]->Read_Checksum())){//Checksum was invalid. Drop the packet. 
          Serial.printf("Failed packet %u, RX checksum invalid \n", rx_pending); 
          rx_packets[rx_pending]->state = 4; //Set state to failed
          rx_packets[rx_pending]->last_start_time = time_us;
          rx_pending = -1; //Stop processing this packet. 
        }
        rx_pending = -1; 
        i++;
        continue; 
      }  
      //Serial.printf("Packet %u contains %u bytes \n", rx_pending, rx_packets[rx_pending]->rx_count);
    }
    i++;
    //Serial.printf("Beginning while loop i = %u, rx_read_len = %u \n", i, LN_port.rx_read_len);  
  }
  LN_port.rx_read_processed = 255; //Mark as fully processed. 
  
  return; 
}  

void LN_Class::rx_queue(){ //Loop through the RX queue and process all packets in it. 
  uint8_t i = 0;
  uint8_t complete = 0;
  time_us = TIME_US;

  while (i < LN_RX_Q){
    //Serial.printf("RX_Queue. i = %u, LN_RX_Q = %u \n", i, LN_RX_Q);
    if (rx_packets[rx_next_decode]) { //Packet exists, check it.
      //Serial.printf("Queue Processing RX packet %u, state is %u \n", rx_next_decode, rx_packets[rx_next_decode]->state);
      switch (rx_packets[rx_next_decode]->state) {
        case 1:
        case 2:
          if (time_us - rx_packets[rx_next_decode]->last_start_time > 15000) { //Pending or Attempting must be finished within 15mS since their state last changed.
            Serial.printf("RX_Q Cleaning up incomplete packet %u \n", rx_next_decode);
            rx_packets[rx_next_decode]->reset_packet();
            if (rx_next_decode == rx_pending) {
              rx_pending = -1;
            } 
          }          
          break;
        case 3: 
          complete = rx_decode(rx_next_decode); 
          if (complete == 0) {
            rx_packets[rx_next_decode]->state == 5; //Completed. Can delete it. 
          }

        case 5:
          LN_port.uart_rx_flush(); //Failed packet. 
        case 4: 
          Serial.printf("RX_Q Cleaning up complete or failed packet %u \n", rx_next_decode);
          rx_packets[rx_next_decode]->reset_packet(); 
          if (rx_next_decode == rx_pending) {
              rx_pending = -1;
          }
      }
    }
    rx_next_decode++;
    if (rx_next_decode >= LN_RX_Q){
      rx_next_decode = 0; 
    }
    i++;
  }
  //Serial.printf("RX_QUEUE Done \n");
  return;
}

uint8_t LN_Class::rx_decode(uint8_t rx_pkt){  //Opcode was found. Lets use it.
  //Finally processing the packet.
  //Serial.printf("Processing received packet from RX_Q slot %u \n", rx_pkt);
  
  char opcode = rx_packets[rx_pkt]->data_ptr[0];
  uint8_t i = 0;
  rx_packets[rx_pkt]->state = 5;
  switch (opcode) {
    
    //2 byte opcodes:
    case 0x81:  //Master Busy
      break;
    case 0x82: //Global power off
      Serial.print("Power off requested \n"); 
      //dccex.power(false);
      break;
    case 0x83: //Global power on
      Serial.print("Power on requested \n");
      //dccex.power(true);
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
      //DCCEX: <H addr state>, eg <H 1 1> for turnout 1 thrown
      rx_req_sw(rx_pkt);
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
      Serial.printf("Slot move %u to %u \n", rx_packets[rx_pkt]->data_ptr[1], rx_packets[rx_pkt]->data_ptr[2]); 
      slot_move(rx_packets[rx_pkt]->data_ptr[1], rx_packets[rx_pkt]->data_ptr[2]); //Process slot data move
      slot_read(rx_packets[rx_pkt]->data_ptr[2]); //Read the moved/modified data  back to the source
      break; 
    case 0xBB: //Request SLOT DATA/status block
      Serial.printf("Throttle requesting slot %u data \n", rx_packets[rx_pkt]->data_ptr[1]);
      slot_read(rx_packets[rx_pkt]->data_ptr[1]); //Read slot data out to Loconet  
      break;
    case 0xBC: //REQ state of SWITCH
      break;   
    case 0xBD: //REQ SWITCH WITH acknowledge function (not DT200
      break;
    case 0xBF: //;REQ loco AD
      Serial.printf("Requested loco %u found/set in slot %u \n", rx_packets[rx_pkt]->data_ptr[0], i); 
      slot_read(rx_packets[rx_pkt]->data_ptr[1]); //Read the slot back to the source. 
    
      break;                                   
    //6 byte opcodes, none known as of Oct 2023: 

    //Variable byte opcodes: 
    case 0xE7: //Slot read data
      
      break;
    case 0xEF: //Slot write data
      slot_write(rx_packets[rx_pkt]->data_ptr[2], rx_pkt); //Accept slot write data and send Long Ack
      break; 
    
    default: 
    Serial.printf("No match for %x \n", rx_packets[rx_pkt]->data_ptr[0]);
    rx_packets[rx_pkt]->state = 4;
  }
  return 0;
}

void LN_Class::tx_queue(){ //Try again to send queued packets on each loop cycle
  uint8_t i = 0;
  uint8_t priority = 0;
  time_us = TIME_US;
  if (tx_pending > -1) { //A packet is already active. Send it, then scan the rest of the queue. 
    //tx_send(tx_pending); 
    priority = tx_packets[tx_pending]-> priority;
    tx_next_send = tx_pending;
  }
  //Serial.printf("TX_Q priority loop \n");
  while (priority <= LN_MAX_PRIORITY){ 
    while (i < LN_TX_Q) {
      //Serial.printf("TX_Q. priority = %u i = %u, tx_next_send %u, LN_TX_Q = %u \n", priority, i, tx_next_send, LN_TX_Q);
      
      if (tx_packets[tx_next_send]) { //Packet exists, check it.
        //Serial.printf("TX_Q Processing TX packet %u, state is %u \n", tx_next_send, tx_packets[tx_next_send]->state);
        switch (tx_packets[tx_next_send]->state) {
          case 0: //No action needed, empty slot. 
          break; 
          case 1: //Pending. 
            if (((tx_pending < 0) || tx_pending == tx_next_send) && (priority == tx_packets[tx_next_send]-> priority)) { //Clear to try sending it.
              tx_pending = tx_next_send;  
              tx_send(tx_pending);   
            }
          break; 
          case 2: //Attempting
            if (tx_next_send != tx_pending) {  //A packet in this state that doesn't match tx_pending is a problem.
              tx_packets[tx_next_send]->state = 4;
            }
          case 3: //Sent. If it doesn't loop back before window expiry, do something about it. 
            if (time_us - tx_packets[tx_next_send]->last_start_time > 15000) { //Attempting must be finished within 15mS since their state last changed.
              tx_packets[tx_next_send]->state = 4;
              tx_packets[tx_next_send]->tx_attempts--;
              tx_packets[tx_next_send]->priority--; //Count down priority for next attempt.
              if (tx_packets[tx_next_send]->priority < LN_MIN_PRIORITY) {//Enforce priority floor
                tx_packets[tx_next_send]->priority = LN_MIN_PRIORITY;
              }
              tx_pending = -1;
            }
          break; 
          case 4: //Failed. 
            if (tx_packets[tx_next_send]->tx_attempts <= 0) {
              Serial.printf("TX_Q Unable to transmit packet from TX queue index %u, dropping. \n", tx_next_send);
              tx_packets[tx_next_send]->reset_packet();     
              if (tx_next_send == tx_pending) {
                tx_pending = -1;
              }
            } else {
              if (((tx_pending < 0) || tx_pending == tx_next_send) && (priority == tx_packets[tx_next_send]-> priority)) { //Clear to try sending it again
                tx_pending = tx_next_send;  
                tx_send(tx_next_send);   
              }
            }
            break;  
          case 5: //Success? Either way we don't need it anymore. 
            tx_packets[tx_next_send]->reset_packet(); 
            if (tx_next_send == tx_pending) { //This should never be needed, but just in case. 
                tx_pending = -1;
            }
        }
      }
      i++;
      tx_next_send++; 
      if (tx_next_send >= LN_TX_Q){ 
        tx_next_send = 0; 
      }
    }
    i = 0;
    priority++; 
  }
  return;
}

void LN_Class::tx_send(uint8_t txptr){
  //Note: tx_priority from LN_Class needs to be replaced by per-packet priority handling. 
  time_us = TIME_US;
  uint8_t i = 0;
  if (!tx_packets[txptr]) { //If the handle is invalid, abort.
    return; 
  }
  
  if ((time_us - tx_packets[txptr]->last_start_time) < (tx_pkt_len * LN_BITLENGTH_US * 8)) {//There is likely data already being sent. Don't add more.
    Serial.printf("Buffer should still have data, send wait \n");
    return;    
  }
   Serial.printf("TX_Send Pending Packet %u has mark %u \n", txptr, tx_packets[txptr]->state);
  if ((tx_packets[txptr]->state == 1) || (tx_packets[txptr]->state == 4)) { //Packet is pending or failed. Mark attempting.
    tx_packets[txptr]->last_start_time = time_us;
    tx_packets[txptr]->state = 2; //Attempting
    Serial.printf("Packet %u changed from 1 or 4 to  %u \n", txptr, tx_packets[txptr]->state);
  }
  
  if ((time_us - rx_last_us) > (LN_BITLENGTH_US * (tx_packets[txptr]->priority + LN_COL_BACKOFF))){
    //Serial.printf("Sending packet %u \n", txptr); 
    rx_last_us = time_us; //Force it to prevent looping transmissions
    tx_pending = txptr; //Save txpending for use in tx_loopback
   
    i = 0;
    tx_pkt_len = tx_packets[txptr]->data_len; //Track how much data is to be sent for overrun prevention.
    if (tx_packets[txptr]->state == 2) { //Only send if it isn't in state attempting. 
      char writedata[tx_pkt_len]; 
      Serial.printf("Transmitting: ");
      while (i < tx_pkt_len){ 
        writedata[i] = tx_packets[txptr]->data_ptr[i];
        Serial.printf("%x ", tx_packets[txptr]->data_ptr[i]);
        i++;
     }
      Serial.printf("\n"); 
      tx_packets[txptr]->last_start_time = time_us;  
      LN_port.uart_write(writedata, tx_pkt_len);
      tx_packets[txptr]->state = 3; //sent 
    }
  } 
  return;
}

uint8_t LN_Class::tx_loopback(){
  uint8_t delta = 0;
  uint8_t i = 0;
  Serial.printf("Loopback: ");  
  while (i <= rx_packets[rx_pending]->rx_count) {
    Serial.printf("%x: %x ", rx_packets[rx_pending]->data_ptr[i], tx_packets[tx_pending]->data_ptr[i]);
    if (rx_packets[rx_pending]->data_ptr[i] != tx_packets[tx_pending]->data_ptr[i]){ //Compare rx data to tx data
      delta++;   
    }
    i++;  
  }
  Serial.printf("\n"); 
 
  if (delta == 0) {//No differences in the data given
    if (i == tx_packets[tx_pending]->data_len) { //Packet is complete with 0 differences. Can remove from both queues. 
      Serial.printf("Transmission %d confirmed in %d \n", tx_pending, rx_pending);
      tx_packets[tx_pending]->reset_packet(); 
      rx_packets[rx_pending]->reset_packet();
      tx_pending = -1;
      rx_pending = -1;  
    }
    return delta;
  } else { //Collision. Transmit break, drop from rx_packet, and decrement tx_attempts to drop if stale.  
    Serial.printf("Collision detected. \n");
    transmit_break();
    tx_packets[tx_pending]->tx_attempts--; 
    tx_packets[tx_pending]->state = 4; 
    if (tx_packets[tx_pending]->tx_attempts <= 0) {
      Serial.printf("Unable to transmit packet from slot %u, dropping. \n", tx_pending);   
      tx_packets[tx_pending]->reset_packet(); 
    }
    tx_pending = -1;
    rx_packets[rx_pending]->reset_packet(); 
    rx_pending = -1; 
  }
  return delta;
}

void LN_Class::transmit_break(){
  //Write 15 bits low for BREAK on collision detection.  
  char txbreak[2] {char(0x00), char(0x01)};
  LN_port.uart_write(txbreak, 2);
  return;
}
bool LN_Class::receive_break(uint8_t break_ptr){ //Possible BREAK input at ptr. 
  bool rx_break = false;
  if (rx_break == true) {
  //TODO: Flush RX_Q
  }
  return rx_break;
}

uint8_t LN_Class::rx_packet_getempty(){ //Scan rx_packets and return 1st empty slot
  uint8_t count = 0; 
  while (count < LN_RX_Q){  
    if (!rx_packets[rx_next_new]){ //Isn't initialized yet, fix this
      rx_packets[rx_next_new] = new LN_Packet();
    }
    if (rx_packets[rx_next_new]->state == 0) { //Exists and is marked empty, claim it.  
      break; //break out of the while loop and use this result. 
    }
    rx_next_new++; 
    if (rx_next_new > LN_RX_Q) {
      rx_next_new = 0;
    }
  }
  if (count == LN_RX_Q) { //Checked all slots without a result
    Serial.printf("WARNING: rx_packets out of space. RX packet %d will be overwritten, consider increasing LN_RX_Q \n", rx_next_new);
  }
  rx_packets[rx_next_new]->reset_packet(); //Sets it to defaults again 
  if (rx_next_new == rx_pending){ //Should not execute, but prevents crashes if this situation happens. 
    rx_pending = -1;  
  }
  return rx_next_new;
}

void LN_Class::rx_packet_del(uint8_t index){ //Delete a packet after it has been processed. 
  
  Serial.printf("Deleting rx_packets[%u]\n", index);
  delete rx_packets[index]; //Invokes the destructor
  rx_packets[index] = NULL; //This is necessary to prevent repeated deallocations. 
 
  return;
}

uint8_t LN_Class::tx_packet_getempty(){ //Scan tx_packets and return 1st empty slot above slot 1
  uint8_t count = 0;
  while (count < LN_TX_Q) {//Iterate through possible arrays
    if (!tx_packets[count]) {//Doesn't exist yet, we can take this slot.
      //Serial.printf("Found uninitialized slot tx_packets[%u]\n", count);
      tx_packets[count] = new LN_Packet();
    }
    if (tx_packets[count]->state == 0) { //Although it already exists, it is in the empty state. Claim it. 
      Serial.printf("Reusing slot tx_packets[%u]\n", count);
        tx_packets[count]->reset_packet();
        if (count == tx_pending) { //Shouldn't happen but just in case. 
          tx_pending = -1;
        }
        return count; 
    }
    count++;
  }
  if (count == LN_TX_Q) { //Checked all slots without a result
    Serial.printf("WARNING: tx_packets out of space. TX packet %d will be overwritten, consider increasing LN_TX_Q \n", count);
  }
  return count;
}

void LN_Class::tx_packet_del(uint8_t index){ //Delete a packet after confirmation of sending
  Serial.printf("Deleting tx_packets[%u] \n", index);
  delete tx_packets[index];
  tx_packets[index] = NULL; //This is necessary to prevent repeated deallocations. 
  return;
}

void LN_Class::rx_req_sw(uint8_t rx_pkt){
  uint16_t addr = 0; 
  uint8_t cmd = 0;
  addr = (((rx_packets[rx_pkt]->data_ptr[2]) & 0x0F) << 8) | (rx_packets[rx_pkt]->data_ptr[1] & 0x7F);
  cmd = ((rx_packets[rx_pkt]->data_ptr[2]) & 0xF0);
  Serial.printf("Req switch addr %u direction %u, output %u \n", addr, (cmd & 0x20) >> 5, (cmd & 0x10) >> 4);
  return;
}

void LN_Class::tx_req_sw(){

  return;
}

LN_Class::LN_Class(){ //Constructor, initializes some values.
  time_us = TIME_US;
  rx_pending = -1;
  tx_pending = -1;
  rx_next_new = 0; 
  rx_next_decode = 0;
  netstate = startup;
  rx_last_us = time_us;
  return;
}

/****************************************
 * Loconet Packet (LN_Packet) functions *
 ****************************************/
uint8_t LN_Packet::packet_size_check(){ //Check that a packet has a valid size.
  uint8_t packet_size = 0;
  uint8_t i = 0;

  //Serial.printf("Input byte %x \n", data_ptr[0]);
  packet_size= data_ptr[0] & 0x60; //D7 was 1, check D6 and D5 for packet length
  i = packet_size>>4; //1 + packetsize >> 4 results in correct packet_size by bitshifting the masked bits
  packet_size = i + 2; //After shifting right 4 bytes, add 2

  if (packet_size >= 6) { //variable length packet, read size from 2nd byte.
    packet_size = data_ptr[1];
    if (packet_size > 127) { //Invalid packet, truncate to 2 bytes so it fails checksum and is discarded. 
      packet_size = 2;  
    }   
  }
  //Serial.printf("Output Packet Size %u, data_len %u \n", packet_size, data_len);
    data_len = packet_size;
  return packet_size; 
}

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
  char xsum = 0x00;
  uint8_t i = 0;
  i = 0;
  //Serial.printf("Checksum: ");
  while (i < data_len){
    xsum = xsum ^ data_ptr[i];
    //Serial.printf("%x ", xsum);
    i++;
  }
  //Serial.printf("\n");
  
  if (xsum == 0xFF) {
    return true;
  }
  return false; 
}

void LN_Packet::reset_packet() {
  data_len = 127; 
  state = 0; 
  rx_count = 0;
  priority = 20; 
  tx_attempts = 15; //15 attempts to send before failed status
  return;
}

LN_Packet::LN_Packet (){ //Functionality moved to LN_Packet::Reset_Packet()
 state = 0; //Initialize to empty, change to new_packet after populating data
 data_len = 0; 
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
  tx_packets[tx_index]->state = 1;
  tx_packets[tx_index]->data_len = response_size;
  //Serial.printf("Preparing Loconet reply data \n");
  i = 0;
  /*
  while (i < response_size) {
   Serial.printf("%x ", tx_packets[tx_index]->data_ptr[i]);
   i++;
  }
  Serial.printf("\n");*/
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
  Serial.printf("Response: ");
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
void LN_Class::slot_write(int8_t slotnumber, uint8_t rx_pkt){ //Handle slot writes
  uint8_t i = 0;
  uint8_t response_size = 4;
  uint8_t tx_index = 0;
  uint8_t ack = 0;
  if (slotnumber < 120) { 
    while (i < 10) {
      slot_ptr[slotnumber]->slot_data[i] = rx_packets[rx_pkt]->data_ptr[i + 3]; //Copy values from slot_data
      i++;
    }
    ack = 0xff; //Find out what the correct ACK code is
  }
  i = 0;  
  tx_index = tx_packet_getempty();
  tx_packets[tx_index]->state = 1;
  tx_packets[tx_index]->data_len = response_size;
  tx_packets[tx_index]->data_ptr[0] = 0xB4; //Long Acknowledge
  tx_packets[tx_index]->data_ptr[1] = 0xEF; //Opcode being given LACK 
  tx_packets[tx_index]->data_ptr[2] = ack;// ACK1 byte
  tx_packets[tx_index]->Make_Checksum(); //Populate checksum
  i = 0;
  /*
  while (i < response_size) {
    Serial.printf("%x ", tx_packets[tx_index]->data_ptr[i]);
    //LN_port.tx_data[LN_port.tx_write_ptr + i] = tx_packets[tx_index]->data_ptr[i];
    i++;
  }*/
  //LN_port.tx_data[LN_port.tx_write_ptr] = LN_port.tx_data[LN_port.tx_write_ptr] + i; 
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
  time_us = TIME_US;
  run_time = (time_us - fastclock_start) * slot_ptr[123]->slot_data[0]; //Elapsed time * clock multiplier

  //Todo: Human readable time conversion from run_time in uS + what it was set to last. 

  return; 
}

uint8_t LN_Class::slot_new(uint8_t index) { //Initialize empty slots
  time_us = TIME_US;
  if (!(slot_ptr[index])){
    Serial.printf("Initializing Loconet slot %u \n", index);
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
      slot_ptr[123]->last_refresh = time_us; //Store when it was set last.   
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
  time_us = TIME_US;
  last_refresh = time_us;
  
  return;
}

LN_Slot_data::~LN_Slot_data(){ //Destructor

  return;
}
