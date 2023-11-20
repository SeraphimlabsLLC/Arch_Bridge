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

class Fastclock_class {
  public:
  bool active; //Clock has been started or not
  uint8_t set_rate; //Ratio of fast time to real time
  uint8_t seconds; //seconds
  uint8_t minutes; //minutes
  uint8_t hours; //hours
  uint16_t days; //days
  
  void clock_init(); //Initialize fastclock to defined value and record set time
  void clock_set(uint8_t s_rate, uint8_t s_seconds, uint8_t s_minutes, uint8_t s_hours, uint8_t s_days); //Set fastclock to specified time 
  void clock_get(); //Read fastclck time

  private:
  uint64_t set_at_us; //time_us the fastclock was last set at
  uint8_t set_seconds; //seconds at last set
  uint8_t set_minutes; //minutes at last set
  uint8_t set_hours; //hours at last set
  uint16_t set_days; //days at last set
};
