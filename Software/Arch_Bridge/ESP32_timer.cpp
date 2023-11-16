#ifndef ESP32_TIMER_H
  #include "ESP32_timer.h"
#endif

uint64_t time_us = 0; 
uint64_t last_time_us = 0;

void Heartbeat(uint64_t seconds){
  time_us = TIME_US;
  if ((time_us - last_time_us) > (seconds * 1000000)) {
    Serial.printf("%Heartbeat scan Jitter: %u uS.\n", (time_us - last_time_us - (seconds * 1000000))); 
    last_time_us = time_us;   
  }
  return; 
}
