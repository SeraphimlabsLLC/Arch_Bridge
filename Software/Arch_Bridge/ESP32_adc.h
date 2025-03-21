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
#define ADC_DRIVER esp_cont //Arduino or eap_cont to choose which api is used



class ADC_Handler {
    public:   
    void adc_channel_config(uint8_t adc_ch, int16_t offset, int32_t adc_ol_trip, uint8_t overload_mode, uint8_t ownerinstance); 
    void adc_sample(); //Sample the ADC using the Arduino IDE api
    void adc_save_sample(int32_t sample); //Save the provided sample
    void adc_read(int32_t* current, int32_t* previous, int32_t* smooth, int32_t* overload); //returns the stored values
    void adc_zero_set(); //Adjusts base_ticks so that current_ticks would be zero
    void adc_loop();
    uint8_t adc_show_slot(); //returns current handler index
    uint8_t get_gpio(); //returns gpio number set by adc_channel_config

    void print_flag(bool state); //true to print current sample
    volatile bool print_flag_var; //true to print current sample

    private: 
    uint8_t ol_mode; //Eventually use a real callback, for now an int to point it to the right library. 
    uint8_t owner_instance; //Index of the owner for these values
    void *ol_callback; 
    SemaphoreHandle_t ticks_mutex; //mutex to protect output
    StaticSemaphore_t ticks_mutex_buffer; //memory allocation for mutex
    volatile int32_t current_ticks; //value read on most recent scan
    volatile int32_t previous_ticks; //value read on prior scan
    volatile int32_t smooth_ticks; // =(adc_smooth_ticks * 15 + adc_current_ticks) / 16
    int32_t base_ticks; //value read from ADC when output is off for calc reference.
    int32_t offset_ticks; //ADC offset in ticks * 1000. 
    int32_t overload_ticks; //Pre-calculate trip threshold in adc ticks
    uint8_t gpio_pin; //GPIO pin used
    std::function<void(int32_t, int32_t)> overload_check; //store overload check callback


};

uint8_t ADC_new_handle(); //Returns what the next available ADC slot is
void ADC_Setup_Commit(); //Configure ADC unit 1 for 12 bits + all configured channels
void ADC_loop(); //ADC polling loop

void ADC_Continuous_Parser(uint32_t samples);// Sample processing loop
void adc_task(void * pvParameters); //ADC Task
void IRAM_ATTR ADC_Ready_ISR(); //Conversion complete ISR callback
void IRAM_ATTR ADC_Full_ISR(); //ADC memory full. Flush it. 
