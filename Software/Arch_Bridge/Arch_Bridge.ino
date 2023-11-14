//Use config.h if present, otherwise defaults
#ifndef CONFIG_H
  #if __has_include ( "config.h")
    #include "config.h"
  #else
    #include "config.example.h"
  #endif
#endif  
#include "esp_timer.h"

#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif

#ifndef ESP32_RMTDCC_H
  #include "ESP32_rmtdcc.h"
#endif

#ifndef ESP32_TRACKS_HW_H
  #include "ESP32_Tracks_HW.h"
#endif



extern ESP_Uart tty; //normal serial port
#ifdef BOARD_TYPE_ARCH_BRIDGE //If this is an arch bridge, define a loconet uart
  #ifndef ESP32_LOCONET_H
    #include "ESP32_Loconet.h"
  
  #endif
extern LN_Class Loconet; //Loconet memory object
  
#ifndef ESP32_DCCEX_H
  #include "ESP32_dccex.h"
#endif
//  extern DCCEX_Class dccex_port;
#endif

extern TrackChannel DCCSigs[];
extern Rmtdcc dcc; 
extern uint8_t max_tracks;
uint64_t time_us = 0; 
uint64_t last_time_us = 0;

void setup() {
  //Display ESP32 Interrupt table: 
//  esp_intr_dump();
  
  ESP_uart_init(); //Initialize tty
  #ifdef BOARD_TYPE_ARCH_BRIDGE
    ESP_LN_init(); //Initialize Loconet
  #endif
  ESP32_Tracks_Setup();  //Initialize GPIO and RMT hardware

//For testing purposes.
DCCSigs[0].ModeChange(1); //set to DCC EXT
//  DCCSigs[1].ModeChange(3);

DCCSigs[0].StateChange(2);//Set to ON_FWD
//  DCCSigs[1].StateChange(3);
Serial.print("Setup Complete \n");  
}

void loop() {  
  uint64_t LN_Scan = 0;
//delay(5);
ESP32_Tracks_Loop(); //Process and update tracks
dcc.loop_process(); //Process and update DCC packets

#ifdef BOARD_TYPE_ARCH_BRIDGE //If this is an arch bridge, check the loconet
  if ((time_us - LN_Scan) > (60*8)) { //Only scan once every 8 bytes at 60uS/byte.
  Loconet.loop_process(); //Process and update Loconet

  LN_Scan = time_us;
  }

#endif 

#define HEARTBEAT_US 10000000 //10 seconds
  time_us = esp_timer_get_time();
  if ((time_us - last_time_us) > HEARTBEAT_US) {
    Serial.printf("%Heartbeat scan Jitter: %u uS.\n", (time_us - last_time_us - HEARTBEAT_US)); 
    last_time_us = time_us;   
  }
}
