#ifndef ESP32_TRACKS_HW_H
  #include "ESP32_Tracks_HW.h"
#endif
//#ifndef ESP32_RMTDCC_HW_H
//  #include "ESP32_rmtdcc.h"
//#endif

//TrackChannel(enable_out_pin, enable_in_pin, uint8_t reverse_pin, brake_pin, adc_channel, adcscale, adc_overload_trip)
TrackChannel DCCSigs[MAX_TRACKS]; //Define track channel objects with empty values.
//extern Rmtdcc dcc; //DCC on RMT

uint8_t max_tracks = 0; //Will count tracks as they are initialized. 
volatile bool Master_Enable = false; 
volatile uint64_t Master_en_chk_time = 0; //Store when it was checked last. 
volatile bool Master_en_deglitch = false; //Interim value
extern bool dcc_ok; //A valid DCC signal is present. 
extern uint64_t time_us; //Reuse time_us from main


void Tracks_Init(){ //Populates track class with values including ADC
  
  
#if BOARD_TYPE == DYNAMO //If this is a Dynamo type booster, define these overall control pins.
  Serial.print("Configuring board for Dynamo booster mode \n");
    gpio_reset_pin(gpio_num_t(MASTER_EN)); //Input from Optocouplers and XOR verifying that at least one opto is on
    gpio_set_direction(gpio_num_t(MASTER_EN), GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio_num_t(MASTER_EN), GPIO_FLOATING);  
    attachInterrupt(MASTER_EN, Master_en_ISR, CHANGE);

//Moved DIR_MONITOR to RMT code

    gpio_reset_pin(gpio_num_t(DIR_OVERRIDE));
    gpio_set_direction(gpio_num_t(DIR_OVERRIDE), GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(gpio_num_t(DIR_OVERRIDE), GPIO_PULLUP_PULLDOWN); 

#endif
#if BOARD_TYPE == ARCH_BRIDGE //If this is an arch bridge, define these overall control pins.
  Serial.print("Configuring board for Arch Bridge mode \n");
  gpio_reset_pin(gpio_num_t(MASTER_EN)); //Serves as an Output Enable on Dynamo
  gpio_set_direction(gpio_num_t(MASTER_EN), GPIO_MODE_OUTPUT);
  gpio_set_pull_mode(gpio_num_t(MASTER_EN), GPIO_PULLUP_PULLDOWN);    
  gpio_set_level(gpio_num_t(MASTER_EN), 1); //Turn OE on 
#endif
/*dcc.rmt_rx_init();
#if DCC_GENERATE == true 
  dcc.rmt_tx_init(); //Initialize DIR_OVERRIDE for DCC generation
#endif*/
  
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
  
  return;
}
void Tracks_Loop(){ //Check tasks each scan cycle.
  uint8_t i = 0;
  MasterEnable(); //Update Master status. Dynamo this is external input, ArchBridge is always true.
 
  for (i = 0; i < max_tracks; i++){ //Check active tracks for faults
    DCCSigs[i].adc_read(); //actually read the ADC and enforce overload cooldown.  
    if (DCCSigs[i].powerstate > 0){ //Power should be on
      DCCSigs[i].CheckEnable();
    }
  }

  return;
}

void TrackChannel::SetupHW(uint8_t en_out_pin, uint8_t en_in_pin, uint8_t rev_pin, uint8_t brk_pin, uint8_t adcpin, uint32_t adcscale, int32_t adcoffset, uint32_t adc_ol_trip, char track) { 
    max_tracks++; //Add 1 to max tracks
    index = max_tracks; //store the index of this track. 
    #ifdef DEBUG
      Serial.printf("Configuring Track %d \n" , index);
    #endif
    trackID = track; //track ID char for DCCEX
    powerstate = 0; //Power is off by default
    powermode = 0; //Mode is not configured by default  
    //Copy config values to class values
    enable_out_pin = gpio_num_t(en_out_pin);
    enable_in_pin = gpio_num_t(en_in_pin);
    reverse_pin = gpio_num_t(rev_pin);
    brake_pin = gpio_num_t(brk_pin);
    adc_channel = adc1_channel_t (adcpin - 1);
    adc_scale = adcscale;
    adc_offset = adcoffset; 
    adc_overload_trip = adc_ol_trip;
    //Configure Enable Out
    gpio_reset_pin(gpio_num_t(enable_out_pin)); //Always configure enable_out_pin
    gpio_set_direction(gpio_num_t(enable_out_pin), GPIO_MODE_OUTPUT_OD); //Open Drain output, level shifters have integral pull-up
    gpio_set_level(gpio_num_t(enable_out_pin), 0); //Enable Out stays off until a mode is selected. 
    //Configure Enable In if present
    #if BOARD_TYPE == ARCH_BRIDGE //Arch Bridge uses enable_in for DCC EXT mode. 
    if (enable_in_pin != 0) { //Only configure enable_in_pin if it is nonzero
      //Serial.printf ("Configuring Enable In pin to %u \n", uint8_t (enable_in_pin));
      gpio_reset_pin(enable_in_pin);
      gpio_set_direction(enable_in_pin, GPIO_MODE_INPUT);
    }
    #endif
    #if BOARD_TYPE == DYNAMO //Dynamo reuses enable_in for DC select
    if (enable_in_pin != 0) { //Only configure enable_in_pin if it is nonzero
      //Serial.printf ("Configuring Enable In pin to %u \n", uint8_t (enable_in_pin));
      gpio_reset_pin(enable_in_pin);
      gpio_set_direction(enable_in_pin, GPIO_MODE_OUTPUT);
      gpio_set_level(enable_in_pin, 1); //Default to DC mode ON so that a Railsync signal is not passed until set to a DCC mode.  
    #endif
    
    adc1_config_channel_atten(adc_channel,ADC_ATTEN_11db);//config attenuation
    adc_current_ticks = adc_previous_ticks = adc_base_ticks = 0; //Set all 3 ADC values to 0 initially
    ModeChange(0); //set power mode none, which will also set power state off.
    adc_read(); //actually read the ADC
    adc_base_ticks = adc_current_ticks; //Copy the zero output ticks to adc_base_ticks
    return;
}

void TrackChannel::ModeChange (int8_t newmode){ //Updates GPIO modes when changing powermode
  //0 = none, 1 = DCC_external, 2 = DCC_override, 3 = DC, 4 = DCX.
  StateChange(0); // //Always turn the track off before changing modes
  switch (newmode) {     
    case 0: //Not Configured, reset the pins and don't configure them
      gpio_reset_pin(gpio_num_t(reverse_pin));
      gpio_reset_pin(gpio_num_t(brake_pin));
      Serial.printf("Track %d configured to NO MODE \n", index);
      StateChange(0); //set state to off since it isn't configured anyway. 
    break;
    case 1: //EXT, external control of all lines 
      gpio_reset_pin(gpio_num_t(reverse_pin));
      gpio_set_direction(gpio_num_t(reverse_pin), GPIO_MODE_INPUT);  //Consider GPIO_MODE_INPUT_OUTPUT_OD to allow IO without mode change
      gpio_reset_pin(gpio_num_t(brake_pin));
      gpio_set_direction(gpio_num_t(brake_pin), GPIO_MODE_INPUT);
      Serial.printf("Track %d configured to DCC EXT \n", index);
    break;   
    case 2: //DCC internal aka rev override.  
      gpio_reset_pin(gpio_num_t(reverse_pin));
      gpio_set_direction(gpio_num_t(reverse_pin), GPIO_MODE_OUTPUT); 
      gpio_reset_pin(gpio_num_t(brake_pin));
      gpio_set_direction(gpio_num_t(brake_pin), GPIO_MODE_OUTPUT); 
      Serial.printf("Track %d configured to DCC INT \n", index);
    break;
    case 3: //DC:
    case 4: //DCX (DC Reverse). reset_pin inverson is now handled in StateChange()
      gpio_reset_pin(gpio_num_t(reverse_pin));
      gpio_set_direction(gpio_num_t(reverse_pin), GPIO_MODE_OUTPUT); 
      gpio_reset_pin(gpio_num_t(brake_pin));
      gpio_set_direction(gpio_num_t(brake_pin), GPIO_MODE_OUTPUT); 
      //TODO: Insert commands to enable brake PWM
      
      Serial.printf("Track %d configured to DC \n", index);
    break;
  }
  #if BOARD_TYPE == DYNAMO //Dynamo reuses enable_in for DC select. This code isn't necessary on Arch Bridge
    if (enable_in_pin != 0) { //Only configure enable_in_pin if it is nonzero
      if ((newmode == 1) || (newmode == 2)) { //DCC mode selected
        gpio_set_level(enable_in_pin, 0);
      }
      if ((newmode == 3) || (newmode == 4)) { //DC or DCX mode selected
        gpio_set_level(enable_in_pin, 1);
      }
    }
  #endif
  
  powermode = newmode; //update powermode with new value
  return;
}

void TrackChannel::StateChange(int8_t newstate){
  int8_t power_rev = 0;
//int8_t powerstate; //0 = off, 1 = on_forward, 2 = on_reverse - indicates overloaded 
//uint8_t powermode; //0 = none, 1 = DCC_external, 2 = DCC_override, 3 = DC, 4 = DCX.
  Serial.printf("TrackChannel track %c State Change %i \n", trackID, newstate); 
  if (newstate <= 0) { //Overloaded or off. Shut down enable_out, but no need to change others. 
    CheckEnable(); //Turns on enable if in a mode that allows it
    powerstate = newstate; //update saved power state  
      #ifdef DEBUG
        Serial.printf("Track %c Off \n", trackID);
      #endif
    return; 
  }  
  //Power should be on in state 1 or 2 at this point. 
  overload_cooldown = TIME_US; //Prime it to avoid nuisance error when powering on initially. 

  /*In DCC EXT, rev and brk is controlled by EXT waveform generator 
  * in DCC INT, rev and brk is controlled by INT waveform generator.
  * Either way, it shouldn't need to be changed here since powermode has control of that. 
  * Only modes DC and DCX need to be delt with.
  */
  if (powermode == 3) {//In DC mode 
    if (newstate == 1) { //DC mode forward  
      gpio_set_level(reverse_pin, 0);
      #ifdef DEBUG
        Serial.printf("Track %c in DC Forward state \n", trackID);
      #endif
    }
    if (newstate == 2) { //DC mode reverse
      gpio_set_level(reverse_pin, 1);
      #ifdef DEBUG
        Serial.printf("Track %c in DC Reverse state \n", trackID);
      #endif
    }
  }
  if (powermode == 4) {//In DCX mode 
    if (newstate == 1) { //DCX mode 'forward' is actually reverse. 
      gpio_set_level(reverse_pin, 1);
      #ifdef DEBUG
        Serial.printf("Track %c in DC Reverse state \n", trackID);
      #endif
    }
    if (newstate == 2) { //DCX mode 'reverse' is actually forward. 
      gpio_set_level(reverse_pin, 0);
      #ifdef DEBUG
        Serial.printf("Track %c in DC Forward state \n", trackID);
      #endif
    }
  }
  CheckEnable(); //Turns on enable if in a mode that allows it
  powerstate = newstate; //update saved power state  
  return;
}

void TrackChannel::adc_read() { //Check output current, change state to -int if overloaded. Turn back on if cooldown was reached. 
  uint16_t adcraw = 0;
  adc_previous_ticks = adc_current_ticks; //update value read on prior scan
  //ADC runs at a max of 5MHz, and needs 25 clock cycles to complete. Effectively 200khz or 5usec minimum. 
  adcraw = adc1_get_raw(adc1_channel_t (adc_channel));
  adc_current_ticks = adcraw * 1000 + adc_offset; //Calculation scale changed from 1000000 to 1000 to better fit int32_t and save some ram. 
  if (adc_current_ticks < 0){ //adc_offset was negative, and would result in a negative ticks. Just set it to 0. 
    adc_current_ticks = 0; 
  }
  adc_smooth_ticks = ((adc_smooth_ticks * 15) + adc_current_ticks) / 16;
  if ((powerstate > 0) && (powermode > 0)) { //Power should be on, enforce limit. 
    if (adc_current_ticks > adc_overload_trip) {
      //if (TIME_US - overload_cooldown > (OL_COOLDOWN * 1000)) { //Only warn when it initially trips, not if it remains. 
        Serial.printf("ADC scale %u ticks * 1000 per A \n", adc_scale); 
        Serial.printf("ADC detected overload on %c at %i mA, threshold %i mA \n",trackID, (adc_current_ticks / (adc_scale/1000)), (adc_overload_trip / (adc_scale / 1000)));
      //}
       overload_cooldown = TIME_US;
      StateChange(powerstate * -1); //Set power state overload by making mode negative. 
    }
  } 
  if ((powerstate < 0) && (powermode > 0)) { //Power should be off. This isn't an else because we don't want it to happen on powerstate 0. 
    if ((TIME_US - overload_cooldown) > (OL_COOLDOWN * 1000)){ 
      StateChange(powerstate * -1); //Turn power on again by changing the mode back to positive.  
    }
  }
  return;
}

uint8_t TrackChannel::CheckEnable(){ //Verify power mode and state, then set enable. Arch_Bridge devices also read enable_in_pin
  uint8_t enable_in = 1; //default value for enable in
  if ((powerstate <= 0) || (powermode <= 0)){ 
    //Always turn enable_out off if state is overloaded or off or powemode is off. 
    enable_in = 0;  
    return enable_in; 
  }
  #if BOARD_TYPE == ARCH_BRIDGE //Enable In pin is only used on Arch Bridge
    if ((enable_in_pin > 0) && (powermode == 1)){ //If enable_in_pin is configured and powermode = DCC EXT, read and update enable_in_pin
      enable_in = enable_in & gpio_get_level(enable_in_pin);
      //Serial.printf("Enable In is %u \n", enable_in);
    }
  #endif
    gpio_set_level(enable_out_pin, enable_in); 
  return enable_in;
}

bool MasterEnable(){ //On Dynamo boards, read MASTER_IN twice MASTER_EN_DEGLITCH uS apart. 
  //If both agree, that is the new state. Arch_Bridge it always returns 1
  //NMRA S-9.1 gives the rise/fall time as 2.5v/uS, the pin could be off for 6uS each edge.
  
  bool master_en = 1; //default value for master_en.
#if BOARD_TYPE == DYNAMO
  time_us = TIME_US;
  master_en = gpio_get_level(gpio_num_t(MASTER_EN));  
  if (master_en != Master_en_deglitch) { 
    Master_en_deglitch = master_en;
    Master_en_chk_time = time_us;
  }
  if ((Master_Enable != Master_en_deglitch) && ((time_us - Master_en_chk_time) > MASTER_EN_DEGLITCH)){ 
    //master_en has been in the same state for the deglitch time. 
    Serial.printf("Master Enable on gpio %u state changed, new state %u \n", MASTER_EN, master_en);
    Master_Enable = master_en; 
  }  
  master_en = master_en && dcc_ok; //Master Enable will still shut off if the RMT DCC decoder isn't getting valid packets.
#endif
 
// BOARD_TYPE_ARCHBRIDGE has no master_en input, this will always return 1. 
  return master_en;
}

#if BOARD_TYPE == DYNAMO
void IRAM_ATTR Master_en_ISR() {
  /*
volatile bool Master_Enable = false; 
volatile uint64_t Master_en_chk_time = 0; //Store when it was checked last. 
volatile bool Master_en_deglitch = false; //Interim value
   */
  time_us = TIME_US;
  bool master_en = 1;
  master_en = gpio_get_level(gpio_num_t(MASTER_EN));  
  if (master_en != Master_en_deglitch) { 
    Master_en_deglitch = master_en;
    Master_en_chk_time = time_us;
  }
  if ((Master_Enable != Master_en_deglitch) && ((time_us - Master_en_chk_time) > MASTER_EN_DEGLITCH)){ 
    //master_en has been in the same state for the deglitch time. 
    Master_Enable = master_en; 
  }
     
  return;
}
#endif
