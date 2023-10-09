#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif

//UART globals
ESP_Uart tty; //normal serial port
#ifdef LN_CONFIG //if a Loconet config exists, activate it
//
ESP_Uart LN_port; //Loconet object
#endif

void ESP_uart_init(){ //Set up uarts
 TTY_CONFIG 
 #ifdef LN_CONFIG //if a Loconet config exists, activate it
   LN_CONFIG
 #endif
 return;
}

//TTY config
void ESP_Uart::uart_init(uint8_t uartnum, uint8_t uartmode, uint8_t txpin, uint8_t rxpin, uint32_t baudrate, uint16_t txbuff, uint16_t rxbuff){ 

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
  if (uartnum == 0) { //Use Arduino serial library for now. This will eventually need to be replaced. 
    Serial.begin (115200);
    Serial.printf("UART Initialized, %d baudrate \n", baudrate);
    return;
  } else { //Is not uart0
  if (uart_mode == 2) { //Loconet transmitter needs to be 1 unless transmitting a 0
    Serial.printf("Configuring Loconet on UART %d ", uartnum);
    Serial.printf("baudrate %d \n", baudrate);
    gpio_reset_pin(tx_pin); //Is used on both boards
    gpio_set_direction(tx_pin, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(tx_pin, GPIO_PULLUP_PULLDOWN);    
    gpio_set_level(tx_pin, 1);//For now fix tx_pin to 1.
  }
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
    ESP_ERROR_CHECK(uart_flush(uartnum));
    //ESP_ERROR_CHECK(uart_set_tx_empty_threshold(0)) //Enables interrupt on TX empty. Can use this to turn on gpio=1
    //ESP_ERROR_CHECK(uart_set_line_inverse(uart_nr, UART_SIGNAL_TXD_INV | UART_SIGNAL_RXD_INV)); //Allows TXD or RXD to be inverted as needed.  
  }
  Serial.printf("UART Initialized, %d baudrate \n", baudrate);
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
    uint16_t readlen = 0;
    ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_num, (size_t*)&readlen));
    return readlen;
  }

uint16_t ESP_Uart::uart_read(uint8_t readlen) {//read the specified number of bytes into rx_data and add to rx_data_len
  rx_data_len = 0;
  rx_data_len = uart_read_bytes(uart_num, rx_data, readlen, 100);
  return rx_data_len;
  
}
