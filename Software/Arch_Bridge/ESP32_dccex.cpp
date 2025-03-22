#ifndef ESP32_DCCEX_H
  #include "ESP32_dccex.h"
#endif 

#ifndef ESP32_UART_H
  #include "ESP32_uart.h"
#endif

#ifndef ESP32_ADC_H
  #include "ESP32_adc.h"
#endif

#ifndef ESP32_TRACKS_HW_H
  #include "ESP32_Tracks_HW.h"
#endif

#ifndef DCC_READ_H
  #include "dcc_read.h"
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
extern dccrx dcc; 
extern ADC_Handler adc_one[];

void DCCEX_Class::loop_process(){
  if (state == 0) { //Startup
    time_us = TIME_US; 
    if (time_us > 100000) { //100mS startup delay
      char* txdata = "<=>\n"; //Query track states from an attached DCC-EX
      tx_send(txdata, 4);
      state = 1; //Running
    }
    return; 
  }
  
  uart_rx(&dccex_port); //Read uart data from dccex port and scan for packets
  if (rx_state == 2) {
    rx_decode();
  }

  uart_rx(&tty); //Read console data 
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

uint8_t DCCEX_Class::uart_rx(ESP_Uart* uart){ //Read incoming data and scan it for packets
  uint16_t i = 0;
  uint16_t read_size = 0; 
  //uint8_t rx_state = 0; 
  if (uart->rx_read_processed == 255) { //Existing dat is processed, try to get more. 
    read_size = uart->uart_read(0); //populate rx_read_data and rx_read_len
    if (uart->rx_read_len > 0){ //Data was actually moved, update the timer.
      rx_last_us = TIME_US;
    } else {
      return 0; 
    }
  }

Serial.printf("dccex: %u bytes read \n", uart->rx_read_len);
  i = uart->rx_read_processed; //Prime the start of processing
  while (i < uart->rx_read_len) {
    //Serial.printf("Char: %c \n");
    if ((uart->rx_read_data[i] == '<') && (rx_state == 0)) { // 0x3e
      //Serial.printf("START: ");
      rx_state = 1; //pending
      data_len = 0; 
    }
    
    if (rx_state == 1) { //Record data until finding >
      data_pkt[data_len] = uart->rx_read_data[i];
      Serial.printf("%c", uart->rx_read_data[i] );
      data_len++;
      
      if (data_len > 255) { //Would exceed buffer size. Discard malformed packet. 
        rx_state = 0;
      }

      if (uart->rx_read_data[i] == '>') { //stop recording data. Reset for next packet.
        rx_state = 2; //RX complete, ready to process  
        //Serial.printf(" COMPLETE ");
        Serial.printf("\n"); 
      }
    }
    i++; 
  }
  uart->rx_read_processed = uart->rx_read_processed + i; //Track how much data has been processed. 
  if (uart->rx_read_processed >= uart->rx_read_len) {
    uart->rx_read_processed = 255; //mark uart buffer complete so it reads again. 
    uart->rx_read_len = 0; 
  }
  return rx_state;    
}

void DCCEX_Class::rx_decode(){
  //An implementation of https://dcc-ex.com/throttles/tech-reference.html#
  uint8_t i = 0;
  uint16_t val = 0; 

  if (rx_state != 2) { //No packet to process
    return; 
  }
  switch (data_pkt[1]) {

    case '0': //Global power off
    case '1': //Global power on
    case '!': //Estop
      if ((data_pkt[2] == '>') || ((data_pkt[2] = ' ') && (data_pkt[3] == 'M') && (data_pkt[4] == 'A') && (data_pkt[5] == 'I') && (data_pkt[6] == 'N') && (data_pkt[7] == '>'))) {
      //Serial.printf("DCCEX global power state %c %i \n", data_pkt[1], data_pkt[1]); 
        global_power(data_pkt[1], false);
        Loconet.global_power(data_pkt[1], DCCEX_TO_LN); //Send to Loconet if true
      }
      break; 
    
    case '=': //Track Manager
      rx_track_manager();
      break; 

    case 'b': //Programming command.
      if (data_pkt[2] == '>') {
        Loconet.global_power(data_pkt[1], DCCEX_TO_LN); //Send to Loconet
      }
      break; 

    case 'c': //Display currents
      output_current(); //display output currents
      if (data_pkt[2] == '0') {
        Serial.printf("DCCEX: Zeroing ADC readings \n");
        for (i = 0; i < max_tracks; i++) {
          uint8_t adc_index = DCCSigs[i].adc_index;
          adc_one[adc_index].adc_zero_set(); 
        }
      }        
      break; 

    case 'i': //DCC-EX version string.
      Serial.printf(BOARD_ID);
      break;   

    case 'p': //power manager. <p0 C> and <p1 C> typical examples
      for (i = 0; i < max_tracks; i++) {
        if (data_pkt[4] == DCCSigs[i].trackID) { //Command relevant to one of ours
          rx_power_manager(i, data_pkt[2]); //Change the state of the matching track
        }
      }
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
      
    case 'j': //Many detail responses are in j
      if (data_pkt[2] == 'T') { //response to turnout query

      }
    //jC is fastclock echo
    break; 

    case 'J': //many detail set/query commands are in J
      if (data_pkt[2] == 'C') { //Fast Clock functions. <JC minutes rate> to set. minutes has range 0-1440, number of minutes in 24h. 
        Serial.printf("JC: Fast Clock currently days %u %u:%u:%u \n", Fastclock.days, Fastclock.hours, Fastclock.minutes, Fastclock.seconds);
        Loconet.slot_read(123); //Broadcast fast clock
        
        break;          
      }
    case 'l': //Throttle broadcast
      rx_cab();
      
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

void DCCEX_Class::global_power(char newstate, bool announce){ //Set power state for all tracks, announce to DCCEX
  uint8_t i = 0; 
  char* txdata = "<0 MAIN>\n"; 
  uint8_t txlen = 9; 
  //Serial.printf("DCCEX: global_power state %c %i announce %i \n", newstate, uint8_t(newstate), announce); 
    if (newstate == '0') { //DCC-EX power off
      for (i = 0; i < max_tracks; i++) {
       rx_power_manager(i, '0'); //Change the state of the matching track
      }
      txdata = "<0 MAIN>\n"; //<0>\n also works.
      txlen = 9; 
    }
    if (newstate == '1') { //DCC-EX power on
      for (i = 0; i < max_tracks; i++) {
       rx_power_manager(i, '1'); //Change the state of the matching track
      }      
      txdata = "<1 MAIN>\n"; //<1>\n also works.
      txlen = 9; 
    }
    if (newstate == '!') { //DCC-EX Estop
      txdata = "<!>\n"; 
      txlen = 4; 
      //TODO: Estop actions
    } 
    if (newstate == 'b') { //DCC-EX actually uses <b as CV programming, so don't ever send it.
      txdata = "<!>\n";
      txlen = 4;  
      announce = false; 
    }
  if (announce == true) {
    tx_send(txdata, txlen);
  }

  return; 
}

void DCCEX_Class::rx_track_manager(){ //Process track manager input
  uint8_t i = 0; 
  char track = (data_pkt[3]);
  uint8_t tmode = 0; 
  int16_t dcaddr = -1; 

  if (data_pkt[2] == '>') { //Query to list commands, not to set them. 
      char* txdata = "<=>\n"; //Query track states from an attached DCC-EX
      tx_send(txdata, 4);
    return; 
  }

//  tmode = tmode * 10 + (data_pkt[5] - 48); //Convert char to int
/*  if ((data_pkt[5] > 47) && (data_pkt[5] < 58)) { //Was given  a symbol from 0-9. Subtract 48 to convert it to int. 
    tmode = data_pkt[5] - 48; 
  } */

  //Command is 0 or NONE, set to OFF mode. 
  if ((data_pkt[5] == 'N') && (data_pkt[6] == 'O') && (data_pkt[7] == 'N') && (data_pkt[8] == 'E')) {
    tmode = 0; 
  }  
  //Command is 1 or MAIN, set to DCC EXT mode. 
  if ((data_pkt[5] == 'M') && (data_pkt[6] == 'A') && (data_pkt[7] == 'I') && (data_pkt[8] == 'N')) {
    tmode = 1; 
  }
  //Command is PROG, set to DCC EXT mode since we can't make our own prog yet. 
   if ((data_pkt[5] == 'P') && (data_pkt[6] == 'R') && (data_pkt[7] == 'O') && (data_pkt[8] == 'G')) {
    tmode = 1; 
  } 
  //Command is DC, or DCX. Check which and set.  
  if ((data_pkt[5] == 'D') && (data_pkt[6] == 'C')) {
    if (data_pkt[7] == ' ')  { //DC mode
      tmode = 3; 
      i = 8; //7 is a ' ', so start from 8.
    }
    if (data_pkt[7] == 'X')  { //DCX mode
      tmode = 4;
      i = 9; //8 should be a ' ', so start from 9. 
    } 

    while ((data_pkt[i] > 47) && (data_pkt[i] < 58)){ //input range is a number. 
      if (dcaddr < 0) { //Don't change it from -1 until we are sure we have a valid input to use. 
        dcaddr = 0; 
      }
      dcaddr = dcaddr * 10 + (data_pkt[i] - 48); //Convert str to int
      i++; 
      if (!(data_pkt[i])) { //Out of data. Command was invalid. 
        return; 
      }
      
    }
    Serial.printf("\n");
    Serial.printf("DC Mode intended for track %c with cab address %u, command %s \n", track, dcaddr, data_pkt);
  }  
 
  Serial.printf("Track %c mode %u \n", track, tmode);
  for (i = 0; i <= max_tracks; i++) {
  if (track == DCCSigs[i].trackID) { //Command relevant to one of ours
      DCCSigs[i].ModeChange(tmode);
      if (dcaddr >= 0) { //Set DC address if one was given. 
        DCCSigs[i].CabAddress(dcaddr); 
      } else {
        DCCSigs[i].CabAddress(NULL);
      }
    }
  }
  return; 
}


void DCCEX_Class::rx_power_manager(uint8_t track, char state){
  uint8_t i = 0; 
  bool power = false;
  //Serial.printf("DCCEX Power State %c, track %c \n", data_pkt[2], data_pkt[4]);
  if (!(DCCSigs[track].trackID)){
    Serial.printf("DCCEX : Invalid track ID to change power of %i \n" , track);    
    return; 
  }
  switch (state) {
    case '2':
    case '1':
      if (state == '2') {
        DCCSigs[track].StateChange(2);
      }
      if (state == '1') {
        DCCSigs[track].StateChange(1);
      }
      //Serial.printf("DCCEX : Turning on power to track %c \n" , DCCSigs[track].trackID);    
      break;
    case '0':
      DCCSigs[track].StateChange(0);
      //Serial.printf("DCCEX : Turning off power to track %c \n" , DCCSigs[track].trackID);  
      break; 
    default: 
      Serial.printf("DCCEX : Invalid track power state %c %i \n", state, state);
  }
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

void DCCEX_Class::output_current(){
  uint8_t adc_index = 0; 
  uint8_t i = 0;
  int32_t reading = 0; 
  int32_t overload = 0; 
      for (i = 0; i < max_tracks; i++) {
        adc_index = DCCSigs[i].adc_index;
        //adc_one[adc_index].print_flag(true);
        adc_one[adc_index].adc_read(&reading, NULL, NULL, &overload); 
        Serial.printf("Track %c ADC analog value = %u milliamps \n", DCCSigs[i].trackID, reading /DCCSigs[i].adc_ticks_scale); //smooth scaled to mA
        //Serial.printf("Track %c ADC analog value = %u milliamps, max %d milliamps, raw %i \n", DCCSigs[i].trackID, reading /DCCSigs[i].adc_ticks_scale, overload /DCCSigs[i].adc_ticks_scale, reading); //smooth scaled to mA
      }
      #ifdef ESP32_LOCONET_H
//    Include Railsync voltage monitor
     adc_index = Loconet.ln_adc_index; 
     //adc_one[adc_index].print_flag(true);
     adc_one[adc_index].adc_read(&reading, NULL, NULL, NULL); 
     Serial.printf("Loconet Railsync drive %u mV, minimum 7000mV. \n", reading / Loconet.adc_ticks_scale); //smooth scaled mV 
     //Serial.printf("Loconet Railsync drive %u mV, minimum 7000mV. raw %i \n", reading / Loconet.adc_ticks_scale, reading); //smooth scaled mV      
     #endif
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
   int16_t index = 0; 
   uint16_t addr = 0; 
   bool dir; 
   uint8_t state; 
   uint8_t newstate; 
   while (data_pkt[i] != ' ') {
     addr = addr * 10 + (data_pkt[i] - 48); //Convert str to int
     i++;
   } 
   i++; //Should put us on the state byte. 
   dir = (data_pkt[i] - 48);
  Serial.printf("DCCEX commanded turnout %u to state %u \n", addr, dir); 
  index = acc_search_id(addr, acc_turnout); 
  if (index < 0) {//Not found, create it
    Serial.printf("DCCEX saving info for new turnout index %i \n", addr);
    index = acc_get_new(); 
    if (index > -1) {
      accessory[index]->set_device(addr, addr, acc_turnout, 3); //DCC-EX ID, DCC-EX ID is addr, acc_turnout type, learned from DCCEX
      rx_sw_state(index, ~state); //set it to not state so that it always writes it the first time
    }
  }
  if (index > -1){ //Found stored addr info
    addr = accessory[index]->get_addr();
    state = accessory[index]->get_state(); 
    //todo: Make newstate 
    if (state != newstate) {
      rx_sw_state(index, state);  
      #if LN_TO_DCCEX == true //Only send if allowed to. 
        Serial.printf("DCCEX Forwarding Turnout %i to state %i \n", addr, dir);
        Loconet.tx_req_sw(addr - 1, dir, 1); //State defaults to 1 for now. It may be necessary to add a delay off that sends a 2nd packet with state 0.
   
      #endif     
    }
  }
  return; 
}
void DCCEX_Class::rx_sw_state(uint16_t index, uint8_t state){
  if (!(accessory[index])){
    //Invalid index, do nothing
    return; 
  }
  accessory[index]->set_state(state);    
  return; 
}

void DCCEX_Class::tx_req_sw(uint16_t addr, bool dir, bool state){ //Send switch command
  char output[15];
  int16_t index = 0; 
  //uint8_t state = 0; 
  uint8_t oldstate = 0; 
  Serial.printf("DCCEX tx_req_sq addr %i, dir, %i \n", addr, dir);
  index = acc_search_dcc_addr(addr - 1, acc_turnout); //Find if there is matching stored info
  if (index < 0 ) { //didn't find it, make it. 
    index = acc_get_new();
    if (index > -1) {
      accessory[index]->set_device(addr, addr - 1, acc_turnout, 2); //DCC-EX ID, DCC-EX ID is addr, acc_turnout type, learned from Loconet
      rx_sw_state(index, ~state); //set it to not state so that it always writes it the first time
    }    
    if (index < 0) {
      //Couldn't get a usable slot. Give up. 
      return; 
    }
  }
  oldstate = accessory[index]->get_state(); 
  if (state != oldstate) { //Only send updated state if it changed. 
    rx_sw_state(index, state); 
    printf(output, "<T %u %u>\n", addr, dir);
    tx_send(output, 15);
  }
  return; 
}

void DCCEX_Class::rx_cab(){
   uint8_t i = 3; 
   uint16_t addr = 0; 
   uint8_t reg = 0; 
   uint8_t spd; 
   bool dir = 0;
   uint8_t funct; 
   while (data_pkt[i] != ' ') { //addr str to int
     addr = addr * 10 + (data_pkt[i] - 48);
     i++;
   } 
   i++;
   while (data_pkt[i] != ' ') {//reg str to int
     reg = reg * 10 + (data_pkt[i] - 48);
     i++;
   } 
   i++;
   while (data_pkt[i] != ' ') {//reg str to int
     spd = spd * 10 + (data_pkt[i] - 48);
     i++;
   } 
   i++;
   while (data_pkt[i] != ' ') {//reg str to int
     funct = funct * 10 + (data_pkt[i] - 48);
     i++;
   } 
   dir = spd << 7; //bit 7 (128-255) is actually dir
   spd = spd & 0x7F; //limit spd to 127

    #if LN_TO_DCCEX == true //Only send if allowed to. 
    Serial.printf("DCCEX_Class::rx_cab<l %u %u %u %u %u> \n", addr, reg, spd, dir, funct);
      Loconet.tx_cab_dir(addr, dir);
      Loconet.tx_cab_speed(addr, spd);
    #endif
  return; 
}
void DCCEX_Class::tx_cab_speed(uint16_t addr, uint8_t spd, bool dir){

  char output[15]; 
  sprintf(output, "<t %u %u %u>\n", addr, spd, dir);
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
    Serial.printf("DCCEX running on DCCEX_UART, uart %u\n",  dccex_port.uart_num);
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

/*****************************************
 * Turnout, Sensor, and Signal (Accessory_Device) Functions *
******************************************/
int16_t DCCEX_Class::acc_search_id(uint16_t id, accessory_type type){ //Find an accessory by its DCCEX ID
  int16_t i; 
  uint16_t sid;
  accessory_type stype;

  for (i = 0; i < MAX_ACCESSORIES; i++) {
    if (!(accessory[i])){ 
      //accessory slot undefined, skip it
      continue; 
    }
    sid = accessory[i]->get_id();
    stype = accessory[i]->get_type();
    if ((sid == id) && (stype == type)){
      return i; 
    }
  }
  return -1;
}
int16_t DCCEX_Class::acc_search_dcc_addr(uint16_t addr, accessory_type type){ //Find an accessory by its DCC Address
  int16_t i; 
  uint16_t saddr;
  accessory_type stype;
  for (i = 0; i < accessory_count; i++) {
    if (!(accessory[i])){ 
      continue; 
    }
    saddr = accessory[i]->get_addr();
    stype = accessory[i]->get_type();
    if ((saddr == addr) && (stype == type)) {
      return i; 
    }
  }
  return -1; 
}

int16_t DCCEX_Class::acc_request_info(uint16_t id, accessory_type type){ //request more info about an item from DCC-EX
  uint8_t output_len = 0; 
  char output[255];
  if (type == acc_turnout) {
      output_len = 12; //Max 5 digits + 7 chars overhead 
      char output[output_len]; 
      sprintf(output, "<JT %u>\n", id); 
  }
  if (type == acc_sensor){

  }
  if (type == acc_signal) {

  }
  if (output_len > 0){ //Only transmit if there was data produced to be sent
    tx_send(output, output_len);  
  }
  return 0; 
}

uint16_t DCCEX_Class::find_sw(uint16_t addr) {

  return 0; 
}

int16_t DCCEX_Class::acc_get_new(){
  int16_t index; 
  accessory_type type; 
  uint8_t learnedfrom; 

  //Search the list
  for (index = 0; index <= accessory_count; index++){
    if (!(accessory[index])){ //not allocated, create it
      accessory[index] = new Accessory_Device; 
      if (!(accessory[index])){
        Serial.printf("Unable to allocate accessory slot. \n"); 
        return -1; 
      } else {
        accessory_count++; 
      }     
    }
    type = accessory[index]->get_type(); 
    learnedfrom = accessory[index]->get_learnedfrom(); 
    if ((type == acc_none) && (learnedfrom == 0)){
      //empty slot. Use it. 
      return index;
    }  
  }   
  //Couldn't find a usable slot, give up.  
  Serial.printf("Out of available accessory slots \n");
  return -1;
}

/*****************************
* Accessory_Device functions *
******************************/

void Accessory_Device::set_reminder(uint64_t usdelay, bool repeat){
  reminder_us = usdelay; 
  repeated = repeat; 
  return;
}

uint64_t Accessory_Device::get_reminder(){
  return reminder_us;
}

uint64_t Accessory_Device::get_lastaction(){
  return last_cmd_us; 
}

uint8_t Accessory_Device::get_learnedfrom(){
  return learned_from;
}

uint8_t Accessory_Device::get_state(){
  return state;
}

void Accessory_Device::set_state(uint8_t newstate){
  state = newstate;
  return;
}

accessory_type Accessory_Device::get_type(){
  return type;
}

int16_t Accessory_Device::get_addr(){
  return addr;
}

uint16_t Accessory_Device::get_id(){
  return ID; 
}

void Accessory_Device::set_device(uint16_t newid, int16_t dccaddr, accessory_type devtype, uint8_t nlearned_from){
    ID = newid; 
    addr = dccaddr; 
    type = devtype; 
    learned_from = nlearned_from;
    last_cmd_us = TIME_US; 
  return; 
}

Accessory_Device::Accessory_Device(){
    addr = -1; //DCC address, -1 means not set yet.
    state = 0; //Empty 
    type = acc_none; //no type asssigned yet. 
    learned_from = 0; //Where it was learned from: 0 = empty, 1 = DCC, 2 = Loconet, 3 = DCCEX
    last_cmd_us = 0; //Time of last action  
    reminder_us = 0; //Reminders off by default
    repeated = false; //no recurring reminder
  return; 
}
