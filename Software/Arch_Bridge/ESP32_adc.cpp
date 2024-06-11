#ifndef ESP32_ADC_H
  #include "ESP32_ADC.h"
#endif

#include "Arduino.h"
#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
//#include <soc/sens_reg.h>
//#include <soc/sens_struct.h>

uint8_t adc_active_count = 0; 
volatile bool ADC_result_ready = false; 
volatile uint16_t ADC_result = 0; 
volatile uint8_t adc_currently_reading = 0; 
adc_continuous_handle_t adc_unit_one;
ADC_Handler adc_one[ADC_SLOTS];

void ADC_loop(){
  //adc_one.adc_loop(); 
  return;
}

void ADC_Handler::adc_loop(){
  /*if (ADC_result_ready == true) {
    
  } */
}

void ADC_Handler::adc_channel_config(uint8_t adcpin, int16_t offset){
  hw_channel = adc_channel_t (adcpin - 1); 
//  adc1_config_channel_atten(adc_channel,ADC_ATTEN_11db);//config attenuation
//  adc1_config_channel_atten(hw_channel,ADC_ATTEN_DB_12);//config attenuation
  offset_ticks = offset;
  current_ticks = previous_ticks = base_ticks = 0; //Set all 3 ADC values to 0 initially
  return;
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
  for (i = 0; i <= adc_active_count; i++) {
//    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_unit_one, adc_one[i].hw_channel, &channel_config));
  }
  return; 
}

void IRAM_ATTR ADC_Done_ISR(){
uint8_t adc_active_count; 
volatile bool ADC_result_ready = false; 
volatile uint16_t ADC_result = 0; 
volatile uint8_t adc_currently_reading = 0; 
ADC_result_ready = true; 


  return;
}
