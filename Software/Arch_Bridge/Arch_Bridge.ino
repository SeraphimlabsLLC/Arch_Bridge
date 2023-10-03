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
/*
int master_en = 3;
int ena_o_pin = 10;
int ena_i_pin = 13;
int reverse_pin = 11;
int brake_pin = 12; 
int adc_pin = 1;
*/
void setup() {
  Serial.begin(115200);
  ESP_uart_init();
  ESP32_Tracks_Setup();  //Initialize GPIO and RMT hardware

//For testing purposes.
  DCCSigs[0].ModeChange(3); //set to DC
  DCCSigs[1].ModeChange(3);

  DCCSigs[0].StateChange(3);//Set to ON_REV
  DCCSigs[1].StateChange(3);  
}

void loop() {
uint8_t i = 0;
uint16_t milliamps = 0;
Master_Enable = MasterEnable(); //Update Master status. Dynamo this is external input, ArchBridge is always true.
  while (i < max_tracks){ //Check active tracks for faults
    if (DCCSigs[i].powerstate >= 2){ //State is set to on forward or on reverse, ok to enable. 
      DCCSigs[i].CheckEnable();
      //gpio_set_level(gpio_num_t(DCCSigs[i].enable_out_pin), 1); //Write 1 to enable out on each track
      DCCSigs[i].adc_read(); //actually read the ADC and enforce overload shutdown
      milliamps = DCCSigs[i].adc_current_ticks * 1000 / DCCSigs[i].adc_scale; //scaled mA
      //milliamps = DCCSigs[i].adc_current_ticks; //raw ticks
      Serial.printf("Track %d ",i);
      Serial.printf("ADC analog value = %d\n",milliamps);
    }   
    i++;
    
    delay(350); 
  }
}
