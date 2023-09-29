//Contains some code snippets from DCC-EX ESP32 branch
#define ESP32_TRACKS__HW_H
#pragma once


#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/rmt.h"
#include "driver/adc.h"
#include "soc/rmt_reg.h"
#include "soc/rmt_struct.h"
#include "string.h"

//What board are we using? BOARD_TYPE_DYNAMO or BOARD_TYPE_ARCH_BRIDGE
#define BOARD_TYPE_ARCH_BRIDGE

//ADC Settings:
#define ADC_DMAX 4095 //4095 in single shot, 8191 in continuous
#define ADC_VMAX 3.1 //Max readable voltage is actually 3.1v using mode ADC_ATTEN_DB_11  

/*Track Configurations
 * Format: Enable Out pin, Enable In pin, rev/sig pin, brake pin, adc pin, adc ticks per amp, adc trip ticks
 * adc ticks per amp is calculated to match the hardware. 
 * adc trip ticks is calculated to where the hardware must shut off
 * 
 * Loconet hardware configuration
 * Format: uart#, mode, tx pin, rx pin, baud, txbuff, rxbuff, read size
 */

#ifdef BOARD_TYPE_DYNAMO
  #define TRACK_1 DCCSigs[0].SetupHW(9, 0, 6, 13, 1, 819, 4090);
  #define TRACK_2 DCCSigs[1].SetupHW(10, 0, 7,  14, 2, 819, 4090);
  #define TRACK_3 DCCSigs[2].SetupHW(11, 0, 8, 48, 3, 819, 4090);
  #define TRACK_4 DCCSigs[3].SetupHW(12, 0, 9, 48, 4, 819, 4090);
  #define DIR_MONITOR 38 //GPIO38
  #define DIR_OVERRIDE 21 //GPIO21
  #define MASTER_EN 15 //GPIO15
  #define DIR_MONITOR_RMT 4 //On ESP32-S3, RMT channels 0-3 are TX only and 4-7 are RX only
  #define DIR_OVERRIDE_RMT 0 
#endif

#ifdef BOARD_TYPE_ARCH_BRIDGE
  #define TRACK_1 DCCSigs[0].SetupHW(10, 13, 11, 12, 1, 16938, 4090);
  #define TRACK_2 DCCSigs[1].SetupHW(14, 48, 21,  47, 2, 1693, 4090);
  #define MASTER_EN 3
  #define DIR_MONITOR_RMT 4 //On ESP32-S3, RMT channels 0-3 are TX only and 4-7 are RX only
  #define DIR_MONITOR 9 //GPIO9, rsync input
  //Loconet setup
//  #define LN_CONFIG LN_port.uart_init( 1, 2, 17, 18, 16666, 255, 16384, 255);
#endif

//RMT Constants
#define RMT_CLOCK_DIVIDER 80  // make calculations easy and set up for microseconds. Taken from DCC-EX DCCRMT.h
#define DCC_1_HALFPERIOD 58  //4640 // 1 / 80000000 * 4640 = 58us
#define DCC_0_HALFPERIOD 100 //8000

//I2C settings: 
#define I2C_SDA_PIN 17 //GPIO17
#define I2C_SCL_PIN 18 //GPIO18
#define I2C_MASTER false
#define I2C_SLAVE_ADDR 43
#define I2C_CLOCK 40000

#define I2C_SLAVE_PORT 0
#define I2C_TX_BUFF 256
#define I2C_RX_BUFF 4096

class TrackChannel {
  //Very similar to DCC-EX class MotorDriver, but no dual signal support. 
  public:
    uint8_t powerstate; //0 = off, 1 = overload, 2 = on_forward, 3 =on_reversed. 
    uint8_t powermode; //0 = none, 1 = DCC_external, 2 = DCC_override, 3 = DC.
    uint16_t adc_previous_ticks; //value read on prior scan
    uint16_t adc_current_ticks; //value read on most recent scan
    void SetupHW(uint8_t en_out_pin, uint8_t en_in_pin, uint8_t rev_pin, uint8_t brk_pin, uint8_t adcpin, uint16_t adcscale, uint16_t adc_ol_trip); 
    void ModeChange (uint8_t newmode);
    void StateChange(uint8_t newstate);
    void adc_read();
    uint16_t adc_scale; //ADC ticks per amp. This can be higher than the adc max value if the hardware is <1A max. 
    uint16_t adc_overload_trip; //Pre-calculate trip threshold in adc ticks
    uint8_t overload_state; //holds previous state on OL, or 0. 
    uint16_t overload_cooldown; //Holds ticks remaining before retry
    uint16_t adc_base_ticks; //value read from ADC when output is off for calc reference.
    gpio_num_t enable_out_pin;
    gpio_num_t enable_in_pin; //Not used in Dynamo, will be used in ArchBridge. 
    gpio_num_t reverse_pin;
    gpio_num_t brake_pin;
    gpio_num_t adc_pin;
};


void ESP_serial_init();
void ESP32_Tracks_Setup();

//void ESP_i2c_init();

void ESP_rmt_rx_init(); //Initialize RMT RX
void ESP_rmt_tx_init(); //Initialize RMT TX

//Move these to DCC.h? They might be needed for RMT
void setDCCBit1(rmt_item32_t* item);
void setDCCBit0(rmt_item32_t* item);
void setEOT(rmt_item32_t* item);
