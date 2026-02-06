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

#define DEBUG_MASTER true
#define IS_MASTER true

#define DEVICE_ADDR "A1:00:5B:D5:A9:9A:E2:9C" 
uint8_t own_short_addr = 0; 
uint16_t Adelay = 16580;

#define MAX_MEASURES 200
Measurement measurements[MAX_MEASURES];
uint8_t amount_measurements = 0;


ExistingDevice Existing_devices[MAX_DEVICES];
uint8_t amount_devices = 0;

uint8_t slaves_indexes[MAX_DEVICES];
uint8_t amount_slaves = 0;


/*Ranging Mode --> Broadcast or Unicast*/
DW1000RangingClass::RangingMode ranging_mode = DW1000RangingClass::UNICAST;
//DW1000RangingClass::RangingMode ranging_mode = DW1000RangingClass::BROADCAST;



/*States used in the FSM*/
enum State{
    DISCOVERY,
    MASTER_RANGING,
    BROADCAST_MASTER_RANGING,
    UNICAST_MASTER_RANGING,
    WAIT_UNICAST_RANGE,
    INITIATOR_HANDOFF,
    WAIT_SWITCH_TO_INITIATOR_ACK,
    SLAVE_RANGING,
    SWITCH_TO_RESPONDER, 
    WAIT_SWITCH_TO_RESPONDER_ACK,
    DATA_REPORT_STATE,
    WAIT_DATA_REPORT
};
State state = DISCOVERY;

/*Time management*/

/*1: "timers"*/
unsigned long current_time = 0; 
unsigned long discovery_start = 0;
unsigned long master_ranging_start = 0;
unsigned long slave_ranging_start = 0;
unsigned long waiting_switch_start = 0;
unsigned long waiting_data_report_start = 0;
unsigned long last_shown_data_timestamp = 0;
unsigned long waiting_unicast_range_start = 0;

/*2: Time constants*/
const unsigned long ranging_period = 500;
const unsigned long waiting_time = 200;
const unsigned long retry_time = 200;


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
uint8_t discovery_attempts = 0;
static bool discovery_previously_done = false;

/*Flags used in state = master_ranging*/
static bool master_is_ranging = false;
static bool waiting_unicast_range = false;
uint8_t active_polling_device_index = 0; // To go through all devices when ranging via unicast. 
static bool unicast_master_ranging_started = false; 


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

/*Message types used to transmit via unicast*/
enum UnicastMessageType{
    MSG_SWITCH_TO_INITIATOR,
    MSG_SWITCH_TO_RESPONDER,
    MSG_DATA_REQUEST,
    MSG_POLL_UNICAST
};

UnicastMessageType message_type;



/*CODE*/

/*SETUP & INITIALIZATIONS*/

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
    
    DW1000Ranging.startAsInitiator(DEVICE_ADDR,DW1000.MODE_1, false,MASTER);

    own_short_addr = getOwnShortAddress();

    state = DISCOVERY;
    
}

uint8_t getOwnShortAddress() {
    byte* sa = DW1000Ranging.getCurrentShortAddress();
    return (uint8_t)sa[0];
}




/*DEVICE DISCOVERY*/

