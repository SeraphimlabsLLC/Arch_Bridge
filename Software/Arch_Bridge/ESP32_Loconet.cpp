 /*   IMPORTANT:
 *  Some of the message formats used in this code are Copyright Digitrax, Inc.
 */

#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif
#ifndef ESP32_LOCONET_H
  #include "ESP32_Loconet.h"
#endif
#ifndef ESP32_DCCEX_H
  #include "ESP32_dccex.h"
#endif

//ESP_Uart LN_port; //Loconet uart object
LN_Class Loconet; //Loconet processing object
extern DCCEX_Class dccex_port;

  /*Strategy: 
   * uart_rx() to move uart data to the rx ring
   * rx_scan() to Scan RX ring buffer for a byte > 0x80.
   * Read the command length bits, check if there is enough data in the buffer to match it
   * If yes, process it. If no, leave the RX read pointer at the opcode byte to try again later.
   * 
   * 
   * tx_encode() to validate an outgoing packet
   * tx_scan() to scan the rx data for the rx echo
   */

uint8_t LN_Class::uart_rx(){
  uint8_t read_size = 0;
  read_size = LN_port.uart_read(read_size); //read data into the ring buffer, we don't really care how many bytes
  return read_size;
}

void LN_Class::rx_scan(){ //Scan ring buffer data for an opcode and return its location
  bool eod = false;
  uint8_t i = 0;
  uint8_t packet_size;
  char xsum;

 if ((LN_port.rx_read_ptr == LN_port.rx_write_ptr)) { //End of file
    eod = true;
  }
  while (!(eod))  { //Scan until end of file is true
    rx_opcode = LN_port.rx_data[LN_port.rx_read_ptr];
    if ((rx_opcode & 0x80) && (rx_opcode != 255)) { //we found an opcode
      Serial.printf("Found opcode %x \n", rx_opcode);  
      packet_size= rx_opcode & 0x60; //D7 was 1, check D6 and D5 for packet length
      //Serial.printf("Packet Size %d \n", packet_size);
      i = packet_size>>4; //1 + packetsize >> 4 results in correct packet_size by bitshifting the masked bits
      packet_size = i + 1; //After shifting right 4 bytes, add 1
      if (packet_size >= 6) { //variable length packet, read size from 2nd byte.  
        if ((LN_port.rx_read_ptr + 1) > LN_port.rx_write_ptr){   //Check that there is enough data for the specified type and buffer it
          Serial.print("Variable length packet hasn't sent size yet, waiting for next byte \n");
          return;
        }        
          packet_size = LN_port.rx_data[(LN_port.rx_read_ptr + 1)] - 2; //value is size - 2 since we already have opcode + size byte
      }
      Serial.printf("Packet Size %d \n", packet_size);
      if ((LN_port.rx_read_ptr + packet_size) > LN_port.rx_write_ptr){   //Check that there is enough data for the specified type and buffer it
        Serial.printf("Malfunction, need input. Read ptr %d, Write ptr %h \n", (LN_port.rx_write_ptr), (LN_port.rx_read_ptr + packet_size));
        return;
      }

      if (rx_opcode == tx_opcode) { //We just sent this opcode. Check if what follows matches our packet.
        tx_loopback(packet_size);
        return;    
      }
      
      Serial.printf("Checksum calculation: "); 
      i = 0; //reuse the temp int
      while (i <= packet_size) {
        xsum = xsum ^  LN_port.rx_data[LN_port.rx_read_ptr + i];
        //Serial.printf("ptr %d char %x xsum %x \n", LN_port.rx_read_ptr + i,LN_port.rx_data[LN_port.rx_read_ptr + i], xsum);
        i++;
      }
      if (xsum == 0xff) { //Valid packet. Send to rx_decode
         rx_decode(packet_size); //Decode incoming opcode 
      } else {      
        Serial.printf("Ignoring invalid packet with opcode at %d \n", LN_port.rx_read_ptr);
      }
    } 
  if ((LN_port.rx_read_ptr == LN_port.rx_write_ptr)) { //End of file
    eod = true;
  } else {
    LN_port.rx_read_ptr++; //Read pointer will increment until an opcode is found.
    }
  }
  return;
}  
  
void LN_Class::rx_decode(uint8_t packet_size){  //Opcode was found. Lets use it.
  //Finally processing the packet. 
  Serial.printf("Processing packet with size %d \n", packet_size);
  switch (rx_opcode) {
    
    //2 byte opcodes:
    case 0x81:  //Master Busy
      break;
    case 0x82: //Global power off
      Serial.print("Power off requested \n");
      //DCCEX.send: <0>
      break;
    case 0x83: //Global power on
      Serial.print("Power on requested \n");
      //DCCEX.send: <1>
      break;
    case 0x85: //Force idle, broadcast estop
      Serial.print("ESTOP! \n");
      break; 
      
    //4 byte opcodes: 
    case 0xA0: //Unknown
      break;
    case 0xA1: //Unknown
      break;
    case 0xB0: //REQ SWITCH function
      break;
    case 0xB1: //Turnout SENSOR state REPORT
      break;  
    case 0xB2: //General SENSOR Input codes
      break;       
    case 0xB4: //;Long acknowledge 
      break;         
    case 0xB5: //WRITE slot stat1
      break;   
    case 0xB6: //SET FUNC bits in a CONSIST uplink elemen
      break; 

    //0xB8 to 0xBF require replies
    case 0xB8: //UNLINK slot ARG1 from slot ARG2
      break;  
    case 0xB9: //LINK slot ARG1 to slot ARG
      break;  
    case 0xBA: //MOVE slot SRC to DEST
      break; 
    case 0xBB: //Request SLOT DATA/status block
      Serial.printf("Throttle requesting slot \n");  
      break;
    case 0xBC: //REQ state of SWITCH
      break;   
    case 0xBD: //REQ SWITCH WITH acknowledge function (not DT200
      break;
    case 0xBF: //;REQ loco AD
      break;                                   
    //6 byte opcodes, none known as of Oct 2023: 

    //Variable byte opcodes: 
    
    default: 
    Serial.printf("No match for %x \n", rx_opcode);
    
  }
  return;
}

void LN_Class::tx_send(){
  tx_failure = 15; //default to 15
  tx_opcode_ptr = LN_port.tx_write_ptr;
  LN_port.uart_write(128); //Loconet has a 128 byte max packet size
  return;
}

uint8_t LN_Class::tx_loopback(uint8_t packet_size){
  uint8_t delta = 0;
  uint8_t i;
  while (((LN_port.rx_read_ptr + i) != LN_port.rx_write_ptr) && (i != packet_size)) {
    if (LN_port.rx_data[LN_port.rx_read_ptr + i] != LN_port.tx_data[tx_opcode_ptr + i]){ //Compare rx data to tx data
      delta++;   
    }
  i++;  
  }
  if (delta > 0) { //Collision or corrupt packet. 
    Serial.printf("Collision or corrupt packet detected. \n");
    transmit_break();
    LN_port.rx_read_ptr = LN_port.rx_read_ptr + i;
    tx_failure--;     
  }
  return delta;
}


void LN_Class::transmit_break(){
  //Write 15 bits low
  //ESP_ERROR_CHECK(uart_set_line_inverse(uart_num, UART_SIGNAL_TXD_INV)); //Line defaults to high, set low for break
  //delay_microseconds(900); //15 bit periods
  //ESP_ERROR_CHECK(uart_set_line_inverse(uart_num, UART_SIGNAL_TXD_INV));
  //Todo: Set txd normal again
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
