#define ESP32_TIMER_H

//Use config.h if present, otherwise defaults
#ifndef CONFIG_H
  #if __has_include ("config.h")
    #include "config.h"
  #endif
  #ifndef CONFIG_H
    #include "config.example.h"
  #endif
#endif  

#include "Arduino.h"

//  #include "esp_timer.h" //Is it really needed? Arduino millis() and micros() are present. 

void Heartbeat(uint64_t seconds); //Checks for and displays console heartbeat
