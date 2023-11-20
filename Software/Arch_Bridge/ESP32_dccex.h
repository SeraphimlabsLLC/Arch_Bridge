#pragma once
#define ESP32_DCCEX_H

//Use config.h if present, otherwise defaults
#ifndef CONFIG_H
  #if __has_include ("config.h")
    #include "config.h"
  #endif
  #ifndef CONFIG_H
    #include "config.example.h"
  #endif
#endif  

#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif
#include "Arduino.h"

//Reflectors to insert into main() instead of using an extern
void dccex_init(); //DCCEX_Class::dccex_init();
void dccex_loop(); //DCCEX_Class::dccex_loop();

/*

//DCC-EX UART settings, from ESP32_uart.h: (uint8_t uartnum, uint8_t uartmode, uint8_t txpin, uint8_t rxpin, uint32_t baudrate, uint16_t txbuff, uint16_t rxbuff);
#define DCCEX_UART dccex.dccex_port.uart_init(2, 42, 41, 115200, 255, 255); //41 and 42 are right next to the uart0 pins for easy routing

#define RELAY_FROM_DCC false //Commands from DCC get sent to DCC-EX. Do not enable if DCC-EX is the DCC source

#define RELAY_FROM_LOCONET true //Commands to DCC-EX get sent from Loconet
#define RELAY_TO_LOCONET true //Commands from DCC-EX get sent to Loconet
*/

class DCCEX_Class {
  public:
  ESP_Uart dccex_port; //Class inside a class
  uint8_t rx_read_processed;
  uint64_t rx_last_us; 
  //uint8_t rx_read_len;
  uint8_t rx_state;
  uint8_t data_len; 
  char data_pkt[256]; 

  void dccex_init(); //in-class initializer
  void loop_process(); //Scanning loop to include in loop();
  uint8_t uart_rx(); //Receive data from uart to rx ring
  void rx_scan(); //Scan rx ring for a valid packet
  void rx_decode(); //Process the opcode that was found
  void tx_send(); //Encode data for sending
  uint8_t tx_loopback(uint8_t packet_size); //RX ring found an opcode we just sent, check if it is ours

  void ddiag(); //Process diagnostic commands
};
