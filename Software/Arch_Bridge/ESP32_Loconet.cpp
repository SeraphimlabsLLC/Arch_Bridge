/*   IMPORTANT:nge
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
  #include "ESP32_timer.h" //For gptimer and fastclock access
  ESP_gptimer LN_gptimer; //Timer handle owned by this instance
  extern Fastclock_class Fastclock; 
#endif

#ifndef ESP32_ADC_H
  #include "ESP32_adc.h"
#endif
 

ESP_Uart LN_port; //Loconet uart object
LN_Class Loconet; //Loconet processing object
TaskHandle_t LNtask; //Loconet task object

volatile uint64_t LN_cd_edge = 0; //time_us of last edge received. 


//extern DCCEX_Class dccex_port;
extern uint64_t time_us;

//extern ADC info
extern ADC_Handler adc_one[ADC_SLOTS];
 

void LN_init(){//Initialize Loconet objects
  LN_gptimer.gptimer_init(); //Initialize ESP gptimer for collision detection
  LN_UART //Initialize uart 
  Loconet.LN_port.uart_mode = 0; //Change to 1 to use uart fast write
  Loconet.LN_set_mode(LN_HOSTMODE); //set default host mode
  Loconet.ln_adc_index = ADC_new_handle();
  //Serial.printf("Loconet railsync adc handle %i \n", Loconet.ln_adc_index); 
  adc_one[Loconet.ln_adc_index].adc_channel_config(LN_ADC_GPIO, LN_ADC_OFFSET, LN_ADC_OL * LN_ADC_SCALE, 2, 0); //Reserve ADC handle. Mode 2 for current < threshold, instance 0
  Loconet.last_ol_time = 0; //prime the overload warning timer
  Loconet.adc_ticks_scale = LN_ADC_SCALE; //ADC ticks to volts, instead of mA. 
  LN_gptimer.alarm_set(-1, 1);//alarm set to where it would take an absurd amount of time to trip. The first edge interrupt to make rx_pin = 1 will lower it. 
  Loconet.line_flags = 0;
  attachInterrupt(Loconet.LN_port.rx_pin, LN_CD_isr, CHANGE); //Attach the pcint used for CD
  xTaskCreatePinnedToCore(LN_task, "lntask", 20000, NULL, 2, &LNtask, 1); 
  return;
}

void LN_loop(){//reflector into LN_Class::loop_Process()
  //Loconet.loop_process(); //Process and update Loconet
  xTaskNotify(LNtask, LN_lineflags::ln_hrt, eSetBits); //LNtask bit 8 for heartbeat scan
  return; 
}

void LN_task(void * pvParameters){
  while (true){
    Loconet.loop_process(); //Process and update Loconet
  } //while
  return; 
}

void LN_Class::loop_process(){
  uint32_t notifications = 0; 
  xTaskNotifyWait(0, 0xffffffff, &notifications, 1000); //blocks until a notification is pending, copies into notifications and clears
  if (notifications > 0) {
  //  Serial.printf("Loconet loop process notification %i, line_flags %i\n", notifications, line_flags);
  }
  
  if (notifications & LN_lineflags::link_disc) { //disconnect detected. 
    line_flags = line_flags & ~(LN_lineflags::link_active); //unset link_active
    Serial.printf("Loconet lost connection to network \n");
  }

  if (notifications & LN_lineflags::link_active) { //Been at least 250mS since a 1 was detected, try to connect.
    Serial.printf("Loconet: connected to network \n");  
    line_flags = line_flags | LN_lineflags::link_active; //set link_active
    line_flags = line_flags & ~(LN_lineflags::link_disc); //Unset link_disconected
    uart_rx(); //read data from buffer
    LN_port.rx_read_processed = 255; //discard read data 
    rx_pending = -1; 
    tx_pending = -1; 
  } 

  if (!(line_flags & LN_lineflags::link_active)) { 
    //network is not up, clear buffers and return. 
    uart_rx(); //read data from buffer
    LN_port.rx_read_processed = 255; //discard read data 
    //Loconet starter
    if ((!(line_flags & LN_lineflags::link_disc)) && (gpio_get_level(gpio_num_t(Loconet.LN_port.rx_pin)) == 1)) {
      Serial.printf("Loconet: loop starting gptimer \n");
      LN_gptimer.alarm_set(250000, 1); //Sets the alarm with an initial count of 0. 
      Loconet.line_flags = Loconet.line_flags | LN_lineflags::link_disc; //reuse link_disc for the restart timer    
    } 
    return; 
  }

  if ((notifications & LN_lineflags::rx_brk) || (line_flags & LN_lineflags::rx_brk))  { //RX Break was set. 
    if (line_flags & LN_lineflags::rx_brk) {
      line_flags = line_flags & ~(LN_lineflags::rx_brk); //Unset rx_break
      Serial.printf("Loconet: RX Break from ISR \n");
      //receive_break(); 
    }
  }
  if ((notifications & LN_lineflags::tx_brk) || (line_flags & LN_lineflags::tx_brk)) { //TX Break was set.
    if (line_flags & LN_lineflags::tx_brk) {
      line_flags = line_flags & ~(LN_lineflags::tx_brk); //Unset tx_brk
      Serial.printf("Loconet: TX Break complete, line_flags %i \n", line_flags);
    }
  }
  if (notifications & LN_lineflags::rx_recv) { //RX Data
  }
  if (notifications & LN_lineflags::tx_snd) { //TX Data
    if (tx_pending > 0) {//pending packet, try to send it
      tx_send(tx_pending);
    }
  } 
  uart_rx(); //Read uart data into the RX ring
  rx_scan(); //Scan RX ring for an opcode
  //Network is up but no notifications were received. Process the queues. 
  rx_queue(); //Process queued RX packets and run rx_decode
  tx_queue(); //Try to send any queued packets
  slot_queue(); //Clean up stale slots
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
  //Serial.printf("Loconet loop complete \n");
  return;
} 

/*void LN_Class::network_startup(){
  uint64_t gptimer_count = LN_gptimer.gptimer_read(); //Get gptimer count since last edge
  Serial.printf("Loconet network status gptimer_count %lu \n", gptimer_count);

  //Initial startup. 
  if ((gpio_get_level(LN_port.rx_pin) == 1) && (((line_flags & LN_lineflags::link_disc) == 0))){
    if (gptimer_count >= 250000) {
//    if (((TIME_US - signal_time) > 250000)) { 
      signal_time = TIME_US; 
      line_flags = line_flags | LN_lineflags::link_disc; //signal ok
      #if LN_DEBUG == true
        Serial.printf("Loconet master detected \n");
      #endif
    } 
    return;
  }
  //Activate connection if port is enabled and link is up. 
  if (gpio_get_level(LN_port.rx_pin) == 1) {
    if ((gptimer_count >= 100000) && (((line_flags & LN_lineflags::link_disc) == 1))) {
      if ((line_flags & LN_lineflags::link_active) == 1) {
        line_flags = line_flags | LN_lineflags::link_active; //set to link active  
      }  
    }
  }
  //Check for loss of master connection. Will reset the link if rx_pin is 0 and has not changed in 100mS. 
  if (gpio_get_level(LN_port.rx_pin) == 0) {
    if (gptimer_count >= 100000) {
//    if (TIME_US - signal_time > 100000) {
      if (line_flags & LN_lineflags::link_active) {
        line_flags = line_flags & ~LN_lineflags::link_active; //set to disconnected
        //detachInterrupt(Loconet.LN_port.rx_pin); //Turn off ISR 
        Serial.printf("Loconet lost connection to master. Resetting link state. \n"); 
      }
      return; 
    }
  }
  return; 
} */

