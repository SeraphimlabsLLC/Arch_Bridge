#ifndef ESP32_TRACKS_HW_H
  #include "ESP32_Tracks_HW.h"
#endif

//TrackChannel(enable_out_pin, enable_in_pin, uint8_t reverse_pin, brake_pin, adc_channel, adcscale, adc_overload_trip)
TrackChannel DCCSigs[4]; //Define track channel objects with empty values.
uint8_t max_tracks = 0;

//ADC Handle
//adc_oneshot_unit_handle_t adc1_handle;

void ESP32_Tracks_Setup(){ //Populates track class with values including ADC
  #ifdef BOARD_TYPE_DYNAMO //If this is a Dynamo type booster, define these control pins.
  Serial.print("Configuring board for Dynamo booster mode \n");
    gpio_reset_pin(gpio_num_t(MASTER_EN)); //Is used on both boards
    gpio_set_direction(gpio_num_t(MASTER_EN), GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio_num_t(MASTER_EN), GPIO_FLOATING);  

    gpio_reset_pin(gpio_num_t(DIR_MONITOR));
    gpio_set_direction(gpio_num_t(DIR_MONITOR), GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio_num_t(DIR_MONITOR), GPIO_FLOATING);  
    //ESP_rmt_rx_init(); //Initialize DIR_MONITOR for RMT monitoring

    gpio_reset_pin(gpio_num_t(DIR_OVERRIDE));
    gpio_set_direction(gpio_num_t(DIR_OVERRIDE), GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(gpio_num_t(DIR_OVERRIDE), GPIO_PULLDOWN_ONLY); 
#endif
#ifdef BOARD_TYPE_ARCH_BRIDGE //If this is an arch bridge, define these control pins.
  Serial.print("Configuring board for Arch Bridge mode \n");

  gpio_reset_pin(gpio_num_t(MASTER_EN)); //Is used on both boards
  gpio_set_direction(gpio_num_t(MASTER_EN), GPIO_MODE_OUTPUT);
  gpio_set_pull_mode(gpio_num_t(MASTER_EN), GPIO_PULLUP_PULLDOWN);    
  gpio_set_level(gpio_num_t(MASTER_EN), 1); //Turn OE on 
#endif
  //ADC Setup
  adc1_config_width(ADC_WIDTH_12Bit);//config adc1 width
/*
  
  adc_oneshot_unit_init_cfg_t init_config1 = {
    .unit_id = ADC_UNIT_1,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
*/
  TRACK_1 //each track definition subsitites the complete function call to DCCSigs::SetupHW 
  #ifdef TRACK_2
  TRACK_2
  #endif 
  #ifdef TRACK_3
  TRACK_3
  #endif
  #ifdef TRACK_4
  TRACK_4
  #endif
  return;
}

void TrackChannel::SetupHW(uint8_t en_out_pin, uint8_t en_in_pin, uint8_t rev_pin, uint8_t brk_pin, uint8_t adcpin, uint32_t adcscale, int32_t adcoffset, uint32_t adc_ol_trip) { 
    max_tracks++; //Add 1 to max tracks
    index = max_tracks; //store the index of this track. 
    #ifdef DEBUG
      Serial.printf("Configuring Track %d \n" , index);
    #endif
    powerstate = 0; //Power is off by default
    powermode = 0; //Mode is not configured by default  
    //Copy config values to class values
    enable_out_pin = gpio_num_t(en_out_pin);
    enable_in_pin = gpio_num_t(en_in_pin);
    reverse_pin = gpio_num_t(rev_pin);
    brake_pin = gpio_num_t(brk_pin);
    adc_channel = adc1_channel_t (adcpin - 1);
    adc_scale = adcscale;
    adc_offset = adcoffset * 1000; //Fix scale factor to work with ticks * 1000000 while keeping scale *1000 consistency
    adc_overload_trip = adc_ol_trip * 1000; //Fix scale factor to work with ticks * 1000000 while keeping scale *1000 consistency
    //Configure Enable Out
    gpio_reset_pin(gpio_num_t(enable_out_pin)); //Always configure enable_out_pin
    gpio_set_direction(gpio_num_t(enable_out_pin), GPIO_MODE_OUTPUT_OD); //Open Drain output, level shifters have integral pull-up
    gpio_set_level(gpio_num_t(enable_out_pin), 0); //Enable Out stays off until a mode is selected. 
    //Configure Enable In if present
    if (enable_in_pin != 0) { //Only configure enable_in_pin if it is nonzero
      gpio_reset_pin(gpio_num_t(enable_in_pin));
      gpio_set_direction(gpio_num_t(enable_in_pin), GPIO_MODE_INPUT);
    }
/*
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_11,   //ADC range 0-3.1
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, adc_channel, &config));
    */
    adc1_config_channel_atten(adc_channel,ADC_ATTEN_11db);//config attenuation
    adc_current_ticks = adc_previous_ticks = adc_base_ticks = 0; //Set all 3 ADC values to 0 initially
    ModeChange(0); //set power mode none, which will also set power state off.
    adc_read(); //actually read the ADC
    adc_base_ticks = adc_current_ticks; //Copy the zero output ticks to adc_base_ticks
    return;
}

void TrackChannel::ModeChange (uint8_t newmode){ //Updates GPIO modes when changing powermode
  //powermode 0 = none, 1 = DCC_external, 2 = DCC_override, 3 = DC.  
  gpio_set_level(gpio_num_t(enable_out_pin), 0); //Always turn the track off before changing modes
  switch (newmode) {     
    case 0: //Not Configured, reset the pins and don't configure them
      gpio_reset_pin(gpio_num_t(reverse_pin));
      gpio_reset_pin(gpio_num_t(brake_pin));
      Serial.printf("Track %d configured to NO MODE \n", index);
      StateChange(0); //set state to off since it isn't configured anyway. 
    break;
    case 1: //DCC external. Configure enable_in if used and rev/brake
      gpio_reset_pin(gpio_num_t(reverse_pin));
      gpio_set_direction(gpio_num_t(reverse_pin), GPIO_MODE_INPUT);  //Consider GPIO_MODE_INPUT_OUTPUT_OD to allow IO without mode change
      gpio_reset_pin(gpio_num_t(brake_pin));
      gpio_set_direction(gpio_num_t(brake_pin), GPIO_MODE_INPUT);
      Serial.printf("Track %d configured to DCC EXT \n", index);
    break;   
    case 2: //DCC internal aka rev override 
      gpio_reset_pin(gpio_num_t(reverse_pin));
      gpio_set_direction(gpio_num_t(reverse_pin), GPIO_MODE_OUTPUT); 
      gpio_reset_pin(gpio_num_t(brake_pin));
      gpio_set_direction(gpio_num_t(brake_pin), GPIO_MODE_OUTPUT); 
      Serial.printf("Track %d configured to DCC INT \n", index);
    break;
    case 3: //DC Mode:
    //todo: DC mode IO changes. For now it is the same as mode 2.
      gpio_reset_pin(gpio_num_t(reverse_pin));
      gpio_set_direction(gpio_num_t(reverse_pin), GPIO_MODE_OUTPUT); 
      gpio_reset_pin(gpio_num_t(brake_pin));
      gpio_set_direction(gpio_num_t(brake_pin), GPIO_MODE_OUTPUT); 
      //TODO: Insert commands to enable brake PWM
      Serial.printf("Track %d configured to DC \n", index);
    break;
  }
  powermode = newmode; //update powermode with new value
  return;
}

void TrackChannel::StateChange(uint8_t newstate){
  switch (newstate) {
    case 3: //ON DC REV
      if (powermode == 3) { //This state can only be selected in DC mode, otherwise it does nothing.
      gpio_set_level(reverse_pin, 1); //Only set reverse in DCC INT or DC mode
      gpio_set_level(brake_pin, 0);  //Replace by PWM control     
      #ifdef DEBUG
        Serial.printf("Track %d in DC Reverse state \n", index);
      #endif
      gpio_set_level(enable_out_pin, 1); //enable out = on   
      }
    break;
    case 2: //ON DCC or ON DC FWD.
      if (powermode == 3) {//DC Forward
        gpio_set_level(reverse_pin, 0);    
        gpio_set_level(brake_pin, 0);  //Replace by PWM control  
      }
      if (powermode == 2) {//DCC INT
        gpio_set_level(reverse_pin, 0); //Replace by RMT output
        gpio_set_level(brake_pin, 0);      
      }
      //DCC EXT is covered by the remaining commands. Powermodes 1 and 0 turn off enable, this turns it back on.
      #ifdef DEBUG
        Serial.printf("Track %d in DCC or DC Forward state \n", index);
      #endif
      gpio_set_level(enable_out_pin, 1); //enable out = on 
    break;
    case 1: //Overloaded. Copy previous state so that it can be changed back after cooldown
      gpio_set_level(enable_out_pin, 0); //enable out = off 
      overload_state = powerstate; //save previous state 
      overload_cooldown = 4310; //4310 ticks of 58usec = about 250ms to let the chip cool
      Serial.printf("Track %d Off due to Overloaded state \n", index);
    break;
    case 0: //Track off. Clear overload cache/timer since we don't need it now. 
      gpio_set_level(enable_out_pin, 0); //enable out = off 
      overload_state = 0;
      overload_cooldown = 0; 
      #ifdef DEBUG
      Serial.printf("Track %d in Off state \n", index);
      #endif
    break;           
  }
  powerstate = newstate; //update saved power state  
  return;
}

void TrackChannel::adc_read() { //Needs the actual ADC read implemented still
  uint16_t adcraw = 0;
  adc_previous_ticks = adc_current_ticks; //update value read on prior scan
  //ADC runs at a max of 5MHz, and needs 25 clock cycles to complete. Effectively 200khz or 5usec minimum. 
  //adc_current_ticks = analogRead(adc_channel); //Read using Arduino IDE, now obsolete
  //ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, adc_channel, adcraw));
  adcraw = adc1_get_raw(adc1_channel_t (adc_channel));
  adc_current_ticks = adcraw * 1000000 + adc_offset; //Expand from uint16_t to uint32_t
  if (adc_current_ticks > (adcraw * 1000000)){ //int rollover happened due to adc_offset being less than adcraw. Just set it to 0
    adc_current_ticks = 0; 
  }
  
  //Serial.printf("ADC read %u, upscaled %u \n", adcraw, adc_current_ticks);
  if (adc_current_ticks > adc_overload_trip) {
    Serial.printf("ADC detected overload at %u ticks, threshold %u \n", adc_current_ticks, adc_overload_trip);
    StateChange(1); //Set power state overload   
  }
  return;
}

uint8_t TrackChannel::CheckEnable(){ //Arch Bridge has to check enable_input_pin for each track. Others this always returns 1 with no pin changes.
  uint8_t enable_in = 1; //default value for enable in
  #ifdef BOARD_TYPE_ARCH_BRIDGE //Enable In is only used on Arch Bridge
    if (enable_in_pin != 0) { //If enable_in is configured, read it
      enable_in = gpio_get_level(enable_in_pin);
      //Serial.printf("Enable In is %d \n",enable_in);
    }
    gpio_set_level(enable_out_pin, enable_in); //enable_out = enable in or 1
  #endif
  return enable_in;
}

uint8_t MasterEnable(){ //Dyamo type boards have an input for master enable. Others this always returns 1. 
    uint8_t master_en = 1; //default value for master_en
#ifdef BOARD_TYPE_DYNAMO
      uint8_t i = 0;
      master_en = gpio_get_level(gpio_num_t(MASTER_EN));
      //if (master_en != 1) {
      //  while (i < max_tracks){
      //    DCCSigs[0].StateChange(0); //Change all tracks to the off state. They have to be individually turned on again. 
      //    i++;     
      //  }
      //}
#endif
// BOARD_TYPE_ARCHBRIDGE has no master_en input, this will always return 1. 
  return master_en;
}

void ESP_rmt_rx_init(){
  //Initialize RMT for DCC auditing
  rmt_config_t rmt_rx_config;
  // Configure the RMT channel for RX
//  bzero(&config, sizeof(rmt_config_t));
  rmt_rx_config.rmt_mode = RMT_MODE_RX;
  rmt_rx_config.channel = rmt_channel_t(DIR_MONITOR_RMT);
  rmt_rx_config.clk_div = RMT_CLOCK_DIVIDER;
  rmt_rx_config.gpio_num = gpio_num_t(DIR_MONITOR);
/* NMRA allows up to 32 bytes per packet, the max length would be 301 bits transmitted and need 38 bytes (3 bits extra). 
 * DCC_EX is limited to only 11 bytes max. Much easier to account for and uses a smaller buffer. 
  */
  rmt_rx_config.mem_block_num = 3; 
  ESP_ERROR_CHECK(rmt_config(&rmt_rx_config));
  // NOTE: ESP_INTR_FLAG_IRAM is *NOT* included in this bitmask
  ESP_ERROR_CHECK(rmt_driver_install(rmt_rx_config.channel, 0, ESP_INTR_FLAG_LOWMED|ESP_INTR_FLAG_SHARED));  
  Serial.printf("RMT RX Initialized \n"); 
  return;
}

void rmt_tx_init(){
  //Initialize RMT for DCC TX 
  #ifdef BOARD_TYPE_DYNAMO //for now only do this on Dynamo
  rmt_config_t rmt_tx_config;
  // Configure the RMT channel for TX
  rmt_tx_config.rmt_mode = RMT_MODE_TX;
  rmt_tx_config.channel = rmt_channel_t(DIR_OVERRIDE_RMT);
  rmt_tx_config.clk_div = RMT_CLOCK_DIVIDER;
  rmt_tx_config.gpio_num = gpio_num_t(DIR_OVERRIDE);
  rmt_tx_config.mem_block_num = 2; // With longest DCC packet 11 inc checksum (future expansion)
                            // number of bits needed is 22preamble + start +
                            // 11*9 + extrazero + EOT = 124
                            // 2 mem block of 64 RMT items should be enough
  ESP_ERROR_CHECK(rmt_config(&rmt_tx_config));
  // NOTE: ESP_INTR_FLAG_IRAM is *NOT* included in this bitmask
  ESP_ERROR_CHECK(rmt_driver_install(rmt_tx_config.channel, 0, ESP_INTR_FLAG_LOWMED|ESP_INTR_FLAG_SHARED));    
  Serial.printf("RMT TX Initialized \n"); 
  #endif
  return;
}

//From DCC-EX ESP32 branch DCCRMT.cpp. 
void setDCCBit1(rmt_item32_t* item) {
  item->level0    = 1;
  item->duration0 = DCC_1_HALFPERIOD;
  item->level1    = 0;
  item->duration1 = DCC_1_HALFPERIOD;
}

void setDCCBit0(rmt_item32_t* item) {
  item->level0    = 1;
  item->duration0 = DCC_0_HALFPERIOD;
  item->level1    = 0;
  item->duration1 = DCC_0_HALFPERIOD;
}

void setEOT(rmt_item32_t* item) {
  item->val = 0;
}

void ESP_i2c_init(){
  i2c_config_t i2c_conf_slave;
    i2c_conf_slave.sda_io_num = gpio_num_t(I2C_SDA_PIN);
    i2c_conf_slave.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf_slave.scl_io_num = gpio_num_t(I2C_SCL_PIN);
    i2c_conf_slave.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf_slave.mode = I2C_MODE_SLAVE;
    i2c_conf_slave.slave.addr_10bit_en = 0;
    i2c_conf_slave.slave.slave_addr = I2C_SLAVE_ADDR;  // slave address of your project
    i2c_conf_slave.slave.maximum_speed = I2C_CLOCK; // expected maximum clock speed
    i2c_conf_slave.clk_flags = 0;   // optional; you can use I2C_SCLK_SRC_FLAG_* flags to choose I2C source clock here
  /*esp_err_t err = i2c_param_config(i2c_slave_number(I2C_SLAVE_PORT), &i2c_conf_slave);
  if (err != ESP_OK) {
    return err;
  }*/
return;
}
