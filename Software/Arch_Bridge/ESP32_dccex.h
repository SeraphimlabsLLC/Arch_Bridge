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

enum accessory_type {acc_none = 0, acc_turnout = 1, acc_sensor = 2, acc_signal = 3};
class Accessory_Device{
  //Valid DCC addresses from 0-2044. 511 actual addresses, each with 4 suboutputs
  //Do not allow broadcast addresses 2045-2048
  //Extended addresses up to 16383
  public:
  void set_device(uint16_t newid, int16_t dccaddr, accessory_type devtype, uint8_t nlearned_from);
  uint16_t get_id(); //Get dccex ID of this index
  int16_t get_addr(); //get dcc address, -1 if not on dcc
  accessory_type get_type();
  void set_state(uint8_t newstate);
  uint8_t get_state();
  uint8_t get_learnedfrom(); //used for reply routing
  uint64_t get_lastaction(); //Timestamp of last output
  uint64_t get_reminder(); //Returns if a reminder is set
  void set_reminder (uint64_t usdelay, bool repeat); //Take action in usdelay uS, 0 to disable
  Accessory_Device(); //Constructor

  private:
  uint16_t ID; //DCC-EX turnout or sensor ID, 0-32767
  accessory_type type; 
  int16_t addr; //DCC address, is limited to 14 bits on Loconet. -1 if not accessible by dcc
  uint8_t state; 
  //bit 0 = output 0/1, bit 1 = direction 0/1. RCN_213 has direction 1 as closed and 0 as thrown. 
  uint8_t learned_from; //Where it was learned from: 0 = empty, 1 = DCC, 2 = Loconet, 3 = DCCEX
  uint64_t last_cmd_us; //Time of last action 
  uint64_t reminder_us; //Take automatic action in uS, such as a turnout having its output switched off
  bool repeated; //True if reminder repeats  

};

class DCCEX_Class {
  public:
  ESP_Uart dccex_port; //uart access
  Accessory_Device* accessory[MAX_ACCESSORIES];
  uint16_t accessory_count = 0; 
  uint8_t rx_read_processed;
  uint64_t rx_last_us; 
  //uint8_t rx_read_len;
  uint8_t rx_state;
  uint8_t data_len; 
  char data_pkt[256]; 

  void dccex_init(); //in-class initializer
  void loop_process(); //Scanning loop to include in loop();
  void rx_decode(); //Process the opcode that was found

//Layout functions: 
  void global_power(char state, bool announce); //global power true/false, echo to DCCEX
  void rx_track_manager(); //Received track mode command
//  void tx_track_manager(); 
  void rx_power_manager(uint8_t track, char state); //Received track power command
//  void tx_power_manager(); 
  void ddiag(); //Process diagnostic commands
  void output_current(); //Display output currents
  void Fastclock_set();
  void Fastclock_get(); 

  //Turnout/accessory functions: 
  void rx_req_sw(); //Received switch command
  void rx_sw_state(uint16_t index, uint8_t status); //Received switch status
  void tx_req_sw(uint16_t addr, bool dir, bool state); //Send switch command
  uint16_t find_sw(uint16_t addr); 
  int16_t acc_get_new(); //Initialize a new accessory slot
  int16_t acc_search_id(uint16_t id, accessory_type type); //Find an accessory by its DCCEX ID and type
  int16_t acc_search_dcc_addr(uint16_t id, accessory_type type); //Find an accessory by its DCC Address and type
  int16_t acc_request_info(uint16_t id, accessory_type type); //request more info about an item from DCC-EX

  //Throttle functions
  void rx_cab(); 
  void tx_cab_speed(uint16_t addr, uint8_t spd, bool dir);

  private:
  uint8_t state; //0 = startup, 1 = runnin
  uint64_t fastclock_ping;
  uint32_t fastclock_next;
  
  uint8_t uart_rx(ESP_Uart* uart); //Receive data from the provided uart handle
  //void rx_scan(ESP_Uart* uart); //Scan the provided rx ring for a valid packet
  void tx_send(char* txdata, uint8_t txsize); //Send data
};
