//Contains some code snippets from DCC-EX ESP32 branch
#pragma once
#define ESP32_RMTDCC_HW_H

#include "Arduino.h"
#include "driver/gpio.h"

#include "driver/rmt.h"
#include "soc/rmt_reg.h"
#include "soc/rmt_struct.h"
#include "soc/rmt_periph.h"
#include "freertos/ringbuf.h" 
#include "esp32-hal.h" //Aduino RMT

/******************
 * Continuous RMT RX may not be posssible in IDF 4.4 without modifying the driver itself
 * See https://github.com/espressif/esp-idf/issues/11478
 * IDF 5.1 will restructure things, revisit this then. 
 */



//Use config.h if present, otherwise defaults
#ifndef CONFIG_H
  #if __has_include ("config.h")
    #include "config.h"
  #endif
  #ifndef CONFIG_H
    #include "config.example.h"
  #endif
#endif  

#if BOARD_TYPE == DYNAMO
  #define DIR_MONITOR 38 //RMT Input pin, 9 on Arch_Bridge 38 on Dynamo
  #define DIR_OVERRIDE 21 //GPIO21, use for RMT Output
#endif

#if BOARD_TYPE == ARCH_BRIDGE
  #define DIR_MONITOR 9 //RMT Input pin, 9 on Arch_Bridge 38 on Dynamo
#endif

#ifndef BOARD_TYPE
  #error "Invalid board type, must choose DYNAMO or ARCH_BRIDGE"
#endif

#define DCC_RX_Q 16
#define DCC_TX_Q 8
  //On ESP32-S3, RMT channels 0-3 are TX only and 4-7 are RX only
#define DIR_MONITOR_RMT 4 //Will soon be hard coded. 
#define DIR_OVERRIDE_RMT 0


void rmt_loop(); //Reflect into Rmtdcc:process_loop();


class Rmtdcc {
  public:
  uint64_t last_rmt_read; 
  uint64_t last_rx_time; //Holder for the timestamp of the last valid packet. 
  uint8_t signalstate; //0 = no signal, 1 = signal o
  
  void rmt_rx_init(); 
  uint8_t rmt_rx(); //Read RMT RX data and convert into bit stream to feed to rx_scan
  uint8_t rx_bit_processor(bool input); //Process bits into packets, meant for RMT input but can use others such as pin reads or timers
  void rx_queue(); //Processes stored packets.
  void rx_decode(uint8_t rx_pkt); //Decode packet contents
  void loop_process(); //Main processing loop
#if DCC_GENERATE == true 
//  rmt_item32_t idle_packet[33]; //Generate and store an idle packet for fast access
  void rmt_tx_init(); 
  void tx_send(); 
#endif
  private:

 //Using ESP-IDF v 4.46 

//  rmt_channel_handle_t rx_ch; //RMT connection object
  RingbufHandle_t rmt_rx_handle; //RMT ring buffer handle 
  rmt_item32_t* rx_rmt_data; //RMT data object
  rmt_item32_t rx_last_bit; //Holds the previously decoded symbol 
  size_t* rx_data_size; //RMT data object size
  
  uint8_t rx_byteout; //Unfinished RX byte
  uint8_t rx_num_bits; //Number of bits in the unfinished RX byte
  uint8_t rx_num_bytes; //Number of bytes in the unfinished packet. 
  uint8_t consecutive_ones; //Number of consecutive 1 bits, used for preamble detection.
  int8_t rx_pending; //Pending packet index 
  uint8_t rx_next_new; //Next empty packet handle
  uint8_t rx_next_decode; //Next packet in decoder queue
  
  uint64_t rmt_rx_detect; //Time of last read
  uint64_t last_preamble; //Time of last preamble detect
 
//  DCC_packet* rx_packets[DCC_RX_Q]; //Array of pointers to DCC packet data
  uint8_t rx_packet_getempty(); //Get the next available rx_packet handle, creating one if necessary.
/*
  void setDCCBit1(rmt_item32_t* item);
  void setDCCBit0(rmt_item32_t* item);
  void setEOT(rmt_item32_t* item);*/
#if DCC_GENERATE == true 
//  DCC_packet* tx_data[DCC_TX_Q]; //Array of pointers to DCC packet data

#endif
}; 