uint8_t LN_Class::uart_rx(){
  uint8_t read_size = 0;
  uint8_t i = 0;

  if (LN_port.rx_read_processed != 255) { //Data processing incomplete, don't read more.  
    return 0; 
  }
  LN_port.rx_read_data[i] = 0; 
  read_size = LN_port.uart_read(read_size); //populate rx_read_data and rx_data
  LN_port.rx_flush(); //Clear the uart after reading. 
  LN_port.rx_read_len = read_size; //Just to be sure it is correct. 
  return read_size;
}

void LN_Class::rx_scan(){ //Scan received data for a valid frame
  uint8_t rx_byte = 0; 
  int8_t coll = 0;  
  time_us = TIME_US; //update time_us
  //Serial.printf("rx_scan LN_port.rx_read_processed %u \n", LN_port.rx_read_processed);
  if (LN_port.rx_read_processed != 0) { //Data has already been processed. 
    return; 
  }
  //Serial.printf("RX_Scan processing %u bytes, pending %i \n", LN_port.rx_read_len, rx_pending);
  for (rx_byte = 0; rx_byte < LN_port.rx_read_len; rx_byte++) {
    //Serial.printf("RX_Scan For, rx_pending %i, rxbyte %x, counter rx_byte %u \n", rx_pending, LN_port.rx_read_data[rx_byte], rx_byte);

//Detect opcode and packet start if there isn't already a packet open
    if (LN_port.rx_read_data[rx_byte] & 0x80) { //rx_byte will only ever have the MSB set on opcodes 
      rx_pending = rx_packet_getempty(); //Get handle of next open packet
      //Serial.printf("Starting new packet %i with opcode %x \n", rx_pending, LN_port.rx_read_data[rx_byte]);
      rx_packets[rx_pending]->state = 1; //Packet is pending data
      rx_packets[rx_pending]->rx_count = 0; //1st byte in packet. 
      rx_packets[rx_pending]->xsum = 0;
      rx_packets[rx_pending]->last_start_time = time_us; //Time this opcode was found
    }
    //Sanity Block: No packet active or packet not in pending or attempting state
    if ((rx_pending < 0) || (rx_packets[rx_pending]->state == 0) || (rx_packets[rx_pending]->state > 2)) {
      //Serial.printf("Not in packet or packet state not ok, going back to for with rx_byte %u and read_len %u \n", rx_byte, LN_port.rx_read_len);
      rx_pending = -1; 
      continue;
    }
     
//Populate packet:      
    uint64_t time_remain = (15000 - (time_us - rx_packets[rx_pending]->last_start_time));
    //Serial.printf("RX_Scan Processing byte %x from packet %u. ", LN_port.rx_read_data[i], rx_pending);
    //Serial.printf("RX_Scan Time remaining (uS): %u \n", time_remain);
    if ((time_us - rx_packets[rx_pending]->last_start_time) > 15000) { //Packets must be fully received within 15mS of opcode detect
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
    #if LN_DEBUG == true
      show_rx_packet(rx_pending);
    #endif

//TX Loopback processing: 
    if (tx_pending > -1) { //rx_pending must be > -1 at this point. && (rx_pending > -1)) { 
      coll = tx_loopback(); 
      if (coll == 0) { //Check if the data so far matches what we sent. On match, mark both as success. 
        tx_pending = -1; 
        
        continue;
      }
      Serial.printf("TX Loopback had %i differences. rx_pending %i, tx_pending %i \n", coll, rx_pending, tx_pending);  
       
    }   
    if (rx_packets[rx_pending]->state == 2) {   
      rx_packets[rx_pending]->last_start_time = TIME_US; //Time it was set to this state.   
      if (rx_packets[rx_pending]->Read_Checksum()) { //Valid checksum, mark as received. 
        //Serial.printf("RX_Scan Completed packet %u ready to decode \n", rx_pending); 
        rx_packets[rx_pending]->state = 3; //Set state to received 
      } else {//Checksum was invalid. Drop the packet. 
        Serial.printf("RX_Scan Failed packet %u, RX checksum invalid \n", rx_pending); 
        rx_packets[rx_pending]->state = 4; //Set state to failed
      }
    }
    rx_pending = -1; 
  }
  LN_port.rx_read_processed = 255; //Mark as fully processed.
  LN_port.rx_read_len = 0;  
  //Serial.printf("RX_Scan Complete \n");
  return; 
} //::rx_scan()  

void LN_Class::rx_queue(){ //Loop through the RX queue and process all packets in it. 
  uint8_t i = 0;
  uint8_t complete = 0;
  uint8_t priority = 255; //Maximum of 20, the out of bounds init means no packet was matched. 
  uint8_t rx_next_decode = 0; 
  time_us = TIME_US;
  for (i = 0; i < LN_RX_Q; i++){
    //Serial.printf("RX_Queue. rx_next_check = %u, LN_RX_Q = %u \n", rx_next_check, LN_RX_Q);
    if (rx_packets[rx_next_check]) { //Packet exists, check it.
      //Serial.printf("Queue Processing RX packet %u, state is %u \n", rx_next_check, rx_packets[rx_next_check]->state);
      switch (rx_packets[rx_next_check]->state) {
        case 1:
        case 2:
          if (time_us - rx_packets[rx_next_check]->last_start_time > 15000) { //Pending or Attempting must be finished within 15mS since their state last changed.
            //Serial.printf("RX_Q Cleaning up incomplete packet %u \n", rx_next_check);
            rx_packets[rx_next_check]->reset_packet();
            if (rx_next_check == rx_pending) {
              rx_pending = -1;
            } 
          }          
          break;
        case 3: 
          if (rx_packets[rx_next_check]-> priority <= priority) { //New most urgent packet. 
            //Serial.printf("RX_Q: Next packet to decode: %u \n", rx_next_check);
            rx_next_decode = rx_next_check;
            priority = rx_packets[rx_next_decode]-> priority; 
          }
          break; 
        case 5:
          //LN_port.uart_rx_flush(); //Failed packet. 
        case 4: 
          //Serial.printf("RX_Q Cleaning up complete or failed packet %u \n", rx_next_check);
          rx_packets[rx_next_check]->reset_packet(); 
          if (rx_next_check == rx_pending) {
              rx_pending = -1;
          }
      }
    }
    rx_next_check++;
    if (rx_next_check >= LN_RX_Q){
      rx_next_check = 0; 
    }
  }

  if ((priority < 21) && (rx_packets[rx_next_decode])) { //A packet was in state 3 and had lowest priority. Ifit still exists, process it. 
    //Serial.printf("RX_Q Before rx_decode of %u \n", rx_next_decode);
    complete = rx_decode(rx_next_decode); 
    //Serial.printf("RX_Q After rx_decode of %u \n", rx_next_decode);
    if (complete == 0) {
      rx_packets[rx_next_decode]->state == 5; //Completed. Can delete it on next cycle. 
    }    
  }
  return;
}

int8_t LN_Class::rx_decode(uint8_t rx_pkt){  //Opcode was found. Lets use it.
  //Finally processing the packet.
  //Serial.printf("Processing received packet from RX_Q slot %u \n", rx_pkt);
  if (!(rx_packets[rx_pkt])) { //An invalid handle was given. Abort. 
    return -1; 
  }

  measure_time_start();
  char opcode = rx_packets[rx_pkt]->data_ptr[0];
  uint8_t i = 0;
  int8_t slotnum= -1; //Used for slot processing. 
  rx_packets[rx_pkt]->state = 5;
  //show_rx_packet(rx_pkt); //display packet for debug
  switch (opcode) {
    
    //2 byte opcodes:
    case 0x81:  //Master Busy
      break;
    case 0x82: //Global power off
      Serial.print("Loconet: Power off requested \n"); 
      LN_TRK = LN_TRK & 0xFE; //Set power bit off in TRK byte
      dccex.global_power('0', DCCEX_TO_LN); //power off, announce to DCCEX if true
      break;
    case 0x83: //Global power on
      Serial.print("Loconet: Power on requested \n");
      LN_TRK = LN_TRK | 0x03; //Set power bit on in TRK byte, set estop release
      dccex.global_power('1', DCCEX_TO_LN); //power off, announce to DCCEX if true
      break;
    case 0x85: //Force idle, broadcast estop
      Serial.print("Loconet: <!> \n"); //DCC-EX Estop
      LN_TRK = LN_TRK & 0xFD; //Clear estop bit for global estop. 
       dccex.global_power('!', DCCEX_TO_LN); //estop, announce to DCCEX if true
      break; 
      
    //4 byte opcodes: 
    case 0xA0: //Set slot speed
      slotnum = rx_packets[rx_pkt]->data_ptr[1];
      if (!(slot_ptr[slotnum])) { //Invalid slot, do nothing. 
        break; 
      }
      //if (slot_ptr[slotnum].
      slot_ptr[slotnum]->slot_data[2] = rx_packets[rx_pkt]->data_ptr[2]; //Update speed byte
      slot_ptr[slotnum]->last_refresh = TIME_US;
      #if DCCEX_TO_LN == true //Only send if allowed to.
        dccex.tx_cab_speed(slot_ptr[slotnum]->slot_data[1] + uint16_t (slot_ptr[slotnum]->slot_data[6] << 7),
        slot_ptr[slotnum]->slot_data[2], bool(~(slot_ptr[slotnum]->slot_data[3]) & 0x20)); //Extract dir bit
      #endif
      break;
    case 0xA1: //Set slot dirf
      slotnum = rx_packets[rx_pkt]->data_ptr[1];
      if (!(slot_ptr[slotnum])) { //Invalid slot, do nothing. 
        break; 
      }
      slot_ptr[slotnum]->slot_data[3] = rx_packets[rx_pkt]->data_ptr[2]; //Update dirf byte
      slot_ptr[slotnum]->last_refresh = TIME_US;
      #if DCCEX_TO_LN == true //Only send if allowed to.
        dccex.tx_cab_speed(slot_ptr[slotnum]->slot_data[1] + uint16_t (slot_ptr[slotnum]->slot_data[6] << 7), 
        slot_ptr[slotnum]->slot_data[2], bool(~(slot_ptr[slotnum]->slot_data[3]) & 0x20)); 
      #endif       
      break;
    case 0xA2: //Set slot sound
      slotnum = rx_packets[rx_pkt]->data_ptr[1];
      if (!(slot_ptr[slotnum])) { //Invalid slot, do nothing. 
        break; 
      }
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
      slotnum = rx_packets[rx_pkt]->data_ptr[1];
      if (!(slot_ptr[slotnum]) || (slotnum > 120)) { //Invalid slot, do nothing. 
        break; 
      }
      slot_ptr[slotnum]->slot_data[0] = rx_packets[rx_pkt]->data_ptr[2]; //Update stat1 byte
      slot_ptr[slotnum]->last_refresh = TIME_US;      
      break;   
    case 0xB6: //SET FUNC bits in a CONSIST uplink elemen
      break; 

      break;  
    case 0xB9: //LINK slot ARG1 to slot ARG
      break;  
    case 0xBA: //MOVE slot SRC to DEST
    //0xB8 to 0xBF require replies
    case 0xB8: //UNLINK slot ARG1 from slot ARG2
      #if LN_DEBUG == true
        Serial.printf("Slot move %u to %u \n", rx_packets[rx_pkt]->data_ptr[1], rx_packets[rx_pkt]->data_ptr[2]); 
      #endif
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
      #if LN_DEBUG == true
        Serial.printf("Throttle requesting slot %u data \n", slotnum);
      if (slotnum > 120) {
        show_rx_packet(rx_pkt); 
      }
      #endif
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
      #if LN_DEBUG == true
        Serial.printf("Request for loco %u found/set in slot %u \n", rx_packets[rx_pkt]->data_ptr[2], slotnum);
      #endif
      //slot_data[0] already set to free if this is a new assignment 
      slot_ptr[slotnum] -> slot_data[1] = rx_packets[rx_pkt]->data_ptr[2];
      slot_ptr[slotnum] -> slot_data[6] = rx_packets[rx_pkt]->data_ptr[1];
      slot_read(slotnum); //Read the slot back to the source. 
    
      break;                                   
    //6 byte opcodes, none known as of Oct 2023: 

    //Variable byte opcodes: 
    case 0xE7: //Slot read data
      slotnum = rx_packets[rx_pkt]->data_ptr[2];
      if (!(slot_ptr[slotnum])) { //Invalid slot, do nothing. 
        break; 
      }
      if (slotnum == 123) { //Set fast clock using sync from some other device. 
        slot_fastclock_set(rx_pkt);
      }
      //slot_read(slot_read);
      break;
    case 0xED: //Send Immeadiate Packet
      send_long_ack(0xED, 0); //Busy response
      break; 
    case 0xEF: //Slot write data
    
      slotnum = slot_write(rx_packets[rx_pkt]->data_ptr[2], rx_pkt); //Accept slot write data and send Long Ack
      send_long_ack(0xEF, slotnum); //Send LACK with the result
      break; 
    
    default: 
    #if LN_DEBUG == true
      Serial.printf("No match for %x \n", rx_packets[rx_pkt]->data_ptr[0]);
    #endif
    rx_packets[rx_pkt]->state = 4;
  }
  measure_time_start();
  return 0;
}

void LN_Class::tx_queue(){ //Try again to send queued packets on each loop cycle
  uint8_t i = 0;
  uint8_t priority = 255; //max allowed is actually 20, so this is out of bounds to indicate no packet matched. 
  uint8_t tx_next_send = 0;
  time_us = TIME_US;

  //Serial.printf("TX_Q priority loop \n");
  //while (priority <= LN_min_priority){ 
  //  while (i < LN_TX_Q) {
  for (i = 0; i < LN_TX_Q; i++) {
  //Serial.printf("TX_Q. priority = %u i = %u, tx_next_check %u, LN_TX_Q = %u \n", priority, i, tx_next_check, LN_TX_Q);  
    if (tx_packets[tx_next_check]) { //Packet exists, check it.
      //Serial.printf("TX_Q Processing TX packet %u, state is %u \n", tx_next_check, tx_packets[tx_next_check]->state);
      time_us = TIME_US; //Update time
      switch (tx_packets[tx_next_check]->state) {
        case 0: //No action needed, empty slot. 
          break; 
        case 1: //Pending. 
          if (tx_packets[tx_next_check]->priority < priority) {
            //Serial.printf("TX_Q: Try sending packet %i \n", tx_next_check);
            priority = tx_packets[tx_next_check]->priority;
            tx_next_send = tx_next_check;
          }
          break; 
        case 2: //Attempting. Check if valid state and within 15mS window
          if (tx_next_check != tx_pending) {  //A packet in this state that doesn't match tx_pending is a problem.
            tx_packets[tx_next_check]->state = 4;
            break;
          } 
          if ((time_us - tx_packets[tx_next_check]->last_start_time) < 15000) { //Still within 15mS window, try tx_send again to see if cooldown has passed.
            Serial.printf("TX_Q: Re-check send packet %i %i \n", tx_pending, tx_next_check); 
            priority = 0;
            tx_next_send = tx_next_check;
           
          } else { //15mS exceeded since start of window, mark as failed. 
            Serial.printf("TX_Q tx_next_check failed to send after %u mS. %u attempts remain \n", (time_us - tx_packets[tx_next_check]->last_start_time) / 1000, tx_packets[tx_next_check]->tx_attempts);
            tx_packets[tx_next_check]->state = 4;
            tx_packets[tx_next_check]->tx_attempts--;
            tx_packets[tx_next_check]->priority--; //Increase priority for the next attempt.
          if (tx_packets[tx_next_check]->priority < LN_max_priority) {//Enforce priority limit
              tx_packets[tx_next_check]->priority = LN_max_priority;
            }
            if (tx_next_check == tx_pending) {
              tx_pending = -1;
            }
          }
          break; 
        case 3: //Sent. 
          if ((time_us - tx_packets[tx_next_check]->last_start_time) > TX_SENT_EXPIRE) { //Packet was sent long enough ago loopback should have seen it by now if it arrived ok. Clear it out if it still exists. 
            tx_packets[tx_next_check]->state = 4;
            tx_packets[tx_next_check]->tx_attempts = 0;
            Serial.printf("TX_Q Purging stale sent packet %u ", tx_next_check);
            Serial.printf("after %u uS \n", (time_us - tx_packets[tx_next_check]->last_start_time));
            tx_packets[tx_next_check]->reset_packet(); 
            if (tx_next_check == tx_pending) {
              Serial.printf("TX_Q Purged pending packet. This should not happen \n");
              tx_pending = -1;
            } 
          }
          break; 
        case 4: //Failed. 
          if (tx_packets[tx_next_check]->tx_attempts <= 0) {
            //Serial.printf("TX_Q Unable to transmit packet from TX queue index %u, dropping. \n", tx_next_check);
            tx_packets[tx_next_check]->reset_packet();     
            if (tx_next_check == tx_pending) {
              tx_pending = -1;
            }
          } else { //Packet still has retries left. 
            if (tx_packets[tx_next_check]->priority < priority) {
            priority = tx_packets[tx_next_check]->priority;
            tx_next_send = tx_next_check;
            }
          }
          break;  
        case 5: //Success? Either way we don't need it anymore.
          show_tx_packet(tx_next_check); 
          tx_packets[tx_next_check]->reset_packet(); 
          if (tx_next_check == tx_pending) { //This should never be needed, but just in case. 
              tx_pending = -1;
          }
        }
      } //endif packet exists
      tx_next_check++; 
      if (tx_next_check >= LN_TX_Q){ 
        tx_next_check = 0; 
      }
  }
  if (priority < 21) { //Send best priority packet if one was found
    if ((tx_pending < 0) || (tx_pending == tx_next_send)) { //Clear to try sending or already are sending
      //tx_pending = tx_next_send;  
      tx_send(tx_next_send);
      }
  }
  
  return;
}

void LN_Class::tx_send(uint8_t txptr){
  //Note: tx_priority from LN_Class needs to be replaced by per-packet priority handling. 
  time_us = TIME_US;
  uint8_t i = 0;
  uint16_t rx_len = 0;
  if (!tx_packets[txptr]) { //If the handle is invalid, abort.
    return; 
  }
  
  if ((time_us - tx_packets[txptr]->last_start_time) < (tx_pkt_len * LN_BITLENGTH_US * 8)) {//There is likely data already being sent. Don't add more.
  //  Serial.printf("LN_TX Buffer should still have data, send wait \n");
    return;    
  }

  rx_len = LN_port.read_len(); //Check if data has been received since the last read. Don't send now if it has. 
  if (rx_len > 0) {
    return; 
  }

  if (LN_host_mode == ln_silent) { //Silent mode, transmitter disabled. 
    tx_packets[txptr]->state = 5; //Set to complete so that it gets dropped.
    if (txptr == tx_pending) { //Clear pending if it was this packet. 
      tx_pending = -1;
    }
    return; 
  }
  
   //Serial.printf("TX_Send Pending Packet %u has mark %u \n", txptr, tx_packets[txptr]->state);
  if ((tx_packets[txptr]->state == 1) || (tx_packets[txptr]->state == 4)) { //Packet is pending or failed. Mark attempting.
    tx_packets[txptr]->last_start_time = time_us;
    tx_packets[txptr]->state = 2; //Attempting
    //Serial.printf("TX_Send Packet %u changed from 1 or 4 to  %u \n", txptr, tx_packets[txptr]->state);
  }
  
  //if ((time_us - rx_last_us) > (LN_BITLENGTH_US * (tx_packets[txptr]->priority + LN_COL_BACKOFF))) {
  //Use LN_cd_time from the ISR instead of rx_last_us because of the rx buffer uncertainty
  uint64_t LN_cd_timer = LN_gptimer.gptimer_read(); //Get gptimer count since last edge
  Loconet.LN_cd_window = (LN_BITLENGTH_US * (tx_packets[txptr]->priority + LN_COL_BACKOFF));

  if (((LN_cd_timer) > Loconet.LN_cd_window) && (gpio_get_level(gpio_num_t(Loconet.LN_port.rx_pin)) == 1)) { 
    //if the last edge was longer than the cd interval, and rx_pin is high indicating an idle bus
  //if ((TIME_US - LN_cd_edge) > (LN_BITLENGTH_US * (tx_packets[txptr]->priority + LN_COL_BACKOFF))) {
    //Serial.printf("Sending packet %u \n", txptr); 
    tx_pending = txptr; //Save txpending for use in tx_loopback
    i = 0;
    tx_pkt_len = tx_packets[txptr]->data_len; //Track how much data is to be sent for overrun prevention.
    if (tx_packets[txptr]->state == 2) { //Only send if it isn't in state attempting. 
      char writedata[tx_pkt_len]; 
      #if LN_DEBUG == true
      Serial.printf("Transmitting %u: ", tx_pending);
      while (i < tx_pkt_len){ 
        writedata[i] = tx_packets[txptr]->data_ptr[i];
        Serial.printf("%x ", tx_packets[txptr]->data_ptr[i]);
        i++;
     }
      Serial.printf("\n"); 
      #endif
      tx_packets[txptr]->last_start_time = time_us;  
      
      //LN_port.uart_write(writedata, tx_pkt_len);
      LN_port.uart_write(tx_packets[txptr]->data_ptr, tx_pkt_len);   
      tx_packets[txptr]->state = 3; //sent
      Loconet.LN_cd_window = 850; //Reset to RX break interval
      line_flags = line_flags | LN_lineflags::tx_snd; //indicate that transmission started
    }
  } else { //CD window wasn't ready or rx_pin wasn't in the wrong state. Set tx_rts.
    line_flags = line_flags | LN_lineflags::tx_rts;
    LN_gptimer.alarm_change(Loconet.LN_cd_window, 2); //set timer for the cd interval
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
    if (rx_packets[rx_pending]->rx_count == tx_packets[tx_pending]->data_len) { //Packet is complete and intact. 
//        Serial.printf("Transmission of packet %d confirmed in %d \n", tx_pending, rx_pending);
        tx_packets[tx_pending]->state = 5; //Mark TX complete
        tx_packets[tx_pending]->last_start_time = TIME_US; //Time it was set to this state.
        line_flags = line_flags & ~(LN_lineflags::tx_snd); //Transmission ended  
        tx_pending = -1;        
        rx_packets[rx_pending]->state = 5; //Mark RX complete
        rx_packets[rx_pending]->last_start_time = TIME_US; //Time it was set to this state.   
    }
    return delta;
  } else { //Collision. Transmit break, drop from rx_packet, and decrement tx_attempts to drop if stale.  
//    Serial.printf("Collision detected, %u differences found. \n", delta);
    //transmit_break();
    tx_packets[tx_pending]->tx_attempts--; 
    tx_packets [tx_pending]->state = 4; 
    line_flags = line_flags & ~(LN_lineflags::tx_snd); //Transmission ended
    //tx_pending = -1;
    //if (rx_packets[rx_pending]->rx_count == rx_packets[rx_pending]->data_len) { //If the complete packet is here, drop from rx_pending
    rx_packets[rx_pending]->state = 4;
      //rx_pending = -1;
    //} 
  }
  return delta;
}

void LN_Class::Overload_Check(int32_t current, int32_t overload){
  if (current < overload) { //Voltage below threshold. Warn the user. 
    if ((TIME_US - last_ol_time) > 10000000) {
      Serial.printf("Loconet railsync voltage low %u mV, required %u mV \n", current / Loconet.adc_ticks_scale, overload / Loconet.adc_ticks_scale);
      last_ol_time = TIME_US; 
    }
  }
  return; 
}

void IRAM_ATTR LN_CD_isr(){ //Pin change ISR has around 4uS latency. 
  //bool rx_pin_state = ~(gpio_get_level(gpio_num_t(LN_COLL_PIN)));
  bool cd_pin = gpio_get_level(gpio_num_t(Loconet.LN_port.rx_pin));
  //uint8_t i = 0; 
  uint64_t gptimer_count = LN_gptimer.gptimer_read(); 
  //Serial.printf("%u-%llu ", cd_pin, gptimer_count);
  if (!(Loconet.line_flags & LN_lineflags::link_active)){  //link is not active, if cd_pin became 1, start the timer. 
    if (cd_pin  == 1) {
      if (!(Loconet.line_flags & LN_lineflags::link_disc)) {
        LN_gptimer.alarm_set(250000, 1); //Sets the alarm with an initial count of 0. 
        Loconet.line_flags = Loconet.line_flags | LN_lineflags::link_disc; //reuse link_disc for the restart timer
        //Serial.printf("LNCD disc set \n");
      }
      //LN_gptimer.alarm_change(250000, 1); //time the 250mS from the 1st detected 1.
      //Serial.printf("LN_CD_ISR_inactive_count\n");  
    }
    return; 
  } else {   //link is active, reset the timers. 
    LN_gptimer.gptimer_set(0); //set the timer count to 0. 
    LN_cd_edge = TIME_US;  //record current time
    //Serial.printf("LN_CD_ISR_active \n");
  }

  if (Loconet.line_flags & LN_lineflags::tx_snd) { //Transmitter should be active, watch for bitwise collision
   // if (cd_pin  == 0){ 
    if ((gpio_get_level(gpio_num_t(Loconet.LN_port.tx_pin))) && (gpio_get_level(gpio_num_t(Loconet.LN_port.rx_pin)))) {
      //Loconet.transmit_break(); 
      }
    return; 
  } 

  if (!(Loconet.line_flags & LN_lineflags::tx_brk))  { //TX_break not set
    if (cd_pin  == 0) { //Input is 0, check if this is break or disconnect.  
      //if (LN_gptimer.alarm_owner() != 2) { //alarm isn't currently set for break detection
        if (Loconet.line_flags & LN_lineflags::tx_rts)  {
          LN_gptimer.alarm_set(Loconet.LN_cd_window, 2); //watch for CD interval
        } else {
          LN_gptimer.alarm_set(850, 2); //fixed 850uS RX BREAK detect
        } 
      //}
    }
  }
  return; 
}
void IRAM_ATTR LN_gptimer_alarm(){//When triggered, check the source and act accordingly. 
  BaseType_t wakeup;
  wakeup = pdFALSE;
  uint64_t alarm_value = LN_gptimer.alarm_value(); //read the value we had set
  uint8_t owner = LN_gptimer.alarm_owner(); 
  bool cd_pin = gpio_get_level(gpio_num_t(Loconet.LN_port.rx_pin));
  //Serial.printf("gptalarm ct %lu %u pin %u \n", gptimer_count, owner, cd_pin); 

    if (owner == 3) { //BREAK was sent
      
      Loconet.LN_port.uart_invert(false, false); //Reset TX to normal
      //LN_cd_edge = TIME_US; //Record time in case ISR doesn't
      //Task will clear line_flags bit for TX break, don't do it here. 
      xTaskNotifyFromISR(LNtask, LN_lineflags::tx_brk, eSetBits, &wakeup); //LNtask bit 7 for end TX break
      //Serial.printf("Loconet timer mode 3 alarm \n"); 
      return;
    }
    if (owner == 2) { //Listening for CD window or RX Break
      //Serial.printf("Loconet timer mode 2 alarm count %u \n", alarm_value); 
      if ((alarm_value >= Loconet.LN_cd_window) && (cd_pin == 1) && (Loconet.line_flags & LN_lineflags::tx_rts)) { 
        //Value remained 1 for the entirety of the CD window
        LN_gptimer.alarm_change((100000), 1); //change to disconnect detection mode
        xTaskNotifyFromISR(LNtask, LN_lineflags::tx_cts, eSetBits, &wakeup); //TX clear to send
        return; 
      }
     
      if ((alarm_value >= 850) && (cd_pin == 0) && ((Loconet.line_flags & LN_lineflags::tx_brk) == 0)) { 
        //rx_pin was 0 for more than RX_break minimum, and we didn't send this break. 
        LN_gptimer.alarm_change((100000), 1); //change to disconnect detection mode
        //Serial.printf("Loconet Gptimer rx_break triggered \n"); 
        xTaskNotifyFromISR(LNtask, LN_lineflags::rx_brk, eSetBits, &wakeup); //RX break received
      } 
      return; 
    } 
     
    if (owner == 1) { //Network state mode
      alarm_value = LN_gptimer.gptimer_read(); 
      //Serial.printf("Loconet timer mode 1 alarm, gptimer %lu \n", gptimer_count); 
      if ((alarm_value >= 250000) && (!(Loconet.line_flags & LN_lineflags::link_active))) { //cold start or disconnected
        //Serial.printf("Loconet startup \n");
        xTaskNotifyFromISR(LNtask, LN_lineflags::link_active, eSetBits, &wakeup); //Network should become active. 
        return; 
      }
      if ((alarm_value >= 100000) && (cd_pin == 0)){
        //rx_pin was at 0 for more than the disconnect interval. Disconnect. 
        //Serial.printf("Loconet master lost \n");
        xTaskNotifyFromISR(LNtask, LN_lineflags::link_disc, eSetBits, &wakeup);
        return; 
      }  
    }
    
  return; 
}

void IRAM_ATTR LN_Class::transmit_break(){
  //Write 15 bits low for BREAK on collision detection.  
  Loconet.line_flags = Loconet.line_flags | LN_lineflags::tx_brk; //Transmit break
  Loconet.line_flags = Loconet.line_flags & ~(LN_lineflags::tx_snd); //Transmission ended
  Loconet.line_flags = Loconet.line_flags & ~(LN_lineflags::tx_rts); //Unset ready to send since buffers must flush.
  LN_port.uart_invert(true, false); //Invert TX to transmit Loconet BREAK 
  LN_gptimer.alarm_set(900, 3);//15 bit at 60us per bit. Owner tag 3. 
  return;
}

void LN_Class::receive_break(){ //Possible BREAK input at ptr. 
  LN_port.rx_flush();
  LN_port.tx_flush();
  line_flags = line_flags & ~(LN_lineflags::rx_brk); //Unset receive break flag
  //Serial.printf("Loconet: Received Break Input\n"); 
  return;
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

void LN_Class::LN_set_mode(LN_hostmode newmode){ //Set current Loconet access mode
  //enum LN_hostmode {ln_master = 0x80, ln_sensor = 0x20, ln_throttle = 0x08, ln_silent = 0}; //Enum for operating level
  if (newmode > LN_HOSTMODE) {
    Serial.printf("Loconet: Attempt to change to a host mode above LN_HOSTMODE. \n"); 
    return; 
  }
  
  LN_host_mode = newmode;   
  switch (LN_host_mode) {
    case ln_master:
      LN_max_priority = 0; //maximum allowed priority for this mode
      LN_min_priority = 20; //minimum allowed priority for this mode
      Serial.printf("Loconet: Host mode set to master \n"); 
      break; 
    case ln_sensor:
      LN_max_priority = 2; //maximum allowed priority for this mode
      LN_min_priority = 6; //minimum allowed priority for this mode
      Serial.printf("Loconet: Host mode set to sensor \n"); 
      break;
    case ln_throttle:
      LN_max_priority = 6; //maximum allowed priority for this mode
      LN_min_priority = 20; //minimum allowed priority for this mode 
      Serial.printf("Loconet: Host mode set to throttle \n");  
      break;
    case ln_silent:  //Not allowed to send
      LN_max_priority = 127; 
      LN_min_priority = 127; 
      Serial.printf("Loconet: Host mode set to silent \n"); 
      break;   
    default: 
      Serial.printf("Loconet Host mode invalid %u \n", newmode);   
  }
  return;
}
LN_hostmode LN_Class::LN_get_mode(){ //Return current Loconet access mode
  return LN_host_mode;
}

void LN_Class::rx_req_sw(uint8_t rx_pkt){
  uint16_t addr = 0; 
  uint8_t cmd = 0; 
  addr = (((rx_packets[rx_pkt]->data_ptr[2]) & 0x0F) << 7) | (rx_packets[rx_pkt]->data_ptr[1] & 0x7F);
  cmd = ((rx_packets[rx_pkt]->data_ptr[2]) & 0xF0);
  Serial.printf("Loconet: Req switch addr %u direction %u, output %u \n", addr, (cmd & 0x20) >> 5, (cmd & 0x10) >> 4);
  #if DCCEX_TO_LN == true //Only send if allowed to. 
  //if (((cmd & 0x10) >> 4) == 1) { //only forward if output is on. 
    dccex.tx_req_sw(addr + 1, (cmd & 0x20) >> 5, (cmd & 0x10) >> 4); //Addr + 1 so the displayed values agree with commanded
  //}
  #endif

  return;
}

void LN_Class::tx_req_sw(uint16_t addr, bool dir, bool state){
  if (addr > 2043) { //Address not valid on Loconet. Don't forward it. 
    return; 
  }
  uint8_t tx_index; 
  uint8_t sw1 = addr & 0x7F; //Truncate to addr bits 0-6
  uint8_t sw2 = ((addr >> 8) & 0x0F) | (0x10 * state) | (0x20 * dir); //addr bits 7-10 | (state * 16) | (dir * 32)
  tx_index = tx_packet_getempty();
  tx_packets[tx_index]->state = 1;
  tx_packets[tx_index]->priority = LN_min_priority; 
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

void LN_Class::tx_cab_speed(uint16_t addr, uint8_t spd){
  /*  0x0 = STOP
   *  0x1 = ESTOP
   *  0x2-0x7F speed range */
  int8_t slotnum;
  uint8_t response_size = 4; //Both response packets are 4 bytes long
  uint8_t tx_index = tx_packet_getempty();

  slotnum = loco_select(uint8_t ((addr >> 7) & 0x7F), uint8_t (addr & 0x7F));

  if ((slot_ptr[slotnum]->slot_data[2] != spd) || ((TIME_US - slot_ptr[slotnum]->last_refresh) > 100000)) {
    //Slot data changed or was more than 100ms old, re-send. 
    slot_ptr[slotnum]->slot_data[0] | 0x30; //Set slot busy + slot active 
    slot_ptr[slotnum]->slot_data[2] = spd; //Update speed byte
    Serial.printf("LN_Class::tx_cab_speed cab %u updated with speed %u Preparing Loconet notification \n", addr, spd);

    tx_packets[tx_index]->priority = LN_max_priority; 
    tx_packets[tx_index]->data_len = response_size;
    tx_packets[tx_index]->data_ptr[0] = 0xA0; //OPC_LOCO_SPD
    tx_packets[tx_index]->data_ptr[1] = slotnum; 
    tx_packets[tx_index]->data_ptr[2] = spd;
    tx_packets[tx_index]->Make_Checksum(); //Populate checksum
    tx_packets[tx_index]->state = 1; //Mark as pending packet
    tx_packets[tx_index]->last_start_time = TIME_US;
    Serial.printf("LN_Class::tx_cab_speed ready to send updated throttle info for cab %u \n", addr);
    if (tx_pending == -1) { //Send now if possible. 
      tx_send(tx_index);
    } 
  }
  slot_ptr[slotnum]->next_refresh = 100000000; //100 seconds
  slot_ptr[slotnum]->last_refresh = TIME_US;
  return;
}
void LN_Class::tx_cab_dir(uint16_t addr, bool dir){
  int8_t slotnum;
  uint8_t dirf = dir * 0x20; 
  uint8_t response_size = 4; //response packets is 4 bytes long
  uint8_t tx_index = 0; 
  slotnum = loco_select(uint8_t ((addr >> 7) & 0x7F), uint8_t (addr & 0x7F));

  if ((slot_ptr[slotnum]->slot_data[3] != dirf) || ((TIME_US - slot_ptr[slotnum]->last_refresh) > 100000)) {
    //Slot data changed or was more than 100ms old, re-send. 
    slot_ptr[slotnum]->slot_data[0] | 0x30; //Set slot busy + slot active
    if (dirf == (slot_ptr[slotnum]->slot_data[3]) & 0x20){ //Refresh slot only, no need to update LN. 
      return; 
    }
    if (dirf == 0x20) {
      slot_ptr[slotnum]->slot_data[3] = slot_ptr[slotnum]->slot_data[3] | 0x20; //Set dir bit
    } else {    
      slot_ptr[slotnum]->slot_data[3] = slot_ptr[slotnum]->slot_data[3] & 0xDF; //Clear dir bit
    }
    tx_packets[tx_index]->priority = LN_max_priority; 
    tx_packets[tx_index]->data_len = response_size;
    tx_packets[tx_index]->data_ptr[0] = 0xA1; //OPC_LOCO_DIRF
    tx_packets[tx_index]->data_ptr[1] = slotnum; 
    tx_packets[tx_index]->data_ptr[2] = slot_ptr[slotnum]->slot_data[3];
    tx_packets[tx_index]->Make_Checksum(); //Populate checksum
    tx_packets[tx_index]->state = 1; //Mark as pending packet
    tx_packets[tx_index]->last_start_time = TIME_US;
    Serial.printf("LN_Class::tx_cab_dir ready to send updated throttle info for cab %u \n", addr);
    if (tx_pending == -1) { //Send now if possible. 
      tx_send(tx_index);
    }
  } 
  slot_ptr[slotnum]->next_refresh = 200000000; //200 seconds
  slot_ptr[slotnum]->last_refresh = TIME_US;
  return;  
}

void LN_Class::send_long_ack(uint8_t opcode, uint8_t response) {
  uint8_t tx_index; 
  tx_index = tx_packet_getempty();
  tx_packets[tx_index]->state = 1;
  tx_packets[tx_index]->priority = LN_min_priority;
  tx_packets[tx_index]->data_len = 4;
  tx_packets[tx_index]->data_ptr[0] = 0xB4; //Long Acknowledge
  tx_packets[tx_index]->data_ptr[1] = opcode & 0x7F; //Opcode being given LACK & 0x7F to unset bit 7. 
  tx_packets[tx_index]->data_ptr[2] = response & 0x7F; //Response & 0x7F to make sure bit 7 isn't set. 
  tx_packets[tx_index]->Make_Checksum(); //Populate checksum
  if (tx_pending < 0) { //Needs to send ASAP, so try to send now. 
    tx_send(tx_index); 
  }
  return; 
}

void LN_Class::show_rx_packet(uint8_t index) { //Display a packet's contents
  #if LN_DEBUG == true
    uint8_t i = 0; 
    uint8_t pkt_len = rx_packets[index]->rx_count; 
    Serial.printf("Loconet show RX Packet: ");
    while (i < pkt_len) {
      Serial.printf("%x ", rx_packets[index]->data_ptr[i]); 
      i++;
    }
    Serial.printf(" \n");
  #endif
  return; 
}
void LN_Class::show_tx_packet(uint8_t index) { //Display a packet's contents
  #if LN_DEBUG == true
    uint8_t i = 0; 
    uint8_t pkt_len = tx_packets[index]->data_len; 
    Serial.printf("Loconet show TX Packet: ");
    while (i < pkt_len) {
      Serial.printf("%x ", tx_packets[index]->data_ptr[i]); 
      i++;
    }
    Serial.printf(" \n");
  #endif
  return; 
}

void LN_Class::global_power(char state, bool announce){ //Track power bytes, echo to Loconet
  uint8_t tx_index; 
  char response = 0; 
  #if LN_DEBUG == true  
    Serial.printf("Loconet global_power sending state %c \n", state); 
  #endif
  switch (state) {
    case 48: //'0'
      response = 0x82; //power off
      LN_TRK = LN_TRK & 0xFE; //Set power bit off in TRK byte
      break;
    case 49: //'1'
      response = 0x83; //power on
      LN_TRK = LN_TRK | 0x03; //Set power bit on in TRK byte, set estop release
      break; 
    case 33: //'!'
      response = 0x85; //estop
      LN_TRK = LN_TRK & 0xFD; //Clear estop bit for global estop. 
      break;
    case 98: //'b'
      if (LN_host_mode == ln_master) { //Only the Loconet master is allowed to use OPC_BUSY
        response = 0x81; //busy
      } else {
        announce = false; 
        return; 
      }
      break;
    default:
      #if LN_DEBUG == true
        Serial.printf("Loconet: global_power received invalid character %c \n", state);
      #endif
      announce = false; 
      return; 
  }
  if (announce == true) {
    tx_index = tx_packet_getempty();
    tx_packets[tx_index]->state = 1;
    tx_packets[tx_index]->priority = LN_min_priority;
    tx_packets[tx_index]->data_len = 2;
    tx_packets[tx_index]->data_ptr[0] = response; 
    tx_packets[tx_index]->Make_Checksum(); //Populate checksum
    if (tx_pending < 0) { //Needs to send ASAP, so try to send now. 
      tx_send(tx_index); 
    }
  }
  return; 
}

LN_Class::LN_Class(){ //Constructor, initializes some values.
  time_us = TIME_US;
  rx_pending = -1;
  tx_pending = -1;
  rx_next_new = 0; 
  rx_next_check = 0;
  line_flags = 0; 
  LN_host_mode = ln_silent; //default to ln_silent 
  LN_max_priority = 127; 
  LN_min_priority = 127; 
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
    slot_ptr[slotnum] -> slot_data[0] = 3; //Slot status set to free, 128 step speed
    slot_ptr[slotnum] -> slot_data[1] = low_addr;
    slot_ptr[slotnum] -> slot_data[6] = high_addr;
  
  return slotnum;
}

void LN_Class::slot_read(int8_t slotnum){ //Handle slot reads
  uint8_t i = 0;
  uint8_t tx_index = 0;
  uint8_t response_size = 0;
  //Serial.printf("Slot_read %i \n", slotnum);
  slot_new(slotnum); //Initialize slot if it isn't already. 
  if (slotnum == 123) { //Update fast clock slot
    slot_fastclock_get(); 
  }
  if (slotnum == 124) { //Programmer not supported. 
    //Serial.printf("Loconet Programmer not supported. \n"); 
    send_long_ack(0x7F, 0x7F); 
    return; 
  }
  if (slotnum == 127) { //Read CS opsw data
    slot_opsw_get(); 
  }
  //Loconet response: 
  response_size = 14; //Packet is 14 bytes long, should be 0x0E
  tx_index = tx_packet_getempty();
  tx_packets[tx_index]->state = 1;
  tx_packets[tx_index]->priority = LN_max_priority; 
  tx_packets[tx_index]->data_len = response_size;
  //Serial.printf("Loconet: slot reply data: ");
  i = 0;
  for (i = 0; i < 10; i++) {
    tx_packets[tx_index]->data_ptr[i + 3] = slot_ptr[slotnum]->slot_data[i]; //Copy values from slot_low_data
    //Serial.printf("%x ", slot_ptr[slotnum]->slot_data[i]);
  }
  //Serial.printf(" \n");
  tx_packets[tx_index]->data_ptr[0] = 0xE7; //OPC_SL_RD_DATA
  tx_packets[tx_index]->data_ptr[1] = response_size; 
  tx_packets[tx_index]->data_ptr[2] = slotnum;
  tx_packets[tx_index]->data_ptr[7] = LN_TRK; //Loconet slot global status. Although slot_data[4] is defined, it isn't used. 
  //Serial.printf("Calculating checksum \n");
  tx_packets[tx_index]->Make_Checksum(); //Populate checksum
  i = 0;

  tx_packets[tx_index]->state = 1; //Mark as pending packet
  //Serial.printf("slot_read %i reply in txpacket %i, tx_pending %i \n", slotnum, tx_index, tx_pending); 
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
  if (slotnum == 124) { //Programming session
    ack = 127; //Not implemented
  }
  if (slotnum == 127) { //Command Station OPSW settings
    slot_opsw_set(rx_pkt);  //Save opsw changes 
    ack = 0x6f; 
  }
  return ack;
}

int8_t LN_Class::slot_move(int8_t slot_src, int8_t slot_dest){ //Handle slot moves
  uint8_t i = 0;
  if (!(slot_ptr[slot_src])) { //Invalid source slot, do nothing. 
        return -1; 
      }
  if (!(slot_ptr[slot_dest])) { //Invalid dest slot, create it. 
        slot_new(slot_dest);  
      }      
  if (slot_src == 0) {//When activated it sets all locomotives to speed 0 functions off
    Serial.printf("Requested slot 0, this shouldn't happen so stop the train \n");
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
  #if LN_DEBUG == true
    Serial.printf("Loconet: Seconds set %i \n", s_seconds); 
  #endif
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
  if (slot_ptr[slotnum]->slot_data[7] == 0) { //Only change slot validity if it is invalid. 
    slot_ptr[slotnum]->slot_data[7] = 32; //Bit D6 = 1 for valid clock data  
  }
  
  slot_ptr[slotnum]->slot_data[6] = Fastclock.days; //Days since fast clock init
  slot_ptr[slotnum]->slot_data[5] = slot_hours + Fastclock.hours; //128 - 24
  slot_ptr[slotnum]->slot_data[4] = LN_TRK; //Global slot byte;
  slot_ptr[slotnum]->slot_data[3] = slot_minutes + Fastclock.minutes; //127 - 60 or 128 - 60 depending on DCS100 compat mode
  slot_ptr[slotnum]->slot_data[2] = minsh; //127 - Fastclock.minutes_rem_uS / 472441; //frac_minutes_us is the remaining uS after calculating minutes. FRAC_MINS_H, 0-127 tick count
  slot_ptr[slotnum]->slot_data[1] = minsl; //127 - (Fastclock.frac_minutes_uS - (slot_ptr[123]->slot_data[2] * 472441)) / 3872; //FRAC_MINS_L, 0-127 tick count
  slot_ptr[slotnum]->slot_data[0] = Fastclock.set_rate; //Clock multiplier
//  Serial.printf("Loconet: Fastclock slot checked, minutes %u, minsh %u, minsl %u \n", slot_ptr[slotnum]->slot_data[3], slot_ptr[slotnum]->slot_data[2], slot_ptr[slotnum]->slot_data[1]);  
  return; 
}

void LN_Class::slot_opsw_set(uint8_t rx_pkt){ //Write CS opsw data
  int i = 0; 
    //Serial.printf("Loconet: Saving opsw packet: ");
    for (i = 0; i < 10; i++) {
      slot_ptr[127]->slot_data[i] = rx_packets[rx_pkt]->data_ptr[i + 3]; //Copy values from slot_data
      //Serial.printf("%x ", slot_ptr[127]->slot_data[i]);
    }
   //Serial.printf(" \n");
   if ((slot_ptr[127]->slot_data[5] & 0x40) == 0x40) { //OPSW 39 memory reset active
    slot_new(127); //Reinitialize slot 127 
    Serial.printf("Loconet: CS opsw data reset to defaults \n"); 
   }
  //Opsw bit locking, prevent incompatible/unsupported options from being selected
  slot_ptr[127]->slot_data[6] & 0x0A; //opsw 42, disable beep on purge. opsw 44, 120 slots.
  slot_ptr[127]->slot_data[5] & 0x07; //opsw 36 - 39 are memory resets. When not impemented, keep them set to off. 
  slot_ptr[127]->slot_data[2] & 0x77; //opsw 20 address 0 not allowed. opsw 21-23 are step modes,
  
  //TODO: Save slot 127 data to eeprom or filesystem
  Serial.printf("Loconet: CS opsw data written \n"); 
   return;
}

void LN_Class::slot_opsw_get(){//Read CS opsw data
  //TODO: Read slot 127 data from eeprom or filesystem
  Serial.printf("Loconet: opsw byte 0 has value %x \n", slot_ptr[127]->slot_data[0]);
  Serial.printf("Loconet: CS opsw data read \n"); 
  return;
}

int8_t LN_Class::slot_new(uint8_t slotnum) { //Initialize empty slots
  if (!(slot_ptr[slotnum])){ //Only create it when it doesn't already exist
    //Serial.printf("Initializing Loconet slot %u \n", slotnum);
    slot_ptr[slotnum] = new LN_Slot_data; 
    if (!(slot_ptr[slotnum])){
      Serial.printf("Failure allocating slot %u \n", index);
      return -1; 
    }
  }  
  slot_ptr[slotnum] -> slot_data[0] = 0; //Slot status set to free.
  slot_ptr[slotnum]->last_refresh = TIME_US;
  slot_ptr[slotnum]->next_refresh = 200000000; //Expect refresh within 200 seconds 
    
  //Implement special slot initialization / stored value reset
  if (slotnum == 123) { //Fast Clock
    slot_ptr[slotnum]->last_refresh = 0; //last refresh of 0 forces initial load quickly.   
    slot_ptr[slotnum]->next_refresh = 90000000; //Fastclock sends pings around every 80-100 seconds instead of receiving within 200. 
    slot_ptr[slotnum]->slot_data[9] = 0x70; //ID2, 0x7x = PC
    slot_ptr[slotnum]->slot_data[8] = 0x7F; //ID1, 0x7F = PC
  }
  if (slotnum == 127) { //OPSW slot
    //Placeholder: JMRI gave a DCS100 OPSW write as Loconet RX Packet: ef e 7f 10 0 8 0 6 0 8 0 0 0 77 
    slot_ptr[slotnum]->slot_data[0] = 0x10; //0x02 = opsw 2, booster only. 0x010 = opsw 5, CS Master mode
    slot_ptr[slotnum]->slot_data[1] = 0x00;
    slot_ptr[slotnum]->slot_data[2] = 0x08;  
    //Byte 02 opsw 20, Address 0 stretching. Forced off. opsw 21-23 fx and step mode. 0x10 on for FX enabled. 
    slot_ptr[slotnum]->slot_data[3] = 0x00;  //0x02 is opsw 26 C, enable routes
    slot_ptr[slotnum]->slot_data[4] = 0x06; //0x01 = opsw 25 disable alias, 0x02 = opsw 26 enable routes, 0x04 = opsw 27 bushby bit
    slot_ptr[slotnum]->slot_data[5] = 0x00;  
    //Byte 05: opsw36 consist reset = 0x08, opsw37 route reset = 0x10, opsw38 roster reset = 0x20
    //opsw39 all memory reset = 0x40
    slot_ptr[slotnum]->slot_data[6] = 0x0A;  //0x02 = opsw 42 C, disable beep on purge. 0x08 = opsw 44 C, 120 slots
    slot_ptr[slotnum]->slot_data[7] = 0x00;
    slot_ptr[slotnum]->slot_data[8] = 0x00;
    slot_ptr[slotnum]->slot_data[9] = 0x00;
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
