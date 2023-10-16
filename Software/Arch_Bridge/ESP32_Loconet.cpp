#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif
#ifndef ESP32_LOCONET_H
  #include "ESP32_Loconet.h"
#endif

//ESP_Uart LN_port; //Loconet uart object
LN_Class Loconet; //Loconet processing object

void LN_Class::rx_detect(){
  /*Strategy: 
   * Scan RX ring buffer for a byte > 0x80.
   * Read the command length bits, check if there is enough data in the buffer to match it
   * If yes, process it. If no, leave the RX read pointer at the opcode byte to try again later.
   */
  //char opcode; Is now redundant with the class definition
  uint8_t rx_offset = 0; //We don't want to change read_ptr itself till we find an opcode
  uint8_t packet_size = 0;
  bool found = false;
  while (LN_port.rx_read_ptr != LN_port.rx_write_ptr)  { //Scan till we run out of data.
    opcode = LN_port.rx_data[LN_port.rx_read_ptr];
    //Serial.printf("Trying byte %d \n", opcode); 
    if (((opcode && 0x80) > 0) && (opcode != 0xFF)) { //we found an opcode
      //Serial.printf ("Found opcode at %d \n", LN_port.rx_read_ptr);
      found = true;
      break; 
    } 
    LN_port.rx_read_ptr++;   
  }
  if (found == false){ //No opcode found, return.
    return;
  }
  //Opcode was found. Lets use it.
  //Serial.printf("Found opcode %d \n", opcode); 
  packet_size = opcode && 0x60; //D7 was 1, check D6 and D5 for packet length
  switch (packet_size) {
    case 0x00: {//2 byte packets
      packet_size = 1;//Value is size - 1 since we already have the opcode
      checksum = LN_port.rx_data[(LN_port.rx_read_ptr + 1)];
      if ((checksum ^ opcode) != 0xFF) {
        Serial.printf("Invalid 2 byte packet opcode %d with checksum %d. \n", opcode, checksum);
      } else {
        Serial.printf("Valid packet: opcode $d with checksum $d \n", opcode, checksum);
      }
      break;
    }
    case 0x20: {//4 byte packets
      packet_size = 3;
      break;
    }
    case 0x40: {//6 byte packets
      packet_size = 5;
      break;
    }
    case 0x60: {//n byte packets, read size from next byte
      packet_size = LN_port.rx_data[(LN_port.rx_read_ptr + 1)] - 2; //value is size - 2 since we already have opcode + size byte
      break;
    }
    Serial.printf("Packet size was packet_size %d \n", packet_size);
  }
  //Check that there is enough data for the specified type and buffer it
  if ((LN_port.rx_read_ptr + packet_size) > LN_port.rx_write_ptr){
    Serial.print("Not enough bytes to process yet. Waiting for more. \n");
    return;
  }

  return;
}
void LN_Class::tx_encode(){
  LN_port.uart_write(32); //Loconet has a 32 byte max packet size
  return;
}
void LN_Class::transmit_break(){
  //Write 15 bits low
  
  return;
}
void LN_Class::receive_break(){ //Invalid data was caught. Flush the buffer.
  LN_port.rx_read_ptr = LN_port.rx_read_ptr =0; //Because this is a ring buffer, if you make both pointers the same all data will be overwritten
  LN_port.rx_data[LN_port.rx_read_ptr] = ' '; 
  return;
}

void ESP_LN_init(){//Initialize Loconet objects
  LN_UART //Initialize uart
  return;
  
}
