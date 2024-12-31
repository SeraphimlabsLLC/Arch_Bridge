#ifndef ESP32_ADC_H
  #include "ESP32_adc.h"
#endif

#include "Arduino.h"
#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
//#include <soc/sens_reg.h>
//#include <soc/sens_struct.h>
#include <list>
using namespace std;

uint8_t adc_active_count = 0; //How many ADC pins are being read? 
ADC_Handler adc_one[ADC_SLOTS];
TaskHandle_t adctask; 
bool adc_task_active = false; 

uint8_t* adc_raw_bytes = NULL; //holder for raw adc data 
adc_continuous_evt_cbs_t* ADC_ISR_callbacks = NULL; //Pointer holder for ISR callbacks

//volatile int16_t ADC_stored_samples = 0; //Approx how many samples in the buffer

//volatile int32_t adc_curent_ticks[ADC_SLOTS]; //Receive the current values from within ISR context
//volatile int32_t adc_overload_ticks[ADC_SLOTS]; //Make the overload thresholds readable to ISR context
//volatile int32_t adc_channel_map[ADC_SLOTS]; //Store what pin was used to configure this slot

adc_continuous_handle_t adc_unit_one;

void ADC_loop(){ 
  #if ADC_DRIVER == Arduino
  uint8_t i = 0;
  if (adc_task_active != true) { 

    for (i = 0; i < ADC_SLOTS; i++) {
      adc_one[i].adc_sample(); //Read the ADC and update the saved values
    }
  }
  #endif
  return; 
}

void ADC_Continuous_Parser(uint32_t samples) {
  esp_err_t res; 
  uint32_t adc_readlen; //samples read
  uint32_t channel; //sample channel
  int gpio_pin; //sample pin
  uint32_t adc_data; //sample data
  
  int32_t i = 0; 
  //Serial.printf("ADC: Continuous Parser. Samples %i \n", samples); 
 if (!(adc_raw_bytes)){
   adc_raw_bytes = new uint8_t[ADC_READSIZE]; 
   //adc_raw_bytes = new adc_digi_output_data_t[ADC_READSIZE]; //USE adc read struct instead of uint8_t
     if (!(adc_raw_bytes)){
       Serial.printf("ADC: Read buffer of %u bytes not allocated, aborting. \n", ADC_READSIZE); 
       return; 
    }  
  }

  if (samples != 0) {
    uint8_t startpos = 0;
    if (samples > ADC_READSIZE){
      Serial.printf("ADC WARNING: Processor not keeping up with ADC sample rate \n");
    }
    res = adc_continuous_read(adc_unit_one, adc_raw_bytes, ADC_READSIZE, &adc_readlen, 0); //adc unit, buffer, max read size, actually read size, timeout ms
    if (res != ESP_OK) {
      Serial.printf("ADC WARNING: Sample problem, values may be wrong! %i \n", samples);
      delay(1000); //BAD, but just to break it visibly. 
      return;     
    }
    for (i = startpos; i < adc_readlen; (i = i + SOC_ADC_DIGI_RESULT_BYTES)) {
      adc_digi_output_data_t *adc_raw = (adc_digi_output_data_t*)&adc_raw_bytes[i]; //Converts the raw bytes to ADC data struct  
      channel = adc_raw->type2.channel; 
      adc_data = adc_raw->type2.data;   
      adc_continuous_channel_to_io(ADC_UNIT_1, adc_channel_t(channel), &gpio_pin);  //lookup the gpio that matches
      //Serial.printf("ADC Channel %i read value %i from GPIO %u \n", channel, adc_data, gpio_pin);
    
      uint8_t j = 0; 
      for (j = 0; j <= adc_active_count; j++){
        if ((adc_one[j].get_gpio()) == gpio_pin) {
          adc_one[j].adc_save_sample(adc_data); //Save ADC sample 
          
        }
      }  
    }
  }
  return;
}

