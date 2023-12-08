#ifndef ESP32_TIMER_H
  #include "ESP32_timer.h"
#endif

//Clock constants for clock_set() and clock_get(). 
#define US_PER_DAY 86400000000
#define US_PER_HOUR 3600000000
#define US_PER_MINUTE 60000000
#define US_PER_SECOND 1000000

volatile uint64_t time_us = 0; 
uint64_t last_time_us = 0;
uint64_t meas_start_time= 0;
uint64_t meas_cumu_time = 0;

uint64_t esp_us(){ //Equivalent to micros(); 
  time_us = micros(); //esp_timer_get_time();
  return time_us;
}

void Heartbeat(uint64_t seconds){
  time_us = TIME_US;
  if ((time_us - last_time_us) > (seconds * 1000000)) {
    Serial.printf("Heartbeat scan Jitter: %u uS.\n", (time_us - last_time_us - (seconds * 1000000))); 
    if (meas_cumu_time > 0) {
      Serial.printf("Measured cycle time %u uS. \n", meas_cumu_time);
      meas_cumu_time = 0; 
    }
    last_time_us = time_us;   
  }
  return; 
}
void measure_time_start(){
  meas_start_time = TIME_US;
  return;
}

void measure_time_stop() {
  time_us = TIME_US;
  if ((time_us - meas_start_time) > meas_cumu_time){ 
    meas_cumu_time = (time_us - meas_start_time); 
  }
  //meas_cumu_time = meas_cumu_time + (time_us - meas_start_time); 
  return;  
}

#if BOARD_TYPE == ARCH_BRIDGE //If this is an arch bridge, include fastclock features
Fastclock_class Fastclock; //For fastclock

void Fastclock_setup(bool enabled){ //Initialize fastclock from outside class
  Fastclock.clock_init(); 
}

void Fastclock_class::clock_init() { //Call clock_set with the values from the config file
  clock_set(FCLK_RATE, FCLK_DAYS, FCLK_HOURS, FCLK_MINUTES, FCLK_SECONDS, 0);
  clock_get();
  active = true;
  return; 
}

void Fastclock_class::clock_set(uint8_t s_rate, uint8_t s_days, uint8_t s_hours, uint8_t s_minutes, uint8_t s_seconds, uint32_t s_uS_remain) {

  set_rate = s_rate;
  set_us = (s_days * US_PER_DAY) + (s_hours * US_PER_HOUR) + (s_minutes * US_PER_MINUTE) + (s_seconds * US_PER_SECOND) + s_uS_remain;
  set_at_us = TIME_US; //Time last set at
  active = true; 
  Serial.printf("Fastclock: Clock set to day %u, %u:%u:%u \n", s_days, s_hours, s_minutes, s_seconds);
  return;
}

void Fastclock_class::clock_get() {
   uint64_t time_delta = 0;
  if (active != true) {
    clock_init(); //Initialize if not yet set. 
  }
  #if FCLK_ENABLE == true //Fastclock only ticks when enabled in config. If not, it will load config values and not change unless intentionally set. 
  time_us = TIME_US;
  time_delta = (time_us - set_at_us) * set_rate + set_us; //(Time passed) * set_rate + set_us
  #endif 
  
  days = time_delta / US_PER_DAY; //time_delta / uS per day
  time_delta = time_delta - ( uint64_t(days) * US_PER_DAY); //Subtract days from time_delta
  
  hours = time_delta / US_PER_HOUR; //time_delta / uS per hour
  time_delta = time_delta - ( uint64_t(hours) * US_PER_HOUR); //Subtract hours from time_delta
  
  minutes = time_delta / US_PER_MINUTE; //time_delta / uS per minute
  time_delta = time_delta - ( uint64_t(minutes) * US_PER_MINUTE); //Subtract minutes from time_delta 
  frac_minutes_uS = time_delta; 
   
  seconds = time_delta / US_PER_SECOND; //uS per second
  time_delta = time_delta - ( uint64_t(seconds) * US_PER_SECOND); //Subtract seconds from time_delta
  //Serial.printf("Fastclock clock_get: day %u, %u:%u:%u \n", days, hours, minutes, seconds);
  return;
}
#endif
