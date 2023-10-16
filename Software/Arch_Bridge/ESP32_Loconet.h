#pragma once
#ifndef ESP32_LOCONET_H
  #define ESP32_LOCONET_H
#endif

#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif
#include "Arduino.h"

//Loconet UART settings, see ESP32_uart.h for meanings
#define LN_UART Loconet.LN_port.uart_init(1, 2, 17, 8, 16666, 255, 255);

#define LN_PRIORITY 32


class LN_Class {
  public:
  ESP_Uart LN_port; //Class inside a class
  char opcode;
  char packet_data[125]; //Max packet length 127 - (opcode + checksum)
  char checksum;
  void rx_detect ();
  void tx_encode();
  void transmit_break();
  void receive_break();
};

void ESP_LN_init();
