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

#define DEBUG false

#define IS_MASTER true
//#define IS_MASTER false

// Address: A for Anchors, B for Tags
#define DEVICE_ADDR "A1:00:5B:D5:A9:9A:E2:9C" 

uint16_t own_short_addr = 0; //I'll get it during the setup.
uint16_t Adelay = 16580;

// To register incoming data reports.
#define MAX_DEVICES 5
Measurement measurements[MAX_DEVICES];
uint8_t amount_measurements = 0;

// To send messages via unicast
ExistingDevice Existing_devices[MAX_DEVICES];
uint8_t amount_devices = 0;
uint8_t amount_slave_anchors = 0;

//Time management
unsigned long current_time = 0; 
unsigned long last_ranging_started = 0;
unsigned long mode_switch_request = 0;
unsigned long last_retry = 0;
unsigned long data_report_request_time = 0;
const unsigned long ranging_period = 250;
const unsigned long mode_switch_ack_timeout = 30;
const unsigned long data_report_ack_timeout = 30;

unsigned long start_initiators_ranging = 0;
static bool initiators_ranging = false;

// Control of the slave's mode.
static bool slaves_are_responders = true;
static bool switchToInitiator = true;

// States to manage the flow control:
uint8_t state = 1;
#define RANGING 1
#define SWITCH_SLAVE 2
#define DATA_REPORT 3

#define MESSAGE_TYPE_MODE_SWITCH 1
#define MESSAGE_TYPE_DATA_REQUEST 2


// Flags to handle the flow control
uint8_t amount_active_slaves = 0;

static bool mode_switch_requested = false;
static bool mode_switch_ack = false;
static bool mode_switch_pending = false;
uint8_t expected_mode_switches = 0;
uint8_t amount_mode_switch_acks_received = 0;

static bool stop_ranging_requested = false;
static bool ranging_ended = false;
static bool seen_first_range = false;

static bool data_report_requested = false;
static bool data_report_received  = false;
uint8_t expected_data_reports =0;
uint8_t amount_data_reports_received = 0;

uint8_t num_retries = 0;

// CODE:
void setup(){

    Serial.begin(115200);
    delay(1000); // 1 sec to launch the serial monitor

    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI); // SPI bus start
    DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); // DW1000 Start

    DW1000.setAntennaDelay(Adelay);
    DW1000Ranging.setResetPeriod(5000);
    // Callbacks "enabled" 
    DW1000Ranging.attachNewRange(newRange);
    DW1000Ranging.attachNewDevice(newDevice);
    DW1000Ranging.attachInactiveDevice(inactiveDevice);   

    last_ranging_started = millis();
    state = RANGING;

    if (IS_MASTER){

        //Master's callbacks: 
        // For when the slaves send a data report:
        DW1000Ranging.attachDataReport(DataReport);
        DW1000Ranging.attachModeSwitchAck(ModeSwitchAck);

        DW1000Ranging.startAsInitiator(DEVICE_ADDR,DW1000.MODE_1, false,MASTER_ANCHOR);

        // This means that the anchor is in charge of starting the comunication (polling)
    }
    else{

        //Callbacks for the slave anchors:
        
        //1: They must respond to a change request message (Sent by the master)        
        DW1000Ranging.attachModeSwitchRequested(ModeSwitchRequested);
       
        //2: Must answer to a data request message (also sent by master)
        DW1000Ranging.attachDataRequested(DataRequested);

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
    
    for (int i=0 ; i < amount_measurements ; i++){

        if ((measurements[i].short_addr_origin == own_sa)&&(measurements[i].short_addr_dest == dest_sa)) {
            return i; 
            // If found, returns the index
        }
    }
    return -1; // if not, returns -1
}

