//If used on more than 1 slave, remember to change de DEVICE_ADDR in each slave.

#include <SPI.h>
#include "DW1000Ranging.h"
#include "DW1000.h"

//Board's pins definitions:
#define SPI_SCK 18
#define SPI_MISO 19
#define SPI_MOSI 23

const uint8_t PIN_RST = 27; // reset pin
const uint8_t PIN_IRQ = 34; // irq pin
const uint8_t PIN_SS = 4;   // spi select pin

/*Device's own definitions*/
#define DEVICE_ADDR "B1:00:5B:D5:A9:9A:E2:9C" 
uint8_t own_short_addr = 0; 
uint16_t Adelay = 16580;

#define IS_MASTER false
#define DEBUG_SLAVE true

/*Structs to handle devices & measurements*/
#define MAX_MEASURES 200
Measurement measurements[MAX_MEASURES];
int amount_measurements = 0;

ExistingDevice Existing_devices[MAX_DEVICES];
uint8_t amount_devices = 0;


//Time management:

// Time constants
const unsigned long waiting_time = 200;
const unsigned long discovery_period = 500;
const unsigned long ranging_timeout = 2000; 

// Timers
unsigned long current_time = 0;
unsigned long discovery_start = 0;
unsigned long slave_ranging_start = 0;
unsigned long broadcast_ranging_start = 0;
unsigned long unicast_ranging_start = 0;
unsigned long waiting_unicast_range_start = 0;

/*Used in DISCOVERY state*/
static bool discovery_previously_done = false;
static bool discovering = false;

uint8_t discovery_attempts = 0;


/*Used in SLAVE_RANGING*/
static bool slave_ranging = false;

/*Used in BROADCAST_RANGING_STATE*/
static bool ranging_broadcast_started = false;

/*state = UNICAST_RANGING*/
static bool unicast_ranging = false;
uint8_t active_polling_device_index = 0;

/*state = WAIT_UNICAST_RANGE*/
static bool waiting_unicast_range = false;

/*To determine Ranging Mode*/
static bool broadcast_ranging = false; // Set when receiving a ModeSwitchRequest to initiator

/*Retry messages management*/
unsigned long last_retry = 0;
uint8_t num_retries = 0;

/*To control board's mode*/
static bool is_initiator = false;

// States for the FSM control:
enum State{
    IDLE,
    DISCOVERY,
    SLAVE_RANGING,
    BROADCAST_RANGING_STATE,
    UNICAST_RANGING,
    WAIT_UNICAST_RANGE,
    SWITCH_TO_INITIATOR,
    SWITCH_TO_RESPONDER
};
State state = DISCOVERY; 

// Message types to send via unicast
enum UnicastMessageType{
    MSG_POLL_UNICAST,
    MSG_SWITCH_TO_RESPONDER
}; 

/*To target the master via unicast*/
byte* short_addr_master;


/*CODE*/

/*SETUP & INITIALIZATIONS*/

void setup(){

    Serial.begin(115200);
    delay(1000); // 1 sec to launch the serial monitor

    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI); // SPI bus start
    DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); // DW1000 Start

    DW1000.setAntennaDelay(Adelay);

    // Callbacks "enabled" 
    attachCallbacks();

    DW1000Ranging.startAsResponder(DEVICE_ADDR,DW1000.MODE_1, false,SLAVE);

    own_short_addr = getOwnShortAddress();
    // I save the own_short_addr after the device has been set up propperly
}

void attachCallbacks(){
    DW1000Ranging.attachNewRange(newRange);
    DW1000Ranging.attachNewDevice(newDevice);
    DW1000Ranging.attachInactiveDevice(inactiveDevice);   

    DW1000Ranging.attachModeSwitchRequested(ModeSwitchRequested);
    DW1000Ranging.attachDataRequested(DataRequested);
    
}

