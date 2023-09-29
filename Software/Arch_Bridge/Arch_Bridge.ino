//Use config.h if present, otherwise defaults
#if __has_include ( "config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif

#ifndef ESP32_TRACKS_HW_H
  #include "ESP32_Tracks_HW.h"
#endif
extern ESP_Uart tty; //normal serial port
#ifdef BOARD_TYPE_ARCH_BRIDGE //If this is an arch bridge, define a loconet uart
extern ESP_Uart LN_port; //Loconet object
#endif
extern TrackChannel DCCSigs[];
extern uint8_t max_tracks;
bool Master_Enable = false;

void setup() {
  ESP_serial_init();
  Serial.begin(115200);
//ESP_i2c_init();
  ESP32_Tracks_Setup();  
  Serial.print("Tracks Initialized");  
  DCCSigs[0].ModeChange(2); //set to INT
  DCCSigs[1].ModeChange(2);
}

void loop() {
uint8_t i = 0;
Master_Enable = true; //force on

if (Master_Enable == true){  //OK to run. Enable tracks in suitable state. 
  while (i < max_tracks){ //check for faults  
    gpio_set_level(gpio_num_t(DCCSigs[0].enable_out_pin), 1);
    gpio_set_level(gpio_num_t(DCCSigs[0].brake_pin), 0);
    gpio_set_level(gpio_num_t(DCCSigs[0].reverse_pin), 1); //Write 1 to enable out for track on
    delay (100);
    DCCSigs[i].adc_current_ticks = analogRead(DCCSigs[0].adc_pin); //update adc reading
    Serial.print("Track "); 
    Serial.print(0);
    Serial.print("has ADC ticks ");+ 
    Serial.print(DCCSigs[i].adc_current_ticks);
    Serial.print(" \n");
    gpio_set_level(gpio_num_t(DCCSigs[0].reverse_pin), 0); //Write 1 to enable out for track on
    delay (100);
    /* 
    if (DCCSigs[i].powerstate >= 2){ //State is set to on forward or on reverse, ok to enable. 
      gpio_set_level(gpio_num_t(DCCSigs[i].enable_out_pin), 1); //Write 1 to enable out on each track
      DCCSigs[i].adc_previous_ticks = DCCSigs[i].adc_current_ticks; //cache previous adc reading
      DCCSigs[i].adc_current_ticks = analogRead(DCCSigs[i].adc_pin); //update adc reading
      if (DCCSigs[i].adc_current_ticks > DCCSigs[i].adc_overload_trip) {
        Serial.print ("Track ");
        Serial.print (i);
        Serial.print ("is overloaded \n");
        gpio_set_level(gpio_num_t(DCCSigs[i].enable_out_pin), 0); //overload tripped, track off.
        DCCSigs[i].powerstate = 1; //set track to overloaded so it stays off until further processing. 
      }
    } */   
    i++; 
  }
} else { //Fault exists, disable all tracks. Leave track states unchanged for later re-enabling. 
  i = 0; //reset i for the next loop
  while (i < max_tracks){
    gpio_set_level(gpio_num_t(DCCSigs[i].enable_out_pin), 0); //Write 0 to enable out on each track
    //digitalWrite(DCCSigs[i].enable_out_pin, 0);
    i++;
  } 
}
}
