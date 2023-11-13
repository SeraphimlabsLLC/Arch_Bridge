#ifndef ESP32_TRACKS_HW_H
  #include "ESP32_Tracks_HW.h"
#endif



//TrackChannel(enable_out_pin, enable_in_pin, uint8_t reverse_pin, brake_pin, adc_channel, adcscale, adc_overload_trip)
TrackChannel DCCSigs[MAX_TRACKS]; //Define track channel objects with empty values.
uint8_t max_tracks = 0; //Will count tracks as they are initialized. 
bool Master_Enable = false; 
uint64_t Master_en_chk_time = 0; //Store when it was checked last. 
bool Master_en_deglitch = false; //Interim value
bool rmt_dcc_ok = true; //RMT reports a valid DCC signal. 
extern uint64_t time_us; //Reuse time_us from main

uint32_t rmt_rxdata_ptr; //RMT RX Ring Buffer locator

void ESP32_Tracks_Setup(){ //Populates track class with values including ADC
  #ifdef BOARD_TYPE_DYNAMO //If this is a Dynamo type booster, define these control pins.
  Serial.print("Configuring board for Dynamo booster mode \n");
    gpio_reset_pin(gpio_num_t(MASTER_EN)); //Input from Optocouplers and XOR verifying that at least one opto is on
    gpio_set_direction(gpio_num_t(MASTER_EN), GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio_num_t(MASTER_EN), GPIO_FLOATING);  

    gpio_reset_pin(gpio_num_t(DIR_MONITOR));
    gpio_set_direction(gpio_num_t(DIR_MONITOR), GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio_num_t(DIR_MONITOR), GPIO_FLOATING);  

    gpio_reset_pin(gpio_num_t(DIR_OVERRIDE));
    gpio_set_direction(gpio_num_t(DIR_OVERRIDE), GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(gpio_num_t(DIR_OVERRIDE), GPIO_PULLUP_PULLDOWN); 
#endif
#ifdef BOARD_TYPE_ARCH_BRIDGE //If this is an arch bridge, define these control pins.
  Serial.print("Configuring board for Arch Bridge mode \n");

  gpio_reset_pin(gpio_num_t(MASTER_EN)); //Serves as an Output Enable on Dynamo
  gpio_set_direction(gpio_num_t(MASTER_EN), GPIO_MODE_OUTPUT);
  gpio_set_pull_mode(gpio_num_t(MASTER_EN), GPIO_PULLUP_PULLDOWN);    
  gpio_set_level(gpio_num_t(MASTER_EN), 1); //Turn OE on 

#endif
  adc1_config_width(ADC_WIDTH_12Bit);//config adc1 width
  
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
  //ESP_rmt_rx_init(); //Initialize DIR_MONITOR for RMT monitoring
  return;
}
void ESP32_Tracks_Loop(){ //Check tasks each scan cycle.
  uint8_t i = 0;
  //uint32_t milliamps = 0;
  MasterEnable(); //Update Master status. Dynamo this is external input, ArchBridge is always true.
    while (i < max_tracks){ //Check active tracks for faults
    if (DCCSigs[i].powerstate >= 2){ //State is set to on forward or on reverse, ok to enable. 
      DCCSigs[i].CheckEnable();
      //gpio_set_level(gpio_num_t(DCCSigs[i].enable_out_pin), 1); //Write 1 to enable out on each track
      DCCSigs[i].adc_read(); //actually read the ADC and enforce overload shutdown
      //milliamps = DCCSigs[i].adc_current_ticks; //raw ticks     
      //milliamps = (DCCSigs[i].adc_current_ticks)/ DCCSigs[i].adc_scale; //scaled mA
      //Serial.printf("Track %u ADC analog value = %u milliamps \n", i + 1, milliamps);
    }
    i++;
  }

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
      Serial.printf ("Configuring Enable In pin to %u \n", uint8_t (enable_in_pin));
      gpio_reset_pin(enable_in_pin);
      gpio_set_direction(enable_in_pin, GPIO_MODE_INPUT);
    }
    
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
    if ((enable_in_pin != 0) && (powermode = 1)) { //If enable_in_pin is configured and powermode = 1 for DCC EXT, read and update enable_in_pin
      enable_in = gpio_get_level(enable_in_pin);
      //Serial.printf("Enable In is %u \n", enable_in);
    }
    gpio_set_level(enable_out_pin, enable_in);
  #endif
  return enable_in;
}

bool MasterEnable(){ //On Dynamo boards, read MASTER_IN twice MASTER_EN_DEGLITCH uS apart. If both agree, that is the new state. Arch_Bridge it always returns 1. 
  //NMRA S-9.1 gives the rise/fall time as 2.5v/uS, the pin could be off for 6uS each edge.
  
  bool master_en = 1; //default value for master_en.
#ifdef BOARD_TYPE_DYNAMO
    time_us = esp_timer_get_time();
    if ((time_us - Master_en_chk_time) > MASTER_EN_DEGLITCH){
      master_en = gpio_get_level(gpio_num_t(MASTER_EN));  
      if (master_en != Master_en_deglitch) { //State changed, update and rescan
        Master_en_deglitch = master_en; 
        master_en = Master_Enable; //Pass through the unchanged value to the return.  
      } 
      //Implied if master_en == Master_en_deglitch it will change Master_Enable too. 
      Master_en_chk_time = time_us; //Update last scan time. 
    }     
#endif
  master_en = master_en && rmt_dcc_ok; //Master Enable will still shut off if the RMT DCC decoder isn't getting valid packets. 
// BOARD_TYPE_ARCHBRIDGE has no master_en input, this will always return 1. 
   if (master_en != Master_Enable) { //Master Enable state has changed.
     Serial.printf("Master Enable on gpio %u state changed, new state %u \n", MASTER_EN, master_en);
     Master_Enable = master_en; //Function directly updates the Master_Enable global in addition to returning
   }  
  return master_en;
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