uint8_t getOwnShortAddress() {
    byte* sa = DW1000Ranging.getCurrentShortAddress();
    //return ((uint16_t)sa[0] << 8) | sa[1];
    return sa[0];
}



/*DEVICE DISCOVERY*/

void newDevice(DW1000Device *device){

    Serial.print("New Device: [");
    Serial.print(device->getShortAddressHeader(), HEX);
    Serial.print("] \tType --> ");
    uint8_t board_type = device->getBoardType();
    switch(board_type){
        case 1:
            Serial.println("Master anchor");
            break;
        case 2:
            Serial.println("Slave Anchor");
            break;
        case 3: 
            Serial.println("Tag");
            break;

        default:
            Serial.print(board_type);
            Serial.println(" Not Known");
            break;

    }
    
    registerDevice(device);
}

void registerDevice(DW1000Device *device){

    if (amount_devices >= MAX_DEVICES) {
        if (DEBUG_SLAVE) {
            Serial.println("-------------------------------------------------------------"); 
            Serial.println("     ERROR: Exceeded MAX_DEVICES limit. Device not added.    "); 
            Serial.println("-------------------------------------------------------------");
        }
        return; 
    }

    Existing_devices[amount_devices].short_addr = device->getShortAddressHeader();
    memcpy(Existing_devices[amount_devices].byte_short_addr, device->getByteShortAddress(), 2);
    uint8_t board_type = device->getBoardType();
    Existing_devices[amount_devices].active = true;
    
    amount_devices ++;
}

void inactiveDevice(DW1000Device *device){

    uint8_t destiny_short_addr = device->getShortAddressHeader();
    Serial.print("Lost connection with device: ");
    Serial.println(destiny_short_addr, HEX);
    
}



/*RANGES & LOGGING*/

void newRange(){

    if(!slave_ranging){return;}

    uint8_t destiny_short_addr = DW1000Ranging.getDistantDevice()->getShortAddressHeader();
    
    if(!broadcast_ranging){ //Range received in unicast mode. Need to check if its from the active polling device

        if(Existing_devices[active_polling_device_index].short_addr != destiny_short_addr){
            if(DEBUG_SLAVE){
                Serial.print("Received a range from an unexpected device: ");
                Serial.print(destiny_short_addr, HEX);
                Serial.println(". Ignoring it.");
            }
            return;
        }
        else if(Existing_devices[active_polling_device_index].short_addr == destiny_short_addr){
            Existing_devices[active_polling_device_index].range_pending = false;
            state = UNICAST_RANGING;
        }
    }

    float dist = DW1000Ranging.getDistantDevice()->getRange();
    float rx_pwr = DW1000Ranging.getDistantDevice()->getRXPower();

    logMeasure(own_short_addr,destiny_short_addr, dist, rx_pwr);

    if(DEBUG_SLAVE){
        Serial.print("From: ");
        Serial.print(destiny_short_addr,HEX);
        Serial.print("\t Distance: ");
        Serial.print(DW1000Ranging.getDistantDevice()->getRange());
        Serial.print(" m");
        Serial.print("\t RX power: ");
        Serial.println(DW1000Ranging.getDistantDevice()->getRXPower());
    }
}

void logMeasure(uint8_t own_sa,uint8_t dest_sa, float dist, float rx_pwr){

    // Firstly, checks if that communication has been logged before
    int index = searchDevice(own_sa,dest_sa);
    
    if(dist < 0){ dist = -dist;} //If the distance is <0, makes it >0

    if (index != -1){ // This means: it was found.

        // Only updates distance and rxPower.
        measurements[index].distance = dist; 
        measurements[index].rxPower = rx_pwr; 
        measurements[index].active = true;

    }
    else if (amount_measurements < MAX_DEVICES){

        // If not found, i need to make a new entry to the struct.
        measurements[amount_measurements].short_addr_origin = own_sa;
        measurements[amount_measurements].short_addr_dest = dest_sa;
        measurements[amount_measurements].distance = dist;
        measurements[amount_measurements].rxPower = rx_pwr;
        measurements[amount_measurements].active = true;
        amount_measurements ++; // And increase the devices number in 1.
        
    }
    else{
        Serial.println("Devices list is full");
    }
}