void registerDevice(DW1000Device *device){


    Existing_devices[amount_devices].short_addr = device->getShortAddress();
    memcpy(Existing_devices[amount_devices].byte_short_addr, device->getByteShortAddress(), 2);
    uint8_t board_type = device->getBoardType();

    if(board_type == SLAVE_ANCHOR){
        Existing_devices[amount_devices].is_slave_anchor = true;
        amount_slave_anchors ++;
    }
    else{ Existing_devices[amount_devices].is_slave_anchor = false;}

    Existing_devices[amount_devices].is_responder = true;
    Existing_devices[amount_devices].active = true;
    Existing_devices[amount_devices].fail_count = 0;
    
    amount_devices ++;
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

void clearMeasures(){

    for(int i=0;i <amount_measurements;i++){
        measurements[i].active = false;
    }

}

void waitForResponse(uint16_t waiting_time){

    uint32_t t0 = millis(); 
    if(DEBUG){Serial.println("Esperando para el Ack");}

    
    while((uint32_t)(millis()-t0)<waiting_time){

        DW1000Ranging.loop();
        
    }
    return;

}

void DataReport(byte* data){

    
    uint16_t index = SHORT_MAC_LEN + 1;

    uint16_t origin_short_addr = ((uint16_t)data[index+1] << 8) | data[index];
    
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
    if(DEBUG){
        Serial.print("Data report recibido de: ");
        Serial.print(origin_short_addr,HEX);
    }
    
    
    for(int i = 0; i <amount_devices; i++){

        if(Existing_devices[i].short_addr == origin_short_addr && Existing_devices[i].is_slave_anchor == true){

            
            if(Existing_devices[i].data_report_pending == true){

                Existing_devices[i].data_report_pending = false;
                amount_data_reports_received++;
                
                
            }
        }
    }

    if(amount_data_reports_received == expected_data_reports){
        showData();
        clearMeasures();
        amount_data_reports_received = 0;
        data_report_received = true;
    }
    

}

void DataRequested(byte* short_addr_requester){

    // Called when the master sends the slave a data request.
    // The slave answers by sending the data report:
    
    uint16_t numMeasures = amount_measurements;

    DW1000Device* requester = DW1000Ranging.searchDistantDevice(short_addr_requester);


    if(!requester){
        //In case the requester is not found, sends the data report via broadcast:
        DW1000Ranging.transmitDataReport((Measurement*)measurements,numMeasures,nullptr);
        return;
    }
    //If it is found, sends the report via unicast
    
    DW1000Ranging.transmitDataReport((Measurement*)measurements,numMeasures,requester);

}

void ModeSwitchRequested(byte* short_addr_requester, bool toInitiator){

    DW1000Device* requester = DW1000Ranging.searchDistantDevice(short_addr_requester);
    if(toInitiator == true){

        DW1000.idle();
       
        DW1000Ranging.startAsInitiator(DEVICE_ADDR,DW1000.MODE_1, false);
        if(requester){ DW1000Ranging.transmitModeSwitchAck(requester,toInitiator);}
    }
    else{

        DW1000.idle();
        
        DW1000Ranging.startAsResponder(DEVICE_ADDR,DW1000.MODE_1, false);
        if(requester){ DW1000Ranging.transmitModeSwitchAck(requester,toInitiator);}
    }
} 

void ModeSwitchAck(bool isInitiator){

    if(mode_switch_requested){ 
        // To avoid false reads
        uint16_t origin_short_add = DW1000Ranging.getDistantDevice()->getShortAddress();
        
        for(int i = 0; i <amount_devices; i++){

            if(Existing_devices[i].short_addr == origin_short_add && Existing_devices[i].is_slave_anchor){

                

                if(Existing_devices[i].mode_switch_pending == true){

                    Existing_devices[i].mode_switch_pending = false;
                    amount_mode_switch_acks_received++;
                    
                }

                //Only if it is registered & is a slave anchor
                if(DEBUG){
                    Serial.print(" Cambio Realizado: ");
                    Serial.print(origin_short_add,HEX);
                    Serial.print(" es --> ");
                    Serial.println(isInitiator ? "INITIATOR" : "RESPONDER");
                }

                
                Existing_devices[i].is_responder =  !isInitiator;

                if(amount_mode_switch_acks_received == expected_mode_switches){
                    amount_mode_switch_acks_received = 0;
                    mode_switch_ack = true;
                    
                    slaves_are_responders = !switchToInitiator;
                    if(mode_switch_pending){mode_switch_pending = false;}
                }
                
            }
        }
    }
    
 
}

void showData(){

    Serial.println("--------------------------- NUEVA MEDIDA ---------------------------");
    
    for (int i = 0; i < amount_measurements ; i++){ 
        
        if(measurements[i].active == true){
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
    }
    Serial.println("--------------------------------------------------------------------");
    
}


void newRange(){

    uint16_t dest_sa = DW1000Ranging.getDistantDevice()->getShortAddress();
    float dist = DW1000Ranging.getDistantDevice()->getRange();
    float rx_pwr = DW1000Ranging.getDistantDevice()->getRXPower();

    logMeasure(own_short_addr,dest_sa, dist, rx_pwr);
    

    if(stop_ranging_requested){
        ranging_ended = true;
        if(DEBUG){Serial.println("El ranging ha terminado");}
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

    registerDevice(device);

    
}

void inactiveDevice(DW1000Device *device){

    uint16_t dest_sa = device->getShortAddress();
    Serial.print("Lost connection with device: ");
    Serial.println(dest_sa, HEX);
    
    for (int i = 0; i < amount_devices ; i++){
        
        

        if(dest_sa == Existing_devices[i].short_addr){

            if (Existing_devices[i].mode_switch_pending && state == SWITCH_SLAVE) {

                Existing_devices[i].mode_switch_pending = false;
                if (expected_mode_switches > 0) expected_mode_switches--;
            }
             
            if (Existing_devices[i].data_report_pending && state == DATA_REPORT) {
                 
                Existing_devices[i].data_report_pending = false;
                if (expected_data_reports > 0) expected_data_reports--;
            }


            Existing_devices[i].active = false;
        }    
    }
    
    
}

void activateRanging(){

    state = RANGING;

    DW1000Ranging.setStopRanging(false);
    stop_ranging_requested = false;
    ranging_ended = false;
    last_ranging_started = current_time;

    amount_active_slaves = 0;

    
}


void transmitUnicast(uint8_t message_type){

    //All messages are sent via unicast. 
    //1st, check what devices are slave anchors:
    


    for(int i = 0; i < amount_devices; i++){

        if(Existing_devices[i].is_slave_anchor == true){

            //If it's a slave anchor, I need the device's object:
            DW1000Device* target = DW1000Ranging.searchDistantDevice(Existing_devices[i].byte_short_addr);

            if(target){ //The device was found

                if(message_type == MESSAGE_TYPE_MODE_SWITCH){

                    

                    if(Existing_devices[i].mode_switch_pending == true){
                        
                        
                        //I check the current state of the slave: 
                        if(Existing_devices[i].is_responder == true){

                            //Currently responder --> Switch it to initiator

                            switchToInitiator = true;
                            DW1000.idle();
                            DW1000Ranging.transmitModeSwitch(switchToInitiator,target);
                            
                            waitForResponse(mode_switch_ack_timeout);
                            if(DEBUG){Serial.println("Solicitado el cambio a initiator por UNICAST");}

                        }
                        else if(Existing_devices[i].is_responder == false){
                            //Currently initiator --> Switch it to responder
                            switchToInitiator = false;
                            
                            DW1000.idle();

                            DW1000Ranging.transmitModeSwitch(switchToInitiator,target);
                            
                            waitForResponse(mode_switch_ack_timeout);
                            if(DEBUG){Serial.println("Solicitado el cambio a responder por UNICAST");}
                        }


                    }
                }

                else if(message_type == MESSAGE_TYPE_DATA_REQUEST){

                    if(Existing_devices[i].data_report_pending == true){

                        DW1000.idle();
                        DW1000Ranging.transmitDataRequest(target);
                        
                        waitForResponse(data_report_ack_timeout);
                        if(DEBUG){Serial.println("Data report solicitado por UNICAST");}

                    }
   
                }
            }
        }
    }

}


void retryTransmission(uint8_t message_type){

    transmitUnicast(message_type);
    last_retry = current_time;
    num_retries = num_retries +1;

    if(num_retries == 5){

        num_retries = 0;
        transmissionFailed();                                           

        if(message_type == MESSAGE_TYPE_MODE_SWITCH){
            
            mode_switch_requested = false;
            mode_switch_ack = false;
            mode_switch_pending = false;

            if(DEBUG){Serial.println("Cambio fallido. Regreso a ranging");}

        }
        else if(message_type == MESSAGE_TYPE_DATA_REQUEST){

            data_report_requested = false;
            data_report_received = false;

            if(DEBUG){Serial.println("Data Report NO recibido. Regreso a ranging");}
        }
        
        

        
    }
}


void transmissionFailed(){

    activateRanging();
    
    
}

void loop(){

    DW1000Ranging.loop();
    current_time = millis();

    
    if (IS_MASTER){

            if(state == RANGING){ 

                
                if(seen_first_range && current_time - last_ranging_started >= ranging_period){
                    
                    
                    DW1000Ranging.setStopRanging(true);
                    stop_ranging_requested = true;
                    ranging_ended = true;
                    state = SWITCH_SLAVE;
                    amount_active_slaves = 0;
                    if(DEBUG){Serial.println("Pido que termine el ranging.");}
                }

            }

            else if(state == SWITCH_SLAVE){

                if(ranging_ended){

                    if(!mode_switch_requested && !mode_switch_ack){ 
                        //If not requested yet
                        
                        expected_mode_switches = 0;
                        for(int i = 0; i < amount_devices; i++){

                            if(Existing_devices[i].is_slave_anchor == true && Existing_devices[i].active == true){
                                
                                //First request to all devices
                                Existing_devices[i].mode_switch_pending = true;
                    
                                expected_mode_switches++;
                            }

                        }

                        mode_switch_requested = true;
                        mode_switch_pending = true;
                        mode_switch_request = current_time;
                        mode_switch_ack = false;
                        transmitUnicast(MESSAGE_TYPE_MODE_SWITCH);

                        
                        
                    }

                    //Mode switch requested. Now Waiting for ack:

                    if(mode_switch_requested && mode_switch_ack){
                        
                        mode_switch_requested = false;
                        mode_switch_ack = false;
                        
                        if(slaves_are_responders){ 

                            //Switched back to responder --> Now, data report.
                            
                            state = DATA_REPORT;
                            amount_active_slaves = 0;
                            //TODO
                            /*Pasar solo a data report cuando se han recibido TODOs Los Acks.
                            Además, en el retry, debería ver a qué slaves no le han llegado*/

                            if(DEBUG){Serial.println("Paso a pedir DATA REPORT");}
                        }
                        
                       
                        else{ //Switched to initiator --> Back to ranging

                            initiators_ranging = true;
                            start_initiators_ranging = current_time;
                            activateRanging();
                            
                            if(DEBUG){Serial.println("cambio a initiator. Ahora vuelvo a activar el ranging");}

                        } 
                        
                    }

                }

                if (mode_switch_pending && current_time-mode_switch_request >= 300){

                    //To reatempt the mode switch if it gets lost.
                
                    if(current_time-last_retry >= 100){
                        
                        if(DEBUG){Serial.println("reintentando el mode switch...");}
                        retryTransmission(MESSAGE_TYPE_MODE_SWITCH);

                    }
                }
            
                if(initiators_ranging == true){
                    if(current_time -start_initiators_ranging>500){
                        initiators_ranging = false;
                        activateRanging();
                            
                        if(DEBUG){Serial.println("cambio a initiator. Ahora vuelvo a activar el ranging");}
                    }
                }
            }
                

            else if (state == DATA_REPORT){

                if(!data_report_requested){

                    expected_data_reports = 0;
                    for(int i = 0; i < amount_devices; i++){

                        if(Existing_devices[i].is_slave_anchor == true && Existing_devices[i].active == true){

                            Existing_devices[i].data_report_pending = true;
                            expected_data_reports++;
                        }

                    }
                    data_report_requested = true;
                    data_report_request_time = current_time;
                    transmitUnicast(MESSAGE_TYPE_DATA_REQUEST);
                    
                    
                    
                    
                }

                if(data_report_requested && data_report_received){

                    data_report_requested = false;
                    data_report_received = false;

                    if(DEBUG){Serial.println("Recibido el data report. Regreso a Ranging");}
                    activateRanging();
                    

                    
                }

                if(data_report_requested && !data_report_received && current_time - data_report_request_time > 500){
                    

                    if(current_time-last_retry >=100){ 
                        if(DEBUG){Serial.println("Reintentando data report...");}
                        retryTransmission(MESSAGE_TYPE_DATA_REQUEST);
                                               
                    }
      
                }
            }
    }
 
}
