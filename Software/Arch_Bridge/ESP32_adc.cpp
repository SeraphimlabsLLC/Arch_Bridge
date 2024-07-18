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
uint8_t* adc_raw_bytes = NULL; //holder for raw adc data
adc_continuous_evt_cbs_t* ADC_ISR_callbacks = NULL; //Pointer holder for ISR callbacks

volatile int16_t ADC_stored_samples = 16; //Approx how many samples in the buffer

volatile int32_t adc_curent_ticks[ADC_SLOTS]; //Receive the current values from within ISR context
volatile int32_t adc_overload_ticks[ADC_SLOTS]; //Make the overload thresholds readable to ISR context
volatile int32_t adc_channel_map[ADC_SLOTS]; //Store what pin was used to configure this slot

adc_continuous_handle_t adc_unit_one;

void ADC_loop(){ //process ISR and enter struct
  esp_err_t res; 
  uint32_t adc_readlen; 
  uint32_t channel;
  uint32_t adc_data;
  
  int32_t i = 0; 
  //Serial.printf("ADC: Loop start. Samples %i \n", ADC_stored_samples); 
 if (!(adc_raw_bytes)){
   adc_raw_bytes = new uint8_t[ADC_READSIZE]; 
     if (!(adc_raw_bytes)){
       Serial.printf("ADC: Read buffer of %u bytes not allocated, aborting. \n", ADC_READSIZE); 
       return; 
    }  
  }
  if (ADC_stored_samples != 0) {
    uint8_t startpos = 0;
    res = adc_continuous_read(adc_unit_one, adc_raw_bytes, ADC_READSIZE, &adc_readlen, 1);
    if ((ADC_stored_samples < 0) || (res != ESP_OK)) {
      Serial.printf("ADC WARNING: Sample problem, values may be wrong! %i \n", ADC_stored_samples);
      ADC_stored_samples = 0; //reset ISR counter
     //TODO: Code to verify ADC state and stop/restart it.
     
    }
    uint32_t samples_processed = 0; 
    while ((adc_readlen > 0) && (res == ESP_OK)) { 
      #define ADC_SAMPLES_PER_SCAN 2
//      Serial.printf("ADC Readlen %u bytes, working %u bytes per loop \n", adc_readlen, SOC_ADC_DIGI_RESULT_BYTES * adc_active_count * ADC_SAMPLES_PER_SCAN); 
      if (adc_readlen > (SOC_ADC_DIGI_RESULT_BYTES * adc_active_count * ADC_SAMPLES_PER_SCAN)) {
        startpos = adc_readlen - SOC_ADC_DIGI_RESULT_BYTES * adc_active_count * ADC_SAMPLES_PER_SCAN;
//        Serial.printf("ADC startpos %i \n", startpos); 
      } 
      for (i = startpos; i < adc_readlen; (i = i + SOC_ADC_DIGI_RESULT_BYTES)) {
      adc_digi_output_data_t *adc_raw = (adc_digi_output_data_t*)&adc_raw_bytes[i]; //Converts the raw bytes to ADC data struct  
      channel = adc_raw->type2.channel; 
      adc_data = adc_raw->type2.data; 
 //     Serial.printf("ADC Channel %i read value %i \n", adc_raw->type2.channel, adc_raw->type2.data);
      
      uint8_t j = 0; 
      for (j = 0; j <= adc_active_count; j++){
        //res = adc_continuous_channell_to_io (adc_channel_map[i], &adc_unit, &adc_ch);
        if (adc_channel_map[j] == channel){
          adc_curent_ticks[j] = adc_data;
          //Serial.printf("ADC Updated ADC_CH %i value %i \n", channel, adc_data); 
        }
      }
      samples_processed++; //Keep track of how many are done.       
    }
    if (samples_processed > ADC_stored_samples) {
      samples_processed = ADC_stored_samples;
    }
    ADC_stored_samples = ADC_stored_samples - samples_processed ; //Decrease for each sample processed.
    
    samples_processed = 0; 
    adc_readlen = 0; //reset it to be sure. 
    res = adc_continuous_read(adc_unit_one, adc_raw_bytes, ADC_READSIZE, &adc_readlen, 1);
  } 

  }
/*  Serial.printf("ADC checking %i GPIO values: ", adc_active_count);
  for (int i = 0; i <= adc_active_count; i++) { 
    Serial.printf("i is %i, ", i); 
 //   if (adc_curent_ticks[i]) {
      Serial.printf("channel %i has value %i, ", adc_channel_map[i] ,adc_curent_ticks[i]);
 //   }
    //adc_one[i].adc_loop(); 
  }
  Serial.printf(" \n"); */
//  Serial.printf("ADC Loop end \n");   
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
  adc_curent_ticks[adc_handle] = 0; 
  adc_overload_ticks[adc_handle] = adc_ol_trip;
  adc_channel_map[adc_handle] = -1;   
  hw_channel = adc_ch; 
  offset_ticks = offset;
  current_ticks = previous_ticks = base_ticks = adc_curent_ticks[adc_handle]; //Set all 3 ADC values to 0 initially
  assigned_slot = adc_handle; //save which slot we are using. 
  Serial.printf("ADC1: Reserved slot %i for channel %i\n", adc_handle, hw_channel); 
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
  adc_channel_t adc_ch; 
  adc_unit_t adc_unit; 
  i = 0; 
  while (i <= adc_active_count) {
    
    res = adc_continuous_io_to_channel (adc_one[i].hw_channel, &adc_unit, &adc_ch);
    if (res == ESP_OK) { //Only add it if valid
      channel_pattern[i].atten = 12; //12
      channel_pattern[i].channel = adc_ch; 
      channel_pattern[i].unit = adc_unit; 
      channel_pattern[i].bit_width = adc_bitwidth_t(12); //ADC_BITWIDTH_12; //12
      adc_channel_map[i] = adc_ch; 
      Serial.printf("ADC1: Configuring ADC in slot %u for GPIO %i, adc_channel %u", i, uint8_t (adc_one[i].hw_channel), uint8_t (adc_channel_map[i]));
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
    ADC_ISR_callbacks->on_pool_ovf = adc_continuous_callback_t(*ADC_Full_ISR);
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc_unit_one, ADC_ISR_callbacks, NULL));
    ADC_stored_samples = 0; //Initialize sample counter
    ESP_ERROR_CHECK(adc_continuous_start(adc_unit_one));
  } else {
    Serial.printf("ADC could not create callback config handle. ADC not started.  \n");
  }
  return; 
}

 //Returns what the next available ADC slot is
uint8_t ADC_new_handle(){
   
  if (adc_one[adc_active_count].hw_channel > 0) {
    adc_active_count++;
  }
  //Serial.printf("ADC new handle %u \n", adc_active_count);
  return adc_active_count; 
}

void IRAM_ATTR ADC_Ready_ISR(){
  ADC_stored_samples++; 

  return;
}

void IRAM_ATTR ADC_Full_ISR(){ //ADC sample memory full, flush it. 
  if (ADC_stored_samples > 0) {
//    ADC_stored_samples = ADC_stored_samples * -1;
  }
  return;
}
