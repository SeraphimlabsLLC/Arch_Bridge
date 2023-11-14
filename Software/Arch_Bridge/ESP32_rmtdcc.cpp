#ifndef ESP32_RMTDCC_HW_H
  #include "ESP32_rmtdcc.h"
#endif

/* NMRA allows up to 32 bytes per packet, the max length would be 301 bits transmitted and need 38 bytes (3 bits extra). 
 * DCC_EX is limited to only 11 bytes max. Much easier to account for and uses a smaller buffer. 
  */

/*      
       // get the ring buffer handle
      rmt_get_ringbuf_handle(rx_inputs[c].channel, &rb);
      
      // get items, if there are any
      items = (rmt_item32_t*) xRingbufferReceive(rb, &rx_size, 10);
 */ 
Rmtdcc dcc; //Define track channel objects with empty values.
extern uint64_t time_us; 

void Rmtdcc::loop_process() { //Workflow loop
  rmt_rx();
//  rx_scan(); 

  return;
}
uint8_t Rmtdcc::rmt_rx() {
  time_us = esp_timer_get_time();
  if ((time_us - rmt_rx_detect) < DCC_1_HALFPERIOD){
    return 0;
  }
  rmt_rx_detect = time_us;
  uint8_t bytes_read = 0;
  uint8_t num_bytes = 0;
  uint8_t i = 0;
  //uint32_t APB_Div = getApbFrequency() / 1000000; //Reported bus frequency in MHz
  //uint8_t rx_size = 0;

 // if (rmtReceiveCompleted(DIR_MONITOR)) { //RMT has data for reading
  //  rmtReadAsync(DIR_MONITOR, rx_data, rx_data_size); //Populates rx_data with rmt data and rx_data_size with the size of data
 // }
  
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
   if ((bitzero == bitone) && (bitzero > -1)) { //Both halves agree on 0 or 1
     if ((bitzero == 0) || ((dur_delta > -6) && (dur_delta < 6))){ //If this is a 0 or both halves are within 6uS, it is a valid bit. 

       //Preamble detect
       if (bitzero == 1) {
         consecutive_ones++; 
       }
       if ((consecutive_ones > 12) && ( bitzero == 0) && (rx_pending < 0)) { //13 1 bits including last stop + 0 bit + no pending = pending
         consecutive_ones = 0; 
         last_preamble = esp_timer_get_time(); //Store the time of the last preamble
         rx_pending = rx_packet_getempty();
         rx_packets[rx_pending]->state = 1;

         //Start counting off bits and bytes into the new empty packet. 
         rx_num_bits = 8; //Set to 8 so that it counts aftee the start bit
         rx_num_bytes = 0;     
         rx_byteout = 0;      
       }
       if (bitzero == 0) {
         consecutive_ones = 0; 
       }
       if (rx_pending < 0) { 
         continue; //No packet, so no need to keep it. Skip the rest of this loop and process the next bit.  
       }
       if (rx_num_bits < 8) { //Valid data in bits 0-7
         rx_byteout = rx_byteout << 1; //Shift right to make room. 
         rx_byteout = rx_byteout | bitzero; //OR the new bit onto the byte, since the bit shift added a zero at the end.
       }
       if (rx_num_bits > 7) { // Bit 8 is start/stop
         rx_packets[rx_pending]->data_len = rx_num_bytes;         
         rx_packets[rx_pending]->packet_data[rx_packets[rx_pending]->data_len] = rx_byteout;
         if (bitzero == 1) { //Bit 8 is 1, end of packet
           rx_packets[rx_pending]->state = 3; //packet rx complete
           rx_packets[rx_pending]->Read_Checksum(); 
           rx_pending = -1;
         }
         rx_num_bits = 0;
         rx_num_bytes++; //Needs to not execute on packet start.
         bytes_read++;
       } else {
         rx_num_bits++; 
       }
     }
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
void Rmtdcc::rx_scan() {

  return;
}


void Rmtdcc::rmt_rx_init(){ 
  rx_ch = NULL; 
  bool rmt_recv = false;
  
  /**
    Initialize the object
    
    New Parameters in Arduino Core 3: RMT tick is set in the rmtInit() function by the 
    frequency of the RMT channel. Example: 100ns tick => 10MHz, thus frequency will be 10,000,000 Hz
    Returns <true> on execution success, <false> otherwise
*/
//bool rmtInit(int pin, rmt_ch_dir_t channel_direction, rmt_reserve_memsize_t memsize, uint32_t frequency_Hz);
//bool rmtInit(int DIR_MONITOR, rmt_ch_dir_t channel_direction, rmt_reserve_memsize_t memsize, uint32_t frequency_Hz);
//if ((rmtInit(DIR_MONITOR, RMT_RX_MODE, RMT_MEM_64, 1000000)) == NULL) {
//  Serial.printf("Failed to initialize RMT Reciver using Arduino interface \n");
//}


  
  /*
  // Configure the RMT channel for RX to audit incoming DCC
    uint32_t APB_Div = getApbFrequency() / 1000000; //Reported bus frequency in MHz
    //Serial.printf("APB Frequency %u MHz \n", APB_Div);
    rmt_config_t rmt_rx_config = {                                           
        .rmt_mode = RMT_MODE_RX,                
        .channel = rmt_channel_t(DIR_MONITOR_RMT),                  
        .gpio_num = gpio_num_t(DIR_MONITOR),                       
        .clk_div = APB_Div,       
        .mem_block_num = 2, //Each block is 64 symbols at 32 bytes each                 
        .flags = 0,  
        //rmt_ll_rx_enable_pingpong(rmt_dev_t *dev, uint32_t channel, true)                           
        .rx_config = {                          
            .idle_threshold = 255, //((DCC_0_MAX_HALFPERIOD + 2) * APB_Div), //Glitch filter has max allowed of 255 ticks   
            .filter_ticks_thresh = ((DCC_1_MIN_HALFPERIOD - 2) * APB_Div), //Idle timeout        
            .filter_en = true,                  
        }                                       
    };
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
  //ESP_ERROR_CHECK(rmt_driver_install(rmt_rx_config.channel, 1024, ESP_INTR_FLAG_LOWMED|ESP_INTR_FLAG_SHARED));  
  ESP_ERROR_CHECK(rmt_isr_register(rmt_isr_handler, NULL, 0, NULL));
  ESP_ERROR_CHECK(rmt_rx_start(rmt_channel_t(DIR_MONITOR_RMT), true)); //Enable RMT RX, true to erase existing RX data
  //ESP_ERROR_CHECK(rmt_get_ringbuf_handle(rmt_channel_t(DIR_MONITOR_RMT), &rmt_rx_handle)); //Ring buffer handle
  Serial.printf("DCC Auditing Initialized using ESP RMT, handle %d \n", rmt_rx_handle); 
*/  
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


  #ifdef BOARD_TYPE_DYNAMO //for now only do this on Dynamo
    //Initialize RMT for DCC TX 
void Rmtdcc::rmt_tx_init(){
  uint32_t APB_Div = getApbFrequency() / 1000000;
  rmt_config_t rmt_tx_config;
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
  Serial.printf("RMT TX Initialized \n"); 

  return;
}
  #endif

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
  item->level1    = 0;
  item->duration1 = DCC_0_HALFPERIOD;
}

void Rmtdcc::setEOT(rmt_item32_t* item) {
  item->val = 0;
}*/

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
