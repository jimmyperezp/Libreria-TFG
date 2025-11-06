#include <SPI.h>
#include "DW1000Ranging.h"
#include "DW1000.h"

#define SPI_SCK 18
#define SPI_MISO 19
#define SPI_MOSI 23
#define DW_CS 4
const uint8_t PIN_RST = 27; // reset pin
const uint8_t PIN_IRQ = 34; // irq pin
const uint8_t PIN_SS = 4;   // spi select pin

#define DEBUG true

#define IS_MASTER true
#define DEVICE_ADDR "A1:00:5B:D5:A9:9A:E2:9C" 
uint8_t own_short_addr = 0; 
uint16_t Adelay = 16580;

#define MAX_DEVICES 5
Measurement measurements[MAX_DEVICES];
uint8_t amount_measurements = 0;

ExistingDevice Existing_devices[MAX_DEVICES];
uint8_t amount_devices = 0;

uint8_t slaves_indexes[MAX_DEVICES];
uint8_t amount_slaves = 0;


/*States used in the FSM*/
uint8_t state = 1;
#define DISCOVERY 1
#define MASTER_RANGING 2
#define INITIATOR_HANDOFF 3
#define WAIT_SWITCH_TO_INITIATOR_ACK 4
#define SLAVE_RANGING 5
#define SWITCH_TO_RESPONDER 6 
#define WAIT_SWITCH_TO_RESPONDER_ACK 7
#define DATA_REPORT 8
#define WAIT_DATA_REPORT 9

/*Time management*/
/*1: "timers"*/
unsigned long current_time = 0; 
unsigned long discovery_start = 0;
unsigned long master_ranging_start = 0;
unsigned long slave_ranging_start = 0;
unsigned long waiting_switch_start = 0;
unsigned long waiting_data_report_start = 0;

/*2: Time constants*/
const unsigned long ranging_period = 2000;
const unsigned long waiting_time = 1000;
const unsigned long retry_time = 500;
const unsigned long timeout = 50;

/*Retry messages management*/
unsigned long last_retry = 0;
uint8_t num_retries = 0;


/*Flags used in activating and stop ranging*/
static bool stop_ranging_requested = false;
static bool seen_first_range = false;

/*Flags used in state = discovering*/
static bool discovering = false;
static bool slaves_discovered = false;
uint8_t amount_active_slaves = 0;

/*Flags used in state = master_ranging*/
static bool master_is_ranging = false;

/*Flags used in state = initiator_handoff*/
static bool initiator_handoff_started = false;
int8_t active_slave_index = 0;

/*Flags used in state = wait_switch_to_(...)*/
static bool waiting_initiator_switch_ack = false;
static bool waiting_responder_switch_ack = false;

/*Flags used in state = slave_ranging*/
static bool slave_is_ranging = false;

/*Flags used in state = data_report*/
static bool data_report_started = false;
int8_t reporting_slave_index = 0;

/*Flags used in state = wait_data_report*/
static bool waiting_data_report = false;

/*Message types used to transmit via unicas*/
uint8_t MSG_SWITCH_TO_INITIATOR = 1;
uint8_t MSG_SWITCH_TO_RESPONDER = 2;
uint8_t MSG_DATA_REQUEST = 3;


void setup(){

    Serial.begin(115200);
    delay(1000);
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI); 
    DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); // DW1000 Start

    DW1000.setAntennaDelay(Adelay);
    DW1000Ranging.setResetPeriod(5000);
    // Callbacks "enabled" 
    DW1000Ranging.attachNewRange(newRange);
    DW1000Ranging.attachNewDevice(newDevice);
    DW1000Ranging.attachInactiveDevice(inactiveDevice);   

    

    DW1000Ranging.attachDataReport(DataReport);
    DW1000Ranging.attachModeSwitchAck(ModeSwitchAck);
    

    DW1000Ranging.startAsInitiator(DEVICE_ADDR,DW1000.MODE_1, false,MASTER_ANCHOR);

    own_short_addr = getOwnShortAddress();

    state = DISCOVERY;
    
}


