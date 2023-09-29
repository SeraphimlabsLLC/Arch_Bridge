#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif

//UART globals
ESP_Uart tty; //normal serial port
#ifdef BOARD_TYPE_ARCH_BRIDGE //If this is an arch bridge, define a loconet uart
ESP_Uart LN_port; //Loconet object
#endif

//TTY config
void ESP_Uart::uart_init(uint8_t uartnum, uint8_t uartmode, uint8_t txpin, uint8_t rxpin, uint16_t baudrate, uint16_t txbuff, uint16_t rxbuff, uint16_t readlen){ 

  uart_num = uartnum; //store for later
  uart_mode = uartmode; 
  tx_pin = gpio_num_t(txpin);
  rx_pin = gpio_num_t(rxpin);
  tx_buff = txbuff;
  rx_buff = rxbuff;
  max_read = readlen;
  const uart_port_t uart_num = uartnum;
  uart_config_t uart_config = {
    .baud_rate = baudrate,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    //.rx_flow_ctrl_thresh = 122, //not used with flowctrl_disable
    //.source_clk = UART_SCLK_DEFAULT,  
  };
    ESP_ERROR_CHECK(uart_driver_install(uartnum, txbuff, rxbuff, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(uartnum, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uartnum, txpin, rxpin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
return;
}

void ESP_Uart::uart_write(char write_str[]) {
  //uint16_t write_len = 0;
  //uart_write_bytes(uart_num, (const char*)write_str[], write_len);
  return;
}

void ESP_Uart::uart_read(uint16_t readlen) {
/*
  ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_num, (size_t*)&read_len));
  if (read_len > TTY_READ_LEN) {
    read_len = TTY_READ_LEN); 
  }
  read_len = uart_read_bytes(uart_num, tty_read_data, read_len, 100);
  */
  return;
}
