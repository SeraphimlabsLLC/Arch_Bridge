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
uint8_t* adc_raw_bytes = NULL; //holder for raw adc data
//adc_continuous_evt_data_t* adc_raw_data; //Holder for pointer to adc raw output

volatile bool ADC_result_ready = false; //Flag set by ISR to indicate new data since last loop

volatile int32_t adc_curent_ticks[ADC_SLOTS]; //Receive the current values from within ISR context
volatile int32_t adc_overload_ticks[ADC_SLOTS]; //Make the overload thresholds readable to ISR context
volatile uint32_t adc_channel_map[ADC_SLOTS]; //Store what pin was used to configure this slot

adc_continuous_handle_t adc_unit_one;

void ADC_loop(){ //process ISR and enter struct
  esp_err_t res; 
  uint32_t adc_readlen; 
  uint32_t channel;
  uint32_t adc_data;
  
  int32_t i = 0; 

//    if (ADC_result_ready == true) {
    // Set ISR flag back to false
//    ADC_result_ready = false;

//Todo: Once the callback is fixed move this to the ISR

    if (!(adc_raw_bytes)){
      adc_raw_bytes = new uint8_t[ADC_READSIZE]; 
      if (!(adc_raw_bytes)){
        Serial.printf("ADC: Read buffer of %u bytes not allocated, aborting. \n", ADC_READSIZE); 
        return; 
      }
    }
    res = adc_continuous_read(adc_unit_one, adc_raw_bytes, ADC_READSIZE, &adc_readlen, 1);
    while ((adc_readlen > 0) && (res == ESP_OK)) { 
      //Serial.printf("ADC Readlen %u bytes, working %u bytes per loop \n", adc_readlen, SOC_ADC_DIGI_RESULT_BYTES); 
      
      for (i = 0; i < adc_readlen; i + SOC_ADC_DIGI_RESULT_BYTES) {
      //Serial.printf("ADC processing sample %i \n", i); 
      adc_digi_output_data_t *adc_raw = (adc_digi_output_data_t*)&adc_raw_bytes[i]; //Converts the raw bytes to ADC data struct  
      channel = adc_raw->type2.channel; 
      adc_data = adc_raw->type2.data; 
      Serial.printf("ADC Channel %i read value %i \n", adc_raw->type2.channel, adc_raw->type2.data);

      channel = (adc_raw_bytes[i+1])>>5;
      adc_data = adc_raw_bytes[i] + (adc_raw_bytes[i+1])<<8;// & uint32_t(0xFFF); 
      Serial.printf("ADC Channel alt %i read value %i \n", channel, adc_data);
      //Serial.printf("ADC Channel byte0 %i, byte1 %i, byte2 %i, byte3 %i \n", &adc_raw_bytes[i], &adc_raw_bytes[i+1], &adc_raw_bytes[i+2], &adc_raw_bytes[i+3]);

      uint8_t j = 0; 
      for (j = 0; j < adc_active_count; j++){
        if (adc_channel_map[j] == channel + 1){
          adc_curent_ticks[j] = adc_data;
        }
      }
      
    }
    Serial.printf("ADC read: ");
    for  (i = 0; i < adc_readlen; i++) {
          Serial.printf("%i ", adc_raw_bytes[i]);       
    }
    Serial.printf("\n");
 
    res = adc_continuous_read(adc_unit_one, adc_raw_bytes, ADC_READSIZE, &adc_readlen, 1);
//  }  

  }
  for (int i = 0; i < adc_active_count; i++) { 
    adc_one[i].adc_loop(); 
  }  
  return;
}

void ADC_Handler::adc_loop(){

return; 
}

