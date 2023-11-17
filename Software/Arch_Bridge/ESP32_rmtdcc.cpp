#ifndef ESP32_RMTDCC_HW_H
  #include "ESP32_rmtdcc.h"
#endif

/* NMRA allows up to 32 bytes per packet, the max length would be 301 bits transmitted and need 38 bytes (3 bits extra). 
 * DCC_EX is limited to only 11 bytes max. Much easier to account for and uses a smaller buffer. 
  */

Rmtdcc dcc; //Define track channel objects with empty values.
extern uint64_t time_us;

void Rmtdcc::loop_process() { //Workflow loop
  rmt_rx();
  rmt_scan(); 

  return;
}
uint8_t Rmtdcc::rmt_rx() {
  rmt_rx_detect = time_us;
  uint8_t bytes_read = 0;
  uint8_t num_bytes = 0;
  uint8_t i = 0;
  //uint32_t APB_Div = getApbFrequency() / 1000000; //Reported bus frequency in MHz
  //uint8_t rx_size = 0;

/*
 if (rmtReceiveCompleted(rx_ch)) { //RMT has data for reading
  //'bool rmtReadAsync(rmt_obj_t*, rmt_data_t*, size_t, void*, bool, uint32_t)'
   rmtReadAsync(rx_ch, rx_rmt_data, rx_data_size, NULL , false, 12); //Populates rx_data with rmt data and rx_data_size with the size of data
   rx_data_size = 1; 
 }*/
  
/*  volatile rmt_item32_t* item;
  item = RMTMEM.chan[DIR_MONITOR_RMT].data32;
  std::cout << "item d0: " << (item->duration0 / APB_Div) << " L0: " << item->level0  << " d1: " << (item->duration1 / APB_Div)  << " :L1: " << item->level1  << std::endl;

  rmt_item32_t* dcc_items = NULL;
   dcc_items = (rmt_item32_t*) xRingbufferReceive(rmt_rx_handle, &rx_size, 10); //Returns items in rmt_rx_handle with size rmt_rx_size within 10 RTOS ticks
   Serial.printf("RMT contained %u items \n", rx_size); */

  rmt_data_t* databit = NULL;
  int dur_delta = 0;
  int8_t bitzero = -1;
  int8_t bitone = -1;
  
  //while (rx_rmt_data[i] != NULL) {
  while (i < 32){
   if (!&rx_rmt_data[i]) { //Avoids crash if no data returned. 
    return 0; 
   }
   databit = &rx_rmt_data[i];
   //Straightforward decoding when the RMT is in-phase with it and 1 symbol = 1 bit. 
   if ((databit->duration0 > DCC_1_MIN_HALFPERIOD) && (databit->duration0 < DCC_1_MAX_HALFPERIOD)) {
      bitzero = 1; 
   }
   if ((databit->duration0 > DCC_0_MIN_HALFPERIOD) && (databit->duration0 < DCC_0_MAX_HALFPERIOD)) {
      bitzero = 0; 
   }
   if ((databit->duration1 > DCC_1_MIN_HALFPERIOD) && (databit->duration1 < DCC_1_MAX_HALFPERIOD)) {
      bitone = 1; 
   }
   if ((databit->duration1 > DCC_0_MIN_HALFPERIOD) && (databit->duration1 < DCC_0_MAX_HALFPERIOD)) {
      bitone = 0; 
   }
   dur_delta = databit->duration0 - databit->duration1;
   
   if ((bitzero != bitone) && (bitzero > -1) && (bitone > -1)) { //Halves don't match, but both have valid times. Use rx_last_bit duration1 as bitone.  
     if (!rx_last_bit) { //Haven't seen any bits prior. Go to the next bit.
      continue; 
     }
     
     if ((rx_last_bit->duration1 > DCC_1_MIN_HALFPERIOD) && (rx_last_bit->duration1 < DCC_1_MAX_HALFPERIOD)) {
       bitone = 1; 
     }
     if ((rx_last_bit->duration1 > DCC_0_MIN_HALFPERIOD) && (rx_last_bit->duration1 < DCC_0_MAX_HALFPERIOD)) {
       bitone = 0; 
     } 
     dur_delta = databit->duration0 - rx_last_bit->duration1; 
   }
   if ((bitzero + bitone == 0) || ((bitzero + bitone == 2) && ((dur_delta > -6) && (dur_delta < 6)))){ 
    //Both bits 0 or both bits 1 && both halves of 1 within 6uS
       rx_bit_processor(bitzero); //Process the bits into packet
   } else { //Invalid bit. Drop the bit and try again. 
     rx_byteout = 0; 
     rx_num_bits = 0; 
   }
   rx_last_bit = &rx_rmt_data[i]; //Store the symbol we just decoded for the next cycle in case of a phase straddle
   i++;
  }
    
  //vRingbufferReturnItem(rmt_rx_handle, (void*) dcc_items); //Free up space in the rmt_rx ring 
  //rmt_memory_rw_rst(rmt_channel_t (DIR_MONITOR_RMT));
  return num_bytes;
}
     
     
uint8_t Rmtdcc::rx_bit_processor(bool input){
  //Preamble detect
  if (input == 1) {
    consecutive_ones++; 
  }
  if ((input == 0) && (consecutive_ones >= 12) && (rx_pending < 0)) { //12 1 bits including last stop + 0 bit + no pending = pending
    //Found start bit. Reset counters to sort what follows into a new empty packet. 
    last_preamble = esp_timer_get_time(); //Store the time of the last preamble
    rx_pending = rx_packet_getempty();
    rx_packets[rx_pending]->state = 1;
    rx_num_bits = 0; //Start counting new byte
    rx_num_bytes = 0; //packet byte 0
    rx_byteout = 0; // empty output byte
    consecutive_ones = 0; //Preamble count
    Serial.printf("New Packet %u started \n", rx_pending); 
    return 1; //Packet was created and will start populating on the next call    
  }
  if (input == 0) {
    consecutive_ones = 0; 
  }
  if (rx_pending < 0) { 
    return 0; //No packet, so no need to continue. Return to bit detector. 
  }
  //Bit Counting
  if (rx_num_bits < 8) { //Valid data in bits 0-7, bit 8 indicates next byte (0) or end of data (1)
    rx_byteout = rx_byteout << 1; //Shift right to make room. 
    rx_byteout = rx_byteout | input; //OR the new bit onto the byte, since the bit shift added a zero at the end.
  } else { //rx_num_bits >= 8, copy the finished byte into the packet and check if the packet is complete. 
    rx_packets[rx_pending]->data_len = rx_num_bytes;         
    rx_packets[rx_pending]->packet_data[rx_packets[rx_pending]->data_len] = rx_byteout;
    Serial.printf("RMTDCC: Byte %x complete, %u in packet %u \n", rx_byteout, rx_num_bytes, rx_pending); 
    if (input == 1) { //Bit 8 is 1, mark packet complete. Checksum it and reset rx_pending. 
      rx_packets[rx_pending]->state = 3; //packet rx complete
      rx_packets[rx_pending]->Read_Checksum(); 
      rx_pending = -1;
    }
    rx_num_bytes++; //Increment byte counter
    rx_num_bits = 0; //Reset rx_num_bits
  }
  rx_num_bits++; 
  return 1;
}

