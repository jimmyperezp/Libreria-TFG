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
#define DEVICE_ADDR "A1:00:5B:D5:A9:9A:E2:9C" 

uint16_t own_short_addr = 0; //I'll get it during the setup.
uint16_t Adelay = 16580;
#define IS_MASTER true
//#define IS_MASTER false

// Variables & constants to register the incoming ranges
#define MAX_DEVICES 5
Measurement measurements[MAX_DEVICES];
Existing_devices ExistingDevices[MAX_DEVICES];
int amountDevices = 0;

// Time, mode switch and data report management: 
unsigned long current_time = 0; 
unsigned long last_switch = 0;
unsigned long last_report = 0;

const unsigned long switch_time = 5000; //Switch the slaves' mode every 10 secs
const unsigned long report_time = 12000; // Ask for a data report every 40 secs

// Current mode management. Used to call the switch mode function.
static bool slaveIsInitiator = false;
static bool report_pending = false;

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

        //Master's callbacks: 
        // For when the slaves send a data report:
        DW1000Ranging.attachDataReport(DataReport);

        DW1000Ranging.startAsInitiator(DEVICE_ADDR,DW1000.MODE_LONGDATA_RANGE_LOWPOWER, false,MASTER_ANCHOR);

        // This means that the anchor is in charge of starting the comunication (polling)
    }
    else{

        //Callbacks for the slave anchors:
        
        //1: They must respond to a change request message (Sent by the master)        
        DW1000Ranging.attachModeChangeRequest(ModeChangeRequest);
       
        //2: Must answer to a data request message (also sent by master)
        DW1000Ranging.attachDataRequest(DataRequest);

        //Finally, slaves are started as responders:
        DW1000Ranging.startAsResponder(DEVICE_ADDR,DW1000.MODE_LONGDATA_RANGE_LOWPOWER, false,SLAVE_ANCHOR);
        
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


void DataReport(byte* data){

    uint16_t index = SHORT_MAC_LEN + 1;

    uint16_t origin_short_addr = ((uint16_t)data[index] << 8) | data[index + 1];
    index += 2;

    uint16_t numMeasures = data[index++];

    //First, I check if the size is OK:
    if(numMeasures*10>LEN_DATA-SHORT_MAC_LEN-4){
        
        //Each measure is 10 bytes
        // The header includes short_mac_len + 2 bytes for shortAddress + 1 byte for messageType + 1 byte for numMeasures.

        Serial.println("The Data received is too long");
        return;
    }

    for (int i = 0; i < numMeasures; i++) {

        uint16_t destiny_short_addr = ((uint16_t)data[index] << 8) | data[index + 1];
        index += 2;

        float distance, rxPower;
        memcpy(&distance, data + index, 4); index += 4;
        memcpy(&rxPower,   data + index, 4); index += 4;

        logMeasure(origin_short_addr, destiny_short_addr, distance, rxPower);
    }

    showData();
}

void DataRequest(byte* short_addr_requester){

    // Called when the master sends the slave a data request.
    // The slave answers by sending the data report:
    
    uint16_t numMeasures = amountDevices;

    DW1000Device* requester = DW1000Ranging.searchDistantDevice(short_addr_requester);


    if(!requester){
        //In case the requester is not found, sends the data report via broadcast:
        DW1000Ranging.transmitDataReport((Measurement*)measurements,numMeasures,nullptr);
        return;
    }
    //If it is found, sends the report via unicast
    
    DW1000Ranging.transmitDataReport((Measurement*)measurements,numMeasures,requester);

}

void ModeChangeRequest(bool toInitiator){

    if(toInitiator == true){

        DW1000.idle();
       
        DW1000Ranging.startAsInitiator(DEVICE_ADDR,DW1000.MODE_LONGDATA_RANGE_LOWPOWER, false);
       
    }
    else{

        DW1000.idle();
        
        DW1000Ranging.startAsResponder(DEVICE_ADDR,DW1000.MODE_LONGDATA_RANGE_LOWPOWER, false);
        
    }
} 

void showData(){

    Serial.println("--------- NUEVA MEDIDA ---------");
    
    for (int i = 0; i < amountDevices ; i++){ 
        
            Serial.print(" Dispositivos: ");
            Serial.print(measurements[i].short_addr_origin,HEX);
            Serial.print(" -> ");
            Serial.print(measurements[i].short_addr_dest,HEX);
            Serial.print("\t Distancia: ");
            Serial.print(measurements[i].distance);
            Serial.print(" m \t RX power: ");
            Serial.print(measurements[i].rxPower);
            Serial.println(" dBm");
        
    }
    Serial.println("--------------------------------");
    report_pending = false;
}


void newRange(){

    uint16_t dest_sa = DW1000Ranging.getDistantDevice()->getShortAddress();
    float dist = DW1000Ranging.getDistantDevice()->getRange();
    float rx_pwr = DW1000Ranging.getDistantDevice()->getRXPower();

    logMeasure(own_short_addr,dest_sa, dist, rx_pwr);

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
    current_time = millis();

        if (IS_MASTER){

            if(!report_pending && (current_time - last_switch >= switch_time)){

                last_switch = current_time;
                slaveIsInitiator = !slaveIsInitiator;
                
                Serial.print("CAMBIO A ");
                Serial.println(slaveIsInitiator ? "INITIATOR" : "RESPONDER");

                DW1000Ranging.transmitModeSwitch(slaveIsInitiator);

                //Only 1 parameter: a boolean to indicate which mode I want to switch to:
                // true = toInitiator

                // 2nd parameter is the target device. 
                // If null --> Broadcast (to all devices listening)
                // If != 0, message is sent to the specified device. 
                
                
            }

            
            if (!slaveIsInitiator && (current_time - last_report >= report_time)){
                
                Serial.println("DATA REQUEST ENVIADO");
                DW1000Ranging.transmitDataRequest();
                //No device as parameter --> Broadcast
                report_pending = true;

                
                last_report = current_time;
            }
    }

}