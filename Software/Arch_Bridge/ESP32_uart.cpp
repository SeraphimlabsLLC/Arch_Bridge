#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif

//UART globals
ESP_Uart tty; //normal serial port
extern uint64_t time_us; //Shared time global

void ESP_uart_init(){ //Set up uarts
 #ifdef TTY_CONFIG
   TTY_CONFIG 
 #endif
 return;
}

//TTY config
void ESP_Uart::uart_init(uint8_t uartnum, uint8_t txpin, uint8_t rxpin, uint32_t baudrate, uint16_t txbuff, uint16_t rxbuff){ 
  //store for later 
  uart_num = uartnum; 
  tx_pin = gpio_num_t(txpin);
  rx_pin = gpio_num_t(rxpin);
  tx_buff = txbuff;
  rx_buff = rxbuff;
  tx_done = true; 

  //Configure RX buffer 
  if (rx_buff < 4) { //Minimum of 4 bytes. 
    rx_buff = 4; 
  }
  if (rx_read_data) {
    delete rx_read_data; //Delete any existing read buffer
  }
  rx_read_data = new char[rx_buff]; //Define rx_read_data as char[rx_buff]
  //Serial.printf("Uart %u rx_read_data %u \n", uart_num, rx_read_data);

  if (uart_num == 0) { //Use Arduino serial library for now. This will eventually need to be replaced. 
    Serial.begin (baudrate);
    Serial.printf(BOARD_ID);
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
    txbuff = 256; 
    rxbuff = 256; 
    Serial.printf("Configuring uart %u to baud %u with txbuff %u rxbuff %u \n", uartnum, baudrate, txbuff, rxbuff); 
    ESP_ERROR_CHECK(uart_driver_install(uart_port_t(uartnum), txbuff, rxbuff, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(uart_port_t(uartnum), &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_port_t(uartnum), tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_flush(uart_port_t(uartnum)));
    //REG_SET_FIELD(UART_CONF1_REG, UART_RXFIFO_FULL_THRHD, 1); //Change RX threshold to return single bytes. 


   
  }
return;
}

void ESP_Uart::uart_write(const char* write_data, uint8_t write_len){ //Write the contents of the pointer to the output buffer
  uint8_t i = 0;
  if (uart_num == 0) { //Don't write to uart0, the arduino Serial library manages that one
    return;
  }
  uart_write_bytes(uart_num, write_data, write_len);
  return;
}

uint16_t ESP_Uart::uart_tx_len(){ //Fetch how much data is currently in the TX buffer
  uint16_t tx_remain = 0;

  return tx_remain;  
}

void ESP_Uart::uart_tx_int_txdone(bool enabled){ //Fetch how much data is currently in the TX buffer
  //turn ISR on or off based on enabled;

  return;  
}

uint16_t ESP_Uart::read_len(){ //returns how much data there is to be read
  uint16_t ready_len = 0;
  if (uart_num == 0) {
      ready_len = Serial.available();
  } else {   
    ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_num, (size_t*)&ready_len));
  }
    //Serial.printf("ready_len was %d \n", ready_len);
  if (ready_len > rx_buff) { //Limit to buffer size
    ready_len = rx_buff;
  }
    return ready_len;
}

uint16_t ESP_Uart::uart_read(uint8_t readlen) {//read the specified number of bytes into rx_read_data and copy to
  uint16_t ready_len = 0;
  ready_len = read_len();
  if (readlen == 0) {//readlen wasn't specified, take what the buffer has.
    readlen = ready_len;
  }
  if ((readlen > ready_len) || (ready_len == 0)){ //Asked to read more bytes than we have or no data ready. Return 0;  
    return 0;
  }
  if (rx_read_processed != 255){ //Exiting data hasn't been processed yet. Don't read new data. 
    return 0;     
  }
  rx_read_len = readlen;

  if (!(rx_read_data)){ //Could not make pointer. 
    return 0; 
  }

  if (uart_num == 0) { //Only runs on uart0, using Arduino libraries. 
    if (readlen > 0) {
      readlen = Serial.readBytesUntil('\n', rx_read_data, readlen); //Reads until NULL or readlen chars
      rx_read_processed = 0; //This is new data to process.
      uint8_t i = 0; 
    }
    rx_read_len = readlen; 
    return readlen;
  } else {
    //Read using ESP Uart library
    //Serial.printf("Receiving bytes into rx_read_data pointer %u \n", rx_read_data);
    readlen = uart_read_bytes(uart_num, rx_read_data, readlen, 100); //Read up to rx_read bytes from uart_num tto rx_read_data with a 100 rt tick timeout
  }
  //Warn if there was a size mismatch. Would only happen if something else read data in between the size check and the actual read. 
  if (rx_read_len != readlen){
    Serial.printf("Read only %d bytes when told to read %d bytes \n", rx_read_len, readlen);
  }
  rx_read_processed = 0; //This is new data to process.
  rx_read_len = readlen; 
  return readlen;
}

//TODO: Rewrite these 4 functions to work with the new system
void ESP_Uart::uart_rx_flush() {//Erase all data in the buffer
  if (uart_num == 0) {
    return;
  }
  ESP_ERROR_CHECK(uart_flush(uart_num));
  return;
}

void ESP_Uart::rx_flush(){ //Reset the rx buffer to 0
  delete rx_read_data; //delete the previous result so we can define it again at a new length
  rx_read_len = 1;
  rx_read_data = new char[rx_buff]; 
  return;
}

void ESP_Uart::tx_flush() { //Reset the tx buffer
  return;
}
