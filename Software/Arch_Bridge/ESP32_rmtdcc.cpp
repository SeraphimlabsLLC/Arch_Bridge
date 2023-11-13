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

 
void Rmtdcc::rx_scan() {

  return;
}

void Rmtdcc::rmt_rx_init(){
  // Configure the RMT channel for RX to audit incoming DCC
    uint32_t APB_Div = getApbFrequency() / 1000000; //Reported bus frequency in MHz
    //Serial.printf("APB Frequency %u MHz \n", APB_Div);
    rmt_config_t rmt_rx_config = {                                           
        .rmt_mode = RMT_MODE_RX,                
        .channel = rmt_channel_t(DIR_MONITOR_RMT),                  
        .gpio_num = gpio_num_t(DIR_MONITOR),                       
        .clk_div = APB_Div,       
        .mem_block_num = 4,                   
        .flags = 0,                             
        .rx_config = {                          
            .idle_threshold = (DCC_1_MIN_HALFPERIOD * APB_Div),    //Timeout filter       
            .filter_ticks_thresh = (DCC_0_MAX_HALFPERIOD * APB_Div), //Glitch filter       
            .filter_en = false,                  
        }                                       
    };
   
  ESP_ERROR_CHECK(rmt_config(&rmt_rx_config));
  //Serial.printf("Loading RMT RX Driver \n");
  // NOTE: ESP_INTR_FLAG_IRAM is *NOT* included in this bitmask
  //It may be necessary to replace the 0 with a ring buffer size. Use rmt_get_ringbuf_handle(rmt_channel_t channel, RingbufHandle_t *buf_handle) to get access.
  ESP_ERROR_CHECK(rmt_driver_install(rmt_rx_config.channel, 256, ESP_INTR_FLAG_LOWMED|ESP_INTR_FLAG_SHARED)); 
  rmt_set_mem_block_num((rmt_channel_t) DIR_MONITOR_RMT, 4); 
  ESP_ERROR_CHECK(rmt_rx_start(rmt_channel_t(DIR_MONITOR_RMT), true)); //Enable RMT RX, true to erase existing RX data
  Serial.printf("DCC Auditing Initialized using ESP RMT \n"); 
  return;
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

/*************************
 * dccpacket definitions *
 *************************/


void dccpacket::Make_Checksum(){ //Populate last byte with valid checksum

  return;
}
bool dccpacket::Read_Checksum(){ //Verify checksum, returns true if valid, false if invalid.
  bool valid = false;

  return valid;
}
uint8_t dccpacket::packet_size_check(){ //Check that a packet has a valid size. 
  uint8_t packet_size = 0;

  return packet_size;
}
void dccpacket::reset_packet(){ //Reset packet slot to defaults
  return;
}

 
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
}
