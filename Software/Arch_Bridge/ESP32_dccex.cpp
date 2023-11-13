#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif

#ifndef ESP32_DCCEX_H
  #include "ESP32_dccex.h"
#endif 
/*
DCCEX_Class dccex;

void DCCEX_Class::loop_process(){
 //Serial.printf("DCCEX uart_rx cycle: \n");
  uart_rx(); //Read uart data into the RX ring
  //Serial.printf("DCCEX rx_scan cycle: \n");
  rx_scan(); //Scan RX ring for an opcode
  //Serial.printf("DCCEX rx_queue cycle: \n");
  rx_queue(); //Process queued RX packets and run rx_decode
  //Serial.printf("DCCEX tx_queue cycle: \n");
  tx_queue(); //Try to send any queued packets
  //Serial.printf("DCCEX loop complete \n");
  return;
}

void DCCEX_Class::rx_scan(){ //Scan ring buffer data for an opcode and return its location
  uint8_t i = 0;
  uint8_t j = 0;
  uint8_t packet_size = 2; //Default packet size. It gets changed later. 
  bool checksum_ok = false;
  
  if (dccex.rx_read_processed != 0) { //Data has already been processed. 
    return; 
  }
  time_us = esp_timer_get_time();
  
  last_rx_process = time_us;
  //Serial.printf("RX_Scan: %u bytes read %u \n", LN_port.rx_read_len, i);
  while (i < dccex_port.rx_read_len) {
    //Serial.printf("RX_Scan While, rxpending %u, rxbyte %x \n", rx_pending, LN_port.rx_read_data[i]);
    if (rx_pending < 0){ 
      Serial.printf("No packet pending, i %u, byte %x \n", i, LN_port.rx_read_data[i]);
    }
    if ((LN_port.rx_read_data[i] & 0x80) && (LN_port.rx_read_data[i] != 255) && (rx_pending < 0)) { //we found an opcode 
      Serial.printf("Found opcode %x, ", LN_port.rx_read_data[i]);
      rx_pending = rx_packet_getempty(); //Find next open slot 
      rx_packet_new(rx_pending, packet_size); //Make new packet with initial size of 2 bytes
      Serial.printf("storing in rx slot %u \n", rx_pending);
      while ((dccex_port.rx_data[dccex_port.rx_read_ptr + packet_size] != '>') && (eod == false)){
          if ((dccex_port.rx_read_ptr == dccex_port.rx_write_ptr)) { //End of data without a '>'
            eod = true;
          } else {
            packet_size++;
          }
          Serial.printf ("Found < or eod /n");
      }
      if (eod == false) {//A < was found before eod, we have a good packet.
        rx_decode(packet_size);
      }
      
    }
  if ((dccex_port.rx_read_ptr == dccex_port.rx_write_ptr)) { //End of data
    eod = true;
  } else {
    dccex_port.rx_read_ptr++; //Read pointer will increment until a start char is found.
    }
  }
  return;    
}

void DCCEX_Class::rx_decode(uint8_t packet_size){
  char rx_opcode;
  dccex_port.rx_read_ptr++; //Advance the pointer by 1 to read the acutal opcode
  rx_opcode = dccex_port.rx_data[dccex_port.rx_read_ptr];

  switch (rx_opcode) { //An implementation of https://dcc-ex.com/throttles/tech-reference.html#

    case 'l': //Loco throttle commands
    Serial.printf("Throttle command detected \n");

    break;
    case 'H': //Turnout and sensor commands
    Serial.printf("Turnout command detected \n");

    break;
    case 'p': //Power states
    Serial.printf("Power command detected \n");

    break;
    case 'r': //Reads from prog track
    Serial.printf("Programming command detected \n");

    break;
    default: 
    Serial.printf("No match for %x \n", rx_opcode);
    
  }
  

  return;
}


void DCCEX_Class::tx_send(){
  dccex_port.uart_write(128); //L
  return;
}


void ESP_dccex_init(){//Initialize Loconet objects
  DCCEX_UART //Initialize uart
  return;
  
}
*/
