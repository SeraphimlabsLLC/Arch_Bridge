#pragma once
#ifndef ESP32_LOCONET_H
  #define ESP32_LOCONET_H
#endif

#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif
#ifndef ESP32_DCCEX_H
  #include "ESP32_dccex.h"
#endif
#include "Arduino.h"
#include "esp_timer.h" //Required for timer functions to work.


//Loconet UART settings, from ESP32_uart.h: (uint8_t uartnum, uint8_t uartmode, uint8_t txpin, uint8_t rxpin, uint32_t baudrate, uint16_t txbuff, uint16_t rxbuff);
#define LN_UART Loconet.LN_port.uart_init(1, 2, 17, 18, 16666, 255, 255);
#define LN_PRIORITY MASTER //MASTER has priority delay 0. SENSOR has priority delay of 6 down to 2. THROTTLE has priority delay of 20 down to 6. 

class LN_Class {
  public:
  ESP_Uart LN_port; //Class inside a class
  uint8_t tx_priority; //Default to LN_Priority, decrement until successful
  uint8_t tx_failure; //Up to 25 failed transmissions before action
  char rx_opcode; //Last opcode received
  uint64_t rx_last_us; //time in startup us of last byte received  
  uint64_t rx_opcode_last_us; //time of last opcode detected
  uint64_t last_rx_process; //time of last read scan

  char tx_opcode; //Last opcode transmitted
  uint64_t tx_last_us; //timestamp of last tx attempt
  uint8_t tx_opcode_ptr; //Track where an opcode was sent
  uint8_t tx_pkt_len; //length of last tx packet
  uint8_t rx_opcode_ptr; //Track where an opcode was seen in loopback
  //char packet_data[127]; //Max packet length 127. Opcode will always be [0], checksum will always be last byte.

  uint8_t slot_ptr[127]; //Stores pointers for accessing slot data.

  void loop_process(); //Process time based data
  
  uint8_t uart_rx(); //Receive data from uart to rx ring
  void rx_scan(); //Scan rx ring for a valid packet
  void rx_decode(uint8_t packet_size); //Process the opcode that was found
 
  void tx_send(); //Encode data for sending
  uint8_t tx_loopback(uint8_t packet_size); //RX ring found an opcode we just sent, check if it is ours
  void transmit_break();
  bool receive_break(uint8_t break_ptr);
};

class LN_Slot_data{ //Each slot will be created and have a pointer stored in LN_Class slotnum
  public: 
  char stat; //Slot status. Bitmask for details. 
  char adr; //Locomotive low addr byte
  char spd; //Speed. 0x00 stop normally, 0x01 stop emergency, 0x02-0x7f speed range
  char dirf; //direction, direction lights, and F1-F4
  char trk; //System/Track status. This byte only exists for packet formatting, pull this data from master_enable.
  char ss2; //slot status 2
  char adr2; //Locomotive high addr byte
  char snd; //sound, F5-8
  char id1; //unused
  char id2; //unused
};

void ESP_LN_init();
