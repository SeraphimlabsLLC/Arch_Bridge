#ifndef ESP32_DCCEX_H
  #include "ESP32_dccex.h"
#endif 

#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif

#ifndef ESP32_TRACKS_HW_H
  #include "ESP32_Tracks_HW.h"
#endif

#if BOARD_TYPE == ARCH_BRIDGE //If this is an arch bridge, include loconet functions
  #ifndef ESP32_LOCONET_H
    #include "ESP32_Loconet.h"
    extern LN_Class Loconet; //Loconet memory object
  #endif
  #ifndef ESP32_TIMER_H
    #include "ESP32_timer.h" //For fastclock access
    extern Fastclock_class Fastclock; 
#endif
  
#endif

DCCEX_Class dccex;

extern ESP_Uart tty; //normal serial port
extern uint64_t time_us; 
extern TrackChannel DCCSigs[];
extern uint8_t max_tracks;
extern Rmtdcc dcc; 

void DCCEX_Class::loop_process(){
  //Serial.printf("DCCEX uart_rx cycle: \n");
  uart_rx(false); //Read uart data 
  //Serial.printf("DCCEX rx_scan cycle: \n");
  rx_scan(); //Scan RX ring for an opcode
  if (rx_state == 2) {
    rx_decode();
  }
  uart_rx(true); //Read console data 
  //Serial.printf("DCCEX rx_scan cycle: \n");
  rx_scan(); //Scan RX ring for an opcode
  if (rx_state == 2) {
    rx_decode();
  }

#if BOARD_TYPE == ARCH_BRIDGE
  if ((Fastclock.active == true) && (Fastclock.set_rate > 0)) { //Broadcast time every fast minute if active at non-zero rate
    if ((time_us - fastclock_ping) > fastclock_next) { //Broadcast time 
      Fastclock_get(); 
    } 
  }
#endif  
  return;
}

uint8_t DCCEX_Class::uart_rx(bool console){ //Read incoming data into a buffer
  uint8_t read_size = 0;
  uint8_t i = 0;
  if (console == true) {
   tty.rx_read_processed = dccex_port.rx_read_processed; //Sync tty rx_read_processed with ours.  
   read_size = tty.uart_read(0); //populate rx_read_data and rx_read_len
   dccex_port.rx_read_data = tty.rx_read_data; //Pointer to the data just read
   dccex_port.rx_read_len = tty.rx_read_len; //Size of rx_read_data
   dccex_port.rx_read_processed = tty.rx_read_processed; //Sync tty rx_read_processed with ours
   tty.rx_read_len = 0; //Steal the data. 
  } else { 
   read_size = dccex_port.uart_read(0); //populate rx_read_data and rx_read_len
   dccex_port.rx_read_data = dccex_port.rx_read_data; //Pointer to the data just read
   dccex_port.rx_read_len = dccex_port.rx_read_len; //Size of rx_read_data
   if (dccex_port.rx_read_len > 0){
     //Serial.printf("UART %u received", dccex_port.uart_num);
     //Serial.printf(dccex_port.rx_read_data); 
     //Serial.printf("\n");
   }
  }
   
  if (dccex_port.rx_read_len > 0){ //Data was actually moved, update the timer.
    rx_last_us = TIME_US;
  }
  return read_size;
}

void DCCEX_Class::rx_scan(){ //Scan ring buffer data for an opcode and return its location
  uint16_t i = 0; 

  while (i < dccex_port.rx_read_len) {
    //Serial.printf("Char: %c \n");
    if ((dccex_port.rx_read_data[i] == '<') && (rx_state == 0)) { // 0x3e
      //Serial.printf("START: ");
      rx_state = 1; //pending
      data_len = 0; 
    }
    
    if (rx_state == 1) { //Record data until finding >
      data_pkt[data_len] = dccex_port.rx_read_data[i];
      Serial.printf("%c", dccex_port.rx_read_data[i] );
      data_len++;

      if (dccex_port.rx_read_data[i] == '>') { //stop recording data. Reset for next packet.
        rx_state = 2; //RX complete, ready to process  
        //Serial.printf(" COMPLETE ");
        Serial.printf("\n"); 
      }
    }
    i++; 
  }
  dccex_port.rx_read_processed = 255; //Mark uart buffer complete so it reads again.
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
    Serial.printf("DCCEX Changing to OFF \n");
      DCCSigs[0].ModeChange(0);
      DCCSigs[1].ModeChange(0);

    break;
    case '1':
    Serial.printf("DCCEX Changing to DCC EXT, ON_FWD \n");
      DCCSigs[0].ModeChange(1);
      DCCSigs[1].ModeChange(1);   
      DCCSigs[0].StateChange(2);//Set to DCC_ON
      DCCSigs[1].StateChange(2);//Set to DCC_ON

    break;
    case '2':
      Serial.printf("DCCEX Changing to DCC EXT, ON_FWD \n");
      DCCSigs[0].ModeChange(3);
      DCCSigs[1].ModeChange(3);   
      DCCSigs[0].StateChange(2);//Set to ON_FWD
      DCCSigs[1].StateChange(2);//Set to ON_FWD  
      break; 

    case '3': 
      Serial.printf("DCCEX Changing to DCC EXT, ON_REV \n");
      DCCSigs[0].ModeChange(3);
      DCCSigs[1].ModeChange(3);   
      DCCSigs[0].StateChange(3);//Set to ON_REV
      DCCSigs[1].StateChange(3);//Set to ON_REV  
      break; 

    case 'D': 
      Serial.printf("DCCEX Debug: \n"); 
      ddiag(); 
      break;
    #if BOARD_TYPE == ARCH_BRIDGE //define ARCH_BRIDGE specific tests

    case 'H': //Turnout command
      Serial.printf("DCCEX Turnout command \n");
      rx_req_sw();
      
      break; 
      
    case 'j': 
    //jC is fastclock echo
    break; 

    case 'J': 
      if (data_pkt[2] == 'C') { //Fast Clock functions. <JC minutes rate> to set. minutes has range 0-1440, number of minutes in 24h. 
        Serial.printf("JC: Fast Clock currently days %u %u:%u:%u \n", Fastclock.days, Fastclock.hours, Fastclock.minutes, Fastclock.seconds);
        Loconet.slot_read(123); //Broadcast fast clock
        
        break;          
      }
    #endif 
    break; 
    case 'X': //DCCEX received invalid command. 
      break; 
    default:
    Serial.printf("Invalid Command \n");
    
  } 
  rx_state = 0;  
  return; 
}

