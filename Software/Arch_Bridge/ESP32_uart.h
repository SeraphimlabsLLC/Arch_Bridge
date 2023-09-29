#pragma once
#define ESP32_UART_H

#include "driver/gpio.h"
#include "driver/uart.h"

//TTY settings:
#define TTY_CONFIG LN_port.uart_init( 0, 0, 43, 44, 115200, 255, 16384, 255);


class ESP_Uart {
  public:
  uint8_t uart_num; 
  gpio_num_t tx_pin;
  gpio_num_t rx_pin;
  uint16_t baud_rate;
  uint8_t uart_mode; //0 = tty, 1 = dccex, 2 = loconet
  void uart_init(uint8_t uartnum, uint8_t uartmode, uint8_t txpin, uint8_t rxpin, uint16_t baudrate, uint16_t txbuff, uint16_t rxbuff, uint16_t readlen);
  void uart_write(char* write_str);
  void uart_read(uint16_t readlen);
  uint16_t read_len();
  private: 
  uint16_t tx_buff;
  uint16_t rx_buff;
  uint16_t max_read;
};
