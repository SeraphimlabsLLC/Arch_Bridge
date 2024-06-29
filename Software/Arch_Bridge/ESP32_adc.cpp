#ifndef ESP32_ADC_H
  #include "ESP32_ADC.h"
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
adc_continuous_evt_data_t* adc_raw_data; //Holder for pointer to adc raw output
volatile bool ADC_result_ready = false; //Flag set by ISR to indicate new data since last loop
volatile int32_t adc_curent_ticks[ADC_SLOTS]; //Receive the current values from within ISR context
volatile int32_t adc_overload_ticks[ADC_SLOTS]; //Make the overload thresholds readable to ISR context
volatile uint32_t adc_channel_map[ADC_SLOTS]; //Store what pin was used to configure this slot


adc_continuous_handle_t adc_unit_one;

void ADC_loop(){ //process ISR and enter struct
    if (ADC_result_ready == true) {
    // Set ISR flag back to false
    ADC_result_ready = false;
    for (int i = 0; i < adc_active_count; i++) { 
      adc_one[i].adc_loop(); 
    }
  }
  return;
}

void ADC_Handler::adc_loop(){

return; 
}

uint8_t ADC_Handler::adc_channel_config(uint8_t adc_ch, int16_t offset, int32_t adc_ol_trip){
  uint8_t index = adc_active_count; 
  adc_active_count++; //Increment it now so we don't forget.
  adc_overload_ticks[index] = adc_ol_trip;
  adc_channel_map[index] = adc_ch;   
  hw_channel = adc_ch; 
  offset_ticks = offset;
  current_ticks = previous_ticks = base_ticks = 0; //Set all 3 ADC values to 0 initially
  Serial.printf("ADC1: Reserved slot %u for channel %u, backup in %u \n", index, adc_channel_map[index], hw_channel); 
  return index;
}

void ADC_Handler::adc_read(){
  int32_t adcraw = 0;
  previous_ticks = current_ticks; //update value read on prior scan
  //ADC runs at a max of 5MHz, and needs 25 clock cycles to complete. Effectively 200khz or 5usec minimum. 
//  adcraw = adc1_get_raw(adc_channel_t (hw_channel));
  current_ticks = adcraw * 1000 + offset_ticks; //Calculation scale changed from 1000000 to 1000 to better fit int32_t and save some ram. 
  if (current_ticks < 0){ //offset_ticks was negative, and would result in a negative ticks. Just set it to 0. 
    current_ticks = 0; 
  } 
  smooth_ticks = ((smooth_ticks * 15) + current_ticks) / 16;
  return;
}

void ADC_Setup_Commit() { //Run last in Setup() to commit the selected ADC configuration. 
  uint8_t i = 0; 
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
  adc_continuous_handle_cfg_t adc_unit_one_config = {
    .max_store_buf_size = 1024,
    .conv_frame_size = 4 * (adc_active_count - 1),
  };
ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_unit_one_config, &adc_unit_one)); //Initialize ADC1 continuous mode driver

  adc_continuous_config_t adc1_channel_config = {
    .pattern_num = adc_active_count,
    .sample_freq_hz = 80000, //80KHz. 
    .conv_mode = ADC_CONV_SINGLE_UNIT_1,
    .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
  };

  adc_digi_pattern_config_t channel_pattern[adc_active_count - 1] = {0};
//  for (i = 0; i <= (adc_active_count - 1); i++) {
  while (i <= (adc_active_count - 1)) {
//    if (adc_one[i].hw_channel > 0) { //Only add it if configured. 
      channel_pattern[i].atten = 12; 
      channel_pattern[i].channel = adc_channel_t (adc_one[i].hw_channel - 1);
      channel_pattern[i].unit = 0; //Set to ADC1. Unit 1 is ADC2. 
      channel_pattern[i].bit_width = ADC_BITWIDTH_12; 
      Serial.printf("ADC1: Configured ADC in slot %u for GPIO %u, hw_channel %u \n", i, uint8_t (adc_channel_map[i]), uint8_t (adc_one[i].hw_channel));
      i++;
    }
  adc1_channel_config.adc_pattern = channel_pattern;
  ESP_ERROR_CHECK(adc_continuous_config(adc_unit_one, &adc1_channel_config));
  adc_continuous_evt_cbs_t ISR_callbacks; 
  
//  ISR_callbacks.on_conv_done = ADC_Ready_ISR;
//  ISR_callbacks.on_pool_ovf = ADC_Full_ISR;
//  ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc_unit_one, *ISR_callbacks, NULL));
  ESP_ERROR_CHECK(adc_continuous_start(adc_unit_one));
 
  return; 
}

void IRAM_ATTR ADC_Ready_ISR(){
/*uint8_t adc_active_count; 
volatile bool ADC_result_ready = false; 
volatile uint16_t ADC_result = 0; 
volatile uint8_t adc_currently_reading = 0;*/ 
ADC_result_ready = true; 

  return;
}

void IRAM_ATTR ADC_Full_ISR(){ //ADC sample memory full, flush it. 

  return;
}