void adc_task(void * pvParameters){ //ADC Task
#if ADC_TASK == true //#define ADC_TASK true to actually use the task driver. Otherwise it runs from arduino loop()
  uint8_t i = 0; 
  uint32_t samples = 0;
  uint64_t last_scan_time_us = 0;
  uint64_t time_us = 0; 
  while (true) { //
    last_scan_time_us = esp_timer_get_time();
    samples = ulTaskNotifyTake(pdTRUE, 1000); //blocks until there is >0 samples
    if (samples == 0){
      //Major problem, disable power
      break; //don't try to process samples.
    } 
    ADC_Continuous_Parser(samples); //Process the data in the adc result queue
    //ADC_loop(); 
    #if ADC_DRIVER == Arduino //Only if Arduino API is in use. 
    for (i = 0; i < ADC_SLOTS; i++) {
      adc_one[i].adc_sample(); //Read the ADC and update the saved values
    }
    #endif
  //  time_us = esp_timer_get_time(); 
  //  delayMicroseconds(500 - (time_us - last_scan_time_us));
    //Serial.printf("ADC Task scan time %u uS \n", time_us - last_scan_time_us);

} //while
#endif
return;
} //function

void ADC_Handler::adc_loop(){

  return;
}

void ADC_Handler::adc_sample(){
  #if ADC_DRIVER == Arduino
  int32_t adc_raw;  
  adc_raw = analogRead(gpio_pin); 
  adc_save_sample(adc_raw); 
  #endif
  return; 
}

void ADC_Handler::adc_save_sample(int32_t sample){
  if (xSemaphoreTake(ticks_mutex, 1000)){ //Check if the channel is free, skip it if its not within 10 ticks
    previous_ticks = current_ticks; //store existing value
    
    current_ticks = sample * 1000 + offset_ticks + base_ticks; 
    smooth_ticks = ((smooth_ticks * 15) + current_ticks) / 16;
    //overload_check(current_ticks, overload_ticks); //Use callback to check for overload and take action to set/clear
    xSemaphoreGive(ticks_mutex); //Release the channel
  } else {
    Serial.printf("ADC sample skipped on gpio %u \n", gpio_pin); 
  }
  return; 
}

void ADC_Handler::adc_read(int32_t* current, int32_t* previous, int32_t* smooth, int32_t* overload){
  if (xSemaphoreTake(ticks_mutex, 1000)){ //Check if the channel is free, skip it if its not within 10 ticks
    if (current) { //Is a valid pointer
      *current = current_ticks;
    }
    if (previous) {
      *previous = previous_ticks;
    }
    if (smooth) {
       *smooth = smooth_ticks;
    }
   if (overload) {
       *overload = overload_ticks;
    }
  xSemaphoreGive(ticks_mutex); //Release
  }
  return;
}

void ADC_Handler::adc_zero_set(int32_t current){ //Adjusts base_ticks so that current_ticks would be zero
  if (xSemaphoreTake(ticks_mutex, 10)){
      base_ticks = base_ticks - current; 
    xSemaphoreGive(ticks_mutex); //Release
  }
return; 
}

uint8_t ADC_Handler::adc_channel_config(uint8_t adc_handle, uint8_t adc_ch, int16_t offset, int32_t adc_ol_trip){
  gpio_pin = adc_ch; 
  offset_ticks = offset;
  overload_ticks = adc_ol_trip; 
  current_ticks = previous_ticks = base_ticks = 0; //Set all 3 ADC values to 0 initially

  assigned_slot = adc_handle; //save which slot we are using. 
  ticks_mutex = xSemaphoreCreateMutexStatic(&ticks_mutex_buffer); //Initialize the mutex for use. 
  Serial.printf("ADC1: Reserved slot %i for channel %i\n", adc_handle, gpio_pin); 
  return adc_handle;
}

uint8_t ADC_Handler::get_gpio(){
  return gpio_pin;
}

