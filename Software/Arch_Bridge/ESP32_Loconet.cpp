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
  extern DCCEX_Class dccex;
#endif
#ifndef ESP32_TIMER_H
  #include "ESP32_timer.h" //For fastclock access
  extern Fastclock_class Fastclock; 
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
//  if (!((time_us - LN_loop_timer) > (LN_LOOP_DELAY_US))) { //Only scan at the interval specified in ESP32_Loconet.h
//    return; 
//  }
//  LN_loop_timer = time_us; //Update last loop time
//  uint8_t i = 0;
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
  measure_time_start();
  rx_queue(); //Process queued RX packets and run rx_decode
  measure_time_stop();
  //Serial.printf("Loconet tx_queue cycle: \n");

#if FCLK_ENABLE == true //Only include the poller if the fastclock should be enabled. If false, the clock won't broadcast. 
  if (Fastclock.active == true) {
    if (!(slot_ptr[123])){ //Initialize fastclock slot if it doesn't exist yet. 
      slot_new(123);
    }
    if ((time_us - slot_ptr[123]->last_refresh) > slot_ptr[123]->next_refresh) { //Broadcast time 
      slot_read(123); //Broadcast Fast clock
    } 
  }
#endif 

  slot_queue(); //Clean up stale slots
 
  tx_queue(); //Try to send any queued packets
  //Serial.printf("Loconet loop complete \n");
  return;
} 

uint8_t LN_Class::uart_rx(){
  uint8_t read_size = 0;
  uint8_t i = 0;

  if (LN_port.rx_read_processed != 255) { //Data processing incomplete, don't read more.  
    return 0; 
  }
  LN_port.rx_read_data[i] = 0; 
  read_size = LN_port.uart_read(read_size); //populate rx_read_data and rx_data
  LN_port.rx_flush(); //Clear the uart after reading. 
  if (read_size > 0){ //Data was actually moved, update the timer.
    time_us = TIME_US;
    rx_last_us = time_us;
    Serial.printf("uart_rx has bytes in rx_read_data: ");
    for (i = 0; i < read_size; i++) {
      Serial.printf("%x ", LN_port.rx_read_data[i]);
    }
    Serial.printf("\n");  
  } 
  LN_port.rx_read_len = read_size; //Just to be sure it is correct. 
  return read_size;
}

