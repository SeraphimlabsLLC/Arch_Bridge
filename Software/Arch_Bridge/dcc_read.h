#pragma once
#define DCC_READ_H

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

#define DCC_RX_Q 128 //max 128 due to the use of int8_t
#define DCC_TX_Q 0
#define DCC_RX_SRC MCPWM //Can use MCPWM capture or RMT capture. 

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

class dccrx {
  public:
  uint64_t last_bit_time; 
  uint64_t last_rx_time; //Holder for the timestamp of the last valid bit. 
  uint8_t signalstate; //0 = no signal, 1 = signal
  
  uint8_t rx_bit_processor(bool input); //Process bits into packets, meant for RMT input but can use others such as pin reads or timers
  void rx_queue(); //Processes stored packets.
  void rx_decode(uint8_t rx_pkt); //Decode packet contents
  void dccrx_init();
  void loop_process(); //Main processing loop

  private:

  uint8_t rx_byteout; //Unfinished RX byte
  uint8_t rx_num_bits; //Number of bits in the unfinished RX byte
  uint8_t rx_num_bytes; //Number of bytes in the unfinished packet. 
  uint8_t consecutive_ones; //Number of consecutive 1 bits, used for preamble detection.
  int8_t rx_pending; //Pending packet index 
  uint8_t rx_next_new; //Next empty packet handle
  uint8_t rx_next_decode; //Next packet in decoder queue
  
  uint64_t last_rx; //Time of last read
  uint64_t last_preamble; //Time of last preamble detect
 
  DCC_packet* rx_packets[DCC_RX_Q]; //Array of pointers to DCC packet data
  uint8_t rx_packet_getempty(); //Get the next available rx_packet handle, creating one if necessary.
  
};

void dccrx_init();
void dccrx_loop();
