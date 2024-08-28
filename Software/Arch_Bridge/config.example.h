//Config file for Arch Bridge or Dynamo boards using ESP32-S3 processors.
#define CONFIG_H

#define BOARD_ID "SeraphimLabs Arch Bridge v0.8 \n"
//Board Select. Define both, then pick which is active
#define DYNAMO 1
#define ARCH_BRIDGE 2
#define BOARD_TYPE ARCH_BRIDGE
#define HEARTBEAT_S 60 //Seconds between console heartbeats

//System limits. DCC can address up to 16384 combined devices between turnouts, sensors, and signals. 
#define MAX_ACCESSORIES 16 

//Command bridging filters
#define LN_TO_DCCEX true
#define RS_TO_DCCEX false //Only enable if DCC-EX is not the CS. 
#define DCCEX_TO_LN true 
#define RCN_TURNOUTS //DCC-EX turnouts use RCN

#define LN_HOSTMODE ln_master //Default ln_master. Allowed: ln_sensor, ln_throttle, ln_silent

//Fast Clock settings:
#define FCLK_ENABLE true //False stops the clock from being activated at all. 
#define FCLK_ACTIVE false //True if fastclock should be active at startup. If false it can still be activated by a Loconet device. 
#define FCLK_RATE 1 //Clock multiplier
#define FCLK_DAYS 0 //Initial clock days
#define FCLK_HOURS 17 //Initial clock hours
#define FCLK_MINUTES 0 //Initial clock minutes
#define FCLK_SECONDS 0 //Initial clock seconds

#define TIME_US esp_timer_get_time()
// Alternative: micros();

/*TTY settings:
* uart_init(uint8_t uartnum, uint8_t txpin, uint8_t rxpin, uint32_t baudrate, uint16_t txbuff, uint16_t rxbuff);
* Parser selection. 0 = no parser, 1 = dccex, 2 = loconet
*/
#define TTY_CONFIG tty.uart_init(0, 43, 44, 115200, 4, 128); //921600
#define DCCEX_UART dccex.dccex_port.uart_init(2, 15, 16, 115200, 4, 128); //41 and 42 are right next to the uart0 pins for easy routing

//I2C settings: 
#define I2C_SDA_PIN 8 //GPIO17 on Dynamo
#define I2C_SCL_PIN 9 //GPIO18 on Dynamo
#define I2C_MASTER false
#define I2C_SLAVE_ADDR 43
#define I2C_CLOCK 40000

#define I2C_SLAVE_PORT 0
#define I2C_TX_BUFF 256
#define I2C_RX_BUFF 4096


//DCC time Constants. Periods from NMRA S9.1 with some additional fudge factor
#define DCC_1_HALFPERIOD 58  //4640 // 1 / 80000000 * 4640 = 58us
#define DCC_1_MIN_HALFPERIOD 50 //NMRA S9.1 says 55uS Minimum half-1. 
#define DCC_1_MAX_HALFPERIOD 66 //NMRA S9.1 says 61uS Maximum half-1
#define DCC_0_HALFPERIOD 100 //8000
#define DCC_0_MIN_HALFPERIOD 90 //NMRA S9.1 says 95uS Minimum half-0
#define DCC_0_MAX_HALFPERIOD 12000 //NMRA S9.1 says 10000uS Maximum half-0, and 12000uS maximum full-0. 