void Rmtdcc::rmt_scan() {

  return;
}

/*
void IRAM_ATTR rmt_isr_handler(void* arg){
  //read RMT interrupt status.
  uint32_t intr_st = RMT.int_st.val;

//  if (intr_st && ch4_rx_thr_event_int_raw){
//    std::cout << "RMT CH4 Threshold event"; 
//  }
    
  
  RMT.conf_ch[DIR_MONITOR_RMT].conf1.rx_en = 0;
  RMT.conf_ch[DIR_MONITOR_RMT].conf1.mem_owner = RMT_MEM_OWNER_TX;
  volatile rmt_item32_t* item = RMTMEM.chan[DIR_MONITOR_RMT].data32;
  if (item) Serial.print ((item)->duration0);
  
//  RMT.conf_ch[DIR_MONITOR_RMT].conf1.mem_wr_rst = 1;
//  RMT.conf_ch[DIR_MONITOR_RMT].conf1.mem_owner = RMT_MEM_OWNER_RX;
//  RMT.conf_ch[DIR_MONITOR_RMT].conf1.rx_en = 1;

  //clear RMT interrupt status.
  RMT.int_clr.val = intr_st;
}
*/ 

void Rmtdcc::rmt_rx_init(){ 
  rx_ch = NULL; 
  bool rmt_recv = false;
  Serial.printf("DCC RMT Initialized \n");

/* Arduino Library 
//bool rmtInit(int pin, rmt_ch_dir_t channel_direction, rmt_reserve_memsize_t memsize, uint32_t frequency_Hz);
//bool rmtInit(int DIR_MONITOR, rmt_ch_dir_t channel_direction, rmt_reserve_memsize_t memsize, uint32_t frequency_Hz);
rmt_recv = rmtInit(DIR_MONITOR, RMT_RX_MODE, RMT_MEM_192);

  if (rmt_recv == false) {
    Serial.printf("Failed to initialize RMT Reciver using Arduino interface \n");
  }
  return;
}*/

  // Configure the RMT channel for RX to audit incoming DCC
  uint32_t APB_Div = getApbFrequency() / 1000000; //Reported bus frequency in MHz
  /*
  //Serial.printf("APB Frequency %u MHz \n", APB_Div);
  rmt_rx_channel_config rmt_rx_config = {                                           
    .clk_src = RMT_CLK_SRC_DEFAULT,   // select source clock
    .resolution_hz = 1 * 1000 * 1000, // 1 MHz tick resolution, i.e., 1 tick = 1 µs
    .mem_block_symbols = 256,         // memory block size, 64 symbols * 4 blocks = 1024 Bytes
    .gpio_num = gpio_num_t(DIR_MONITOR),                    // GPIO number
    .flags.invert_in = false,         // do not invert input signal
    .flags.with_dma = false,          // do not need DMA backend    
  };
  */
  /*        
    .channel = rmt_channel_t(DIR_MONITOR_RMT),                  
    .gpio_num = gpio_num_t(DIR_MONITOR),                       
    .clk_div = APB_Div,       
    .mem_block_num = 4, //Each block is 64 symbols at 32 bytes each                 
    .flags = 0,  
    //rmt_ll_rx_enable_pingpong(rmt_dev_t *dev, uint32_t channel, true)                           
    .rx_config = {                          
      .idle_threshold = 255, //((DCC_0_MAX_HALFPERIOD + 2) * APB_Div), //Glitch filter has max allowed of 255 ticks   
      .filter_ticks_thresh = ((DCC_1_MIN_HALFPERIOD - 2) * APB_Div), //Idle timeout        
      .filter_en = true,                  
    }                                       
  };*/
  /*
  ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_rx_config, &rx_ch));
  */
/*If possible configure:
* RMT_MEM_RX_WRAP_EN_CHm so it reads in a loop. 
* RMT_CHm_RX_LIM_REG set the number of RX entries before threshold interrupt
* RMT_CHm_RX_THR_EVENT_INT_RAW(m = 4-7)  so that it interrupts on RX_LIM_REG instead of full
 */
 /*
  ESP_ERROR_CHECK(rmt_config(&rmt_rx_config));
  //ESP_ERROR_CHECK();
  //ESP_ERROR_CHECK();
  //ESP_ERROR_CHECK();
  //ESP_ERROR_CHECK();
  ESP_ERROR_CHECK(rmt_set_memory_owner(rmt_channel_t(DIR_MONITOR_RMT), rmt_mem_owner_t (1))); //Set RMT RX memory owner
  ESP_ERROR_CHECK(rmt_driver_install(rmt_rx_config.channel, 1024, ESP_INTR_FLAG_LOWMED|ESP_INTR_FLAG_SHARED));  
//  ESP_ERROR_CHECK(rmt_isr_register(rmt_isr_handler, NULL, 0, NULL));
//  ESP_ERROR_CHECK(rmt_rx_start(rmt_channel_t(DIR_MONITOR_RMT), true)); //Enable RMT RX, true to erase existing RX data
  ESP_ERROR_CHECK(rmt_get_ringbuf_handle(rmt_channel_t(DIR_MONITOR_RMT), &rmt_rx_handle)); //Ring buffer handle
  */
//  Serial.printf("DCC Auditing Initialized using ESP RMT, handle %d \n", rmt_rx_handle); 
  
  return;
}

