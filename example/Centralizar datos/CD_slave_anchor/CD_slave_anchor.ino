/* CENTRALIZE DATA IN AN ANCHOR */

/* This is the Anchor(s) code. One of them must be declared as the master, and the rest must be slaves.
Don't forget to change the DEVICE_ADDR on each anchor used */

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
// Nomenclature: A for Anchors, B for Tags
#define DEVICE_ADDR "A2:00:5B:D5:A9:9A:E2:9C" 

uint16_t own_short_addr = 0; //I'll get it during the setup.
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


unsigned long last_ranging_started  =0;


static bool stop_ranging_requested = false;
static bool ranging_ended = false;
static bool seen_first_range = false;

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

    last_switch = millis();
    last_report = millis();

    if (IS_MASTER){

        
    }
    else{

        //Callbacks for the slave anchors:
        
        //1: They must respond to a change request message (Sent by the master)        
        DW1000Ranging.attachModeSwitchRequested(ModeSwitchRequested);
       
        //2: Must answer to a data request message (also sent by master)
        DW1000Ranging.attachDataRequested(DataRequested);

        DW1000Ranging.attachStopRangingRequested(stopRangingRequested);

        //Finally, slaves are started as responders:
        DW1000Ranging.startAsResponder(DEVICE_ADDR,DW1000.MODE_1, false,SLAVE_ANCHOR);
        
    } 

    own_short_addr = getOwnShortAddress();
    // I save the own_short_addr after the device has been set up propperly
}

uint16_t getOwnShortAddress() {
    byte* sa = DW1000Ranging.getCurrentShortAddress();
    return ((uint16_t)sa[0] << 8) | sa[1];
}

int searchDevice(uint16_t own_sa,uint16_t dest_sa){
    
    for (int i=0 ; i < amountDevices ; i++){

        if ((measurements[i].short_addr_origin == own_sa)&&(measurements[i].short_addr_dest == dest_sa)) {
            return i; 
            // If found, returns the index
        }
    }  
    return -1; // if not, returns -1
}

void logMeasure(uint16_t own_sa,uint16_t dest_sa, float dist, float rx_pwr){

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
    // Called when the master sends the slave a data request.
    // The slave answers by sending the data report.
    // Adds debug and ensures a clean TX context before replying.
    Serial.println("Data report pedido");
    uint16_t numMeasures = amountDevices;

    if (DEBUG) {
        Serial.print("DATA REQUEST recibido de: ");
        Serial.print(((uint16_t)short_addr_requester[0] << 8) | short_addr_requester[1], HEX);
        Serial.print(" | Medidas: ");
        Serial.println(numMeasures);
    }

    // Try to unicast back to the requester if known
    DW1000Device* requester = DW1000Ranging.searchDistantDevice(short_addr_requester);

    // Ensure radio is idle before sending a custom frame
    DW1000.idle();

    if(!requester){
        // Requester not found locally → broadcast the report
        if (DEBUG) Serial.println("Enviando DATA_REPORT por broadcast (requester no encontrado)");
        DW1000Ranging.transmitDataReport((Measurement*)measurements, numMeasures, nullptr);
        return;
    }

    if (DEBUG) {
        Serial.print("Enviando DATA_REPORT a: ");
        Serial.println(requester->getShortAddress(), HEX);
    }
    // If it is found, sends the report via unicast
    DW1000Ranging.transmitDataReport((Measurement*)measurements, numMeasures, requester);
    clearMeasures();
    Serial.println("lo envío y rehabilito el ranging");
    DW1000Ranging.setStopRanging(false);
    
    stop_ranging_requested = false;
}

void ModeSwitchRequested(byte* short_addr_requester, bool toInitiator){

    DW1000Device* requester = DW1000Ranging.searchDistantDevice(short_addr_requester);

    if(toInitiator == true){

        Serial.println("Pasando a INITIATOR");
        DW1000.idle();
        DW1000Ranging.setStopRanging(false);
        Serial.println("activo el ranging");
        stop_ranging_requested = false;
        // Preserve board type on role switch
        DW1000Ranging.startAsInitiator(DEVICE_ADDR, DW1000.MODE_1, false, SLAVE_ANCHOR);
        if(requester){ DW1000Ranging.transmitModeSwitchAck(requester,toInitiator);}
       
    }
    else{

        DW1000.idle();
        
        // Preserve board type on role switch
        DW1000Ranging.startAsResponder(DEVICE_ADDR, DW1000.MODE_1, false, SLAVE_ANCHOR);
        if(requester){ DW1000Ranging.transmitModeSwitchAck(requester,toInitiator);}
    }
} 

void stopRangingRequested(byte* short_addr_requester){

    Serial.println("Stop ranging request recibido");
    short_addr_master = short_addr_requester;
    DW1000Device* requester = DW1000Ranging.searchDistantDevice(short_addr_requester);
    DW1000Ranging.setStopRanging(true);
    stop_ranging_requested = true;
    
    
}



void newRange(){

    uint16_t dest_sa = DW1000Ranging.getDistantDevice()->getShortAddress();
    float dist = DW1000Ranging.getDistantDevice()->getRange();
    float rx_pwr = DW1000Ranging.getDistantDevice()->getRXPower();

    logMeasure(own_short_addr,dest_sa, dist, rx_pwr);

    if(stop_ranging_requested){

        DW1000Device* requester = DW1000Ranging.searchDistantDevice(short_addr_master);
        if(DEBUG){Serial.println("El ranging ha terminado");}
        
        if(requester){
            DW1000Ranging.transmitStopRangingAck(requester);
        }
        else{
            DW1000Ranging.transmitStopRangingAck(nullptr);
        }

        //state = SWITCH_SLAVE;
        
        
    }
    else{
        ranging_ended = false;
    }
    if(!seen_first_range){
        seen_first_range = true;
        last_ranging_started = current_time;
    }

}

void newDevice(DW1000Device *device){

    Serial.print("New Device: ");
    Serial.println(device->getShortAddress(), HEX);
}

void inactiveDevice(DW1000Device *device){

    uint16_t dest_sa = device->getShortAddress();
    Serial.print("Lost connection with device: ");
    Serial.println(dest_sa, HEX);
    
    //int index = searchDevice(own_short_addr,sa);
    //if (index != 0){ measurements[index].active = false;}
}

void loop(){

    DW1000Ranging.loop();
       

}
