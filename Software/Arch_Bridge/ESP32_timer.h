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

//#include "esp_timer.h" //Is it really needed? Arduino millis() and micros() are present. 

uint64_t esp_us(); //Equivalent to micros(); 

void Heartbeat(uint64_t seconds); //Checks for and displays console heartbeat
void measure_time_start();
void measure_time_stop(); 

#if BOARD_TYPE == ARCH_BRIDGE //If this is an arch bridge, include fastclock features
void Fastclock_setup(bool enabled); //Initialize fastclock from outside class

class Fastclock_class {
  public:
  bool active; //Clock has been started or not
  uint8_t set_rate; //Ratio of fast time to real time
  uint8_t days; 
  uint8_t hours; 
  uint8_t minutes; 
  uint8_t seconds; 
  uint32_t frac_minutes_uS; //uS remainder after calculating minutes time. Used in Loconet.
  
  void clock_init(); //Initialize fastclock to defined value and record set time
  void clock_set(uint8_t s_rate, uint8_t s_days, uint8_t s_hours, uint8_t s_minutes, uint8_t s_seconds, uint32_t s_uS_remain); //Set fastclock to specified time 
  void clock_get(); //Read fastclck time

  private:
  uint64_t set_us; //Time clock was set to at start in uS
  uint64_t set_at_us; //time_us to start counting from

};

#endif
