#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif

//UART globals
ESP_Uart tty; //normal serial port
#ifdef BOARD_TYPE_ARCH_BRIDGE //If this is an arch bridge, define a loconet uart
ESP_Uart LN_port; //Loconet object
#endif

void ESP_uart_init(){ //Set up uarts
 TTY_CONFIG 
 #ifdef BOARD_TYPE_ARCH_BRIDGE //If this is an arch bridge, define a loconet uart
   LN_CONFIG
 #endif
 return;
}

//TTY config
void ESP_Uart::uart_init(uint8_t uartnum, uint8_t uartmode, uint8_t txpin, uint8_t rxpin, uint16_t baudrate, uint16_t txbuff, uint16_t rxbuff){ 

  //store for later 
  uart_num = uartnum; 
  uart_mode = uartmode; 
  tx_pin = gpio_num_t(txpin);
  rx_pin = gpio_num_t(rxpin);
  tx_buff = txbuff;
  rx_buff = rxbuff;
  tx_data_len = 0; //Initialize the length counters
  rx_data_len = 0;
  const uart_port_t uart_num = uartnum;
  if (uartnum == 0) { //Use Arduino serial library 
    //Serial.begin (baudrate);
    //Serial.write("UART Initialized \n");
    return;
  } else { //Is not uart0
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
  }

return;
}

void ESP_Uart::uart_write(uint8_t writelen) {//write the specified number of bytes from tx_data and subtract from tx_data_len
  if (tx_data_len < writelen){
    writelen = tx_data_len; //empty the buffer
  }
  uart_write_bytes(uart_num, (const char*) tx_data, writelen);
  tx_data_len = tx_data_len - writelen;
  return;
}

  uint16_t ESP_Uart::read_len(){ //returns how much data there is to be read
    uint16_t readlen;
    ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_num, (size_t*)&readlen));
    return readlen;
  }

uint16_t ESP_Uart::uart_read(uint8_t readlen) {//read the specified number of bytes into rx_data and add to rx_data_len
  rx_data_len = uart_read_bytes(uart_num, rx_data, readlen, 100);
  return rx_data_len;
  
}