int searchDevice(uint8_t own_sa,uint8_t dest_sa){
    
    for (int i=0 ; i < amount_measurements ; i++){

        if ((measurements[i].short_addr_origin == own_sa)&&(measurements[i].short_addr_dest == dest_sa)) {
            return i; 
            // If found, returns the index
        }
    }  
    return -1; // if not, returns -1
}



/*MODE SWITCHING*/

void ModeSwitchRequested(byte* short_addr_requester, bool to_initiator, bool _broadcast_ranging){

    DW1000Device* requester = DW1000Ranging.searchDistantDevice(short_addr_requester);
    short_addr_master = short_addr_requester;
    if(to_initiator == true){ //Asked to change to initiator
        
        if(is_initiator) {
            if(DEBUG_SLAVE) Serial.println("Already initiator. Only needs to send the Ack");
        }

        else{
            state = SWITCH_TO_INITIATOR;
            broadcast_ranging = _broadcast_ranging;
        }

    
        if(requester){ 
            if(DEBUG_SLAVE){

                Serial.print("Sending switch ACK (to Initiator) via Unicast to -->");
                Serial.println(requester->getShortAddressHeader(),HEX);

            }
            DW1000Ranging.transmitModeSwitchAck(requester, true);
        }
        else{
            if(DEBUG_SLAVE) Serial.println("Requester not found. Sending ACK (to Initiator) via broadcast.");
            DW1000Ranging.transmitModeSwitchAck(nullptr, true);
        }
 
    }

    else{
       //If requested mode switch to responder.

        if(!is_initiator){
            if(DEBUG_SLAVE) Serial.println("Already responder. Only needs to send the Ack");
        }

        else{
            state = SWITCH_TO_RESPONDER;
        }


        if(requester){
            if(DEBUG_SLAVE){
                Serial.print("Sending switch ACK (to responder) via Unicast to -->");
                Serial.println(requester->getShortAddressHeader(),HEX);
            }
            DW1000Ranging.transmitModeSwitchAck(requester, false);
        }
        
        else{
            if(DEBUG_SLAVE) Serial.println("Requester not found. Sending ACK (to Responder) via broadcast.");
            DW1000Ranging.transmitModeSwitchAck(nullptr, false);
        }


    }
}

void switchToResponder(){

    if(DEBUG_SLAVE){Serial.println("Switching to RESPONDER");}
    
    //Reset all the "ranging flags". Next initiator period, the slave restarts the process.
    for(int i=0; i<amount_devices; i++){
        Existing_devices[i].range_pending = false;
    }
    slave_ranging = false;
    unicast_ranging = false;
    ranging_broadcast_started = false;

    is_initiator = false;
    DW1000.idle();
    DW1000Ranging.startAsResponder(DEVICE_ADDR, DW1000.MODE_1, false, SLAVE);
    attachCallbacks();
    state = IDLE;
}

void switchToInitiator(){

    if(DEBUG_SLAVE) {Serial.println("Switching to INITIATOR");}
    
    is_initiator = true;
    
        
    DW1000.idle();
    DW1000Ranging.startAsInitiator(DEVICE_ADDR, DW1000.MODE_1, false, SLAVE);
    DW1000.setAntennaDelay(Adelay);
    attachCallbacks();
}


/*DATA REPORTS*/

