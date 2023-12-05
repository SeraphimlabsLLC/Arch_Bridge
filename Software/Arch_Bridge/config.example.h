//Config file for Arch Bridge or Dynamo boards using ESP32-S3 processors.
#define CONFIG_H

#define BOARD_ID "SeraphimLabs Arch Bridge v0.1 \n"
//Board Select. Define both, then pick which is active
#define DYNAMO 1
#define ARCH_BRIDGE 2
#define BOARD_TYPE ARCH_BRIDGE
#define HEARTBEAT_S 60 //Seconds between console heartbeats

/*TTY settings:
* uart_init(uint8_t uartnum, uint8_t txpin, uint8_t rxpin, uint32_t baudrate, uint16_t txbuff, uint16_t rxbuff);
* Parser selection. 0 = no parser, 1 = dccex, 2 = loconet
*/
#define TTY_CONFIG tty.uart_init(0, 43, 44, 115200, 4, 256); 
//#define DCCEX_UART dccex.dccex_port.uart_init(0, 43, 44, 115200, 4, 256); 

//Command bridging filters
#define LN_TO_DCCEX true
#define RS_TO_DCCEX false //Only enable if DCC-EX is not the CS. 
#define DCCEX_TO_LN true 

#define LN_MODE Master //MASTER has priority delay 0. SENSOR has priority delay of 6 down to 2. THROTTLE has priority delay of 20 down to 6. 

#define TIME_US micros() 
// Alternative: esp_timer_get_time();

//Fast Clock settings:
#define FCLK_ENABLE true //False stops the clock from being activated at all. 
#define FCLK_ACTIVE false //True if fastclock should be active at startup. If false it can still be activated by a Loconet device. 
#define FCLK_RATE 10 //Clock multiplier
#define FCLK_DAYS 0 //Initial clock days
#define FCLK_HOURS 5 //Initial clock hours
#define FCLK_MINUTES 0 //Initial clock minutes
#define FCLK_SECONDS 0 //Initial clock seconds