void ADC_Handler::adc_read(){
  int32_t adcraw = 0;
  previous_ticks = current_ticks; //update value read on prior scan
  //ADC runs at a max of 5MHz, and needs 25 clock cycles to complete. Effectively 200khz or 5usec minimum. 
//  adcraw = adc1_get_raw(adc_channel_t (hw_channel));
  adcraw = adc_curent_ticks[assigned_slot];  
  current_ticks = adcraw * 1000 + offset_ticks + base_ticks; //Calculation scale changed from 1000000 to 1000 to better fit int32_t and save some ram. 
  if (current_ticks < 0){ //offset_ticks was negative, and would result in a negative ticks. Just set it to 0. 
    current_ticks = 0; 
  } 
  smooth_ticks = ((smooth_ticks * 15) + current_ticks) / 16;
  return;
}

uint8_t ADC_Handler::adc_channel_config(uint8_t adc_handle, uint8_t adc_ch, int16_t offset, int32_t adc_ol_trip){
  adc_curent_ticks[adc_handle] = -65535; //set initial ticks value negative to show it hasn't been initialized yet. 
  adc_overload_ticks[adc_handle] = adc_ol_trip;
  adc_channel_map[adc_handle] = adc_ch;   
  hw_channel = adc_ch; 
  offset_ticks = offset;
  current_ticks = previous_ticks = base_ticks = 0; //Set all 3 ADC values to 0 initially
  assigned_slot = adc_handle; //save which slot we are using. 
  //Serial.printf("ADC1: Reserved slot %u for channel %u, backup in %u \n", index, adc_channel_map[index], hw_channel); 
  return adc_handle;
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
//Buffer size 1024 works. 
  adc_continuous_handle_cfg_t adc_unit_one_config = {
    .max_store_buf_size = 256,
    .conv_frame_size = 4 * (adc_active_count),
  };

  
ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_unit_one_config, &adc_unit_one)); //Initialize ADC1 continuous mode driver

  adc_continuous_config_t adc1_channel_config = {
    .pattern_num = adc_active_count,
    .sample_freq_hz = 20000, //83KHz max
    .conv_mode = ADC_CONV_SINGLE_UNIT_1,
    .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
  };

  adc_digi_pattern_config_t channel_pattern[adc_active_count] = {0};
//  for (i = 0; i <= (adc_active_count); i++) {

  esp_err_t res; 
  adc_channel_t adc_ch; 
  adc_unit_t adc_unit; 
  while (i <= adc_active_count) {
    Serial.printf("ADC1: Configuring ADC in slot %u for GPIO %u, hw_channel %u", i, uint8_t (adc_channel_map[i]), uint8_t (adc_one[i].hw_channel));
    res = adc_continuous_io_to_channel (adc_channel_map[i], &adc_unit, &adc_ch);
    if (res == ESP_OK) { //Only add it if valid
      channel_pattern[i].atten = 12; //12
      channel_pattern[i].channel = adc_ch; 
      channel_pattern[i].unit = adc_unit; 
      channel_pattern[i].bit_width = adc_bitwidth_t(12); //ADC_BITWIDTH_12; //12
      Serial.printf("slot %i configured ", i);
    }
    Serial.printf(" \n");
    i++;
  }
  adc1_channel_config.adc_pattern = channel_pattern;
  ESP_ERROR_CHECK(adc_continuous_config(adc_unit_one, &adc1_channel_config));
  adc_continuous_evt_cbs_t ISR_callbacks; 
  
//  ISR_callbacks.on_conv_done = ADC_Ready_ISR;
//  ISR_callbacks.on_pool_ovf = ADC_Full_ISR;
//  ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc_unit_one, *ISR_callbacks, NULL));

  ESP_ERROR_CHECK(adc_continuous_start(adc_unit_one));
  Serial.printf("ADC started \n"); 
  return; 
}

 //Returns what the next available ADC slot is
uint8_t ADC_new_handle(){
   
  if (adc_channel_map[adc_active_count] > 0) {
    adc_active_count++;
  }
  //Serial.printf("ADC new handle %u \n", adc_active_count);
  return adc_active_count; 
}

void IRAM_ATTR ADC_Ready_ISR(){
ADC_result_ready = true; 

  return;
}

void IRAM_ATTR ADC_Full_ISR(){ //ADC sample memory full, flush it. 

  return;
}