void DataRequested(byte* short_addr_requester){
    
    uint8_t num_measures = amount_measurements;

    if (DEBUG_SLAVE) {
        Serial.print("Data report requested from: ");
        Serial.println(((uint16_t)short_addr_requester[0] << 8) | short_addr_requester[1], HEX);
    }
    
    DW1000Device* requester = DW1000Ranging.searchDistantDevice(short_addr_requester);

    DW1000.idle();
    if(requester){

        if (DEBUG_SLAVE) {
            Serial.print("Data report sent to: ");
            Serial.print(requester->getShortAddressHeader(), HEX);
            Serial.print("\t Sent ");
            Serial.print(num_measures);
            Serial.println(" measures");
        }
        DW1000Ranging.transmitDataReport((Measurement*)measurements, num_measures, requester);
    }

    else{

        if(DEBUG_SLAVE){
            Serial.print("Requester not found. Sent data report via broadcast.");
            Serial.print("\t Sent ");
            Serial.print(num_measures);
            Serial.println(" measures");
        } 
              
        DW1000Ranging.transmitDataReport((Measurement*)measurements, num_measures, nullptr);
    }
        
    clearMeasures();

}

void clearMeasures(){
    for(int i=0;i <amount_measurements;i++){
        measurements[i].active = false;
    }
}


/*MESSAGE TRANSMISSION VIA UNICAST*/

void transmitUnicast(uint8_t message_type){

    if(message_type == MSG_POLL_UNICAST){

        DW1000Device* target = DW1000Ranging.searchDeviceByShortAddHeader(Existing_devices[active_polling_device_index].short_addr);

        if(target){

            if(DEBUG_SLAVE){
                    Serial.print("Ranging poll transmitted to: [");
                    Serial.print(Existing_devices[active_polling_device_index].short_addr,HEX);
                    Serial.println("] via unicast");
                }

            DW1000Ranging.transmitPoll(target);
            
        }

        else{
            if(DEBUG_SLAVE){Serial.println("Target Not found. Ranging poll via unicast not sent");}
            Existing_devices[active_polling_device_index].range_pending = false;
            waiting_unicast_range = false; // To restart the timer next time state is state = WAIT_UNICAST_RANGE.
            state = UNICAST_RANGING; 
        }

    }

    else if(message_type == MSG_SWITCH_TO_RESPONDER){

        DW1000Device* master = DW1000Ranging.searchDistantDevice(short_addr_master);

        if(master){
            if(DEBUG_SLAVE){
                    Serial.print("Mode switch to RESPONDER ACK transmitted via Unicast to: [");
                    Serial.print(master->getShortAddressHeader(),HEX);
                    Serial.println("]");
                }

            DW1000Ranging.transmitModeSwitchAck(master, false);
            
        }

        else{
            if(DEBUG_SLAVE){Serial.println("Master not found. Mode switch to RESPONDER ACK transmitted via broadcast.");}
            DW1000Ranging.transmitModeSwitchAck(nullptr, false);
        }
    }
    
}

void retryTransmission(uint8_t message_type){

    transmitUnicast(message_type);
    last_retry = current_time;
    num_retries = num_retries +1;

    if(num_retries == 5){

        num_retries = 0;
                                                 

        if(message_type == MSG_POLL_UNICAST){
            
            if(DEBUG_SLAVE){
                Serial.print("Poll via unicast with [");
                Serial.print(Existing_devices[active_polling_device_index].short_addr,HEX); Serial.println("] FAILED. Moving on to next device. Back to unicast ranging");
                
            }
            waiting_unicast_range = false; // To restart the timer next time state is state = WAIT_UNICAST_RANGE.
            Existing_devices[active_polling_device_index].range_pending = false;
            state = UNICAST_RANGING;
        }
        
    
    }
}



/*LOOP*/

