#ifndef ESP32_TRACKS_HW_H
  #include "ESP32_Tracks_HW.h"
#endif

//UART globals
ESP_Uart tty; //normal serial port
#ifdef BOARD_TYPE_ARCH_BRIDGE //If this is an arch bridge, define a loconet uart
ESP_Uart LN_port; //Loconet object
#endif

//TrackChannel(enable_out_pin, enable_in_pin, uint8_t reverse_pin, brake_pin, adc_pin, adcscale, adc_overload_trip)
TrackChannel DCCSigs[4]; //Define track channel objects with empty values.
uint8_t max_tracks = 1;

void ESP_serial_init(){
//  TTY_CONFIG //Perform tty setup

#ifdef BOARD_TYPE_ARCH_BRIDGE    
  LN_CONFIG //Perform Loconet uart setup
#endif 
  return;
}


void ESP32_Tracks_Setup(){ //Populates track class with values including ADC
  adc1_config_width(ADC_WIDTH_BIT_12);
  TRACK_1 //each track definition subsitites the complete function call to DCCSigs::SetupHW
  #ifdef TRACK_2
    TRACK_2
    max_tracks = 2;
  #endif 
  #ifdef TRACK_3
    TRACK_3
    max_tracks = 3;
  #endif
  #ifdef TRACK_4
    TRACK_4
    max_tracks = 4;
  #endif
  gpio_reset_pin(gpio_num_t(MASTER_EN)); //Is used on both boards
  
  #ifdef BOARD_TYPE_DYNAMO //If this is a Dynamo type booster, define these control pins.
    gpio_set_direction(gpio_num_t(MASTER_EN), GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio_num_t(MASTER_EN), GPIO_FLOATING);  

    gpio_reset_pin(gpio_num_t(DIR_MONITOR));
    gpio_set_direction(gpio_num_t(DIR_MONITOR), GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio_num_t(DIR_MONITOR), GPIO_FLOATING);  
    ESP_rmt_rx_init(); //Initialize DIR_MONITOR for RMT monitoring

    gpio_reset_pin(gpio_num_t(DIR_OVERRIDE));
    gpio_set_direction(gpio_num_t(DIR_OVERRIDE), GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(gpio_num_t(DIR_OVERRIDE), GPIO_PULLDOWN_ONLY); 
#endif
#ifdef BOARD_TYPE_ARCH_BRIDGE //If this is an arch bridge, define these control pins.
    gpio_set_direction(gpio_num_t(MASTER_EN), GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(gpio_num_t(MASTER_EN), GPIO_PULLUP_PULLDOWN);    
    gpio_set_level(gpio_num_t(MASTER_EN), 1); //Turn OE on   
#endif

  return;
}

void TrackChannel::SetupHW(uint8_t en_out_pin, uint8_t en_in_pin, uint8_t rev_pin, uint8_t brk_pin, uint8_t adcpin, uint16_t adcscale, uint16_t adc_ol_trip) { 
    powerstate = 0; //Power is off by default
    powermode = 0; //Mode is not configured by default  
    //Copy config values to class values
    enable_out_pin = gpio_num_t(en_out_pin);
    enable_in_pin = gpio_num_t(en_in_pin);
    reverse_pin = gpio_num_t(rev_pin);
    brake_pin = gpio_num_t(brk_pin);
    adc_pin = gpio_num_t(adcpin);
    adc_scale = adcscale;
    adc_overload_trip = adc_ol_trip;
    //Configure GPIOs
    //gpio_reset_pin(gpio_num_t(adc_pin)); //Not sure if this is a good idea. It could disconnect the ADC by mistake
    gpio_reset_pin(gpio_num_t(enable_out_pin)); //Always configure enable_out_pin
    gpio_set_direction(gpio_num_t(enable_out_pin), GPIO_MODE_OUTPUT_OD); //Open Drain output, level shifters have integral pull-up
    if (enable_in_pin != 0) { //Only configure enable_in_pin if it is nonzero
      gpio_reset_pin(gpio_num_t(enable_in_pin));
      gpio_set_direction(gpio_num_t(enable_in_pin), GPIO_MODE_INPUT);
    }
    ModeChange(0); //set power mode none
    //configure ADC
//    adc1_config_channel_atten(pinToADC1Channel(adc_pin),ADC_ATTEN_DB_11); //ADC range 0-3.1v
    adc_current_ticks = adc_previous_ticks = adc_base_ticks = 0; //Set all 3 ADC values to 0 initially
    adc_read(); //actually read the ADC
    adc_base_ticks = adc_current_ticks; //Copy the zero output ticks to adc_base_ticks
    return;
}

void TrackChannel::ModeChange (uint8_t newmode){ //Updates GPIO modes when changing powermode
  powermode = newmode; //powermode 0 = none, 1 = DCC_external, 2 = DCC_override, 3 = DC.  
  if (powermode = 3) { //Remove when DC mode is added. This prevents mode 3 from being selected. 
    powermode = 0; 
  }
  switch (powermode) { 
    case 0: //Not Configured, reset the pins
      gpio_set_level(gpio_num_t(enable_out_pin), 0); //force track off
      StateChange(0); //set state to off since it isn't configured anyway. 
      gpio_reset_pin(gpio_num_t(reverse_pin));
      gpio_reset_pin(gpio_num_t(brake_pin));
    break;
    case 1: //DCC external. Configure enable_in if used and rev/brake
      gpio_reset_pin(gpio_num_t(reverse_pin));
      gpio_set_direction(gpio_num_t(reverse_pin), GPIO_MODE_INPUT);  //Consider GPIO_MODE_INPUT_OUTPUT_OD to allow IO without mode change
      gpio_reset_pin(gpio_num_t(brake_pin));
      gpio_set_direction(gpio_num_t(brake_pin), GPIO_MODE_INPUT);
    break;   
    case 2: //DCC internal aka rev override
      gpio_reset_pin(gpio_num_t(reverse_pin));
      gpio_set_direction(gpio_num_t(reverse_pin), GPIO_MODE_OUTPUT); //Open Drain output, level shifters have integral pull-up. 
      gpio_reset_pin(gpio_num_t(brake_pin));
      gpio_set_direction(gpio_num_t(brake_pin), GPIO_MODE_OUTPUT); //Open Drain output, level shifters have integral pull-up
    break;
    case 3: //DC Mode:
      //todo: DC mode IO changes  
    break;
  }
  return;
}

void TrackChannel::StateChange(uint8_t newstate){
  int enable_in = 1; //default value for enable in
  if (newstate >= 2) { //On forward or on reverse
    if (enable_in_pin != 0) {
      enable_in = gpio_get_level(enable_in_pin);
    }
    gpio_set_level(enable_out_pin, enable_in); //enable_out = enable in or 1
    if (newstate = 2){
      gpio_set_level(reverse_pin, 0); //Only set reverse in DCC INT or DC mode      
    }
    if (newstate = 3){
      gpio_set_level(reverse_pin, 1); //Only set reverse in DCC INT or DC mode
    }
  } else { //Overloaded or off
    if (newstate = 1) { //Overloaded. Copy previous state so that it can be changed back after cooldown
       overload_state = powerstate; //save previous state 
       overload_cooldown = 4310; //4310 ticks of 58usec = about 250ms to let the chip cool     
    } else { //Track off. Clear overload cache/timer since we don't need it now. 
      overload_state = 0;
      overload_cooldown = 0; 
    }
    gpio_set_level(gpio_num_t(enable_out_pin), 0); //enable out = off    
  }
  powerstate = newstate; //update saved power state  
  return;
}

void TrackChannel::adc_read() { //Needs the actual ADC read implemented still
  adc_previous_ticks = adc_current_ticks; //update value read on prior scan
  adc_current_ticks = adc_base_ticks + 0; //Replace 0 with an actual read from the ADC
  if (adc_current_ticks > adc_overload_trip) {
    StateChange(1); //Set power state overload   
  }
  return;
}

void ESP_rmt_rx_init(){
  //Initialize RMT for DCC auditing
  rmt_config_t rmt_rx_config;
  // Configure the RMT channel for RX
//  bzero(&config, sizeof(rmt_config_t));
  rmt_rx_config.rmt_mode = RMT_MODE_RX;
  rmt_rx_config.channel = rmt_channel_t(DIR_MONITOR_RMT);
  rmt_rx_config.clk_div = RMT_CLOCK_DIVIDER;
  rmt_rx_config.gpio_num = gpio_num_t(DIR_MONITOR);
/* NMRA allows up to 32 bytes per packet, the max length would be 301 bits transmitted and need 38 bytes (3 bits extra). 
 * DCC_EX is limited to only 11 bytes max. Much easier to account for and uses a smaller buffer. 
  */
  rmt_rx_config.mem_block_num = 3; 
  ESP_ERROR_CHECK(rmt_config(&rmt_rx_config));
  // NOTE: ESP_INTR_FLAG_IRAM is *NOT* included in this bitmask
  ESP_ERROR_CHECK(rmt_driver_install(rmt_rx_config.channel, 0, ESP_INTR_FLAG_LOWMED|ESP_INTR_FLAG_SHARED));   
  return;
}

void rmt_tx_init(){
  //Initialize RMT for DCC TX 
  #ifdef BOARD_TYPE_DYNAMO //for now only do this on Dynamo
  rmt_config_t rmt_tx_config;
  // Configure the RMT channel for TX
  rmt_tx_config.rmt_mode = RMT_MODE_TX;
  rmt_tx_config.channel = rmt_channel_t(DIR_OVERRIDE_RMT);
  rmt_tx_config.clk_div = RMT_CLOCK_DIVIDER;
  rmt_tx_config.gpio_num = gpio_num_t(DIR_OVERRIDE);
  rmt_tx_config.mem_block_num = 2; // With longest DCC packet 11 inc checksum (future expansion)
                            // number of bits needed is 22preamble + start +
                            // 11*9 + extrazero + EOT = 124
                            // 2 mem block of 64 RMT items should be enough
  ESP_ERROR_CHECK(rmt_config(&rmt_tx_config));
  // NOTE: ESP_INTR_FLAG_IRAM is *NOT* included in this bitmask
  ESP_ERROR_CHECK(rmt_driver_install(rmt_tx_config.channel, 0, ESP_INTR_FLAG_LOWMED|ESP_INTR_FLAG_SHARED));    
  #endif
  return;
}

//From DCC-EX ESP32 branch DCCRMT.cpp. 
void setDCCBit1(rmt_item32_t* item) {
  item->level0    = 1;
  item->duration0 = DCC_1_HALFPERIOD;
  item->level1    = 0;
  item->duration1 = DCC_1_HALFPERIOD;
}

void setDCCBit0(rmt_item32_t* item) {
  item->level0    = 1;
  item->duration0 = DCC_0_HALFPERIOD;
  item->level1    = 0;
  item->duration1 = DCC_0_HALFPERIOD;
}

void setEOT(rmt_item32_t* item) {
  item->val = 0;
}

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


void ESP_i2c_init(){
  i2c_config_t i2c_conf_slave;
    i2c_conf_slave.sda_io_num = gpio_num_t(I2C_SDA_PIN);
    i2c_conf_slave.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf_slave.scl_io_num = gpio_num_t(I2C_SCL_PIN);
    i2c_conf_slave.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf_slave.mode = I2C_MODE_SLAVE;
    i2c_conf_slave.slave.addr_10bit_en = 0;
    i2c_conf_slave.slave.slave_addr = I2C_SLAVE_ADDR;  // slave address of your project
    i2c_conf_slave.slave.maximum_speed = I2C_CLOCK; // expected maximum clock speed
    i2c_conf_slave.clk_flags = 0;   // optional; you can use I2C_SCLK_SRC_FLAG_* flags to choose I2C source clock here
  /*esp_err_t err = i2c_param_config(i2c_slave_number(I2C_SLAVE_PORT), &i2c_conf_slave);
  if (err != ESP_OK) {
    return err;
  }*/
return;
}