void newDevice(DW1000Device *device){

    Serial.print("New Device: [");
    Serial.print(device->getShortAddressHeader(), HEX);
    Serial.print("]\tType --> ");
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

    uint8_t incoming_short_addr = device->getShortAddressHeader();
    uint8_t incoming_board_type = device->getBoardType();

    for(int i=0;i<amount_devices;i++){
        if(Existing_devices[i].short_addr == incoming_short_addr){
            //Device already registered. Update its info but don't increase amount_devices.

            if(Existing_devices[i].active == false){

                Existing_devices[i].active = true;

                if(incoming_board_type == SLAVE){
            
                    Existing_devices[i].is_slave = true;
                    amount_active_slaves++; //Reactivated a slave.
                    if(DEBUG_MASTER){ 
                        Serial.print("Device ["); Serial.print(incoming_short_addr, HEX);   Serial.println("] re-activated as slave anchor!"); }
                }   

                else{ Existing_devices[i].is_slave = false;}

            }
                                   
            return;
        }
    }

    //If code reaches this point, the device wasn't previously registered. I add it to the list

    if (amount_devices >= MAX_DEVICES) {
        if (DEBUG_MASTER) {
            Serial.println("-------------------------------------------------------------"); 
            Serial.println("     ERROR: Exceeded MAX_DEVICES limit. Device not added.    "); 
            Serial.println("-------------------------------------------------------------");
        }
        return; 
    }


    Existing_devices[amount_devices].short_addr = incoming_short_addr;
    memcpy(Existing_devices[amount_devices].byte_short_addr, device->getByteShortAddress(), 2);
    
    
    if(incoming_board_type == SLAVE){
        
        Existing_devices[amount_devices].is_slave = true;
        slaves_indexes[amount_slaves] = amount_devices;
        
        if(!slaves_discovered) slaves_discovered = true;
        amount_slaves ++;
        amount_active_slaves++;
    }

    else{ Existing_devices[amount_devices].is_slave = false;}

    Existing_devices[amount_devices].is_responder = true;
    Existing_devices[amount_devices].active = true;
    
    amount_devices ++;
}

void inactiveDevice(DW1000Device *device){

    uint8_t origin_short_addr = device->getShortAddressHeader();
    Serial.print("Lost connection with device: ");
    Serial.println(origin_short_addr, HEX);
    
    bool was_a_slave = false;
    for(int i = 0; i < amount_devices; i++){
        if(Existing_devices[i].short_addr == origin_short_addr){

            Existing_devices[i].active = false; //sets it as inactive, but doesn's delete it.

            if(Existing_devices[i].is_slave){
                was_a_slave = true;
            }
            break; 
        } 
    }


    if(was_a_slave){
        Serial.println("A SLAVE has disconnected! Aborting current cycle, returning to master ranging.");

        // Reset all fsm's flags
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
            Serial.println("All slaves disconnected. Going back to discovery");
            slaves_discovered = false;
            state = DISCOVERY; 
            return;
        }

        state = MASTER_RANGING; 
        
    }
    
}




/*RANGES & LOGGING*/