void loop(){

    DW1000Ranging.loop();
    current_time = millis();

    if(slave_ranging && current_time - slave_ranging_start >= ranging_timeout){

        if(DEBUG_SLAVE){Serial.println("Ranging timeout. Forcing slave back to responder.");}
        slave_ranging = false;
        state = SWITCH_TO_RESPONDER;

    }
    
    if(state == IDLE){
        // Simply wait

    }

    else if(state == SWITCH_TO_INITIATOR){
        switchToInitiator();
    }

    else if(state == SWITCH_TO_RESPONDER){
        switchToResponder();
    }

    else if(state == DISCOVERY){

        if(discovery_previously_done){
            discovery_attempts++;
            
            if(discovery_attempts < 5){
                state = SLAVE_RANGING;
                return;
            }
            

            if(DEBUG_SLAVE){Serial.println("Repeating discovery to update devices list");}
            discovery_attempts = 0;
            discovering = false;
            discovery_previously_done = false;

            
        }
        if(!discovering){
            DW1000Ranging.setRangingMode(DW1000RangingClass::BROADCAST);
            discovering = true; // To avoid entering here again next loop.
            
            discovery_start = current_time;
        }

        if(current_time - discovery_start >= discovery_period){

            discovering = false;
            discovery_previously_done = true;
            state = SLAVE_RANGING;
            if(DEBUG_SLAVE){Serial.println("Discovery ended. Now --> Slave Ranging");}
        }

    }

    else if(state == SLAVE_RANGING){ 

        if(!slave_ranging){
            slave_ranging = true;
            slave_ranging_start = current_time;
            
            if(DEBUG_SLAVE){Serial.print("Slave starts ranging");}
        }

        if(broadcast_ranging == true){
            if(DEBUG_SLAVE){Serial.println(" in BROADCAST mode");}
            state = BROADCAST_RANGING_STATE;
        }

        else if(broadcast_ranging == false){
            if(DEBUG_SLAVE){Serial.println(" in UNICAST mode");}
            state = UNICAST_RANGING;
        }
    }

    else if(state == BROADCAST_RANGING_STATE){

        if(!ranging_broadcast_started){
            ranging_broadcast_started = true;
            DW1000Ranging.setRangingMode(DW1000RangingClass::BROADCAST);
            broadcast_ranging_start = current_time;
        }
    }

    else if(state == UNICAST_RANGING){

        if(!unicast_ranging){
            unicast_ranging = true;
            DW1000Ranging.setRangingMode(DW1000RangingClass::UNICAST);
            unicast_ranging_start = current_time;
            active_polling_device_index = -1;
        }
        active_polling_device_index++;

        if(active_polling_device_index < amount_devices){
            
            if(Existing_devices[active_polling_device_index].active){
                Existing_devices[active_polling_device_index].range_pending = true;

                if(DEBUG_SLAVE){
                    Serial.print("Polling via unicast with: [");
                    Serial.print(Existing_devices[active_polling_device_index].short_addr, HEX);
                    Serial.println("]");
                }
                transmitUnicast(MSG_POLL_UNICAST);
                state = WAIT_UNICAST_RANGE;
            }

            else{ //If device not active

                if(DEBUG_SLAVE){
                    Serial.print("Skipped Polling with an inactive device: ");
                    Serial.println(Existing_devices[active_polling_device_index].short_addr,HEX);
                }

            }
        }

        else{ 
            //All known devices have been polled via unicast.
            //Slave switches back to responder and notifies the master that it has finished. 

            if(DEBUG_SLAVE){Serial.println("All known devices have been polled via unicast. Restarting slave ranging cycle.");}
            
            switchToResponder();
            transmitUnicast(MSG_SWITCH_TO_RESPONDER); 

        }

    }

    else if(state == WAIT_UNICAST_RANGE){

        if(!waiting_unicast_range){
            waiting_unicast_range = true;
            waiting_unicast_range_start = current_time;
            if(DEBUG_SLAVE){
                Serial.print("Waiting for unicast range from: ");
                Serial.println(Existing_devices[active_polling_device_index].short_addr,HEX);
            }
        }

        else{
            if(current_time - waiting_unicast_range_start >= waiting_time){
                waiting_unicast_range_start = current_time;
                retryTransmission(MSG_POLL_UNICAST);
                if(DEBUG_SLAVE){Serial.println("Retrying unicast range transmission.");}
            }

        }
    }
}