uint8_t getOwnShortAddress() {
    byte* sa = DW1000Ranging.getCurrentShortAddress();
    //return ((uint16_t)sa[0] << 8) | sa[1];
    return (uint8_t)sa[0];
}


void showData(){

    Serial.println("--------------------------- NEW MEASURE ---------------------------");
    
    for (int i = 0; i < amount_measurements ; i++){ 
        
        if(measurements[i].active == true){
            Serial.print(" Devices: ");
            Serial.print(measurements[i].short_addr_origin,HEX);
            Serial.print(" -> ");
            Serial.print(measurements[i].short_addr_dest,HEX);
            Serial.print("\t Distance: ");
            Serial.print(measurements[i].distance);
            Serial.print(" m \t RX power: ");
            Serial.print(measurements[i].rxPower);
            Serial.println(" dBm");
        }
    }
    Serial.println("--------------------------------------------------------------------");
    
}


void registerDevice(DW1000Device *device){


    Existing_devices[amount_devices].short_addr = device->getShortAddressHeader();
    memcpy(Existing_devices[amount_devices].byte_short_addr, device->getByteShortAddress(), 2);
    uint8_t board_type = device->getBoardType();

    if(board_type == SLAVE_ANCHOR){
        
        Existing_devices[amount_devices].is_slave_anchor = true;
        slaves_indexes[amount_slaves] = amount_devices;
        
        slaves_discovered = true;
        amount_slaves ++;
        amount_active_slaves++;

        if(DEBUG){
            Serial.print("Device ");
            Serial.print(Existing_devices[amount_devices].short_addr,HEX);
            Serial.println(" is a slave.");}
        
    }
    else{ 
        Existing_devices[amount_devices].is_slave_anchor = false;
    }

    Existing_devices[amount_devices].is_responder = true;
    Existing_devices[amount_devices].active = true;
    
    
    amount_devices ++;
}


int searchDevice(uint8_t own_sa,uint8_t dest_sa){
    
    for (int i=0 ; i < amount_measurements ; i++){

        if ((measurements[i].short_addr_origin == own_sa)&&(measurements[i].short_addr_dest == dest_sa) || (measurements[i].short_addr_origin == dest_sa)&&(measurements[i].short_addr_dest == own_sa)  ) {
            return i; 
            // If found, returns the index
        }
    }
    return -1; // if not, returns -1
}

void newDevice(DW1000Device *device){

    Serial.print("New Device: ");
    Serial.println(device->getShortAddressHeader(), HEX);

    registerDevice(device);
}

