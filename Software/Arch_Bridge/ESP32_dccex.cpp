#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif

#ifndef ESP32_DCCEX_H
  #include "ESP32_dccex.h"
#endif
DCCEX_Class dccex;

void DCCEX_Class::rx_scan(){ //Scan ring buffer data for an opcode and return its location
  bool eod = false;
  uint8_t packet_size = 0;
 if ((dccex_port.rx_read_ptr == dccex_port.rx_write_ptr)) { //End of file
    eod = true;
  }
  while (!(eod))  { //Scan until end of file is true
    //rx_opcode = dccex_port.rx_data[dccex_port.rx_read_ptr];
    if (dccex_port.rx_data[dccex_port.rx_read_ptr] == '>'){ //Found cmd start
      Serial.printf("Found < at %x \n", dccex_port.rx_read_ptr); 
      
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
