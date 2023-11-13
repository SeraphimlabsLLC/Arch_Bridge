#pragma once
#define ESP32_DCCEX_H

#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif
#include "Arduino.h"
/*

//DCC-EX UART settings, from ESP32_uart.h: (uint8_t uartnum, uint8_t uartmode, uint8_t txpin, uint8_t rxpin, uint32_t baudrate, uint16_t txbuff, uint16_t rxbuff);
#define DCCEX_UART dccex.dccex_port.uart_init(2, 1, 42, 41, 115200, 255, 255); //41 and 42 are right next to the uart0 pins for easy routing

#define RELAY_FROM_DCC false //Commands from DCC get sent to DCC-EX. Do not enable if DCC-EX is the DCC source

#define RELAY_FROM_LOCONET true //Commands to DCC-EX get sent from Loconet
#define RELAY_TO_LOCONET true //Commands from DCC-EX get sent to Loconet

class DCCEX_Packet{ //DCCEX Packet class
  char* cmdstr[80];   
};

class DCCEX_Class {
  public:
  ESP_Uart dccex_port; //Class inside a class
  uint8_t uart_rx(); //Receive data from uart to rx ring
  void rx_scan(); //Scan rx ring for a valid packet
  void rx_decode(uint8_t packet_size); //Process the opcode that was found

  void DCCEX_Class::loop_process(); //Scanning loop to include in loop();
 
  void tx_send(); //Encode data for sending
  uint8_t tx_loopback(uint8_t packet_size); //RX ring found an opcode we just sent, check if it is ours
};

void ESP_dccex_init(); */
