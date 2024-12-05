#pragma once
#define ESP32_ADC_H

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
//#include "freertos/mutex.h"
#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
//#include <soc/sens_reg.h>
//#include <soc/sens_struct.h>

//ADC Settings:
#define OL_COOLDOWN 5000 //Time in mS that the power should be off when overload detected. 
#define ADC_DMAX 4095 //4095 in single shot, 8191 in continuous
#define ADC_VMAX 3.1 //Max readable voltage is actually 3.1v using mode ADC_ATTEN_DB_11  
#define ADC_MIN_OFFSET 60 //ADC is inaccurate at low values.

//Number of ADC slots to use: 
#if BOARD_TYPE == DYNAMO
  #define ADC_SLOTS 4
#endif
#if BOARD_TYPE == ARCH_BRIDGE
  #define ADC_SLOTS 3
#endif

#define ADC_READSIZE 128
#define ADC_TASK true



class ADC_Handler {
    public:   
    uint8_t adc_channel_config(uint8_t adc_handle, uint8_t adc_ch, int16_t offset, int32_t adc_ol_trip); 
    void adc_sample(); //Sample the voltage and update the values
    void adc_read(int32_t* current, int32_t* previous, int32_t* smooth, int32_t* overload); //returns the stored values
    void adc_zero_set(int32_t current); //Adjusts base_ticks so that current_ticks would be zero
    void adc_loop();
    uint8_t adc_show_slot(); //returns current handler index

    private: 
    SemaphoreHandle_t ticks_mutex; //mutex to protect outputs
    int32_t current_ticks; //value read on most recent scan
    int32_t previous_ticks; //value read on prior scan
    int32_t smooth_ticks; // =(adc_smooth_ticks * 15 + adc_current_ticks) / 16
    int32_t base_ticks; //value read from ADC when output is off for calc reference.
    int32_t offset_ticks; //ADC offset in ticks * 1000. 
    int32_t overload_ticks; //Pre-calculate trip threshold in adc ticks
    uint8_t assigned_slot; //Index of current and overload ticks for the values we want.  
//    uint8_t hw_channel; //Hardware channel used
    uint8_t gpio_pin; //GPIO pins used
    std::function<void(int32_t, int32_t)> overload_check; //store overload check callback

};

uint8_t ADC_new_handle(); //Returns what the next available ADC slot is
void ADC_Setup_Commit(); //Configure ADC unit 1 for 12 bits + all configured channels
void ADC_loop(); //ADC polling loop
void adc_task(void * pvParameters); //ADC Task
void IRAM_ATTR ADC_Ready_ISR(); //Conversion complete ISR callback
void IRAM_ATTR ADC_Full_ISR(); //ADC memory full. Flush it. 
