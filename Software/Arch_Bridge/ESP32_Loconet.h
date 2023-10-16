#pragma once
#ifndef ESP32_LOCONET_H
  #define ESP32_LOCONET_H
#endif

#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif
#include "Arduino.h"
#include "driver/rmt.h"
#include "soc/rmt_reg.h"
#include "soc/rmt_struct.h"

//Loconet UART settings, from ESP32_uart.h: (uint8_t uartnum, uint8_t uartmode, uint8_t txpin, uint8_t rxpin, uint32_t baudrate, uint16_t txbuff, uint16_t rxbuff);
#define LN_UART Loconet.LN_port.uart_init(1, 2, 17, 18, 16666, 255, 255);
//Loconet RMT settings
//#define LN_RMT Loconet.LN_port.rmt_init(17,18);

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