void ADC_Setup_Commit() { //Run last in Setup() to commit the selected ADC configuration. 
  uint8_t i = 0; 
  #if ADC_TASK == true
    adc_task_active = true;
    xTaskCreatePinnedToCore(adc_task, "adctask", 10000, NULL, 1, &adctask, 1); //priority is number before &
    Serial.printf("ADC_Task started \n"); 
  #endif
/*  adc_oneshot_unit_init_cfg_t init_config = {
    .unit_id = ADC_UNIT_1,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_unit_one));
  
  adc_oneshot_chan_cfg_t channel_config = {
    .bitwidth = ADC_BITWIDTH_12,
    .atten = ADC_ATTEN_DB_12,
  };
*/

//adc_active_count at this point should contain the configured number of channels
//4 bytes per conversion * number of channels active
//Buffer size 1024 works. 
  adc_continuous_handle_cfg_t adc_unit_one_config = {
    .max_store_buf_size = 1024,
    .conv_frame_size = 4 * (adc_active_count + 1),
  };
  
ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_unit_one_config, &adc_unit_one)); //Initialize ADC1 continuous mode driver
  Serial.printf("ADC1: adc_active_count %i \n", adc_active_count);
  adc_continuous_config_t adc1_channel_config = {
    .pattern_num = adc_active_count,
    .sample_freq_hz = 1000, //83KHz max
    .conv_mode = ADC_CONV_SINGLE_UNIT_1,
    .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
  };

  adc_digi_pattern_config_t channel_pattern[adc_active_count] = {0};
//  for (i = 0; i <= (adc_active_count); i++) {

  esp_err_t res;
  uint8_t adc_gpio; 
  adc_channel_t adc_ch; 
  adc_unit_t adc_unit; 
  i = 0; 
  while (i <= adc_active_count) {

    adc_gpio = adc_one[i].get_gpio();
    
    res = adc_continuous_io_to_channel (adc_gpio, &adc_unit, &adc_ch); //converts GPIO number to adc unit and channel
    if (res == ESP_OK) { //Only add it if valid
      channel_pattern[i].atten = 12; //12
      channel_pattern[i].channel = adc_ch; 
      channel_pattern[i].unit = adc_unit; 
      channel_pattern[i].bit_width = adc_bitwidth_t(12); //ADC_BITWIDTH_12; //12
      //adc_channel_map[i] = adc_ch; 
      Serial.printf("ADC1: Configuring ADC in slot %u for GPIO %i, adc_channel %u.", adc_gpio, adc_gpio, adc_ch);//), uint8_t (adc_channel_map[i]));
      Serial.printf(" slot %i configured to adc_ch %i ", i, adc_ch);
    }
    Serial.printf(" \n");
    i++;
  }
  adc1_channel_config.adc_pattern = channel_pattern;
  ESP_ERROR_CHECK(adc_continuous_config(adc_unit_one, &adc1_channel_config));
  //if (res != ESP_OK){
    //Serial.printf("ADC channel config returned problem \n"); 
  //}
  
  ADC_ISR_callbacks = new adc_continuous_evt_cbs_t;
  if (ADC_ISR_callbacks){ //The pointer is initialized, go for it. 
    ADC_ISR_callbacks->on_conv_done = adc_continuous_callback_t(ADC_Ready_ISR);
    ADC_ISR_callbacks->on_pool_ovf = adc_continuous_callback_t(ADC_Full_ISR);
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc_unit_one, ADC_ISR_callbacks, NULL));
    //ADC_stored_samples = 0; //Initialize sample counter
    ESP_ERROR_CHECK(adc_continuous_start(adc_unit_one));
  } else {
    Serial.printf("ADC could not create callback config handle. ADC not started.  \n");
  }
  
  return; 
}

 //Returns what the next available ADC slot is
uint8_t ADC_new_handle(){
/*  uint8_t i = 0; 
  Serial.printf("ADC New Handle read slot i %u \n", i);
  while ((adc_one[i].ticks_mutex) && (i < ADC_SLOTS)) {
    i++; 
  }
  Serial.printf("ADC New Handle %u of %u \n", i, ADC_SLOTS);
  return i; 
*/
  adc_active_count++; 
  return (adc_active_count - 1); 
}

void IRAM_ATTR ADC_Ready_ISR(){
  //ADC_stored_samples++; 
  BaseType_t wakeup;
  wakeup = pdFALSE;
  xTaskNotifyFromISR(adctask, 0, eIncrement, &wakeup); //adc processing task,  fixed value to set if undefined, behavior of ++, if a higher priority task must wake
  if (wakeup == pdTRUE) {
      //context switch to adc task if necessary
    portYIELD_FROM_ISR (wakeup);
  }
  return;
}

void IRAM_ATTR ADC_Full_ISR(){ //ADC sample memory full, flush it. 
//  if (ADC_stored_samples > 0) {
//    ADC_stored_samples = ADC_stored_samples * -1;
//  }
  return;
}
