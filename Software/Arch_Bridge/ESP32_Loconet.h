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

//Loconet UART settings, from ESP32_uart.h: (uint8_t uartnum, uint8_t uartmode, uint8_t txpin, uint8_t rxpin, uint32_t baudrate, uint16_t txbuff, uint16_t rxbuff);
#define LN_UART Loconet.LN_port.uart_init(1, 2, 17, 18, 16666, 255, 255);

#define LN_PRIORITY 20


class LN_Class {
  public:
  ESP_Uart LN_port; //Class inside a class
  uint8_t tx_priority; //Default to LN_Priority, decrement until successful
  uint8_t tx_failure; //Up to 15 failed transmissions before action
  char rx_opcode; //Last opcode received

  char tx_opcode; //Last opcode transmitted
  uint8_t tx_opcode_ptr; //Track where an opcode was sent
  uint8_t tx_pkt_len; //length of last tx packet
  uint8_t rx_opcode_ptr; //Track where an opcode was seen in loopback
  char packet_data[125]; //Max packet length 127 - (opcode + checksum)
  
  uint8_t uart_rx(); //Receive data from uart to rx ring
  void rx_scan(); //Scan rx ring for a valid packet
  void rx_decode(uint8_t packet_size); //Process the opcode that was found
 
  void tx_send(); //Encode data for sending
  uint8_t tx_loopback(uint8_t packet_size); //RX ring found an opcode we just sent, check if it is ours
  void transmit_break();
  void receive_break();
};

void ESP_LN_init();
