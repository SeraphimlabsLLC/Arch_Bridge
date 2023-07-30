//ADC Settings:
#DEFINE ADC_DMAX 4095 //4095 in single shot, 8191 in continuous
#DEFINE ADC_VMAX 3.1 //Max readable voltage is actually 3.1v using mode ADC_ATTEN_DB_11  

//TTY settings:
/* Pin Map
TXD = GPIO43(U0TXD)
RXD = GPIO44(U0RXD)
*/
#DEFINE TTY_TX_BUFF = 4096
#DEFINE TTY_RX_BUFF = 4096

//I2C settings: 
/* Pin Map
SDA = GPIO15
SCL = GPIO16
*/
#DEFINE I2C_TX_BUFF = 4096
#DEFINE I2C_RX_BUFF = 4096

//Loconet settings:
/* Pin Map
LN_TX = GPIO17(U1RXD)
LN_RX = GPIO18 (U1RXD)
LN_COLL = GPIO0
*/
#DEFINE LN_TX_BUFF = 4096
#DEFINE LN_RX_BUFF = 4096

//Railsync input settings:
/* Pin Map
RSYNC_IN = GPIO9
*/

//Railsync output settings:
/*  Pin Map
ENA_10 = GPIO10
DCC_1 = GPIO11
BRK_1 = GPIO12
ENA_1i = GPIO13
SNS1 = GPIO1(ADC1_CH0)
*/
#DEFINE OUT1_MAX_A 3610 //ADC_DMAX / ADC_VMAX * SENSE_V for 200mA + 0.1%

//Track output settings:
/* Pin Map
ENA_20 = GPIO14
DCC_2 = GPIO21
BRK_2 = GPIO47
ENA_2i = GPIO48
SNS2 = GPIO2(ADC1_CH1)
*/
#DEFINE OUT2_MAX_A 3610 //ADC_DMAX / ADC_VMAX * SENSE_V for 2A + 0.1%

