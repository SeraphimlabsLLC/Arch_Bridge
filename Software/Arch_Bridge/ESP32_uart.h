#pragma once
#define ESP32_UART_H

#include "Arduino.h"
#include "driver/gpio.h"
#include "driver/uart.h"

//TTY settings:
#define TTY_CONFIG tty.uart_init( 0, 0, 43, 44, 115200, 255, 255);

class ESP_Uart {
  public:
  uint8_t uart_mode; //Parser selection. 0 = no parser, 1 = dccex, 2 = loconet
  char rx_data[256]; //Ring buffer for data read from RX.
  char tx_data[256]; //Ring buffer for data to be moved to TX.
  uint8_t rx_write_ptr;
  uint8_t rx_read_ptr; 
  uint8_t tx_write_ptr; 
  uint8_t tx_read_ptr;

  void uart_init(uint8_t uartnum, uint8_t uartmode, uint8_t txpin, uint8_t rxpin, uint32_t baudrate, uint16_t txbuff, uint16_t rxbuff);
  void uart_write(uint8_t writelen); //write the specified number of bytes from tx_data and subtract from tx_data_len
  uint16_t uart_read(uint8_t readlen); //read the specified number of bytes into rx_data and add to rx_data_len
  uint16_t read_len(); //returns how much data there is to be read 
  void uart_rx_flush(); //Erase the RX buffer contents
  
  void rx_shift(uint8_t start, int8_t offset); //Move rx data at specification location ahead or behind by offset
  void rx_flush(); //Reset the rx ring buffer to 0,0
  void tx_shift(uint8_t start, int8_t offset); //Move tx data at specification location ahead or behind by offset
  void tx_flush(); //Reset the tx ring buffer to 0,0
 
  uint8_t uart_num; 
  gpio_num_t tx_pin;
  gpio_num_t rx_pin;
  uint32_t baud_rate;
  uint16_t tx_buff; //tx buffer size
  uint16_t rx_buff; //rx buffer size

};

void ESP_uart_init();