void LN_Class::rx_scan(){ //Scan received data for a valid frame
  uint8_t rx_byte = 0;  
  time_us = TIME_US; //update time_us
  //Serial.printf("rx_scan LN_port.rx_read_processed %u \n", LN_port.rx_read_processed);
  if (LN_port.rx_read_processed != 0) { //Data has already been processed. 
    return; 
  }
  last_rx_process = time_us; 
  //Serial.printf("RX_Scan processing %u bytes, pending %i \n", LN_port.rx_read_len, rx_pending);
  for (rx_byte = 0; rx_byte < LN_port.rx_read_len; rx_byte++) {
    //Serial.printf("RX_Scan For, rx_pending %i, rxbyte %x, counter rx_byte %u \n", rx_pending, LN_port.rx_read_data[rx_byte], rx_byte);

//Detect opcode and packet start if there isn't already a packet open
    if ((LN_port.rx_read_data[rx_byte] & 0x80) && (rx_pending < 0)) { 
      rx_pending = rx_packet_getempty(); //Get handle of next open packet
      //Serial.printf("Starting new packet %i with opcode %x \n", rx_pending, LN_port.rx_read_data[rx_byte]);
      rx_packets[rx_pending]->state = 1; //Packet is pending data
      rx_packets[rx_pending]->rx_count = 0; //1st byte in packet. 
      rx_packets[rx_pending]->xsum = 0;
      rx_packets[rx_pending]->last_start_time = time_us; //Time this opcode was found
    }
    if ((rx_pending < 0) || (rx_packets[rx_pending]->state == 0) || (rx_packets[rx_pending]->state > 2)) {//No packet active or packet not in pending or attempting state
      Serial.printf("Not in packet or packet state not ok, going back to for with rx_byte %u and read_len %u \n", rx_byte, LN_port.rx_read_len);
      rx_pending = -1; 
      continue;
    }
     
//Populate packet:      
    uint64_t time_remain = (15000 - (time_us - rx_packets[rx_pending]->last_start_time));
    //Serial.printf("RX_Scan Processing byte %x from packet %u. ", LN_port.rx_read_data[i], rx_pending);
    //Serial.printf("RX_Scan Time remaining (uS): %u \n", time_remain);
    if ((time_us - rx_packets[rx_pending]->last_start_time) > 15000) { //Packets must be fully received within 15mS of opcode detect
      //show_rx_packet(rx_pending); //Print packet contents. 
      //Serial.printf("RX Timeout, dropping %u bytes from slot %u \n", rx_packets[rx_pending]->rx_count, rx_pending);
      rx_packets[rx_pending]->state = 4; //Set state to failed
      rx_packets[rx_pending]->last_start_time = time_us;
      rx_pending = -1; //Stop processing this packet.  
      continue; //Skip the rest of this loop and start the next from while
    } 

    //Copy RX byte into packet. 
    rx_packets[rx_pending]->data_ptr[rx_packets[rx_pending]->rx_count] = LN_port.rx_read_data[rx_byte]; //Copy the byte being read
    //Serial.printf("Copying data %x position %u in packet %u \n", rx_packets[rx_pending]->data_ptr[rx_packets[rx_pending]->rx_count], rx_packets[rx_pending]->rx_count, rx_pending);
    LN_port.rx_read_data[rx_byte] = 0; //Set the byte we just read to 0. Not strictly necessary, but to prevent duplicate reads. 

    //Check packet length once it becomes possible to. 
    if ((rx_packets[rx_pending]->rx_count) == 1){ //Fix packet size now that both bytes 0 and 1 are present 
      rx_packets[rx_pending]->packet_size_check();
      rx_packets[rx_pending]->state = 2; //Packet being received now has known size, change to state 2. 
      //Serial.printf("RX_Scan Packet Size set to %u \n", rx_packets[rx_pending]->data_len); 
    }
    rx_packets[rx_pending]->rx_count++;  
    
    //Packet isn't finished yet. Next iteration. 
    if ((rx_packets[rx_pending]->rx_count) < (rx_packets[rx_pending]->data_len)) {
      //Serial.printf("RX_Scan Packet Size %u, needs %u \n", rx_packets[rx_pending]->rx_count, rx_packets[rx_pending]->data_len); 
      continue; 
    }
    //Serial.printf("RX_Scan full packet %u with size %u has bytes %u \n", rx_pending, rx_packets[rx_pending]->data_len, rx_packets[rx_pending]->rx_count);
    //Packet is the length it should be. 
    rx_packets[rx_pending]->state = 3; //Packet has been fully sent to this device.
    rx_packets[rx_pending]->last_start_time = TIME_US; //Time it was set to this state.        

//TX Loopback processing: 
    //Serial.printf("RX_Scan loopback tx_pending %i rx_pending %i \n", tx_pending, rx_pending);
    if ((tx_pending > -1) && (rx_pending > -1)) { 
      if (tx_loopback() == 0) { //Check if the data so far matches what we sent. On match, mark both as success. 
        continue;
      }
    }
      
    if (rx_packets[rx_pending]->Read_Checksum()) { //Valid checksum, mark as received. 
      //Serial.printf("RX_Scan Completed packet %u ready to decode \n", rx_pending); 
      rx_packets[rx_pending]->state = 3; //Set state to received 
    } else {//Checksum was invalid. Drop the packet. 
      Serial.printf("RX_Scan Failed packet %u, RX checksum invalid \n", rx_pending); 
      rx_packets[rx_pending]->state = 4; //Set state to failed
    }
    rx_pending = -1; 
  }
  LN_port.rx_read_processed = 255; //Mark as fully processed.
  LN_port.rx_read_len = 0;  
  //Serial.printf("RX_Scan Complete \n");
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
            //Serial.printf("RX_Q Cleaning up incomplete packet %u \n", rx_next_decode);
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
          //LN_port.uart_rx_flush(); //Failed packet. 
        case 4: 
          //Serial.printf("RX_Q Cleaning up complete or failed packet %u \n", rx_next_decode);
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
  int8_t slotnum= -1; //Used for slot processing. 
  rx_packets[rx_pkt]->state = 5;
  switch (opcode) {
    
    //2 byte opcodes:
    case 0x81:  //Master Busy
      break;
    case 0x82: //Global power off
      Serial.print("Power off requested \n"); 
      LN_TRK = LN_TRK & 0xFE; //Set power bit off in TRK byte
       #if DCCEX_TO_LN == true //Only send if allowed to. 
      //dccex.power(false);
      #endif
      break;
    case 0x83: //Global power on
      Serial.print("Power on requested \n");
      LN_TRK = LN_TRK | 0x03; //Modify TRK byte, power on and clear estop. 
       #if DCCEX_TO_LN == true //Only send if allowed to. 
      //dccex.power(true);
      #endif
      break;
    case 0x85: //Force idle, broadcast estop
      Serial.print("<!> \n"); //DCC-EX Estop
      LN_TRK = LN_TRK & 0xFD; //Clear estop bit for global estop. 
       #if DCCEX_TO_LN == true //Only send if allowed to. 
       
       #endif
      break; 
      
    //4 byte opcodes: 
    case 0xA0: //Set slot speed
      slotnum = rx_packets[rx_pkt]->data_ptr[1];
      slot_ptr[slotnum]->slot_data[2] = rx_packets[rx_pkt]->data_ptr[2]; //Update speed byte
      slot_ptr[slotnum]->last_refresh = TIME_US;
      #if DCCEX_TO_LN == true //Only send if allowed to.
        dccex.tx_cab_speed(slot_ptr[slotnum]->slot_data[1] + uint16_t (slot_ptr[slotnum]->slot_data[6] << 7),
        slot_ptr[slotnum]->slot_data[2], bool(~(slot_ptr[slotnum]->slot_data[3]) & 0x20)); //Extract dir bit
      #endif
      break;
    case 0xA1: //Set slot dirf
      slotnum = rx_packets[rx_pkt]->data_ptr[1];
      slot_ptr[slotnum]->slot_data[3] = rx_packets[rx_pkt]->data_ptr[2]; //Update dirf byte
      slot_ptr[slotnum]->last_refresh = TIME_US;
      #if DCCEX_TO_LN == true //Only send if allowed to.
        dccex.tx_cab_speed(slot_ptr[slotnum]->slot_data[1] + uint16_t (slot_ptr[slotnum]->slot_data[6] << 7), 
        slot_ptr[slotnum]->slot_data[2], bool(~(slot_ptr[slotnum]->slot_data[3]) & 0x20)); 
      #endif       
      break;
    case 0xA2: //Set slot sound
      slotnum = rx_packets[rx_pkt]->data_ptr[1];
      slot_ptr[slotnum]->slot_data[3] = rx_packets[rx_pkt]->data_ptr[7]; //Update sound byte
      slot_ptr[slotnum]->last_refresh = TIME_US; 
      break;      
    case 0xB0: //REQ SWITCH function
      rx_req_sw(rx_pkt);
      break;
    case 0xB1: //Turnout SENSOR state REPORT
      break;  
    case 0xB2: //General SENSOR Input codes
      break;       
    case 0xB4: //Long acknowledge 
      
      break;         
    case 0xB5: //WRITE slot stat1
    
      break;   
    case 0xB6: //SET FUNC bits in a CONSIST uplink elemen
      break; 

      break;  
    case 0xB9: //LINK slot ARG1 to slot ARG
      break;  
    case 0xBA: //MOVE slot SRC to DEST
    //0xB8 to 0xBF require replies
    case 0xB8: //UNLINK slot ARG1 from slot ARG2
      Serial.printf("Slot move %u to %u \n", rx_packets[rx_pkt]->data_ptr[1], rx_packets[rx_pkt]->data_ptr[2]); 
      slotnum = slot_move(rx_packets[rx_pkt]->data_ptr[1], rx_packets[rx_pkt]->data_ptr[2]); //Process slot data move
      if (slotnum < 0) { //No free slot found. Send LACK fail. 
        send_long_ack(0xBA, 0);
        break;
      } else {
        slot_read(slotnum); //Read the modified data from dest
      }
      break; 
    case 0xBB: //Request SLOT DATA/status block
      slotnum = rx_packets[rx_pkt]->data_ptr[1];
      Serial.printf("Throttle requesting slot %u data \n", slotnum);
      slot_read(slotnum); //Read slot data out to Loconet  
      break; 
    case 0xBC: //REQ state of SWITCH. LACK response 7F for ok. 
    case 0xBD: //REQ SWITCH WITH acknowledge function (not DT200
      //send_long_ack(0xBD, 0x7F); 
      break;
    case 0xBF: //;Request Locomotive Address
      slotnum = loco_select(rx_packets[rx_pkt]->data_ptr[1], rx_packets[rx_pkt]->data_ptr[2]); //0xBF addr_h, addr_l, chk 
      if (slotnum < 0) { //No free slot found. Send LACK fail. 
        send_long_ack(0xBF, 0);
        break;
      }
      Serial.printf("Request for loco %u found/set in slot %u \n", rx_packets[rx_pkt]->data_ptr[2], slotnum);
      //slot_data[0] already set to free if this is a new assignment 
      slot_ptr[slotnum] -> slot_data[1] = rx_packets[rx_pkt]->data_ptr[2];
      slot_ptr[slotnum] -> slot_data[6] = rx_packets[rx_pkt]->data_ptr[1];
      slot_read(slotnum); //Read the slot back to the source. 
    
      break;                                   
    //6 byte opcodes, none known as of Oct 2023: 

    //Variable byte opcodes: 
    case 0xE7: //Slot read data
      slotnum = rx_packets[rx_pkt]->data_ptr[2];
      if (slotnum == 123) { //Set fast clock using sync from some other device. 
        slot_fastclock_set(rx_pkt);
      }
      //slot_read(slot_read);
      break;
    case 0xED: //Send Immeadiate Packet
      send_long_ack(0xED, 0);
      break; 
    case 0xEF: //Slot write data
    
      slotnum = slot_write(rx_packets[rx_pkt]->data_ptr[2], rx_pkt); //Accept slot write data and send Long Ack
      send_long_ack(0xEF, slotnum); //Send LACK with the result
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
    tx_send(tx_pending); 
    priority = tx_packets[tx_pending]-> priority;
    tx_next_send = tx_pending;
  }
  //Serial.printf("TX_Q priority loop \n");
  while (priority <= LN_MIN_PRIORITY){ 
    while (i < LN_TX_Q) {
      //Serial.printf("TX_Q. priority = %u i = %u, tx_next_send %u, LN_TX_Q = %u \n", priority, i, tx_next_send, LN_TX_Q);
      
      if (tx_packets[tx_next_send]) { //Packet exists, check it.
        //Serial.printf("TX_Q Processing TX packet %u, state is %u \n", tx_next_send, tx_packets[tx_next_send]->state);
        time_us = TIME_US; //Update time
        switch (tx_packets[tx_next_send]->state) {
          case 0: //No action needed, empty slot. 
          break; 
          case 1: //Pending. 
            if (((tx_pending < 0) || tx_pending == tx_next_send) && (priority == tx_packets[tx_next_send]-> priority)) { //Clear to try sending it.
              tx_pending = tx_next_send;  
              tx_send(tx_pending);
            }
          break; 
          case 2: //Attempting. Check 
            if (tx_next_send != tx_pending) {  //A packet in this state that doesn't match tx_pending is a problem.
              tx_packets[tx_next_send]->state = 4;
            } 
            if ((time_us - tx_packets[tx_next_send]->last_start_time) > 15000) { //Attempting must be finished within 15mS since their state last changed.
              Serial.printf("TX_Q tx_next_send failed to send after %u mS. %u attempts remain \n", (time_us - tx_packets[tx_next_send]->last_start_time) / 1000, tx_packets[tx_next_send]->tx_attempts);
              tx_packets[tx_next_send]->state = 4;
              tx_packets[tx_next_send]->tx_attempts--;
              tx_packets[tx_next_send]->priority--; //Increase priority for the next attempt.
              if (tx_packets[tx_next_send]->priority < LN_MAX_PRIORITY) {//Enforce priority limit
                tx_packets[tx_next_send]->priority = LN_MAX_PRIORITY;
              }
              if (tx_next_send == tx_pending) {
                tx_pending = -1;
              }
            }
            break; 
          case 3: //Sent. 
            if ((time_us - tx_packets[tx_next_send]->last_start_time) > TX_SENT_EXPIRE) { //Packet was sent long enough ago loopback should have seen it by now if it arrived ok. Clear it out if it still exists. 
              tx_packets[tx_next_send]->state = 4;
              tx_packets[tx_next_send]->tx_attempts = 0;
              Serial.printf("TX_Q Purging stale sent packet %u ", tx_next_send);
              Serial.printf("after %u uS \n", (time_us - tx_packets[tx_next_send]->last_start_time));
              tx_packets[tx_next_send]->reset_packet(); 
              if (tx_next_send == tx_pending) {
                Serial.printf("TX_Q Purged pending packet. This should not happen \n");
                tx_pending = -1;
              } 
            }
          
          break; 
          case 4: //Failed. 
            if (tx_packets[tx_next_send]->tx_attempts <= 0) {
              //Serial.printf("TX_Q Unable to transmit packet from TX queue index %u, dropping. \n", tx_next_send);
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
   //Serial.printf("TX_Send Pending Packet %u has mark %u \n", txptr, tx_packets[txptr]->state);
  if ((tx_packets[txptr]->state == 1) || (tx_packets[txptr]->state == 4)) { //Packet is pending or failed. Mark attempting.
    tx_packets[txptr]->last_start_time = time_us;
    tx_packets[txptr]->state = 2; //Attempting
    //Serial.printf("TX_Send Packet %u changed from 1 or 4 to  %u \n", txptr, tx_packets[txptr]->state);
  }
  
  if ((time_us - rx_last_us) > (LN_BITLENGTH_US * (tx_packets[txptr]->priority + LN_COL_BACKOFF))){
    //Serial.printf("Sending packet %u \n", txptr); 
    rx_last_us = time_us; //Force it to prevent looping transmissions
    tx_pending = txptr; //Save txpending for use in tx_loopback
   
    i = 0;
    tx_pkt_len = tx_packets[txptr]->data_len; //Track how much data is to be sent for overrun prevention.
    if (tx_packets[txptr]->state == 2) { //Only send if it isn't in state attempting. 
      char writedata[tx_pkt_len]; 
      Serial.printf("Transmitting %u: ", tx_pending);
      while (i < tx_pkt_len){ 
        writedata[i] = tx_packets[txptr]->data_ptr[i];
        Serial.printf("%x ", tx_packets[txptr]->data_ptr[i]);
        i++;
     }
      Serial.printf("\n"); 
      tx_packets[txptr]->last_start_time = time_us;  
      //LN_port.uart_write(writedata, tx_pkt_len);
      LN_port.uart_write(tx_packets[txptr]->data_ptr, tx_pkt_len);
      tx_packets[txptr]->state = 3; //sent 
    }
  } 
  return;
}

uint8_t LN_Class::tx_loopback(){
  uint8_t delta = 0;
  uint8_t i = 0;
  //Serial.printf("Loopback rx_pending %i : %i tx_pending \n", rx_pending, tx_pending);  
  //Serial.printf("RX Packet, %u bytes:", rx_packets[rx_pending]->rx_count);
  while (i <= rx_packets[rx_pending]->rx_count) {
    //Serial.printf(" %x", rx_packets[rx_pending]->data_ptr[i]);
    i++;
  }
  //Serial.printf(" \nTX Packet, %u bytes:", tx_packets[tx_pending]->data_len);
  i = 0; 
  while (i <= tx_packets[tx_pending]->data_len) {
    //Serial.printf(" %x", tx_packets[tx_pending]->data_ptr[i]);
    i++;
  }
  //Serial.printf("\n");
  i = 0;
  //Serial.printf("Comparing rx : tx for %u bytes \n", rx_packets[rx_pending]->rx_count);
  while ((i < rx_packets[rx_pending]->rx_count) && (i < tx_packets[tx_pending]->data_len)) {
    //Serial.printf(" %x:%x ", rx_packets[rx_pending]->data_ptr[i], tx_packets[tx_pending]->data_ptr[i]);
    if (rx_packets[rx_pending]->data_ptr[i] != tx_packets[tx_pending]->data_ptr[i]){ //Compare rx data to tx data
      Serial.printf("\n Found difference %x:%x at position %u \n", rx_packets[rx_pending]->data_ptr[i], tx_packets[tx_pending]->data_ptr[i], i);
      delta++;   
    }
    i++;  
  }
  //Serial.printf("\n"); 
 
  if (delta == 0) {//No differences in the data given
    if (rx_packets[rx_pending]->rx_count == tx_packets[tx_pending]->data_len) { //No differences in provided data. 
      if (rx_packets[rx_pending]->rx_count == tx_packets[tx_pending]->data_len) { //Packet is complete and intact. 
        Serial.printf("Transmission of packet %d confirmed in %d \n", tx_pending, rx_pending);
        tx_packets[tx_pending]->state = 5; //Mark TX complete
        rx_packets[rx_pending]->state = 5; //Mark RX complete
        tx_pending = -1;
        rx_pending = -1;  
      }
    }
    return delta;
  } else { //Collision. Transmit break, drop from rx_packet, and decrement tx_attempts to drop if stale.  
    Serial.printf("Collision detected, %u differences found. \n", delta);
    transmit_break();
    tx_packets[tx_pending]->tx_attempts--; 
    tx_packets[tx_pending]->state = 4; 
    tx_pending = -1;
    if (rx_packets[rx_pending]->rx_count == rx_packets[rx_pending]->data_len) { //If the complete packet is here, drop from rx_pending
      rx_packets[rx_pending]->state = 4;
      rx_pending = -1;
    } 
  }
  return delta;
}

void LN_Class::transmit_break(){
  //Write 15 bits low for BREAK on collision detection.  
  //char txbreak[2] {char(0x00), char(0x01)};
  //LN_port.uart_write(txbreak, 2);
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
    if (rx_next_new >= LN_RX_Q) {
      rx_next_new = 0;
    }
    count++; 
  }
  if (count == LN_RX_Q) { //Checked all slots without a result
    Serial.printf("WARNING: rx_packets out of space. RX packet %d will be overwritten, consider increasing LN_RX_Q \n", rx_next_new);
  }
  rx_packets[rx_next_new]->reset_packet(); //Sets it to defaults again 
//  if (rx_next_new == rx_pending){ //Should not execute, but prevents crashes if this situation happens. 
//    rx_pending = -1;  
//  }
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
      //Serial.printf("Reusing slot tx_packets[%u]\n", count);
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
  #if DCCEX_TO_LN == true //Only send if allowed to. 
  if (((cmd & 0x10) >> 4) == true) { //only forward if output is on. 
    dccex.tx_req_sw(addr, (cmd & 0x20) >> 5, (cmd & 0x10) >> 4);
  }
  #endif

  return;
}

void LN_Class::tx_req_sw(uint16_t addr, bool dir, bool state){
  uint8_t tx_index; 
  uint8_t sw1 = addr & 0x7F; //Truncate to addr bits 0-6
  uint8_t sw2 = ((addr >> 8) & 0x0F) | (0x10 * state) | (0x20 * dir); //addr bits 7-10 | (state * 16) | (dir * 32)
  tx_index = tx_packet_getempty();
  tx_packets[tx_index]->state = 1;
  tx_packets[tx_index]->priority = LN_MIN_PRIORITY; 
  tx_packets[tx_index]->data_len = 4;
  tx_packets[tx_index]->data_ptr[0] = 0xB0; //REQ_SW
  tx_packets[tx_index]->data_ptr[1] = sw1; 
  tx_packets[tx_index]->data_ptr[2] = sw2; 
  tx_packets[tx_index]->Make_Checksum(); //Populate checksum  
  //Don't send this one right away, let it go through the queue. 
  return;
}

void LN_Class::rx_cab(){

  return;
}
void LN_Class::tx_cab_speed(uint16_t addr, uint8_t spd, bool dir){

  return;
}

void LN_Class::send_long_ack(uint8_t opcode, uint8_t response) {
  uint8_t tx_index; 
  tx_index = tx_packet_getempty();
  tx_packets[tx_index]->state = 1;
  tx_packets[tx_index]->priority = LN_MIN_PRIORITY;
  tx_packets[tx_index]->data_len = 4;
  tx_packets[tx_index]->data_ptr[0] = 0xB4; //Long Acknowledge
  tx_packets[tx_index]->data_ptr[1] = opcode & 0x7F; //Opcode being given LACK & 0x7F to unset bit 7. 
  tx_packets[tx_index]->data_ptr[2] = response & 0x7F; //Response & 0x7F to make sure bit 7 isn't set. 
  tx_packets[tx_index]->Make_Checksum(); //Populate checksum
  if (tx_pending < 0) { //Needs to send ASAP, so try to send now. 
    tx_pending = tx_index; 
    tx_send(tx_index); 
  }
  return; 
}

void LN_Class::show_rx_packet(uint8_t index) { //Display a packet's contents
   uint8_t i = 0; 
   uint8_t pkt_len = rx_packets[index]->rx_count; 
   Serial.printf("Show_rx_packet: ");
   while (i < pkt_len) {
    Serial.printf("%x ", rx_packets[index]->data_ptr[i]); 
    i++;
   }
   Serial.printf(" \n");
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
  LN_TRK = 0x06; //Slot global TRK. 0x06 = Loconet 1.1 capable, track idle, power off. 
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
      //Serial.printf("Packet_size_check: Invalid packet size %u \n", packet_size);
      packet_size = 2;  
    }   
  } 
  //Serial.printf("packet_size_check %u, data_len %u \n", packet_size, data_len);
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
  //Serial.printf("Read_Checksum: ");
  while (i < data_len){
    xsum = xsum ^ data_ptr[i];
    //Serial.printf("%x ", xsum);
    i++;
  }
 // Serial.printf("\n");
  
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
  tx_attempts = 3; //15 attempts to send before failed status
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

int8_t LN_Class::loco_select(uint8_t high_addr, uint8_t low_addr){ //Return the slot managing this locomotive, or assign one if new. 
  int8_t slotnum = -1; //Slot numbers < 0 indicate failure to find 
  uint8_t freeslotnum = 0; //First empty slot, we can note this while scanning for the address to save time
  uint8_t i; //Skip slot 0 since it is system data.

 //Note: If high_addr > 0 there may be bit shifting required between high_addr and low_addr
  
  for (i = 1; i < 120; i++) { //Scan slots 1-120 to see if this loco is known
    if (!(slot_ptr[i])){ //Slot has not been initialized.
      if (freeslotnum == 0) { //If this is the first unused or free slot, take note.
        freeslotnum = i; 
      }
      continue; 
    }
    if (((slot_ptr[i] -> slot_data[0] && 0x30) == 0) && (freeslotnum == 0)){ //Note first slot in free status
      freeslotnum = i;        
    }
    if ((slot_ptr[i] -> slot_data[1] == low_addr) && (slot_ptr[i] -> slot_data[6] == high_addr)) { //Found this loco, return result
      //Serial.printf("Found locomotive %u in slot number %u \n", low_addr, i);
      return i;
    }
  }
  //Didn't find that loco in an existing slot. Assign one for it.
  slotnum = freeslotnum;
  if (!(slot_ptr[slotnum])){ //Empty slot. Initialize it. 
    slot_new(slotnum); 
  }
    slot_ptr[slotnum] -> slot_data[0] = 0; //Slot status set to free.
    slot_ptr[slotnum] -> slot_data[1] = low_addr;
    slot_ptr[slotnum] -> slot_data[6] = high_addr
    
    ;
  
  return slotnum;
}

void LN_Class::slot_read(int8_t slotnum){ //Handle slot reads
  uint8_t i = 0;
  uint8_t tx_index = 0;
  uint8_t response_size = 0;

  slot_new(slotnum); //Initialize slot if it isn't already. 
  if (slotnum == 123) { //Update fast clock slot
    slot_fastclock_get(); 
  }
  if (slotnum == 124) { //Programmer not supported. 
    //Serial.printf("Loconet Programmer not supported. \n"); 
    send_long_ack(0x7F, 0x7F); 
    return; 
  }
  //Loconet response: 
  response_size = 14; //Packet is 14 bytes long, should be 0x0E
  tx_index = tx_packet_getempty();
  tx_packets[tx_index]->state = 1;
  tx_packets[tx_index]->priority = LN_MAX_PRIORITY; 
  tx_packets[tx_index]->data_len = response_size;
  //Serial.printf("Preparing Loconet reply data \n");
  i = 0;
  /*
  while (i < response_size) {
   Serial.printf("%x ", tx_packets[tx_index]->data_ptr[i]);
   i++;
  }
  Serial.printf("\n");*/

  for (i = 0; i < 10; i++) {
    tx_packets[tx_index]->data_ptr[i + 3] = slot_ptr[slotnum]->slot_data[i]; //Copy values from slot_low_data

  }
  tx_packets[tx_index]->data_ptr[0] = 0xE7; //OPC_SL_RD_DATA
  tx_packets[tx_index]->data_ptr[1] = response_size; 
  tx_packets[tx_index]->data_ptr[2] = slotnum;
  tx_packets[tx_index]->data_ptr[7] = LN_TRK; //Loconet slot global status. Although slot_data[4] is defined, it isn't used. 
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
  
  if (tx_pending == -1) { //Send now if possible. 
    tx_send(tx_index);
  }
  return;
}
int8_t LN_Class::slot_write(int8_t slotnum, uint8_t rx_pkt){ //Handle slot writes
  uint8_t i = 0;
  uint8_t response_size = 4;
  uint8_t tx_index = 0;
  uint8_t ack = 0;
  if (!(slot_ptr[slotnum])) { 
    slot_new(slotnum); 
  }
  if (slotnum < 120) { 
    for (i = 0; i < 10; i++) {
      slot_ptr[slotnum]->slot_data[i] = rx_packets[rx_pkt]->data_ptr[i + 3]; //Copy values from slot_data
    }
    ack = 0x7f; //Find out what the correct ACK code is
  }
  if (slotnum == 123) { //Set fast clock
    slot_fastclock_set(rx_pkt); 
  }
  return ack;
}

int8_t LN_Class::slot_move(int8_t slot_src, int8_t slot_dest){ //Handle slot moves
  uint8_t i = 0;
  if (slot_src == 0) {//When activated it sets all locomotives to speed 0 functions off
    Serial.printf("Requested slot 0, stop the train \n");
    for (i = 1; i < 120; i++){
      if (slot_ptr[i]) {
        slot_ptr[i]->slot_data[1] = (slot_ptr[i]->slot_data[1] & 0xDF) | 0x10  ; //Force slot to state common
        slot_ptr[i]->slot_data[2] = 0; //speed 0
        slot_ptr[i]->slot_data[3] = 0; //Direction and functions off
      }
    }
    return 0;    
  }
  if ((slot_src > 120) || (slot_dest > 120)) { //Not allowed to move slots above 120
    return -1;
  }
  
  if (slot_src == slot_dest) { //NULL MOVE, a throttle is claiming this slot for use
    //Set status to IN_USE and read back the result.  
    slot_ptr[slot_dest]->slot_data[1] | 0x20; //D5 to 11, IN_USE
    slot_ptr[slot_dest]->next_refresh = 200000000; //200 seconds
    slot_ptr[slot_dest]->last_refresh = TIME_US;
    return slot_dest;
  }

  return slot_dest;
}



void LN_Class::slot_fastclock_set(uint8_t rx_pkt){
  const uint8_t slotnum = 123;
  slot_ptr[slotnum]->last_refresh = TIME_US;
  slot_ptr[slotnum]->slot_data[9] = rx_packets[rx_pkt]->data_ptr[12]; //ID2
  slot_ptr[slotnum]->slot_data[8] = rx_packets[rx_pkt]->data_ptr[11]; //ID1
  slot_ptr[slotnum]->slot_data[7] = rx_packets[rx_pkt]->data_ptr[10]; //CLK_CTRL, valid data this is 32. 
  slot_ptr[slotnum]->slot_data[6] = rx_packets[rx_pkt]->data_ptr[9]; //Days
  slot_ptr[slotnum]->slot_data[5] = rx_packets[rx_pkt]->data_ptr[8]; //Hours
  slot_ptr[slotnum]->slot_data[4] = LN_TRK; //Global status byte. Not actually used here, set anyway just because. 
  slot_ptr[slotnum]->slot_data[3] = rx_packets[rx_pkt]->data_ptr[6]; //Minutes
  slot_ptr[slotnum]->slot_data[2] = rx_packets[rx_pkt]->data_ptr[5]; //Fractional minutes high
  slot_ptr[slotnum]->slot_data[1] = rx_packets[rx_pkt]->data_ptr[4]; //Fractional minutes low 
  slot_ptr[slotnum]->slot_data[0] = rx_packets[rx_pkt]->data_ptr[3]; //Rate

  //Frac_Mins to uS seconds conversion.  
  uint32_t frac_minutes_uS = 0; //frac_minutes converted to uS
  uint8_t s_seconds = 0; 
  if ((slot_ptr[slotnum]->slot_data[2] - 120) > 0) { //Minimum value is 120, which has a shorter period than 121-127
   frac_minutes_uS = (slot_ptr[slotnum]->slot_data[2] - 121) * 8388480 + 1245165; //rollovers from 121-127 + the shorter period of cycle 120
  } 
  frac_minutes_uS = frac_minutes_uS + (slot_ptr[slotnum]->slot_data[1] * 65535); 

  //Convert the uS to sec  s_seconds = minutes_rem_uS / 10000000;
  //uint8_t s_seconds = uint32_t(472440 * (127 - rx_packets[rx_pkt]->data_ptr[5])) / 1000000; 
  if (s_seconds > 59) { //Probable math error here, this works around it. 
    s_seconds = s_seconds - 60; 
  }
  Serial.printf("Loconet: Seconds set %i \n", s_seconds); 
  Fastclock.clock_set(slot_ptr[slotnum]->slot_data[0], slot_ptr[slotnum]->slot_data[6], slot_ptr[slotnum]->slot_data[5] - slot_hours, slot_ptr[slotnum]->slot_data[3] - slot_minutes, s_seconds, 0);
  Fastclock.clock_get(); //Update clock values with new setpoint. 
  Serial.printf("Loconet: Fastclock set to day %u %u:%u:%u \n", Fastclock.days, Fastclock.hours, Fastclock.minutes, Fastclock.seconds);  
  return; 
}



void LN_Class::slot_fastclock_get(){
  const uint8_t slotnum = 123;
  uint8_t minsl = 0; 
  uint8_t minsh = 120;
  uint32_t frac_mins_uS = 0;
  //Serial.printf("Processing fast clock update. \n");
  Fastclock.clock_get(); //Update fastclock values
  //Convert minutes_rem_uS to frac_minsh and frac_minsl. 
  frac_mins_uS = Fastclock.minutes_rem_uS;
  if (frac_mins_uS > 1245165) { //Equivalent minsl count of 109 to 127, used for minsh cycle 0. 
    minsh++; 
    frac_mins_uS = frac_mins_uS - 1245165; 
  } else { 
    minsl = 109; //Prime minsl for the 1st loop
  }
  while (frac_mins_uS > 8388480) { //Equivalent minsl count of 0-127, used for cycles 1-7
    minsh++; 
    frac_mins_uS = frac_mins_uS - 8388480;
  }
  if (minsh > 127) { //Enforce high limit of 127. 
    minsh = 127; 
  }
  minsl = minsl + frac_mins_uS / 65535; //Divide out the remainder for minsl. 

  
  slot_ptr[slotnum]->last_refresh = time_us; //The existing global should have been set by Fastclock.clock_get(); 
/* //Leave the ID untouched, it is set when the clock is. 
  slot_ptr[slotnum]->slot_data[9] = 0x7F; //ID2, 0x7x = PC
  slot_ptr[slotnum]->slot_data[8] = 0x7F; //ID1, 0x7F = PC */
  slot_ptr[slotnum]->slot_data[7] = 32; //Bit D6 = 1 for valid clock data
  slot_ptr[slotnum]->slot_data[6] = Fastclock.days; //Days since fast clock init
  slot_ptr[slotnum]->slot_data[5] = slot_hours + Fastclock.hours; //128 - 24
  slot_ptr[slotnum]->slot_data[4] = LN_TRK; //Global slot byte;
  slot_ptr[slotnum]->slot_data[3] = slot_minutes + Fastclock.minutes; //127 - 60 or 128 - 60 depending on DCS100 compat mode
  slot_ptr[slotnum]->slot_data[2] = minsh; //127 - Fastclock.minutes_rem_uS / 472441; //frac_minutes_us is the remaining uS after calculating minutes. FRAC_MINS_H, 0-127 tick count
  slot_ptr[slotnum]->slot_data[1] = minsl; //127 - (Fastclock.frac_minutes_uS - (slot_ptr[123]->slot_data[2] * 472441)) / 3872; //FRAC_MINS_L, 0-127 tick count
  slot_ptr[slotnum]->slot_data[0] = Fastclock.set_rate; //Clock multiplier
  return; 
}

int8_t LN_Class::slot_new(uint8_t slotnum) { //Initialize empty slots
  if (!(slot_ptr[slotnum])){
    //Serial.printf("Initializing Loconet slot %u \n", slotnum);
    slot_ptr[slotnum] = new LN_Slot_data; 
    slot_ptr[slotnum] -> slot_data[0] = 0; //Slot status set to free.
    slot_ptr[slotnum]->last_refresh = TIME_US;
    slot_ptr[slotnum]->next_refresh = 200000000; //Expect refresh within 200 seconds 
  }
  if (!(slot_ptr[slotnum])){
    Serial.printf("Failure allocating slot %u \n", index);
    return -1; 
  }  
  //Implement special slot initialization
  if (slotnum == 123) { //Fast Clock, set last_refresh to 0 so the initial load happens right away. 
    slot_ptr[slotnum]->last_refresh = 0;  
    slot_ptr[slotnum]->next_refresh = 80000000; //Fastclock sends pings around every 80-100 seconds instead of receiving within 200. 
    slot_ptr[slotnum]->slot_data[9] = 0x7F; //ID2, 0x7x = PC
    slot_ptr[slotnum]->slot_data[8] = 0x7F; //ID1, 0x7F = PC
  }
  return slotnum;
}



uint8_t LN_Class::slot_del(uint8_t index) {
  delete slot_ptr[index];
  return 0; 
}

void LN_Class::slot_queue() { //Scan slots and purge inactive
  int i; 
  time_us = TIME_US; 
  for (i = 1; i < 120; i++) {
    if (!(slot_ptr[i])){ //Undefined slot. Skip it. 
      continue; 
    }
    if ((time_us - slot_ptr[i]->last_refresh > slot_ptr[i]->next_refresh) && (slot_ptr[i]->slot_data[0] & 0x30)) { //Time expired. See if it can be purged. 
      slot_ptr[i]->slot_data[0] = slot_ptr[i]->slot_data[0] & 0xEF; //Unset SL_Active, leaving the slot in Idle or Free state. 0xDF to unset SL_Busy
                  
    }
    
  }
  return;   
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
