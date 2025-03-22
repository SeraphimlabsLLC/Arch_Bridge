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
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/mcpwm.h"
#include "soc/mcpwm_struct.h"
#include "soc/mcpwm_reg.h"

//#if TIME_US != micros() 
 #include "esp_timer.h" //Is it really needed? Arduino millis() and micros() are present. 
//#endif

#if BOARD_TYPE == DYNAMO
  #define DIR_MONITOR 38 //RMT Input pin, 9 on Arch_Bridge 38 on Dynamo
  #define DIR_OVERRIDE 21 //GPIO21, use for RMT Output
#endif

#if BOARD_TYPE == ARCH_BRIDGE
  #define DIR_MONITOR 9 //RMT Input pin, 9 on Arch_Bridge 38 on Dynamo
  //Clock constants for clock_set() and clock_get()in Loconet
  #define US_PER_DAY 86400000000
  #define US_PER_HOUR 3600000000
  #define US_PER_MINUTE 60000000
  #define US_PER_SECOND 1000000
#endif

#ifndef BOARD_TYPE
  #error "Invalid board type, must choose DYNAMO or ARCH_BRIDGE"
#endif

uint64_t esp_us(); //Equivalent to micros(); 

void Heartbeat(uint64_t seconds); //Checks for and displays console heartbeat
void measure_time_start();
void measure_time_stop(); 


void esp_gptimer_init(); //Reflector into ESP_gptimer class
class ESP_gptimer {
  public:
    uint8_t gptimer_init(); 
    uint64_t gptimer_read(); //Get current value from the gptimer
    void IRAM_ATTR gptimer_set(uint64_t newcount); //set gptimer count
    void IRAM_ATTR alarm_set(uint64_t count, uint8_t owner); //Sets the alarm with a count of 0.
    void IRAM_ATTR alarm_change(uint64_t count, uint8_t owner); //changes the setpoint without touching the count, updated when the ISR callback returns. Give it time to do so. 
    uint64_t alarm_value(); //returns alarm value currently set
    uint8_t IRAM_ATTR alarm_owner(); //read value of alarm owner
    bool alarm_is_set(); //returns true if alarm is set
    
  private:
  gptimer_handle_t gptimer = NULL; //Holder for hardware handle
  bool is_active; //true if the timer is running
  volatile uint8_t alarm_owner_v; //alarm owner value
  volatile bool alarm_counting; //true if alarm is set
  volatile uint32_t alarm_set_value; //Value set in alarm
};
void IRAM_ATTR ESP_gptimer_alarm_isr(); //ISR for gptimer alarm

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
  uint32_t minutes_rem_uS; //uS remainder after calculating minutes time. Used in Loconet.
  
  void clock_init(); //Initialize fastclock to defined value and record set time
  void clock_set(uint8_t s_rate, uint8_t s_days, uint8_t s_hours, uint8_t s_minutes, uint8_t s_seconds, uint32_t s_uS_remain); //Set fastclock to specified time 
  void clock_get(); //Read fastclck time

  private:
  uint64_t set_us; //Time clock was set to at start in uS
  uint64_t set_at_us; //time_us to start counting from

};

class ESP_pwmcap {
  public:

  private:

};

void ESP_pwmcap_isr(); 

#endif