void inactiveDevice(DW1000Device *device){

    uint8_t origin_short_addr = device->getShortAddressHeader();
    Serial.print("Lost connection with device: ");
    Serial.println(origin_short_addr, HEX);
    
    bool was_a_slave = false;
    for(int i = 0; i < amount_devices; i++){
        if(Existing_devices[i].short_addr == origin_short_addr){

            Existing_devices[i].active = false; // <-- SOLO MARCAR, NO BORRAR

            if(Existing_devices[i].is_slave_anchor){
                was_a_slave = true;
            }
            break; 
        } 
    }


    if(was_a_slave){
        if(DEBUG){Serial.println("A SLAVE has disconnected! Aborting current cycle, returning to master ranging.");}

        // Resetea todas las banderas de estado transitorio de la FSM
        master_is_ranging = false;
        initiator_handoff_started = false;
        data_report_started = false;
        waiting_initiator_switch_ack = false;
        waiting_responder_switch_ack = false;
        slave_is_ranging = false;
        waiting_data_report = false;
        num_retries = 0;

        amount_active_slaves--;
        for (int i = 0; i < amount_devices; i++) {
            Existing_devices[i].mode_switch_pending = false;
            Existing_devices[i].data_report_pending = false;
        }

        if(amount_active_slaves == 0){
            if(DEBUG) Serial.println("All slaves disconnected. Going back to discovery");
            slaves_discovered = false;
            state = DISCOVERY; 
            return;
        }

        state = MASTER_RANGING; 
        
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

void newRange(){

    uint8_t dest_sa = DW1000Ranging.getDistantDevice()->getShortAddressHeader();
    float dist = DW1000Ranging.getDistantDevice()->getRange();
    float rx_pwr = DW1000Ranging.getDistantDevice()->getRXPower();

    logMeasure(own_short_addr,dest_sa, dist, rx_pwr);
    
    /*
    for (int i = 0; i < amount_devices; i++) {
        if (Existing_devices[i].short_addr == dest_sa && !Existing_devices[i].active) {
            Existing_devices[i].active = true;
            if(DEBUG){
                Serial.print("Device RE-ACTIVATED: ");
                Serial.println(dest_sa, HEX);
            }
            break; 
        }
    }
    */

    if(stop_ranging_requested){
        
        if(DEBUG){Serial.println("Master ranging ended");}      
        
    }
    
    if(!seen_first_range){
        seen_first_range = true;
        
    }

}




void stopRanging(){

    DW1000Ranging.setStopRanging(true);
    stop_ranging_requested = true;
    
}

void activateRanging(){

    DW1000Ranging.setStopRanging(false);
    stop_ranging_requested = false;
    seen_first_range = false;
    master_ranging_start = current_time;
   
}


void transmitUnicast(uint8_t message_type){


    if(message_type == MSG_SWITCH_TO_INITIATOR ||message_type == MSG_SWITCH_TO_RESPONDER){

        bool switch_to_initiator = (message_type == MSG_SWITCH_TO_INITIATOR);
        
        DW1000Device* target = DW1000Ranging.searchDeviceByShortAddHeader(Existing_devices[slaves_indexes[active_slave_index]].short_addr);

        if(target){

            if(DEBUG){
                    Serial.print("Mode switch requested --> ");
                    Serial.print(Existing_devices[slaves_indexes[active_slave_index]].short_addr,HEX);
                    Serial.print(switch_to_initiator ?  " to initiator" : " to responder");
                    Serial.println(" via unicast");
                }

            DW1000Ranging.transmitModeSwitch(switch_to_initiator,target);
            
            

        }

        else{
            if(DEBUG){Serial.println("Target Not found. Mode switch not sent");}
            if(switch_to_initiator){
                state = INITIATOR_HANDOFF; //This one was not sent. Now, I'll try the next.
                if(DEBUG){Serial.println("Trying switch to initiator on next slave");}
                Existing_devices[slaves_indexes[active_slave_index]].mode_switch_pending = false;
            }
            else{
                // I need that there is only 1 initiator. If the message failed, I'll try to send it again.
                state = SWITCH_TO_RESPONDER;
                if(DEBUG){Serial.println("Back to switching it to responder.");}
            }
            
        }
    }
    
    
    else if(message_type == MSG_DATA_REQUEST){

        DW1000Device* target = DW1000Ranging.searchDeviceByShortAddHeader(Existing_devices[slaves_indexes[reporting_slave_index]].short_addr);

        if(target){

            if(DEBUG){
                    Serial.print("Data report requested to: ");
                    Serial.print(Existing_devices[slaves_indexes[reporting_slave_index]].short_addr,HEX);
                    Serial.println(" via unicast");
                }

            DW1000Ranging.transmitDataRequest(target);
            
          

        }

        else{
            if(DEBUG){
                Serial.print("Target Not found. Data Report not sent");
                Serial.println("Requesting data report to the next slave.");
                Existing_devices[slaves_indexes[reporting_slave_index]].data_report_pending = false;
                state = DATA_REPORT;
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
                                                 

        if(message_type == MSG_SWITCH_TO_INITIATOR){
            
            if(DEBUG){Serial.print("Switch to Initiator failed. ");}
            
            bool switching_to_initiator = true;
            modeSwitchFailed(switching_to_initiator);          

        }
        else if(message_type == MSG_SWITCH_TO_RESPONDER){

            if(DEBUG){Serial.print("Switch to responder failed. ");}

            bool switching_to_initiator = false;
            modeSwitchFailed(switching_to_initiator);
            
        }
        else if(message_type == MSG_DATA_REQUEST){

            
            DataReportFailed();
            
            
        }
   
    }
}


void modeSwitchFailed(bool switching_to_initiator){

    if(switching_to_initiator){

        if(DEBUG){Serial.println("Moving on to the next slave. Back to initiator handoff");}
        Existing_devices[slaves_indexes[active_slave_index]].mode_switch_pending = false;
        state = INITIATOR_HANDOFF;
    
    }

    else{

        if(DEBUG){Serial.println("I need to switch it back to responder. restarting the switch to responder cycle");}

        state = SWITCH_TO_RESPONDER;
    }
}


void DataReportFailed(){

    Existing_devices[slaves_indexes[reporting_slave_index]].data_report_pending = false;

    if(DEBUG){Serial.println("Data report failed. Trying on next slave. Back to state = data report");}

    state = DATA_REPORT;

}

void DataReport(byte* data){

    
    
    uint8_t origin_short_addr = DW1000Ranging.getDistantDevice()->getShortAddressHeader();
    
    for (int i = 0; i < amount_devices; i++) {
        if (Existing_devices[i].short_addr == origin_short_addr && !Existing_devices[i].active) {
            Existing_devices[i].active = true;
            if(DEBUG){ Serial.print("Device RE-ACTIVATED via Report: "); Serial.println(origin_short_addr, HEX); }
            if (Existing_devices[i].is_slave_anchor) {
                slaves_discovered = true;
            }
            break;
        }
    }

    
    if(Existing_devices[slaves_indexes[reporting_slave_index]].short_addr == origin_short_addr && Existing_devices[slaves_indexes[reporting_slave_index]].data_report_pending == true){

        Existing_devices[slaves_indexes[reporting_slave_index]].data_report_pending = false;

        uint8_t index = SHORT_MAC_LEN+1;
        uint8_t numMeasures = data[index++];

        //First, I check if the size is OK:
        if(numMeasures*5>LEN_DATA-SHORT_MAC_LEN-4){
            // x5 because every measure takes 5 bytes.
            Serial.println("The Data received is too long");
            return;
        }

        for (int i = 0; i < numMeasures; i++) {

            uint8_t destiny_short_addr = data[index++];

            uint16_t distance_cm;
            memcpy(&distance_cm, data + index, 2); 
            index += 2;
            // Now, transforms the distance in cm to distance in meters.
            float distance = (float)distance_cm / 100.0f;

            int16_t _rxPower; // int16_t (already has a sign)
            memcpy(&_rxPower, data + index, 2); 
            index += 2; 
            // Same as before, now I transform it.
            float rxPower = (float)_rxPower / 100.0f;

            logMeasure(origin_short_addr, destiny_short_addr, distance, rxPower);
        }
        

        if(DEBUG){
            Serial.print("Data Report received from: ");
            Serial.print(Existing_devices[slaves_indexes[reporting_slave_index]].short_addr,HEX);
            Serial.println(" Now, back to data report to get it from the next slave");
        }

        waiting_data_report = false;
        num_retries = 0;
        state = DATA_REPORT;
    }
    
}


void ModeSwitchAck(bool is_initiator){

    uint8_t origin_short_addr = DW1000Ranging.getDistantDevice()->getShortAddressHeader();

    for (int i = 0; i < amount_devices; i++) {
        if (Existing_devices[i].short_addr == origin_short_addr && !Existing_devices[i].active) {
            Existing_devices[i].active = true;
            if(DEBUG){ Serial.print("Device RE-ACTIVATED via ACK: "); Serial.println(origin_short_addr, HEX); }
            if (Existing_devices[i].is_slave_anchor) {
                slaves_discovered = true;
            }
            break;
        }
    }

    if(Existing_devices[slaves_indexes[active_slave_index]].short_addr == origin_short_addr && Existing_devices[slaves_indexes[active_slave_index]].mode_switch_pending == true){

        //Only if the ack is received from the active slave, and if it has the mode switch pending.

        Existing_devices[slaves_indexes[active_slave_index]].mode_switch_pending = false;
        Existing_devices[slaves_indexes[active_slave_index]].is_responder = !is_initiator;

        if(DEBUG){
            Serial.print("Mode switch completed: ");
            Serial.print(origin_short_addr,HEX);
            Serial.print(" is --> ");
            Serial.println(is_initiator ? "Initiator" : "Responder");
        }

        if(is_initiator){
            state = SLAVE_RANGING;
            if(DEBUG){Serial.println("Slave switched to initiator. Now --> Slave ranging");}
        }
        else{
            state = INITIATOR_HANDOFF; //Back to responder. Now, turn for the next slave.
            if(DEBUG) Serial.println("Slave back to responder. Now, turn for the next slave to range");
        }
    }      

}





void loop(){

    DW1000Ranging.loop();
    current_time = millis();

    if(state == DISCOVERY){

        if(!discovering){
            discovering =true;
            discovery_start = current_time;

        }

        if(current_time - discovery_start >= ranging_period){

            if(slaves_discovered){
                discovering = false;
                state = MASTER_RANGING;
                if(DEBUG){Serial.println("Slaves have been found. Now --> Master ranging");}

            }
            else{
                if(DEBUG){Serial.println("No slaves detected. Back to discovering");}
                discovery_start = current_time;
            }
        }
    }

    else if(state == MASTER_RANGING){

        if(!master_is_ranging){
            master_is_ranging = true;
            activateRanging();
            if(DEBUG){Serial.println("Master starts ranging");}
        }

        if(current_time - master_ranging_start >= ranging_period){

            //Master_ranging_start is set inside "activateRanging"
            if(seen_first_range){
                stopRanging();
               
                if(DEBUG){Serial.println("Master ranging ended. Now --> initiator handoff");}
                master_is_ranging = false;
                state = INITIATOR_HANDOFF;
            }

            else{                
                master_ranging_start = current_time;
                master_is_ranging = false;
                if(DEBUG){Serial.println("No ranges made. Restarting master ranging");}
            }
            
        }
    }

    else if(state == INITIATOR_HANDOFF){

        if(!initiator_handoff_started){
            initiator_handoff_started = true;
            active_slave_index = -1; //Set at -1 so that when doing active_slave_index++, the first index is 0.
            if(DEBUG) Serial.println("Initiator handoff started.");
        }
        active_slave_index++;

        if(Existing_devices[slaves_indexes[active_slave_index]].active){
            if(active_slave_index < amount_slaves){

                Existing_devices[slaves_indexes[active_slave_index]].mode_switch_pending = true;

                if(DEBUG){Serial.println("Switching next slave to initiator.");}

                //transmitUnicast(MSG_SWITCH_TO_INITIATOR);
                state = WAIT_SWITCH_TO_INITIATOR_ACK;

            }

            else{
                initiator_handoff_started = false;
                state = DATA_REPORT;

                if(DEBUG){Serial.println("All slaves have been initiators and ranged. Now, starting data reports");}
            }
        }
        else{
            if(DEBUG){
                Serial.print("Skipped an inactive slave: ");
                Serial.println(Existing_devices[slaves_indexes[active_slave_index]].short_addr,HEX);
            }
        }
    }

    else if(state == WAIT_SWITCH_TO_INITIATOR_ACK){

        if(!waiting_initiator_switch_ack){
            waiting_initiator_switch_ack = true;
            waiting_switch_start = current_time;
            if(DEBUG){
                Serial.print("Waiting for switch to initiator ack from ");
                Serial.println(Existing_devices[slaves_indexes[active_slave_index]].short_addr,HEX);
            }
        }

        else{

            if(current_time - waiting_switch_start >= waiting_time){
                waiting_switch_start = current_time;
                retryTransmission(MSG_SWITCH_TO_INITIATOR);
                if(DEBUG){Serial.println("Retrying switch to initiator.");}
            }
        }
    }

    else if(state == SLAVE_RANGING){

        if(!slave_is_ranging){
            slave_is_ranging = true;
            slave_ranging_start = current_time;
            if(DEBUG){
                Serial.print("The device ");
                Serial.print(Existing_devices[slaves_indexes[active_slave_index]].short_addr,HEX);
                Serial.println(" starts its ranging");
            }
            
        }

        else{

            if(current_time-slave_ranging_start >= ranging_period){
                slave_is_ranging = false;
                state = SWITCH_TO_RESPONDER;
                if(DEBUG){Serial.println("End of slave ranging period. Switching it back to responder.");}
            }
        }
    }

    else if(state == SWITCH_TO_RESPONDER){

        Existing_devices[slaves_indexes[active_slave_index]].mode_switch_pending = true;
        //transmitUnicast(MSG_SWITCH_TO_RESPONDER);
        
        state = WAIT_SWITCH_TO_RESPONDER_ACK;
    }
    
    else if(state == WAIT_SWITCH_TO_RESPONDER_ACK){

        if(!waiting_responder_switch_ack){
            waiting_responder_switch_ack = true;
            waiting_switch_start = current_time;
            if(DEBUG){
                Serial.print("Waiting for switch to responder ack from ");
                Serial.println(Existing_devices[slaves_indexes[active_slave_index]].short_addr,HEX);
            }
        }

        else{

            if(current_time - waiting_switch_start >= waiting_time){
                waiting_switch_start = current_time;
                retryTransmission(MSG_SWITCH_TO_RESPONDER);
                if(DEBUG){Serial.println("Retrying switch to responder.");}
            }
        }
    }

    else if(state == DATA_REPORT){

        if(!data_report_started){
            data_report_started = true;
            reporting_slave_index = -1;

            if(DEBUG){Serial.println("Starting Data Reports.");}
        }

        reporting_slave_index++;

        if(reporting_slave_index < amount_slaves){

            if(Existing_devices[slaves_indexes[reporting_slave_index]].active){

                Existing_devices[slaves_indexes[reporting_slave_index]].data_report_pending = true;
                //transmitUnicast(MSG_DATA_REQUEST);

                state = WAIT_DATA_REPORT;
            }

            else{
                if(DEBUG){
                    Serial.print("Skipped inactive slave for data report: ");
                    Serial.println(Existing_devices[slaves_indexes[reporting_slave_index]].short_addr,HEX);
                }
            }
        }

        else{
            data_report_started = false;
            if(DEBUG) Serial.println("Received from all slaves. Showing data: ");
            showData();
            if(DEBUG) Serial.println("Restarting the cycle --> going back to master ranging");
            
            state = MASTER_RANGING;
        }
    }
    


    else if(state == WAIT_DATA_REPORT){

        if(!waiting_data_report){
            waiting_data_report = true;
            waiting_data_report_start = current_time;
            if(DEBUG){
                Serial.print("Waiting data report from ");
                Serial.println(Existing_devices[slaves_indexes[reporting_slave_index]].short_addr,HEX);
            }
        }

        else{

            if(current_time - waiting_data_report_start >= waiting_time){
                waiting_data_report_start = current_time;
                retryTransmission(MSG_DATA_REQUEST);
                if(DEBUG){
                    Serial.print("Retrying data report from: ");
                    Serial.println(Existing_devices[slaves_indexes[reporting_slave_index]].short_addr,HEX);
                }
            }
        }
        
    }


    

}