void newRange(){

    if(master_is_ranging == false) return;

    uint8_t dest_sa = DW1000Ranging.getDistantDevice()->getShortAddressHeader();

    if(ranging_mode == DW1000RangingClass::UNICAST)
    
        if (Existing_devices[active_polling_device_index].short_addr == dest_sa && Existing_devices[active_polling_device_index].range_pending == true){
            Existing_devices[active_polling_device_index].range_pending = false;
            waiting_unicast_range = false;  // To restart the timer next time state is state = WAIT_UNICAST_RANGE.
            state = UNICAST_MASTER_RANGING; 
        }
        else return;

    // If code reaches this point, the measure is valid and can be logged and registered.

    float dist = DW1000Ranging.getDistantDevice()->getRange();
    float rx_pwr = DW1000Ranging.getDistantDevice()->getRXPower();
    
    logMeasure(own_short_addr,dest_sa, dist, rx_pwr);
    
    if(!seen_first_range){ seen_first_range = true;}

    if(DEBUG_MASTER){
        Serial.print("From: ");
        Serial.print(dest_sa,HEX);
        Serial.print("\t Distance: ");
        Serial.print(dist);
        Serial.print(" m");
        Serial.print("\t RX power: ");
        Serial.println(rx_pwr);

        if(ranging_mode == DW1000RangingClass::UNICAST){
            Serial.println("Unicast Range Completed. Back to unicast master ranging.");
        }
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
    else if (amount_measurements < MAX_MEASURES){

        // If not found, i need to make a new entry to the struct.
        measurements[amount_measurements].short_addr_origin = own_sa;
        measurements[amount_measurements].short_addr_dest = dest_sa;
        measurements[amount_measurements].distance = dist;
        measurements[amount_measurements].rxPower = rx_pwr;
        measurements[amount_measurements].active = true;
        amount_measurements ++; // And increase the devices number in 1.
        
    }
    else{
        Serial.println("-------------------------------------------------------------");
        Serial.println("                   Devices list is full                      ");
        Serial.println("-------------------------------------------------------------");
        
    }
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

void activateRanging(){

    DW1000Ranging.setStopRanging(false);
    stop_ranging_requested = false;
    seen_first_range = false;

    master_ranging_start = current_time;

}

void stopRanging(){

    DW1000Ranging.setStopRanging(true);
    stop_ranging_requested = true;
    
}



/*MODE SWITCHING*/

void ModeSwitchAck(bool is_initiator){

    uint8_t origin_short_addr = DW1000Ranging.getDistantDevice()->getShortAddressHeader();

    for (int i = 0; i < amount_devices; i++) {
        if (Existing_devices[i].short_addr == origin_short_addr && !Existing_devices[i].active) {
            Existing_devices[i].active = true;
            if(DEBUG_MASTER){ Serial.print("Device RE-ACTIVATED via ACK: "); Serial.println(origin_short_addr, HEX); }
            if (Existing_devices[i].is_slave) {
                slaves_discovered = true;
            }
            break;
        }
    }

    if(Existing_devices[slaves_indexes[active_slave_index]].short_addr != origin_short_addr){return;} //ACK received from a device that is not the active slave. Ignore it.

    else{

        if(DEBUG_MASTER){
            Serial.print("Mode switch completed: ");
            Serial.print(origin_short_addr,HEX);
            Serial.print(" is --> ");
            Serial.println(is_initiator ? "Initiator" : "Responder");
        }

        if(is_initiator){

            // Slave is initiator --> Always needs to have the mode_switch_pending = true to reach this point.
            if(Existing_devices[slaves_indexes[active_slave_index]].mode_switch_pending == false){return;} 
            else{
                // Ack received from a slave with the change pending. OK
                Existing_devices[slaves_indexes[active_slave_index]].mode_switch_pending = false;
                Existing_devices[slaves_indexes[active_slave_index]].is_responder = false;
                state = SLAVE_RANGING;
                waiting_initiator_switch_ack = false;
                if(DEBUG_MASTER){Serial.println("Slave switched to initiator. Now --> Slave ranging");}
                 

            }
        }
        
        else{ //Slave finished its ranging & switched back to responder.

            waiting_responder_switch_ack = false;
            Existing_devices[slaves_indexes[active_slave_index]].mode_switch_pending = false; // Just in case.
            Existing_devices[slaves_indexes[active_slave_index]].is_responder = true;
            state = INITIATOR_HANDOFF; // Back to responder. Now, turn for the next slave.

            if(DEBUG_MASTER) Serial.println("Slave back to responder. Now, turn for the next slave to range");
        }
    }
}

void modeSwitchFailed(bool switching_to_initiator){

    if(switching_to_initiator){

        if(DEBUG_MASTER){Serial.println("Moving on to the next slave. Back to initiator handoff");}
        Existing_devices[slaves_indexes[active_slave_index]].mode_switch_pending = false;
        waiting_initiator_switch_ack = false;
        state = INITIATOR_HANDOFF;
    
    }
    else{
        if(DEBUG_MASTER){Serial.println("I need to switch it back to responder. restarting the switch to responder cycle");}

        waiting_responder_switch_ack = false;
        state = SWITCH_TO_RESPONDER;
    }
}



/*UNICAST TRANSMISSIONS*/

void transmitUnicast(uint8_t message_type){

    if(message_type == MSG_POLL_UNICAST){

        DW1000Device* target = DW1000Ranging.searchDeviceByShortAddHeader(Existing_devices[active_polling_device_index].short_addr);

        if(target){

            if(DEBUG_MASTER){
                    Serial.print("Ranging poll transmitted to: [");
                    Serial.print(Existing_devices[active_polling_device_index].short_addr,HEX);
                    Serial.println("] via unicast");
                }

            DW1000Ranging.transmitPoll(target);
            
        }

        else{
            if(DEBUG_MASTER){Serial.println("Target Not found. Ranging poll via unicast not sent");}
            Existing_devices[active_polling_device_index].range_pending = false;
            waiting_unicast_range = false; // To restart the timer next time state is state = WAIT_UNICAST_RANGE.
            state = UNICAST_MASTER_RANGING; 
        }

    }

    else if(message_type == MSG_SWITCH_TO_INITIATOR ||message_type == MSG_SWITCH_TO_RESPONDER){

        bool switch_to_initiator = (message_type == MSG_SWITCH_TO_INITIATOR);
        
        DW1000Device* target = DW1000Ranging.searchDeviceByShortAddHeader(Existing_devices[slaves_indexes[active_slave_index]].short_addr);

        if(target){

            if(DEBUG_MASTER){
                    Serial.print("Mode switch requested --> ");
                    Serial.print(Existing_devices[slaves_indexes[active_slave_index]].short_addr,HEX);
                    Serial.print(switch_to_initiator ?  " to initiator" : " to responder");
                    Serial.println(" via unicast");
                }

            DW1000Ranging.transmitModeSwitch(switch_to_initiator,target);
            
        }

        else{
            if(DEBUG_MASTER){Serial.println("Target Not found. Mode switch not sent");}
            if(switch_to_initiator){
                state = INITIATOR_HANDOFF; //This one was not sent. Now, I'll try the next.
                if(DEBUG_MASTER){Serial.println("Trying switch to initiator on next slave");}
                Existing_devices[slaves_indexes[active_slave_index]].mode_switch_pending = false;
            }
            else{
                // I need that there is only 1 initiator. If the message failed, I'll try to send it again.
                state = SWITCH_TO_RESPONDER;
                if(DEBUG_MASTER){Serial.println("Back to switching it to responder.");}
            }
            
        }
    }
    
    
    else if(message_type == MSG_DATA_REQUEST){

        DW1000Device* target = DW1000Ranging.searchDeviceByShortAddHeader(Existing_devices[slaves_indexes[reporting_slave_index]].short_addr);

        if(target){

            if(DEBUG_MASTER){
                    Serial.print("Data report requested to: ");
                    Serial.print(Existing_devices[slaves_indexes[reporting_slave_index]].short_addr,HEX);
                    Serial.println(" via unicast");
                }

            DW1000Ranging.transmitDataRequest(target);

        }

        else{
            if(DEBUG_MASTER){
                Serial.print("Target Not found. Data Report not sent");
                Serial.println("Requesting data report to the next slave.");
                Existing_devices[slaves_indexes[reporting_slave_index]].data_report_pending = false;
                state = DATA_REPORT_STATE;
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
                                                 

        if(message_type == MSG_POLL_UNICAST){
            
            PollUnicastFailed();
        }
        else if(message_type == MSG_SWITCH_TO_INITIATOR){
            
            if(DEBUG_MASTER){Serial.print("Switch to Initiator failed. ");}
            
            bool switching_to_initiator = true;
            modeSwitchFailed(switching_to_initiator);          

        }
        else if(message_type == MSG_SWITCH_TO_RESPONDER){

            if(DEBUG_MASTER){Serial.print("Switch to responder failed. ");}

            bool switching_to_initiator = false;
            modeSwitchFailed(switching_to_initiator);
            
        }
        else if(message_type == MSG_DATA_REQUEST){

            DataReportFailed(); 
        }
   
    }
}

void PollUnicastFailed(){

    if(DEBUG_MASTER){
        Serial.print("Poll via unicast with [");
        Serial.print(Existing_devices[active_polling_device_index].short_addr,HEX); Serial.println("] FAILED. Moving on to next device. Back to unicast ranging.");
        
    }
    waiting_unicast_range = false; // To restart the timer next time state is state = WAIT_UNICAST_RANGE.
    Existing_devices[active_polling_device_index].range_pending = false;
    state = UNICAST_MASTER_RANGING; 
}



/*DATA REPORTS*/

void DataReportFailed(){

    Existing_devices[slaves_indexes[reporting_slave_index]].data_report_pending = false;

    if(DEBUG_MASTER){Serial.println("Data report failed. Trying on next slave. Back to state = data report");}

    state = DATA_REPORT_STATE;

}

void DataReport(byte* data){

   
    uint8_t origin_short_addr = DW1000Ranging.getDistantDevice()->getShortAddressHeader();

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
        

        if(DEBUG_MASTER){
            Serial.print("Data Report received from: ");
            Serial.print(origin_short_addr,HEX);
            Serial.println(" Now, back to data report to get it from the next slave");
        }

        waiting_data_report = false;
        num_retries = 0;
        state = DATA_REPORT_STATE;
    }
            
}

void showData(){

    bool inactive_measures_exist = false;

    Serial.println("--------------------------- DATA REPORT ---------------------------");
    
    unsigned long time_between_prints = current_time - last_shown_data_timestamp;
    last_shown_data_timestamp = current_time;
    Serial.print("                   Time since last print --> ");
    Serial.print(time_between_prints);
    Serial.println(" ms\n");

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

    for (int i = 0; i < amount_measurements ; i++){ 
        
        if(measurements[i].active == false){
            inactive_measures_exist = true;
            break;
        }
    }

    if(inactive_measures_exist == true){
        Serial.println("\n\t    ############ INACTIVE MEASURES ############");
    
        for (int i = 0; i < amount_measurements ; i++){ 
        
            if(measurements[i].active == false){
                inactive_measures_exist = true;
                
                Serial.print("\t\tDevice: ["); Serial.print(measurements[i].short_addr_origin,HEX); Serial.print("] didn't range with: [");
                Serial.print(measurements[i].short_addr_dest,HEX); Serial.println("]");
                
            }
        }

        Serial.println("\t    ###########################################");
    
    }
    
    Serial.println("--------------------------------------------------------------------");
    
}



/*LOOP*/

void loop(){

    DW1000Ranging.loop();
    current_time = millis();

    if(state == DISCOVERY){

        if(discovery_previously_done){
            discovery_attempts++;
            
            if(discovery_attempts < 5){
                state = MASTER_RANGING; // Only repeats discovery every 5 cycles.
                return;
            }
            

            if(DEBUG_MASTER){Serial.println("Repeating discovery to update devices list");}
            discovery_attempts = 0;
            discovering = false;
            slaves_discovered = false;
            discovery_previously_done = false;
        }


        if(!discovering){
            DW1000Ranging.setRangingMode(DW1000RangingClass::BROADCAST);
            discovering =true;
            discovery_start = current_time;

        }

        if(current_time - discovery_start >= ranging_period){

            if(slaves_discovered || amount_active_slaves >0){
                discovery_previously_done = true;
                discovering = false;
                state = MASTER_RANGING;
                if(DEBUG_MASTER){Serial.println("Slaves have been found. Now --> Master ranging");}

            }
            else{
                if(DEBUG_MASTER){Serial.println("No slaves detected. Back to discovering");}
                discovery_start = current_time;
            }
        }
    }

    else if(state == MASTER_RANGING){

        if(!master_is_ranging){
            master_is_ranging = true;
            activateRanging();
            DW1000Ranging.setRangingMode(ranging_mode); // After discovery, master can use the ranging mode defined by user (broadcast or unicast).
            if(DEBUG_MASTER){Serial.print("Master starts ranging");}
        }

        if(ranging_mode == DW1000RangingClass::BROADCAST){
            if(DEBUG_MASTER){Serial.println(" in BROADCAST mode");}
            state = BROADCAST_MASTER_RANGING;
        }
        
        else if(ranging_mode == DW1000RangingClass::UNICAST){
            if(DEBUG_MASTER){Serial.println(" in UNICAST mode");}
            state = UNICAST_MASTER_RANGING; 
        }
    }

    else if(state == BROADCAST_MASTER_RANGING){

        if(current_time - master_ranging_start >= ranging_period){

            //Master_ranging_start is set inside "activateRanging"
            if(seen_first_range){
                stopRanging();
               
                if(DEBUG_MASTER){Serial.println("Master ranging ended. Now --> initiator handoff");}
                master_is_ranging = false;  
                state = INITIATOR_HANDOFF;
            }

            else{                
                master_ranging_start = current_time;
                master_is_ranging = false;
                if(DEBUG_MASTER){Serial.println("No ranges made. Restarting master ranging");}
            }
            
        }
    }
    
    else if(state == UNICAST_MASTER_RANGING){

        if(!unicast_master_ranging_started){
            unicast_master_ranging_started = true;
            active_polling_device_index = -1; //Set at -1 so that when doing active_polling_index++, the first index is 0.
            if(DEBUG_MASTER){Serial.println("Master starts unicast ranging. Now, transmitting ranging polls via unicast to the slaves.");}
        }
        active_polling_device_index++;

        if(active_polling_device_index < amount_devices){ //If there are still devices to poll
            
            if(Existing_devices[active_polling_device_index].active == true){
                Existing_devices[active_polling_device_index].range_pending = true;
                if(DEBUG_MASTER){
                Serial.print("Transmitting ranging poll to: ");
                Serial.print(Existing_devices[active_polling_device_index].short_addr,HEX);
                Serial.println(" via unicast");
            }
                transmitUnicast(MSG_POLL_UNICAST); 
                state = WAIT_UNICAST_RANGE;
            }

            else{
                if(DEBUG_MASTER){
                    Serial.print("Skipped Polling with an inactive device: ");
                    Serial.println(Existing_devices[active_polling_device_index].short_addr,HEX);
                }
                //Do nothing --> Next loop, state will still be unicast master ranging, and the index will increase, trying the next device.
            }
        }        
        else{ //All devices have been targeted with the unicast poll. Next, switching to initiator handoff.
            unicast_master_ranging_started = false;
           
            stopRanging();
            if(DEBUG_MASTER){Serial.println("All slaves have been polled via unicast. Now --> initiator handoff");}
            state = INITIATOR_HANDOFF; 

        }
    }
    
    else if(state == WAIT_UNICAST_RANGE){

        if(!waiting_unicast_range){
            waiting_unicast_range = true;
            waiting_unicast_range_start = current_time;
            if(DEBUG_MASTER){
                Serial.print("Waiting for unicast range from: ");
                Serial.println(Existing_devices[active_polling_device_index].short_addr,HEX);
            }
        }

        else{
            if(current_time - waiting_unicast_range_start >= waiting_time){
                waiting_unicast_range_start = current_time;
                retryTransmission(MSG_POLL_UNICAST);
                if(DEBUG_MASTER){Serial.println("Retrying unicast range transmission.");}
            }
        }
    }
    
    else if(state == INITIATOR_HANDOFF){

        if(!initiator_handoff_started){
            initiator_handoff_started = true;
            active_slave_index = -1; //Set at -1 so that when doing active_slave_index++, the first index is 0.
            if(DEBUG_MASTER) Serial.print("Initiator handoff started. ");
        }
        active_slave_index++;

        if(active_slave_index < amount_slaves){
            
            
            if(Existing_devices[slaves_indexes[active_slave_index]].active){

                Existing_devices[slaves_indexes[active_slave_index]].mode_switch_pending = true;
                if(DEBUG_MASTER){Serial.println("Switching next slave to initiator.");}

                transmitUnicast(MSG_SWITCH_TO_INITIATOR);
                state = WAIT_SWITCH_TO_INITIATOR_ACK;
            }
            else{
                
                if(DEBUG_MASTER){
                    Serial.print("Skipped an inactive slave: ");
                    Serial.println(Existing_devices[slaves_indexes[active_slave_index]].short_addr,HEX);
                }
            }
        }
        else{
            
            initiator_handoff_started = false;
            state = DATA_REPORT_STATE; 

            if(DEBUG_MASTER){Serial.println("All slaves have been initiators and ranged. Now, starting data reports");}
        }

        
    }

    else if(state == WAIT_SWITCH_TO_INITIATOR_ACK){

        if(!waiting_initiator_switch_ack){
            waiting_initiator_switch_ack = true;
            waiting_switch_start = current_time;
            if(DEBUG_MASTER){
                Serial.print("Waiting for switch to initiator ack from ");
                Serial.println(Existing_devices[slaves_indexes[active_slave_index]].short_addr,HEX);
            }
        }

        else{

            if(current_time - waiting_switch_start >= waiting_time){
                waiting_switch_start = current_time;
                retryTransmission(MSG_SWITCH_TO_INITIATOR);
                if(DEBUG_MASTER){Serial.println("Retrying switch to initiator.");}
            }
        }
    }

    else if(state == SLAVE_RANGING){

        if(!slave_is_ranging){
            slave_is_ranging = true;
            slave_ranging_start = current_time;
            
            if(DEBUG_MASTER){
                Serial.print("The device ");
                Serial.print(Existing_devices[slaves_indexes[active_slave_index]].short_addr,HEX);
                Serial.println(" starts its ranging");
            }
            
        }

        else{

            if(current_time-slave_ranging_start >= ranging_period){
                //Acts as a timeout, no matter the ranging_mode. If the slave finished before, it would have sent the ACK and changed the state already. So, if code reaches this point, it means that the slave finished or the time is out. In both cases, I need to switch it back to responder and move on to the next slave.
                slave_is_ranging = false;
                state = SWITCH_TO_RESPONDER;
                if(DEBUG_MASTER){Serial.println("End of slave ranging period. Switching it back to responder.");}
            }
        }
    }

    else if(state == SWITCH_TO_RESPONDER){

        Existing_devices[slaves_indexes[active_slave_index]].mode_switch_pending = true;
        transmitUnicast(MSG_SWITCH_TO_RESPONDER);
        
        state = WAIT_SWITCH_TO_RESPONDER_ACK;
    }
    
    else if(state == WAIT_SWITCH_TO_RESPONDER_ACK){

        if(!waiting_responder_switch_ack){
            waiting_responder_switch_ack = true;
            waiting_switch_start = current_time;
            if(DEBUG_MASTER){
                Serial.print("Waiting for switch to responder ack from ");
                Serial.println(Existing_devices[slaves_indexes[active_slave_index]].short_addr,HEX);
            }
        }

        else{

            if(current_time - waiting_switch_start >= waiting_time){
                waiting_switch_start = current_time;
                retryTransmission(MSG_SWITCH_TO_RESPONDER);
                if(DEBUG_MASTER){Serial.println("Retrying switch to responder.");}
            }
        }
    }

    else if(state == DATA_REPORT_STATE){

        if(!data_report_started){
            data_report_started = true;
            reporting_slave_index = -1;

            if(DEBUG_MASTER){Serial.println("Starting Data Reports.");}
        }

        reporting_slave_index++;

        if(reporting_slave_index < amount_slaves){

            if(Existing_devices[slaves_indexes[reporting_slave_index]].active){

                Existing_devices[slaves_indexes[reporting_slave_index]].data_report_pending = true;
                transmitUnicast(MSG_DATA_REQUEST);

                state = WAIT_DATA_REPORT;
            }

            else{
                if(DEBUG_MASTER){
                    Serial.print("Skipped inactive slave for data report: ");
                    Serial.println(Existing_devices[slaves_indexes[reporting_slave_index]].short_addr,HEX);
                }
            }
        }

        else{
            data_report_started = false;
            if(DEBUG_MASTER) Serial.println("Received from all slaves. Showing data: ");
            showData();
            //After showing the data, I clear the measures for the next cycle.
            for (int i = 0; i < amount_measurements; i++) {               
                measurements[i].active = false;
            }

            for (int i = 0; i < amount_measurements; i++) {
                if (measurements[i].active) {
                    measurements[i].active = false;
                }
            }
            
            if(DEBUG_MASTER) Serial.println("Restarting the cycle --> going back to master ranging");
            
            state = DISCOVERY;
        }
    }
    
    else if(state == WAIT_DATA_REPORT){

        if(!waiting_data_report){
            waiting_data_report = true;
            waiting_data_report_start = current_time;
            
            if(DEBUG_MASTER){
                Serial.print("Waiting data report from ");
                Serial.println(Existing_devices[slaves_indexes[reporting_slave_index]].short_addr,HEX);
            }
        }

        else{

            if(current_time - waiting_data_report_start >= waiting_time){
                waiting_data_report_start = current_time;
                retryTransmission(MSG_DATA_REQUEST);
                if(DEBUG_MASTER){
                    Serial.print("Retrying data report from: ");
                    Serial.println(Existing_devices[slaves_indexes[reporting_slave_index]].short_addr,HEX);
                }
            }
        }
        
    }
}