uint8_t Rmtdcc::rx_packet_getempty(){ //Scan rx_packets and return 1st empty slot
  uint8_t count = 0; 
  while (count < DCC_RX_Q){  
    if (!rx_packets[rx_next_new]){ //Isn't initialized yet, fix this
      rx_packets[rx_next_new] = new DCC_packet();
    }
    if (rx_packets[rx_next_new]->state == 0) { //Exists and is marked empty, claim it.  
      break; //break out of the while loop and use this result. 
    }
    rx_next_new++; 
    if (rx_next_new > DCC_RX_Q) {
      rx_next_new = 0;
    }
  }
  if (count == DCC_RX_Q) { //Checked all slots without a result
    Serial.printf("WARNING: rx_packets out of space. RX packet %d will be overwritten, consider increasing LN_RX_Q \n", rx_next_new);
  }
  rx_packets[rx_next_new]->reset_packet(); //Sets it to defaults again   
  return rx_next_new;
}

#if BOARD_TYPE == DYNAMO //for now only do this on Dynamo
    //Initialize RMT for DCC TX 
void Rmtdcc::rmt_tx_init(){
  uint32_t APB_Div = getApbFrequency() / 1000000;
/*  rmt_config_t rmt_tx_config;
  // Configure the RMT channel for TX
  rmt_tx_config.rmt_mode = RMT_MODE_TX;
  rmt_tx_config.channel = rmt_channel_t(DIR_OVERRIDE_RMT);
  rmt_tx_config.clk_div = APB_Div; //Multiplier is now dynamic based on reported APB bus frequency. 
  rmt_tx_config.gpio_num = gpio_num_t(DIR_OVERRIDE);
  rmt_tx_config.mem_block_num = 2; // With longest DCC packet 11 inc checksum (future expansion)
                            // number of bits needed is 22preamble + start +
                            // 11*9 + extrazero + EOT = 124
                            // 2 mem block of 64 RMT items should be enough
  ESP_ERROR_CHECK(rmt_config(&rmt_tx_config));
  // NOTE: ESP_INTR_FLAG_IRAM is *NOT* included in this bitmask
  ESP_ERROR_CHECK(rmt_driver_install(rmt_tx_config.channel, 0, ESP_INTR_FLAG_LOWMED|ESP_INTR_FLAG_SHARED));    
 */
  
  Serial.printf("RMT TX Initialized \n"); 
  return;
}

