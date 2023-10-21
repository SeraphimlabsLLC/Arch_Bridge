#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif

//UART globals
ESP_Uart tty; //normal serial port

void ESP_uart_init(){ //Set up uarts
 TTY_CONFIG 
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
  tx_write_ptr = tx_read_ptr = 0; //Initialize tx ring buffer pointers
  rx_write_ptr = rx_read_ptr = 0; //Inialize rx ring buffer pointers
  const uart_port_t uart_num = uartnum; 
  if (uart_num == 0) { //Use Arduino serial library for now. This will eventually need to be replaced. 
    Serial.begin (115200);
    Serial.printf("Configuring Arduino Serial on UART %d, baud rate %d \n", uart_num, baudrate);
    return;
  } else { //Is not uart0 
    if (uart_mode == 1) { //DCC-EX transmitter
    Serial.printf("Configuring DCC-EX on UART %d, baud rate %d \n", uart_num, baudrate);
  }
  if (uart_mode == 2) { //Loconet transmitter
    Serial.printf("Configuring Loconet on UART %d, baud rate %d \n", uart_num, baudrate);
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
    ESP_ERROR_CHECK(uart_driver_install(uart_num, tx_buff, rx_buff, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_flush(uart_num));
    //ESP_ERROR_CHECK(uart_set_tx_empty_threshold(0)) //Enables interrupt on TX empty. Can use this to turn on gpio=1
  }
return;
}

void ESP_Uart::uart_write(uint8_t writelen) {//Write the data in the TX ring to the port. 
  if (uart_num == 0) {
    return;
  }
  if (writelen == 0) { //Enforce minimum length  of 1 to avoid crashing
    writelen = 1;
  }
  uint8_t bytes_written = 0;
  char write_data[writelen];//Holder for cutting out the bytes to be written
  Serial.printf("Transmitting: ");
  while ((tx_read_ptr != tx_write_ptr) || (bytes_written == writelen)) { //Because the buffer will wrap, so keep writing unil the pointers are in the same spot
    write_data[bytes_written] = tx_data[tx_read_ptr];
    Serial.printf(" %x ", write_data[bytes_written]);
    tx_read_ptr++;
    bytes_written++;
  }
  Serial.printf("\n");
  uart_write_bytes(uart_num, write_data, bytes_written); //Somehow we ended up being able to write multiple bytes at once after all. 
  return;
}

uint16_t ESP_Uart::read_len(){ //returns how much data there is to be read
  if (uart_num == 0) {
    return 0;
  }    
    uint8_t ready_len = 0;
    ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_num, (size_t*)&ready_len));
    //Serial.printf("Readlen was %d, ready_len was %d \n", readlen, ready_len);
    return ready_len;
  }

uint16_t ESP_Uart::uart_read(uint8_t readlen) {//read the specified number of bytes into rx_data
  if (uart_num == 0) {
    return 0;
  }
  uint16_t bytes_read = 0;
  uint8_t ready_len = 0;
  char rx_read[80];
  ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_num, (size_t*)&ready_len));
  //Serial.printf("Readlen was %d, ready_len was %d \n", readlen, ready_len);
  if (readlen > 80) { //Limit max read size to 80, same as a vt100 terminal
    readlen = 80;
  }
  if (!(ready_len > 0)){ //No data to read at this time. 
    return 0;
  } 
  if (readlen == 0) {//readlen wasn't specified, take what the buffer has.
    readlen = ready_len;
  }
  if (readlen > ready_len){ //Asked to read more bytes than we have. Best to wait for more data.
    return 0;
  }
  bytes_read = uart_read_bytes(uart_num, &rx_read, readlen, 100); //Read up to rx_read bytes from uart_num with a 100 rt tick timeout
  if (bytes_read != readlen){
    //Serial.printf("Read only %d bytes when told to read %d bytes \n", bytes_read, readlen);
    return bytes_read;
  }
  uint8_t i = 0;
  Serial.printf("Receiving: ");
  while (i < bytes_read){ 
    rx_data[rx_write_ptr] = rx_read[i];
    Serial.printf(" %x ", rx_read[i]);
    i++;
    rx_write_ptr++;
  }
  Serial.printf("\n");
  return bytes_read;
}

void ESP_Uart::uart_rx_flush() {//Erase all data in the buffer
  if (uart_num == 0) {
    return;
  }
  ESP_ERROR_CHECK(uart_flush(uart_num));
  return;
}

void ESP_Uart::rx_shift(uint8_t start, int8_t offset){ //Move rx data at specification location ahead or behind by offset
  
  return; 
}
void ESP_Uart::rx_flush(){ //Reset the rx ring buffer to 0,0
  rx_read_ptr = rx_write_ptr = 0;
  return;
}

void ESP_Uart::tx_shift(uint8_t start, int8_t offset) { //Move tx data at specification location ahead or behind by offset

  return;
}
void ESP_Uart::tx_flush() { //Reset the tx ring buffer to 0,0
  tx_read_ptr = tx_write_ptr = 0;
  return;
}
