#ifndef ESP32_DCCEX_H
  #include "ESP32_dccex.h"
#endif 

#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif

#ifndef ESP32_TRACKS_HW_H
  #include "ESP32_Tracks_HW.h"
#endif

uint8_t config_dccex = CONFIG;

DCCEX_Class dccex;
extern uint64_t time_us; 
extern TrackChannel DCCSigs[];
extern Rmtdcc dcc; 

void DCCEX_Class::loop_process(){
  //Serial.printf("DCCEX uart_rx cycle: \n");
  uart_rx(); //Read uart data into the RX ring
  //Serial.printf("DCCEX rx_scan cycle: \n");
  rx_scan(); //Scan RX ring for an opcode
  rx_decode();
  //Serial.printf("DCCEX rx_queue cycle: \n");
  //rx_queue(); //Process queued RX packets and run rx_decode
  //Serial.printf("DCCEX tx_queue cycle: \n");
  //tx_queue(); //Try to send any queued packets
  //Serial.printf("DCCEX loop complete \n");
  return;
}

uint8_t DCCEX_Class::uart_rx(){ //Read incoming data into a buffer
  uint8_t read_size = 0;
  uint8_t i = 0;
  read_size = dccex_port.uart_read(read_size); //populate rx_read_data and rx_data
  //Serial.printf("DCCEX uart %u \n", read_size);
  if (read_size > 0){ //Data was actually moved, update the timer.
    rx_last_us = esp_timer_get_time();
  }
  return read_size;
}

void DCCEX_Class::rx_scan(){ //Scan ring buffer data for an opcode and return its location
  uint16_t i = 0; 

  while (i < dccex_port.rx_read_len) {
    Serial.printf("Char: %c \n");
    if ((dccex_port.rx_read_data[i] == '<') && (rx_state == 0)) { // 0x3e
      Serial.printf("Starting CMD: %c \n", dccex_port.rx_read_data[i]);
      rx_state = 1; //pending
      data_len = 0; 
    }
    
    if (rx_state == 1) { //Record data until finding >
      data_pkt[data_len] = dccex_port.rx_read_data[i];
      data_len++;

      if (dccex_port.rx_read_data[i] == '>') { //stop recording data. Reset for next packet.
        rx_state = 2; //RX complete, ready to process 
        Serial.printf("Stopping CMD: %c \n", dccex_port.rx_read_data[i]); 
      }
    }
    i++; 
  }
  return;    
}

void DCCEX_Class::rx_decode(){
  //An implementation of https://dcc-ex.com/throttles/tech-reference.html#
  uint8_t i = 0;
  if (rx_state != 2) { //No packet to process
    return; 
  }
  switch (data_pkt[1]) {
    case 0:  //power off
    Serial.printf("Changing to OFF \n");
      DCCSigs[0].ModeChange(0);
      DCCSigs[1].ModeChange(0);

    break;
    case '1':
    Serial.printf("Changing to DCC EXT, ON_FWD \n");
      DCCSigs[0].ModeChange(1);
      DCCSigs[1].ModeChange(1);   
      DCCSigs[0].StateChange(2);//Set to DCC_ON
      DCCSigs[1].StateChange(2);//Set to DCC_ON

    break;
    case '2':
      Serial.printf("Changing to DCC EXT, ON_FWD \n");
      DCCSigs[0].ModeChange(3);
      DCCSigs[1].ModeChange(3);   
      DCCSigs[0].StateChange(2);//Set to ON_FWD
      DCCSigs[1].StateChange(2);//Set to ON_FWD  
      break; 

    case '3': 
      Serial.printf("Changing to DCC EXT, ON_REV \n");
      DCCSigs[0].ModeChange(3);
      DCCSigs[1].ModeChange(3);   
      DCCSigs[0].StateChange(3);//Set to ON_REV
      DCCSigs[1].StateChange(3);//Set to ON_REV  
      break; 

    case 'D': 
      Serial.printf("Debug: \n"); 
      i = gpio_get_level(DCCSigs[0].enable_in_pin);
      Serial.printf("Enable in: %u \n", i);
      i = gpio_get_level(DCCSigs[0].reverse_pin);
      Serial.printf("Reverse: %u \n", i);
      i = gpio_get_level(DCCSigs[0].brake_pin);
      Serial.printf("Brake in: %u \n", i);
      i = 0;

    default:
    Serial.printf("Invalid Command \n");
    
  } 
  rx_state = 0;  
  return; 
}

void DCCEX_Class::dccex_init(){
  rx_state = 0;
  data_len = 0;
}

void dccex_init(){ //Reflector into DCCEX_Class
  dccex.dccex_init(); 
  return; 
}

void dccex_loop(){ //Reflector into DCCEX_Class
  dccex.loop_process();
  return; 
}