void DCCEX_Class::ddiag() { //Diagnostic mode features
  uint8_t i = 0; 
  switch (data_pkt[2]) {
    case 'T':
      i = gpio_get_level(DCCSigs[0].enable_in_pin);
      Serial.printf("Enable in: %u \n", i);
      i = gpio_get_level(DCCSigs[0].reverse_pin);
      Serial.printf("Reverse: %u \n", i);
      i = gpio_get_level(DCCSigs[0].brake_pin);
      Serial.printf("Brake in: %u \n", i);
      i = 0;
      break;
#if BOARD_TYPE == ARCH_BRIDGE //define ARCH_BRIDGE specific tests
    case 'L':
      Loconet.slot_read(123); //Broadcast fast clock
      break;
#endif
     
    default:
    Serial.printf("Unknown diag mode %c \n", data_pkt[2]);     
  }
  
  return; 
}

void DCCEX_Class::Fastclock_get() {
#if BOARD_TYPE == ARCH_BRIDGE
  uint16_t time_minutes = 0; 
   char output[15]; 
   Fastclock.clock_get();
   time_minutes = Fastclock.minutes + Fastclock.hours * 60;
   fastclock_next = 60000000 / Fastclock.set_rate; //Ping faster at higher rates
   sprintf(output, "<JC %u %u>\n", time_minutes, Fastclock.set_rate); 
   tx_send(output, 15); 
   fastclock_ping = time_us; 
#endif
  return;
}

void DCCEX_Class::Fastclock_set() {
#if BOARD_TYPE == ARCH_BRIDGE

#endif
  return;
}

void DCCEX_Class::rx_req_sw(){ //Received switch command
   uint8_t i = 3; 
   uint16_t addr = 0; 
   bool dir; 
   while (data_pkt[i] != ' ') {
     addr = addr * 10 + (data_pkt[i] - 48); //Convert str to int
     i++;
   } 
   i++; //Should put us on the state byte. 
   dir = (data_pkt[i] - 48);
  Serial.printf("DCCEX commanded turnout %u to state %u \n", addr, dir); 
  #if LN_TO_DCCEX == true //Only send if allowed to. 
  Loconet.tx_req_sw(addr, dir, 1); //State defaults to 1 for now. It may be necessary to add a delay off that sends a 2nd packet with state 0. 
  #endif
  return; 
}

void DCCEX_Class::tx_req_sw(uint16_t addr, bool dir, bool state){ //Send switch command
  char output[15];
  sprintf(output, "<T %u %u>\n", addr, dir);
  tx_send(output, 15);
  return; 
}

void DCCEX_Class::tx_send(char* txdata, uint8_t txsize){
  dccex_port.uart_write(txdata, txsize);
  Serial.printf(txdata);
  //Serial.printf(" < sent to DCCEX \n");
  return; 
}

void DCCEX_Class::dccex_init(){
  rx_state = 0;
  data_len = 0;
  dccex_port.rx_read_processed = 255; //Mark buffer processed so it can be read again. 
  #ifdef DCCEX_UART //Use our own uart
    DCCEX_UART
    Serial.printf("DCCEX running on DCCEX_UART, uart %u, dccex_port.rx_read_data %u, dccex_port.tx_write_data %u \n",  dccex_port.uart_num, dccex_port.rx_read_data, dccex_port.tx_write_data);
  #endif   
  return;
}

void dccex_init(){ //Reflector into DCCEX_Class
  dccex.dccex_init(); 
  return; 
}

void dccex_loop(){ //Reflector into DCCEX_Class
  dccex.loop_process();
  return; 
}
