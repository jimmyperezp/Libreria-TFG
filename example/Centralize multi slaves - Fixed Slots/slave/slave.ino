/* CENTRALIZE DATA IN AN ANCHOR */

/* Anchor's code. If used on more than 1 device, user should change the shortAddress. */

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

// Devices' own definitions:
// Nomenclature: A for master, B for slave, C for tags
#define DEVICE_ADDR "B1:00:5B:D5:A9:9A:E2:9C" 

uint8_t own_short_addr = 0; //I'll get it during the setup.
uint16_t Adelay = 16580;

#define IS_MASTER false
#define DEBUG_SLAVE true

// Variables & constants to register the incoming ranges
#define MAX_DEVICES 5
Measurement measurements[MAX_DEVICES];
int amount_devices = 0;

// Time, mode switch and data report management: 
unsigned long current_time = 0; 
unsigned long last_switch = 0;
unsigned long last_report = 0;
unsigned long initiator_start = 0;
unsigned long last_ranging_started  =0;

static bool stop_ranging_requested = false;
static bool ranging_ended = false;
static bool seen_first_range = false;
static bool is_initiator = false;

volatile bool pending_switch_to_initiator = false;
volatile bool pending_switch_to_responder = false;

uint8_t amount_completed_devices = 0;
#define AMOUNT_VALID_RANGINGS 3
byte* short_addr_master;

// CODE:
void setup(){

    Serial.begin(115200);
    delay(1000); // 1 sec to launch the serial monitor

    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI); // SPI bus start
    DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); // DW1000 Start

    DW1000.setAntennaDelay(Adelay);

    // Callbacks "enabled" 
    attachCallbacks();

    DW1000Ranging.startAsResponder(DEVICE_ADDR,DW1000.MODE_1, false,SLAVE_ANCHOR);

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

int searchDevice(uint8_t own_sa,uint8_t dest_sa){
    
    for (int i=0 ; i < amount_devices ; i++){

        if ((measurements[i].short_addr_origin == own_sa)&&(measurements[i].short_addr_dest == dest_sa)) {
            return i; 
            // If found, returns the index
        }
    }  
    return -1; // if not, returns -1
}

void setCompleteRanging(uint8_t dest_sa, bool is_ranging_done){

    DW1000Device* dev = DW1000Ranging.searchDeviceByShortAddHeader(dest_sa);
    
    if(dev){
        
        dev->setRangingComplete(is_ranging_done);
    }
}

void resetCompleteRanging(){

    for(int i = 0; i<amount_devices;i++){
        uint8_t dest_sa;
        measurements[amount_devices].completed_rangings = 0;
        dest_sa = measurements[amount_devices].short_addr_dest;
        DW1000Device* dev = DW1000Ranging.searchDeviceByShortAddHeader(dest_sa);
        
        if(dev){
            dev->setRangingComplete(false);
        }
        
    }
}

void rangingFinished(){

    switchToResponder();
    DW1000Device* master = DW1000Ranging.searchDistantDevice(short_addr_master);
    if(master){
        DW1000Ranging.transmitRangingFinished(master);
        if(DEBUG_SLAVE){
            Serial.print("Ranging Finished early. Sending finished message to: [");
            Serial.print(master->getShortAddressHeader(),HEX); 
            Serial.println("]. ");
        }
    }

}

