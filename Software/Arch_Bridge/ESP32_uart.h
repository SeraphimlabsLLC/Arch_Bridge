#pragma once
#define ESP32_UART_H

#include "Arduino.h"
#include "driver/gpio.h"
#include "driver/uart.h"

//Use config.h if present, otherwise defaults
#ifndef CONFIG_H
  #if __has_include ("config.h")
    #include "config.h"
  #endif
  #ifndef CONFIG_H
    #include "config.example.h"
  #endif
#endif  
const uint8_t config_uart = CONFIG;

class ESP_Uart {
  public:
  char* rx_read_data; //Pointer to the data just read
  uint16_t rx_read_len; //Size of rx_read_data
  uint8_t rx_read_processed; //read status, 0 when new, 255 when fully processed.
  char* tx_write_data; //Pointer for data to be written
  uint16_t tx_write_len; //Size of tx_write_data
  uint8_t tx_write_processed; //write status, 0 when new, 255 when fully processed.

  void uart_init(uint8_t uartnum, uint8_t txpin, uint8_t rxpin, uint32_t baudrate, uint16_t txbuff, uint16_t rxbuff);
  uint16_t uart_read(uint8_t readlen); //read the specified number of bytes into rx_read_data
  uint16_t read_len(); //returns how much data there is to be read 
  void uart_write(const char* write_data, uint8_t write_len); //Write to uart up to write_len bytes from write_data
  void uart_rx_flush(); //Erase the RX buffer contents
  void rx_flush(); //Reset the rx buffer
  void tx_flush(); //Reset the tx buffer

  private: 
  uint8_t uart_num; 
  gpio_num_t tx_pin;
  gpio_num_t rx_pin;
  uint32_t baud_rate;
  uint16_t tx_buff; //tx buffer size
  uint16_t rx_buff; //rx buffer size
};

void ESP_uart_init();
