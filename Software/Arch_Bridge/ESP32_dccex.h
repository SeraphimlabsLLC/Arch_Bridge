#pragma once
#define ESP32_DCCEX_H

//Use config.h if present, otherwise defaults
#ifndef CONFIG_H
  #if __has_include ("config.h")
    #include "config.h"
  #endif
  #ifndef CONFIG_H
    #include "config.example.h"
  #endif
#endif  

#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif
#include "Arduino.h"


//Reflectors to insert into main() instead of using an extern
void dccex_init(); //DCCEX_Class::dccex_init();
void dccex_loop(); //DCCEX_Class::dccex_loop();

//DCC-EX UART settings, from ESP32_uart.h: (uint8_t uartnum, uint8_t txpin, uint8_t rxpin, uint32_t baudrate, uint16_t txbuff, uint16_t rxbuff);

enum dccex_power_mode {OFF = 0, MAIN = 1, PROG = 1, DC = 2, DCX = 3}; //DCCEX track modes

enum accessory_type {none = 0, turnout = 1, sensor = 2, signals = 3};
class Accessory_Device{
  public:
  int16_t addr; //DCC address, is limited to 14 bits on Loconet
  uint16_t ID; //DCC-EX turnout or sensor ID
  uint8_t state; 
  accessory_type type; 
  //bit 0 = output 0/1, bit 1 = direction 0/1. RCN_213 has direction 1 as closed and 0 as thrown. 
  uint8_t source; //Where it was learned from: 0 = empty, 1 = DCC, 2 = Loconet, 3 = DCCEX
  uint64_t last_cmd_us; //Time of last action  
};

class DCCEX_Class {
  public:
  ESP_Uart dccex_port; //uart access
  Accessory_Device* accessory[MAX_ACCESSORIES];
  uint8_t rx_read_processed;
  uint64_t rx_last_us; 
  //uint8_t rx_read_len;
  uint8_t rx_state;
  uint8_t data_len; 
  char data_pkt[256]; 

  void dccex_init(); //in-class initializer
  void loop_process(); //Scanning loop to include in loop();
  void rx_decode(); //Process the opcode that was found

  void rx_track_manager(); //Received track mode command
//  void tx_track_manager(); 
  void rx_power_manager(); //Received track power command
//  void tx_power_manager(); 
  void ddiag(); //Process diagnostic commands
  void Fastclock_set();
  void Fastclock_get(); 

  //Turnout functions: 
  void rx_req_sw(); //Received switch command
  void tx_req_sw(uint16_t addr, bool dir, bool state); //Send switch command
  uint16_t find_sw(uint16_t addr); 
  int16_t acc_get_new( uint16_t index); //Check if an accessory is empty and initialize it. 
  int16_t acc_search_id(uint16_t id, accessory_type type); //Find an accessory by its DCCEX ID and type
  int16_t acc_search_addr(uint16_t id, accessory_type type); //Find an accessory by its DCC Address and type

  //Throttle functions
  void rx_cab(); 
  void tx_cab_speed(uint16_t addr, uint8_t spd, bool dir);

  private:
  uint64_t fastclock_ping;
  uint32_t fastclock_next;
  
  uint8_t uart_rx(bool console); //Receive data from uart, either from the dccex input or console
  void rx_scan(); //Scan rx ring for a valid packet
  void tx_send(char* txdata, uint8_t txsize); //Send data
};
