#ifndef ESP32_TIMER_H
  #include "ESP32_timer.h"
#endif
#if BOARD_TYPE == ARCH_BRIDGE
  #include "ESP32_Loconet.h"
#endif

//ESP_gptimer esp_gptimer;

volatile uint64_t time_us = 0; 
uint64_t last_time_us = 0;
uint64_t meas_start_time= 0;
uint64_t meas_cumu_time = 0;

uint64_t esp_us(){ //Equivalent to micros(); 
  time_us = esp_timer_get_time(); //micros(); //
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


//ESP GPTimer functions

void IRAM_ATTR ESP_gptimer::alarm_set(uint32_t count, uint8_t owner){
  alarm_owner_v = owner; 
  //ESP_ERROR_CHECK(gptimer_stop(gptimer));
  gptimer_stop(gptimer);
  gptimer_disable(gptimer);
  
  gptimer_alarm_config_t alarm_config = {
    .alarm_count = count, // alarm target = count in us
  };
  ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

  gptimer_event_callbacks_t cbs = {
    .on_alarm = gptimer_alarm_cb_t(ESP_gptimer_alarm_isr), // register user callback
  };
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
  gptimer_set (0); //Set the count to 0
  gptimer_enable(gptimer);
  gptimer_start(gptimer);
  alarm_counting = true; 
  return;
}

uint8_t IRAM_ATTR ESP_gptimer::alarm_owner(){ //Returns alarm owner. 
  return alarm_owner_v;
}

bool ESP_gptimer::alarm_is_set(){ //returns true if alarm is set
  if (alarm_owner_v == 0){ //If there is no owner then it shouldn't be counting. 
    alarm_counting = false; 
  }
  return alarm_counting;
}

uint32_t ESP_gptimer::alarm_value(){ //returns value set in alarm
  return alarm_set_value;
}


uint8_t ESP_gptimer::gptimer_init(){
gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = 1 * 1000 * 1000, // 1MHz, 1 tick = 1us
};
ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));
gptimer_set (0); //Set the count to 0
ESP_ERROR_CHECK(gptimer_enable(gptimer)); //enable gptimer, priming it for later
ESP_ERROR_CHECK(gptimer_start(gptimer)); //start gptimer, priming it for later 
return 0; //Eventually make this return non-zero if the gptimer_new_timer fails
}

uint64_t IRAM_ATTR ESP_gptimer::gptimer_read(){
  uint64_t read_value; 
  ESP_ERROR_CHECK(gptimer_get_raw_count(gptimer, &read_value));
}
  
void ESP_gptimer::gptimer_set(uint64_t newcount){
  gptimer_set_raw_count(gptimer, newcount);
  return; 
}

void IRAM_ATTR ESP_gptimer_alarm_isr(){ //ISR for gptimer alarm
  #if BOARD_TYPE == ARCH_BRIDGE
    #ifndef ESP32_LOCONET_H
      #include "ESP32_Loconet.h"
    #endif
    LN_gptimer_alarm(); //This will be better handled some other way eventually.    
  #endif
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
  Serial.printf("Fastclock: Clock set to day %u, %u:%u:%u, rate %ux\n", s_days, s_hours, s_minutes, s_seconds, set_rate);
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
  minutes_rem_uS = time_delta; 
   
  seconds = time_delta / US_PER_SECOND; //uS per second
  time_delta = time_delta - ( uint64_t(seconds) * US_PER_SECOND); //Subtract seconds from time_delta
  //Serial.printf("Fastclock clock_get: day %u, %u:%u:%u \n", days, hours, minutes, seconds);
  return;
}
#endif

void IRAM_ATTR ESP_pwmcap_isr(){ //ISR Handler for the timer
  

  return;
}
