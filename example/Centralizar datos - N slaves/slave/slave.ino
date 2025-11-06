/* CENTRALIZE DATA IN AN ANCHOR */

/* Anchor's code. If used on more than 1 device, user should change the shortAddress. */

#include <SPI.h>
#include "DW1000Ranging.h"
#include "DW1000.h"

//Board's pins definitions:
#define SPI_SCK 18
#define SPI_MISO 19
#define SPI_MOSI 23
#define DW_CS 4

const uint8_t PIN_RST = 27; // reset pin
const uint8_t PIN_IRQ = 34; // irq pin
const uint8_t PIN_SS = 4;   // spi select pin

// Devices' own definitions:
// Nomenclature: A for master, B for slave, C for tags
#define DEVICE_ADDR "B1:00:5B:D5:A9:9A:E2:9C" 

uint8_t own_short_addr = 0; //I'll get it during the setup.
uint16_t Adelay = 16580;

#define IS_MASTER false
#define DEBUG true

// Variables & constants to register the incoming ranges
#define MAX_DEVICES 5
Measurement measurements[MAX_DEVICES];
int amountDevices = 0;

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


byte* short_addr_master;

// CODE:
void setup(){

    Serial.begin(115200);
    delay(1000); // 1 sec to launch the serial monitor

    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI); // SPI bus start
    DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); // DW1000 Start

    DW1000.setAntennaDelay(Adelay);

    // Callbacks "enabled" 
    DW1000Ranging.attachNewRange(newRange);
    DW1000Ranging.attachNewDevice(newDevice);
    DW1000Ranging.attachInactiveDevice(inactiveDevice);   

    DW1000Ranging.attachModeSwitchRequested(ModeSwitchRequested);
    DW1000Ranging.attachDataRequested(DataRequested);
    DW1000Ranging.attachStopRangingRequested(stopRangingRequested);

    DW1000Ranging.startAsResponder(DEVICE_ADDR,DW1000.MODE_1, false,SLAVE_ANCHOR);

    own_short_addr = getOwnShortAddress();
    // I save the own_short_addr after the device has been set up propperly
}

uint8_t getOwnShortAddress() {
    byte* sa = DW1000Ranging.getCurrentShortAddress();
    //return ((uint16_t)sa[0] << 8) | sa[1];
    return sa[0];
}

int searchDevice(uint8_t own_sa,uint8_t dest_sa){
    
    for (int i=0 ; i < amountDevices ; i++){

        if ((measurements[i].short_addr_origin == own_sa)&&(measurements[i].short_addr_dest == dest_sa)) {
            return i; 
            // If found, returns the index
        }
    }  
    return -1; // if not, returns -1
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
    else if (amountDevices < MAX_DEVICES){

        // If not found, i need to make a new entry to the struct.
        measurements[amountDevices].short_addr_origin = own_sa;
        measurements[amountDevices].short_addr_dest = dest_sa;
        measurements[amountDevices].distance = dist;
        measurements[amountDevices].rxPower = rx_pwr;
        measurements[amountDevices].active = true;
        amountDevices ++; // And increase the devices number in 1.
        
    }
    else{
        Serial.println("Devices list is full");
    }
}

void clearMeasures(){

    for(int i=0;i <amountDevices;i++){
        measurements[i].active = false;
    }

}



void DataRequested(byte* short_addr_requester){
    
    
    uint8_t num_measures = amountDevices;

    if (DEBUG) {
        Serial.print("Data report requested from: ");
        Serial.println(((uint16_t)short_addr_requester[0] << 8) | short_addr_requester[1], HEX);
    }

    
    DW1000Device* requester = DW1000Ranging.searchDistantDevice(short_addr_requester);

    
    DW1000.idle();

    if(requester){

        if (DEBUG) {
            Serial.print("Data report sent to: ");
            Serial.print(requester->getShortAddressHeader(), HEX);
            Serial.print("\t Sent ");
            Serial.print(num_measures);
            Serial.println(" measures");
        }
        DW1000Ranging.transmitDataReport((Measurement*)measurements, num_measures, requester);
    }

    else{

        if(DEBUG){
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

    if(to_initiator == true){
        
        switchToInitiator();

        if(requester){ 
            
            if(DEBUG){
                Serial.print("Sending switch ACK via Unicast to -->");
                Serial.println(requester->getShortAddressHeader(),HEX);
            }
            DW1000Ranging.transmitModeSwitchAck(requester,to_initiator);
        }
        else{

            if(DEBUG){
                Serial.println("Requester not found. Sending ACK via broadcast."); 
            }
            DW1000Ranging.transmitModeSwitchAck(nullptr,to_initiator);
        }
    }
    else{

        switchToResponder();

        if(requester){ 
            
            if(DEBUG){
                Serial.print("Sending switch ACK via Unicast to-->");
                Serial.println(requester->getShortAddressHeader(),HEX);
            }

            DW1000Ranging.transmitModeSwitchAck(requester,to_initiator);

        }
        else{
            if(DEBUG){
                Serial.println("Requester not found. Sending ACK via broadcast.");
                DW1000Ranging.transmitModeSwitchAck(nullptr,to_initiator);
            }
        }
    }
} 

void switchToResponder(){

    if(DEBUG){Serial.println("Switching to RESPONDER");}
    //DW1000.idle();
    is_initiator = false;
    DW1000Ranging.startAsResponder(DEVICE_ADDR, DW1000.MODE_1, false, SLAVE_ANCHOR);

}

void switchToInitiator(){

    if(DEBUG) {Serial.println("Switching to INITIATOR");}
        
    //DW1000.idle();
    is_initiator = true;
    initiator_start = current_time;
        
    DW1000Ranging.startAsInitiator(DEVICE_ADDR, DW1000.MODE_1, false, SLAVE_ANCHOR);
}

void stopRangingRequested(byte* short_addr_requester){

    Serial.println("Stop ranging request received");
    short_addr_master = short_addr_requester;
    DW1000Device* requester = DW1000Ranging.searchDistantDevice(short_addr_requester);
    DW1000Ranging.setStopRanging(true);
    stop_ranging_requested = true;
    
    
}


void newRange(){

    uint8_t destiny_short_addr = DW1000Ranging.getDistantDevice()->getShortAddressHeader();
    float dist = DW1000Ranging.getDistantDevice()->getRange();
    float rx_pwr = DW1000Ranging.getDistantDevice()->getRXPower();

    logMeasure(own_short_addr,destiny_short_addr, dist, rx_pwr);

    if(stop_ranging_requested){

        if(DEBUG){Serial.println("Ranging has ended");}
        
    }
    else{
        ranging_ended = false;
    }
    if(!seen_first_range){
        seen_first_range = true;
        last_ranging_started = current_time;
    }

    if(DEBUG){
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
    Serial.println(device->getShortAddress(), HEX);
}

void inactiveDevice(DW1000Device *device){

    uint8_t destiny_short_addr = device->getShortAddressHeader();
    Serial.print("Lost connection with device: ");
    Serial.println(destiny_short_addr, HEX);
    
}

void loop(){

    DW1000Ranging.loop();

    if(is_initiator){

        if(current_time - initiator_start >= 5000){

            if(DEBUG){
                Serial.println("Timeout as a initiator. Going back to responder");
            }
            switchToResponder();

        }
    }

}
