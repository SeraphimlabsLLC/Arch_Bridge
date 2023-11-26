#ifndef ESP32_TIMER_H
  #include "ESP32_timer.h"
#endif

uint64_t time_us = 0; 
uint64_t last_time_us = 0;
uint64_t meas_start_time= 0;
uint64_t meas_cumu_time = 0;
Fastclock_class Fastclock; //For fastclock

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
void Fastclock_setup(bool enabled){ //Initialize fastclock from outside class
  Fastclock.clock_init(); 
}

void Fastclock_class::clock_init() { //Call clock_set with the values from the config file
  clock_set(FCLK_RATE, FCLK_SECONDS, FCLK_MINUTES, FCLK_HOURS, FCLK_DAYS);
  clock_get();
  active = true;
  return; 
}

void Fastclock_class::clock_set(uint8_t s_rate, uint8_t s_seconds, uint8_t s_minutes, uint8_t s_hours, uint8_t s_days) {

  set_rate = s_rate;
  set_seconds = s_seconds;
  set_minutes = s_minutes; 
  set_hours = s_hours; 
  set_days = s_days;
  set_at_us = TIME_US; //Time last set at
  active = true; 
  return;
}

void Fastclock_class::clock_get() {
   uint64_t time_delta;
  if (active != true) {
    clock_init(); //Initialize if not yet set. 
  }
  time_us = TIME_US;
  time_delta = (time_us - set_at_us) * set_rate; //Fastclock time passed since last set
  days = set_days + (time_delta / 86400000000); //uS per day
  
  hours = set_hours + (time_delta / 3600000000); //uS per hour
  hours = hours - (days * 24); 
  
  minutes = set_minutes + (time_delta / 60000000); //uS per minute
  minutes = minutes - (hours * 60); 
  
  seconds = set_seconds + (time_delta / 1000000); //uS per second
  seconds = seconds - (minutes * 60);  

  Serial.printf("Fastclock: day %u, %u:%u:%u \n", days, hours, minutes, seconds);
  
  return;
}
#endif
