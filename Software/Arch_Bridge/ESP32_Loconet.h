#pragma once
#ifndef ESP32_LOCONET_H
  #define ESP32_LOCONET_H
#endif

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
//#ifndef ESP32_DCCEX_H
//  #include "ESP32_dccex.h"
//#endif

#include "Arduino.h"
#include "esp_timer.h" //Required for timer functions to work.

//Loconet UART settings, from ESP32_uart.h: (uint8_t uartnum, uint8_t txpin, uint8_t rxpin, uint32_t baudrate, uint16_t txbuff, uint16_t rxbuff);
#define LN_UART Loconet.LN_port.uart_init(1, 17, 18, 16666, 255, 255);
#define LN_COLL_PIN 8 //GPIO pin for hardware CD

//Constants that shouldn't be changed.
#define LN_LOOP_DELAY_US 0 //60uS * 8 bits

#define LN_BITLENGTH_US 60 
#define LN_COL_BACKOFF 20

//Loconet Railsync ADC parameters
#define LN_ADC_GPIO 9 //GPIO for Railsync load meter ADC input
#define LN_ADC_OFFSET 0 //ADC zero offset
#define LN_ADC_OL 3650000 //Railsync overload threshold in ADC ticks x 1000;
#define LN_ADC_SCALE 160 //ticks per volt

//TODO: Change this to use an enum or class and assign min priority by opcode
#define LN_MASTER 1
#define LN_SENSOR 2
#define LN_THROTTLE 4

//typedef enum LN_Priority_min {Master = 0, Sensor = 2, Throttle = 6;
//#if LN_PRIORITY == MASTER 
  #define LN_MAX_PRIORITY 0
  #define LN_MIN_PRIORITY 20
//#endif
/*
#if LN_PRIORITY == SENSOR
  #define LN_MAX_PRIORITY 2 
  #define LN_MIN_PRIORITY 6
#endif  
#if LN_PRIORITY == THROTTLE
  #define LN_MAX_PRIORITY 6
  #define LN_MIN_PRIORITY 20
#endif 
*/

//Queue settings: 
#define LN_RX_Q 16
#define LN_TX_Q 32
#define TX_SENT_EXPIRE 64000 //Time to keep sent packets waiting for loopback

//Processing reflectors, use these to avoid having to include the entire class in main.
void LN_init(); //Initialize Loconet interface
void LN_loop(); //Loconet process loop

//Enums: 
enum LN_netstate {startup = 0, disconnected = 1, inactive = 2, active = 3};

class LN_Packet{ //Track packet states. The packet data itself goes in a char[] and this only stores the ptr
  public: 
  uint8_t priority; //0 = highest, 20 = lowest. Throttles have min 20, Sensors have min 6, Master has min 0
  volatile uint8_t state; //empty = 0, pending = 1, attempting = 2, sent/received = 3, failed = 4, success = 5} 
  int8_t tx_attempts; //Starts at 25 and decrements, tx failure at 0.
  uint64_t last_start_time; //Time in uS of tx or rx window start. Must be fully sent or received within 15mS.
  uint8_t data_len; //Length of packet
  uint8_t rx_count; //Bytes currently read into packet
  char data_ptr[128]; //Pointer to the char* with the actual packet in it. 
  char xsum; //Holds the checksum for use on the fly
  void Make_Checksum(); //Populate last byte with valid checksum
  bool Read_Checksum(); //Verify checksum, returns true if valid, false if invalid.
  uint8_t packet_size_check(); //Check that a packet has a valid size. 
  void reset_packet(); //Reset a packet to state 0 and data_len 127
  LN_Packet (); //Constructor 
};

class LN_Slot_data{ //Each slot will be created and have a pointer stored in LN_Class slotnum
  public: 
  char slot_data[10]; //Slot data num, stat, adr, spd, dirf, trk, data ss2, adr2, snd, id1, id2
  /* slot_data[10] usage. Slot number  is the index of the LN_Slot_data instance
  0 char stat; //Slot status. Bitmask for details. 
  1 char adr; //Locomotive low addr byte
  2 char spd; //Speed. 0x00 stop normally, 0x01 stop emergency, 0x02-0x7f speed range
  3 char dirf; //direction, direction lights, and F1-F4
  4 char trk; //Not used- copy from LN_TRK instead
  5 char ss2; //slot status 2
  6 char adr2; //Locomotive high addr byte
  7 char snd; //sound, F5-8
  8 char id1; //unused
  9 char id2; //unused */
  uint64_t last_refresh; //TIME_US of last refresh
  uint32_t next_refresh; //uS elapsed to try to send at. 