/*
//From DCC-EX ESP32 branch DCCRMT.cpp. 
void Rmtdcc::setDCCBit1(rmt_item32_t* item) {
  item->level0    = 1;
  item->duration0 = DCC_1_HALFPERIOD;
  item->level1    = 0;
  item->duration1 = DCC_1_HALFPERIOD;
}

void Rmtdcc::setDCCBit0(rmt_item32_t* item) {
  item->level0    = 1;
  item->duration0 = DCC_0_HALFPERIOD;
  item->level1    = 0
}

void Rmtdcc::setEOT(rmt_item32_t* item) {
  item->val = 0;
}*/

#endif

void rmt_loop() { //Reflector into Rmtdcc::loop_process();
  dcc.loop_process(); 
}


/*************************
 * dccpacket definitions *
 *************************/


void DCC_packet::Make_Checksum(){ //Populate last byte with valid checksum

  return;
}
bool DCC_packet::Read_Checksum(){ //Verify checksum, returns true if valid, false if invalid.
  bool valid = false;

  return valid;
}
uint8_t DCC_packet::packet_size_check(){ //Check that a packet has a valid size. 
  uint8_t packet_size = 0;

  return packet_size;
}
void DCC_packet::reset_packet(){ //Reset packet slot to defaults
  return;
}