void logMeasure(uint8_t own_sa,uint8_t dest_sa, float dist, float rx_pwr){

    // Firstly, checks if that communication has been logged before
    int index = searchDevice(own_sa,dest_sa);
    
    if(dist < 0){ dist = -dist;} //If the distance is <0, makes it >0

    if (index != -1){ // This means: it was found.

        // Only updates distance and rx_power.
        measurements[index].distance = dist; 
        measurements[index].rx_power = rx_pwr; 
        measurements[index].active = true;
        measurements[index].completed_rangings++;
        if(measurements[index].completed_rangings >= AMOUNT_VALID_RANGINGS){
            setCompleteRanging(dest_sa, true);
            amount_completed_devices++;
            if(amount_completed_devices >= amount_devices){
                rangingFinished();
            }
        }

    }
    else if (amount_devices < MAX_DEVICES){

        // If not found, i need to make a new entry to the struct.
        measurements[amount_devices].short_addr_origin = own_sa;
        measurements[amount_devices].short_addr_dest = dest_sa;
        measurements[amount_devices].distance = dist;
        measurements[amount_devices].rx_power = rx_pwr;
        measurements[amount_devices].active = true;
        measurements[amount_devices].completed_rangings = 1;
        amount_devices ++; // And increase the devices number in 1.
        
    }

    else{
        Serial.println("-------------------------------------------------------------");
        Serial.println("                   Devices list is full                      ");
        Serial.println("-------------------------------------------------------------");
        
    }
}

void clearMeasures(){

    for(int i=0;i <amount_devices;i++){
        measurements[i].active = false;
    }

}

void DataRequested(byte* short_addr_requester){
    
    
    uint8_t num_measures = amount_devices;

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

void ModeSwitchRequested(byte* short_addr_requester, bool to_initiator){

    DW1000Device* requester = DW1000Ranging.searchDistantDevice(short_addr_requester);
    short_addr_master = short_addr_requester; // I save the shortAddress from the master that requested me to range. 

    if(to_initiator == true){
        

        if(is_initiator) {
             if(DEBUG_SLAVE) Serial.println("Already initiator. Only needs to send the Ack");
        }

        else pending_switch_to_initiator = true;
        
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

        else pending_switch_to_responder = true;


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
    
    is_initiator = false;
    DW1000.idle();
    DW1000Ranging.startAsResponder(DEVICE_ADDR, DW1000.MODE_1, false, SLAVE_ANCHOR);
    
    attachCallbacks();
}

void switchToInitiator(){

    if(DEBUG_SLAVE) {Serial.println("Switching to INITIATOR");}
        
    
    is_initiator = true;
    initiator_start = current_time;
        
    DW1000.idle();
    DW1000Ranging.startAsInitiator(DEVICE_ADDR, DW1000.MODE_1, false, SLAVE_ANCHOR);
    DW1000.setAntennaDelay(Adelay);
    attachCallbacks();
}

void newRange(){

    if(DW1000Ranging.getDistantDevice()->getRangingComplete() == true) return;

    uint8_t destiny_short_addr = DW1000Ranging.getDistantDevice()->getShortAddressHeader();
    float dist = DW1000Ranging.getDistantDevice()->getRange();
    float rx_pwr = DW1000Ranging.getDistantDevice()->getRXPower();

    logMeasure(own_short_addr,destiny_short_addr, dist, rx_pwr);

    if(stop_ranging_requested){

        if(DEBUG_SLAVE){Serial.println("Ranging has ended");}
        
    }
    else{
        ranging_ended = false;
    }
    if(!seen_first_range){
        seen_first_range = true;
        last_ranging_started = current_time;
    }

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

void newDevice(DW1000Device *device){

    Serial.print("New Device: ");
    Serial.print(device->getShortAddressHeader(), HEX);
    Serial.print("\tType: ");
    Serial.println(device->getBoardType());
    
}

void inactiveDevice(DW1000Device *device){

    uint8_t destiny_short_addr = device->getShortAddressHeader();
    Serial.print("Lost connection with device: ");
    Serial.println(destiny_short_addr, HEX);
    
}

void loop(){

    DW1000Ranging.loop();

    current_time = millis();
    if(is_initiator){

        if(current_time - initiator_start >= 5000){

            if(DEBUG_SLAVE){
                Serial.println("Timeout as a initiator. Going back to responder");
            }
            switchToResponder();

        }
    }

    if(pending_switch_to_responder){
        delay(30); //Gives time for the ack to be sent correctly.
        pending_switch_to_responder = false;
        switchToResponder();
    }

    if(pending_switch_to_initiator){
        delay(30); //Gives time for the ack to be sent correctly.
        pending_switch_to_initiator = false;
        switchToInitiator();
    }

}