  LN_Slot_data(); //Constructor
  ~LN_Slot_data(); //Destructor
  
};
class LN_Class {
  public:
  uint64_t LN_loop_timer; //Time since last loop_process
  ESP_Uart LN_port; //Class inside a class
  LN_netstate netstate; //Network operating condition
  uint64_t signal_time; //time of last netstate change
//  uint64_t rx_last_us; //time in startup us of last byte received  
  uint8_t tx_pkt_len; //length of last tx packet
 
  void loop_process(); //Process time based data

  //Turnout handlers 
  void rx_req_sw(uint8_t rx_pkt); //received 0xB0 request switch
  void tx_req_sw(uint16_t addr, bool dir, bool state); //Send 0xB0 request switch
  void tx_del_sw(uint16_t addr, bool dir, bool state, uint32_t ontime); //Invokes tx_req_sw with a reminder to send followup off state

  //Throttle functions
  void rx_cab();  
  void tx_cab_dir(uint16_t addr, bool dir);
  void tx_cab_speed(uint16_t addr, uint8_t spd);

  int8_t slot_new(uint8_t index); //Check if a slot is empty and initialize it. 
  void slot_read(int8_t slotnum); //Handle slot reads
  int8_t slot_write(int8_t slotnum, uint8_t rx_pkt); //Handle slot writes
  int8_t slot_move(int8_t slot_src, int8_t slot_dest); //Handle slot moves
  void slot_fastclock_set(uint8_t rx_pkt);
  void slot_fastclock_get();
  void slot_opsw_set(uint8_t rx_pkt); 
  void slot_opsw_get(); 
  
  int8_t loco_select(uint8_t high_addr, uint8_t low_addr); //Return the slot managing this locomotive, or assign one if new. 

  //Railsync ADC
  uint8_t ln_adc_index;
  int32_t adc_ticks_scale; //ADC ticks per Volt
  
  LN_Class(); //Constructor

  private:
  
  uint64_t last_time_us;
  
  LN_Packet* rx_packets[LN_RX_Q]; //Pointers to RX packets
  uint8_t rx_next_new; 
  uint8_t rx_next_check;
  volatile int8_t rx_pending; //Index of which packet slot is receiving. Set to -1 if none.
  LN_Packet* tx_packets[LN_TX_Q]; //Pointers to TX packets
  uint8_t tx_next_new;
  uint8_t tx_next_check; 
  volatile int8_t tx_pending; //Index of which packet is actively sending. Set to -1 if none. 

  const uint8_t slot_hours = 104; 
  uint8_t slot_minutes = 68; //DCS100 mode. Use 68 for others.   
  LN_Slot_data* slot_ptr[128]; //Stores pointers for accessing slot data. Must be 128 for range 0-127
  char LN_TRK; //Byte for track status flags, shared between all slots
  
  uint8_t uart_rx(); //Receive data from uart to rx ring
  void rx_scan(); //Scan rx ring for a valid packet
  void rx_queue(); //Process RX queue into rx_decode
  int8_t rx_decode(uint8_t rx_pkt); //Process the opcode that was found. Return 0 when complete. 

  void tx_queue(); //Process TX queue into tx_send
  void tx_send(uint8_t txptr); //Try to send data and update tracking info
  uint8_t tx_loopback(); //Check if the current rx_packet matches the tx_packet. If it does, drop both from their queues. 
  
  uint8_t rx_packet_getempty(); //Get the next available rx_packet handle, creating one if necessary.
  void show_rx_packet(uint8_t index); //Print contents of a received packet. 
  void rx_packet_del(uint8_t index); //Delete a packet
  uint8_t tx_packet_getempty(); //Get the next available rx_packet handle, creating one if necessary
  void show_tx_packet(uint8_t index); ////Print contents of a transmitted packet
  void tx_packet_del(uint8_t index); //Delete a packet

  void transmit_break();
  bool receive_break(uint8_t break_ptr);

  void fastclock_update(); //Calculate updated fast clock
  void slot_queue(); //Scan slots and purge inactive
  uint8_t slot_del(uint8_t index); //Remove a slot from memory
  void send_long_ack(uint8_t opcode, uint8_t response);
};

void IRAM_ATTR LN_CD_isr(); //ISR for handling collision detection
