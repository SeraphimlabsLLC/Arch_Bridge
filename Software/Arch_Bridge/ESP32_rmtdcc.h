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

class DCC_packet {
  public:
  uint8_t state; //empty = 0, pending = 1, receiving = 2, complete = 3, failed = 4, success = 5} 
  char packet_data[48]; //Should only need 38 bytes + a few odd bits. 
  uint8_t data_len; //Length of packet
  uint64_t packet_time;

  void Make_Checksum(); //Populate last byte with valid checksum
  bool Read_Checksum(); //Verify checksum, returns true if valid, false if invalid.
  uint8_t packet_size_check(); //Check that a packet has a valid size. 
  void reset_packet(); //Reset packet slot to defaults
};

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
 
  DCC_packet* rx_packets[DCC_RX_Q]; //Array of pointers to DCC packet data
  uint8_t rx_packet_getempty(); //Get the next available rx_packet handle, creating one if necessary.
/*
  void setDCCBit1(rmt_item32_t* item);
  void setDCCBit0(rmt_item32_t* item);
  void setEOT(rmt_item32_t* item);*/
#if DCC_GENERATE == true 
  DCC_packet* tx_data[DCC_TX_Q]; //Array of pointers to DCC packet data

#endif
}; 

//RMT time Constants. Periods from NMRA S9.1 with some additional fudge factor
#define DCC_1_HALFPERIOD 58  //4640 // 1 / 80000000 * 4640 = 58us
#define DCC_1_MIN_HALFPERIOD 50 //NMRA S9.1 says 55uS Minimum half-1. 
#define DCC_1_MAX_HALFPERIOD 66 //NMRA S9.1 says 61uS Maximum half-1
#define DCC_0_HALFPERIOD 100 //8000
#define DCC_0_MIN_HALFPERIOD 90 //NMRA S9.1 says 95uS Minimum half-0
#define DCC_0_MAX_HALFPERIOD 12000 //NMRA S9.1 says 10000uS Maximum half-0, and 12000uS maximum full-0. 
