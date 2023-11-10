#pragma once
#ifndef ESP32_LOCONET_H
  #define ESP32_LOCONET_H
#endif

#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif
#ifndef ESP32_DCCEX_H
  #include "ESP32_dccex.h"
#endif
#include "Arduino.h"
#include "esp_timer.h" //Required for timer functions to work.

//Loconet UART settings, from ESP32_uart.h: (uint8_t uartnum, uint8_t uartmode, uint8_t txpin, uint8_t rxpin, uint32_t baudrate, uint16_t txbuff, uint16_t rxbuff);
#define LN_UART Loconet.LN_port.uart_init(1, 2, 17, 18, 16666, 255, 255);
#define LN_PRIORITY MASTER //MASTER has priority delay 0. SENSOR has priority delay of 6 down to 2. THROTTLE has priority delay of 20 down to 6. 

//Fast Clock settings:
#define CLK_RATE 4; //Clock multiplier
#define CLK_HOURS 12; //Initial clock hours
#define CLK_MINUTES 0; //Initial clock minutes

//Queue settings: 
#define LN_RX_Q 32
#define LN_TX_Q 32

//Enums: 
enum LN_packetstate {empty = 0, pending = 1, attempting = 2, sent = 3, failed = 4, success = 5}; //Packet state flags 
enum LN_netstate {startup = 0, disconnected = 1, inactive = 2, active = 3};
class LN_Packet{ //Track packet states. The packet data itself goes in a char[] and this only stores the ptr
  public: 
  uint8_t priority; //0 = highest, 20 = lowest. Throttles have min 20, Sensors have min 6, Master has min 0
  uint8_t state; //See enum packetstate. Couldn't get the enum to actually work here. 
  int8_t tx_attempts; //Starts at 25 and decrements, tx failure at 0.
  uint64_t tx_last_start; //Time in uS of last tx window start. Each window lasts 15mS. 
  uint8_t data_len; //Length of packet
  char* data_ptr; //Pointer to the char* with the actual packet in it. 
  void Make_Checksum(); //Populate last byte with valid checksum
  bool Read_Checksum(); //Verify checksum, returns true if valid, false if invalid.
  LN_Packet (uint8_t datalen); //Constructor needs length of packet to create the char[] for it;
  ~LN_Packet(); //Destructor to make sure data_ptr gets deallocated again
};

class LN_Slot_data{ //Each slot will be created and have a pointer stored in LN_Class slotnum
  public: 
  enum slot_data_label {stat, adr, spd, dirf, trk, ss2, adr2, snd, id1, id2}; //Data labels. They're all char, so they can be stored in an array. 
  char slot_data[10]; //Use enum above to access 
/*  char stat; //Slot status. Bitmask for details. 
  char adr; //Locomotive low addr byte
  char spd; //Speed. 0x00 stop normally, 0x01 stop emergency, 0x02-0x7f speed range
  char dirf; //direction, direction lights, and F1-F4
  char trk; //System/Track status. This byte only exists for packet formatting, pull this data from master_enable.
  char ss2; //slot status 2
  char adr2; //Locomotive high addr byte
  char snd; //sound, F5-8
  char id1; //unused
  char id2; //unused */
  uint64_t last_refresh;

  LN_Slot_data(); //Constructor
  ~LN_Slot_data(); //Destructor
  
};

class LN_Class {
  public:
  ESP_Uart LN_port; //Class inside a class
  LN_netstate netstate; //Network operating condition
  char rx_opcode; //Last opcode received
  uint64_t rx_last_us; //time in startup us of last byte received  
  uint64_t rx_opcode_last_us; //time of last opcode detected
  uint64_t last_rx_process; //time of last read scan
  
  char tx_opcode; //Last opcode transmitted
  uint64_t tx_last_us; //timestamp of last tx attempt
  uint8_t tx_pkt_len; //length of last tx packet

  void loop_process(); //Process time based data
  uint64_t fastclock_start; //Note the timestamp at which the fast clock was initialized
  LN_Class(); //Constructor

  private:
  LN_Packet* rx_packets[LN_RX_Q]; //Pointers to RX packets
  int8_t rx_pending; //Index of which packet slot is receiving. Set to -1 if none.
  LN_Packet* tx_packets[LN_TX_Q]; //Pointers to TX packets
  int8_t tx_pending; //Index of which packet is actively sending. Set to -1 if none. 

  LN_Slot_data* slot_ptr[127]; //Stores pointers for accessing slot data.
  uint8_t uart_rx(); //Receive data from uart to rx ring
  void rx_scan(); //Scan rx ring for a valid packet
  void rx_decode(); //Process the opcode that was found

  void tx_queue(); //Process transactions in queue to send
  void tx_send(uint8_t txptr); //Try to send data and update tracking info
  uint8_t tx_loopback(uint8_t packet_size); //RX ring found an opcode we just sent, check if it is ours

  uint8_t rx_packet_getempty(); //Scan packets and return 1st empty slot
  void rx_packet_new(uint8_t index, uint8_t packetlen); //Create new packet and initialize it

  uint8_t tx_packet_getempty(); //Scan packets and return 1st empty slot
  void tx_packet_new(uint8_t index, uint8_t packetlen); //Create new packet and initialize it
  void tx_packet_del(uint8_t index); //Delete a packet from the queue after confirmation of send. 
   
  void transmit_break();
  bool receive_break(uint8_t break_ptr);

  void fastclock_update(); //Calculate updated fast clock
  int8_t loco_select(uint8_t low_addr); //Return the slot managing this locomotive, or assign one if new. 
  void slot_read(int8_t slotnumber); //Handle slot reads
  void slot_write(int8_t slotnumber); //Handle slot writes
  void slot_move(int8_t slotnumber, int8_t newslotnumber); //Handle slot moves
  
  uint8_t slot_new(uint8_t index); //Check if a slot is empty and initialize it. 
  uint8_t slot_del(uint8_t index); //Remove a slot from memory
};

void ESP_LN_init();
