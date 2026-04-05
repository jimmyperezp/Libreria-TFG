/*MESH NETWORK - COORDINATOR*/

#include <SPI.h>
#include <ArduinoJson.h>
#include "DW1000Ranging.h"
#include "DW1000.h"

#define SPI_SCK 18
#define SPI_MISO 19
#define SPI_MOSI 23
#define DW_CS 4
const uint8_t PIN_RST = 27; // reset pin
const uint8_t PIN_IRQ = 34; // irq pin
const uint8_t PIN_SS = 4;   // spi select pin

#define DEBUG_COORDINATOR true
#define PLOTTING true
#define IS_MASTER true

#define DEVICE_ADDR "C1:00:5B:D5:A9:9A:E2:9C" 
uint8_t own_short_addr = 0; 
uint16_t Adelay = 16580;

#define MAX_MEASURES 200
Measurement measurements[MAX_MEASURES];
uint8_t amount_measurements = 0;


ExistingDevice Existing_devices[MAX_DEVICES];
uint8_t amount_devices = 0;

uint8_t nodes_indexes[MAX_DEVICES];
uint8_t amount_nodes = 0;
uint8_t amount_active_nodes = 0;


/*Time management*/
unsigned long current_time = 0;
const unsigned long WAITING_TIME = 1000; 

/*Retry messages management*/
#define MAX_RETRIES 2
unsigned long last_retry = 0;
uint8_t num_retries = 0;


/*state = DISCOVERY*/
const unsigned long DISCOVERY_PERIOD = 700;
static bool _discovery = false;
static bool nodes_discovered = false;
unsigned long discovery_start = 0;

//To only re-discover after certain number of cycles:
#define UPDATE_DISCOVERY_ATTEMPTS 2
static bool discovery_previously_done = false;
uint8_t discovery_attempts = 0;

/*State = COORDINATOR_RANGING*/
static bool _coordinator_ranging = false;
uint8_t ranging_device_index = 0; // To go through all devices when ranging via unicast.


/*state = WAIT_UNICAST_RANGE*/
static bool _wait_unicast_range = false;
unsigned long wait_unicast_range_start = 0;

/*state == TOKEN_HANDOFF_STATE*/
uint8_t cycle_id = 1; // Sent "downwards" to all the devices. Used to make sure the token is sent to everyone in each cycle.
int16_t token_target_address = -1;


/*state = WAIT_TOKEN_HANDOFF_ACK*/
static bool _wait_token_handoff_ack = false;
unsigned long wait_token_handoff_ack_start = 0;


/*state = WAIT_FOR_RETURN*/
static bool _wait_for_return = false;
static bool return_received = false; // To avoid processing the same report more than once in case it is received multiple times due to retries and ACK failures.
unsigned long wait_for_return_start = 0;
const unsigned long WAIT_FOR_RETURN_TIMEOUT = 15000;

/*Used to print results*/
unsigned long last_shown_data_timestamp = 0;
unsigned long last_plot_timestamp = 0;

/*States used in the FSM*/
enum State{
    DISCOVERY,
    COORDINATOR_RANGING,
    WAIT_UNICAST_RANGE,
    TOKEN_HANDOFF_STATE,
    WAIT_TOKEN_HANDOFF_ACK,
    WAIT_FOR_RETURN,
    DATA_REPORT_STATE,
    END_CYCLE
};
State state = DISCOVERY;

/*Message types to be sent via Unicast*/
enum UnicastMessageType{
    MSG_POLL_UNICAST,
    MSG_TOKEN_HANDOFF,
    MSG_DATA_REPORT_ACK
};


/*Function prototypes*/
//transmitUnicast presents errors as one of the parameters is set as null
void transmitUnicast(uint8_t message_type, DW1000Device* explicit_target = nullptr);



/*CODE*/

/*SETUP & INITIALIZATIONS*/

void setup(){

    Serial.begin(115200);
    delay(1000);
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI); 
    DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); // DW1000 Start

    DW1000.setAntennaDelay(Adelay);
        
    attachCallbacks();
   
    DW1000Ranging.startAsInitiator(DEVICE_ADDR,DW1000.MODE_1, false,COORDINATOR);

    own_short_addr = getOwnShortAddressHeader();
    DW1000Ranging.setOwnCycleId(cycle_id); //Initializes cycle ID to avoid initial collisions (all nodes start on cycle 0)

    state = DISCOVERY;
    
}

uint8_t getOwnShortAddressHeader() {
    byte* sa = DW1000Ranging.getCurrentShortAddress();
    return (uint8_t)sa[0];
}

void attachCallbacks(){

    DW1000Ranging.attachNewRange(newRange);
    DW1000Ranging.atttachDiscoveredDevice(discoveredDevice);
    DW1000Ranging.attachNewDevice(newDevice);
    DW1000Ranging.attachInactiveDevice(inactiveDevice); 
    DW1000Ranging.attachTokenHandoffAck(tokenHandoffAck); 
    DW1000Ranging.attachTokenHandoffNack(tokenHandoffNack);
    DW1000Ranging.attachAggregatedDataReport(aggregatedDataReport);  
}


/*DEVICE DISCOVERY*/

void newDevice(DW1000Device *device){

    Serial.print("New Device: [");
    Serial.print(device->getShortAddressHeader(), HEX);
    Serial.print("]\tType --> ");
    uint8_t board_type = device->getBoardType();
    switch(board_type){
        case 1:
            Serial.println("COORDINATOR");
            break;
        case 2:
            Serial.println("NODE");
            break;
        case 3: 
            Serial.println("TAG");
            break;

        default:
            Serial.println("PENDING (discovered via blink)");
            break;

    }
    
    registerDevice(device);

    if(_discovery){
        if(!nodes_discovered) nodes_discovered = true;
    }
}

void discoveredDevice(DW1000Device *device){
    //This callback is called when doing discovery, and a known device answers to a blink.

    if(_discovery == false) return;

    uint8_t short_addr_origin = device->getShortAddressHeader();
    uint8_t incoming_board_type = device->getBoardType();

    if(incoming_board_type == NODE){
        if(!nodes_discovered) nodes_discovered = true;
    }

    registerDevice(device);

    if(DEBUG_COORDINATOR){
        Serial.print("Device discovered: ["); 
        Serial.print(short_addr_origin,HEX); Serial.println("]");
    }

}

void registerDevice(DW1000Device *device){

    uint8_t incoming_short_addr = device->getShortAddressHeader();
    uint8_t incoming_board_type = device->getBoardType();

    for(int i=0;i<amount_devices;i++){
        if(Existing_devices[i].short_addr == incoming_short_addr){
            //Device already registered. Update its info but don't increase amount_devices.

            if(Existing_devices[i].active == false){

                Existing_devices[i].active = true;

                if(incoming_board_type == NODE){
            
                    Existing_devices[i].is_node = true;
                    amount_active_nodes++;
                    
                    if(DEBUG_COORDINATOR){ 
                        Serial.print("Device ["); Serial.print(incoming_short_addr, HEX);   Serial.println("] re-activated as a NODE!"); }
                }   

                else{ Existing_devices[i].is_node = false;}

            }
            return;
        }
    }

    //If code reaches this point, the device wasn't previously registered. I add it to the list

    if (amount_devices >= MAX_DEVICES) {
        if (DEBUG_COORDINATOR) {
            Serial.println("-------------------------------------------------------------"); 
            Serial.println("     ERROR: Exceeded MAX_DEVICES limit. Device not added.    "); 
            Serial.println("-------------------------------------------------------------");
        }
        return; 
    }


    Existing_devices[amount_devices].short_addr = incoming_short_addr;
    memcpy(Existing_devices[amount_devices].byte_short_addr, device->getByteShortAddress(), 2);
    
    
    if(incoming_board_type == NODE){
        
        Existing_devices[amount_devices].is_node = true;
        nodes_indexes[amount_nodes] = amount_devices;
        
        if(!nodes_discovered) nodes_discovered = true;
        amount_nodes++;
        amount_active_nodes++;
       
    }

    else{ Existing_devices[amount_devices].is_node = false;}

    Existing_devices[amount_devices].active = true;
    
    amount_devices ++;
}

void inactiveDevice(DW1000Device *device){

    uint8_t origin_short_addr = device->getShortAddressHeader();
    Serial.print("Lost connection with device: ");
    Serial.println(origin_short_addr, HEX);
    
    bool node_disconnected = false;

    for(int i = 0; i < amount_devices; i++){
        if(Existing_devices[i].short_addr == origin_short_addr){

            Existing_devices[i].active = false; //sets it as inactive, but doesn's delete it.
            
            if(Existing_devices[i].is_node){
                node_disconnected = true;
                amount_active_nodes--;
            }
            
            if(Existing_devices[i].range_pending){
                Existing_devices[i].range_pending = false;
                PollUnicastFailed(); //If it was ranging, sets as failed to move on
                num_retries = 0; // As failure was set "manually", the retry count must be restarted
            }

            if(Existing_devices[i].token_handoff_pending){
                Existing_devices[i].token_handoff_pending = false;
                TokenHandoffFailed(); //If it was doing the token handoff, sets as failed to move on
                num_retries = 0; // As failure was set "manually", the retry count must be restarted
            }

        }
    }
}




/*TOKEN HANDOFF*/

int16_t getNextHop(){

    /*Returns the shortAddress of the closest node that isn't on the same cycle ID as me
    This function is called when looking for a device to send the token to*/
    
    float min_distance = 9999999.9;
    uint8_t examined_node_short_addr_header = 0; 
    uint8_t closest_node_short_addr_header = 0;
    bool min_distance_updated = false;

    for(int i = 0; i<amount_measurements; i++){ //Uses measurements instead of devices. Token Handoff will only be sent to devices with which a ranging has been made this cycle

        if((measurements[i].active) && (measurements[i].short_addr_origin == own_short_addr)){

            examined_node_short_addr_header = measurements[i].short_addr_dest;
            int examined_idx = searchDevice(examined_node_short_addr_header);

            //If the examined node isn't found or it has the same cycleID as me, then it has received the token from someone else. I skip it
            if((examined_idx == -1) || 
               (Existing_devices[examined_idx].cycle_id) == DW1000Ranging.getOwnCycleId()){
                continue;
            }

            if((Existing_devices[examined_idx].is_node)&&(Existing_devices[examined_idx].active))

                // In order to examine a device, it must: 
                // 1) be a node 
                // 2) Be active
                // 3) have an existing & active measure with the coordinator.

                if(measurements[i].distance < min_distance){
                    min_distance_updated = true;
                    min_distance = measurements[i].distance;
                    closest_node_short_addr_header = examined_node_short_addr_header;
                }      
            }       
        }
        
    if(min_distance_updated) return closest_node_short_addr_header;
    else return -1;

}

void tokenHandoffAck(){

    DW1000Device* origin_device = DW1000Ranging.getDistantDevice();
    uint8_t origin_short_addr = origin_device->getShortAddressHeader();

    if(!(origin_short_addr == token_target_address)){ // If the ACK received is not from the target of the token handoff, it is ignored.

        if(DEBUG_COORDINATOR){
            Serial.print("Token Handoff ACK received from ["); Serial.print(origin_short_addr,HEX);
            Serial.print("] but expected from ["); Serial.print(token_target_address,HEX); Serial.println("]. ACK ignored");
        }

        return;
    }

    // If code reaches here, the ACK received is valid 

    int dev_idx = searchDevice(origin_short_addr);
    uint8_t own_cycle_id = DW1000Ranging.getOwnCycleId();
    
    
    if(dev_idx != -1){
        Existing_devices[dev_idx].token_handoff_pending = false;
        Existing_devices[dev_idx].cycle_id = own_cycle_id;
    }

    origin_device->setCycleId(own_cycle_id); //To keep both lists updated with same values


    state = WAIT_FOR_RETURN;

    if(DEBUG_COORDINATOR){
        Serial.print("Token handoff ACK received from: [");
        Serial.print(origin_short_addr,HEX);
        Serial.println("]. Token handoff completed.");
    }
}

void tokenHandoffNack(){

    if(state != WAIT_TOKEN_HANDOFF_ACK) return;
    
    DW1000Device* origin_device = DW1000Ranging.getDistantDevice();
    uint8_t origin_short_addr = origin_device->getShortAddressHeader();
    uint8_t origin_cycle_id = origin_device->getCycleId();

    if(!(origin_short_addr == token_target_address)){ // If the NACK received is not from the target of the token handoff, it is ignored.

        if(DEBUG_COORDINATOR){
            Serial.print("Token Handoff NACK received from ["); Serial.print(origin_short_addr,HEX);
            Serial.print("] but currently passing the token to: ["); Serial.print(token_target_address,HEX); Serial.println("]. NACK ignored");
        }
        return;
    }

    // If reaching here, the NACK is valid:

    uint8_t own_cycle_id = DW1000Ranging.getOwnCycleId();
    if(DEBUG_COORDINATOR){
        Serial.print("Token passed to ["); Serial.print(origin_short_addr,HEX); Serial.print("] REJECTED. "); 
    }

    int dev_idx = searchDevice(origin_short_addr);
    if(dev_idx != -1){
        Existing_devices[dev_idx].token_handoff_pending = false;
        Existing_devices[dev_idx].cycle_id = own_cycle_id;
    }
    
    origin_device->setCycleId(own_cycle_id); //To keep both lists updated with same values

    state = TOKEN_HANDOFF_STATE; //If this one was rejected, then I evaluate next
}

/*RANGES & LOGGING*/

void newRange(){

    if(_coordinator_ranging == false) return; //If ranges arrive while not ranging, ignore them

    uint8_t short_addr_origin   = DW1000Ranging.getDistantDevice()->getShortAddressHeader();
    uint8_t incoming_board_type = DW1000Ranging.getDistantDevice()->getBoardType();
    uint8_t incoming_cycle_id   = DW1000Ranging.getDistantDevice()->getCycleId();

    
    if (Existing_devices[ranging_device_index].short_addr == short_addr_origin && Existing_devices[ranging_device_index].range_pending == true){
        Existing_devices[ranging_device_index].active = true;
        Existing_devices[ranging_device_index].range_pending = false;
        Existing_devices[ranging_device_index].cycle_id = incoming_cycle_id;
        Existing_devices[ranging_device_index].is_node = (incoming_board_type == NODE);
        
        _wait_unicast_range = false;  // To restart the timer next time state is state = WAIT_UNICAST_RANGE.
        state = COORDINATOR_RANGING;
    }
    else return; //If range arrives from a device with which master is not currently ranging.
    
    
    // If code reaches this point, the measure is valid and can be logged and registered.

    float dist = DW1000Ranging.getDistantDevice()->getRange();
    float rx_pwr = DW1000Ranging.getDistantDevice()->getRXPower();
    
    logMeasure(own_short_addr,short_addr_origin, dist, rx_pwr);
    
    if(DEBUG_COORDINATOR){
        Serial.print("New Range --> ");
        Serial.print("From: ");
        Serial.print(short_addr_origin,HEX);
        Serial.print("\t Distance: ");
        Serial.print(dist);
        Serial.print(" m");
        Serial.print("\t RX power: ");
        Serial.println(rx_pwr);
    }
}

void logMeasure(uint8_t own_sa,uint8_t dest_sa, float dist, float rx_pwr){

    // Firstly, checks if that communication has been logged before
    int measure_idx = searchMeasure(own_sa,dest_sa);
    
    if(dist < 0){ dist = -dist;} //If the distance is <0, makes it >0

    if (measure_idx != -1){ // This means: it was found.

        // Only updates distance and rxPower.
        measurements[measure_idx].distance = dist; 
        measurements[measure_idx].rxPower = rx_pwr; 
        measurements[measure_idx].active = true;

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

int searchDevice(uint8_t short_addr){
    for(int i=0;i<amount_devices;i++){
        if(Existing_devices[i].short_addr == short_addr){
            return i; // If found, returns the index
        }
    }
    return -1; // if not, returns -1
}

int searchMeasure(uint8_t own_sa,uint8_t dest_sa){
    
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
    
}

void stopRanging(){
      
    DW1000Ranging.setStopRanging(true);
        
}



/*UNICAST TRANSMISSIONS*/

void transmitUnicast(uint8_t message_type, DW1000Device* explicit_target){

    if(message_type == MSG_POLL_UNICAST){

        //This message type doesn't need an explicit device. It selects it according to the ranging_device_index value.

        DW1000Device* target = DW1000Ranging.searchDeviceByShortAddHeader(Existing_devices[ranging_device_index].short_addr);

        if(target){

            if(DEBUG_COORDINATOR){
                    Serial.print("Ranging poll transmitted to: [");
                    Serial.print(Existing_devices[ranging_device_index].short_addr,HEX);
                    Serial.println("] via unicast");
                }

            DW1000Ranging.transmitPoll(target);
            
        }

        else{
            if(DEBUG_COORDINATOR){Serial.println("Target Not found. Ranging poll via unicast not sent");}
            Existing_devices[ranging_device_index].range_pending = false;
            _wait_unicast_range = false; 
            state = COORDINATOR_RANGING; 
        }

    }
    else if(message_type == MSG_TOKEN_HANDOFF){

        DW1000Device* target = explicit_target ? explicit_target : DW1000Ranging.searchDeviceByShortAddHeader(token_target_address);

       if(target){

        uint8_t _token_target_address = target->getShortAddressHeader(); //To make sure the explicit target is received correctly
        
        if(DEBUG_COORDINATOR){
                Serial.print("Passing token to: [");
                Serial.print(_token_target_address,HEX);
                Serial.print("] via unicast. ");
        }
        DW1000Ranging.transmitTokenHandoff(target);
        
        }

        else{
            if(DEBUG_COORDINATOR) Serial.println("Target Not found. Token handoff not sent");
            // Simply prints the transmission was not sent. the retryTransmission logic handles the retries and, in case of failure, moving on to the next closest node
        }
    
    }
    else if(message_type == MSG_DATA_REPORT_ACK){

        DW1000Device* target = explicit_target ? explicit_target : DW1000Ranging.searchDeviceByShortAddHeader(token_target_address);
        uint8_t _target_address = target->getShortAddressHeader();

        if(target){

            if(DEBUG_COORDINATOR){
                    Serial.print("Data report ACK sent to: [");
                    Serial.print(_target_address,HEX);
                    Serial.println("] via unicast");
                }

            DW1000Ranging.transmitDataReportAck(target);
        }

        else{
            if(DEBUG_COORDINATOR) Serial.println("Target device not found. Sending data report ACK via broadcast.");
            DW1000Ranging.transmitDataReportAck(nullptr);
        }

    }

}

void retryTransmission(uint8_t message_type){

    if(DEBUG_COORDINATOR) Serial.print("Retrying... ");
    transmitUnicast(message_type);
    last_retry = current_time;
    num_retries = num_retries +1;

    if(num_retries == MAX_RETRIES){

        num_retries = 0;
                                                 
        if(message_type == MSG_POLL_UNICAST){
            PollUnicastFailed();
        }
        
        else if(message_type == MSG_TOKEN_HANDOFF){

            if(DEBUG_COORDINATOR){Serial.print("\nToken handoff failed. ");}

            TokenHandoffFailed(); 
        }
   
    }
}

void PollUnicastFailed(){

    if(DEBUG_COORDINATOR){
        Serial.print("Poll via unicast with [");
        Serial.print(Existing_devices[ranging_device_index].short_addr,HEX); Serial.println("] FAILED. Moving on to next device. Back to unicast ranging.");
        
    }
    Existing_devices[ranging_device_index].active = false;
    Existing_devices[ranging_device_index].range_pending = false;
    _wait_unicast_range = false; 
    num_retries = 0;
    state = COORDINATOR_RANGING; 
}

void TokenHandoffFailed(){

    if(DEBUG_COORDINATOR){
        Serial.print("Token handoff with [");
        Serial.print(getNextHop(),HEX); Serial.println("] FAILED. Retrying token handoff... ");
        
    }
    
    // Set both measure and device as inactive, so that it is not selected again when calling getNextHop().
    
    int dev_idx = searchDevice(token_target_address);
    if(dev_idx != -1) Existing_devices[dev_idx].active = false;

    int meas_idx = searchMeasure(own_short_addr,token_target_address);
    if(meas_idx != -1) measurements[meas_idx].active = false;
    
    num_retries = 0;
    state = TOKEN_HANDOFF_STATE; // Goes back to handoff. When calling getNextHop(), the failed node won't be selected (it is set as inactive), so token will go to the next closest node. 


}


/*DATA REPORTING*/

void aggregatedDataReport(byte* data){

    DW1000Device* reporting_device = DW1000Ranging.getDistantDevice();

    uint8_t reporting_node_short_addr = reporting_device->getShortAddressHeader();

    if(reporting_node_short_addr != token_target_address){

        // Only process reports received from the node to which the token was passed.
        if(DEBUG_COORDINATOR){
            Serial.print("Data report received from ["); Serial.print(reporting_node_short_addr,HEX); 
            Serial.print("] but expected from ["); Serial.print(token_target_address,HEX); Serial.println("]. Data report ignored");
        }
        return;
    }

    else{ //The report comes from the valid device

        if(DEBUG_COORDINATOR){
            Serial.print("Data report received from: [");
            Serial.print(reporting_node_short_addr,HEX);
            Serial.print("]");
        }

        if(state == WAIT_TOKEN_HANDOFF_ACK){

            /*This section is for the following case: the parent didn't receive the token Handoff ACK (THA) but the "son" did receive the TH. In this case, the "son" has finished its rangings and sent the data report back to its parent, but the parent was still busy retrying to get the THA
            Receiving a data report from the son while expecting the THA carrys whithin an implicit reception of said THA.*/


            if(DEBUG_COORDINATOR) Serial.print("\nImplicit ACK! Received data report while waiting for Token Handoff ACK. Proceeding...");
            
            int dev_idx = searchDevice(reporting_node_short_addr);
            if(dev_idx != -1) Existing_devices[dev_idx].token_handoff_pending = false;

            // Set the flags as if the ack was received correctly and the FSM had advanced to state = wait for return 
            // This allows me to manage the report correctly (these flags are used in the next "if" conditions)
            state = WAIT_FOR_RETURN;
            _wait_for_return = true;
            return_received = false;

        }
        
        if(_wait_for_return == true && return_received == false){ //The device is valid + I was waiting for the report 
            return_received = true;
            _wait_for_return = false; // To restart the timer next time state is WAIT_FOR_RETURN.
            if(DEBUG_COORDINATOR) Serial.print("\nSending ACK and processing data... ");
        }

        else if(return_received == true){ //The device is valid but the report was already received before
             
            if(DEBUG_COORDINATOR) Serial.println(" but already received before. Only need to send ACK");
            transmitUnicast(MSG_DATA_REPORT_ACK,reporting_device);
            delay(50);
            return;

        }

        else if(_wait_for_return == false){
            if(DEBUG_COORDINATOR) Serial.print(" but I wasn't waiting for it anymore. Sending ack of reception but ignoring data\n");
            transmitUnicast(MSG_DATA_REPORT_ACK,reporting_device);
            delay(50);
            return;
        }

        transmitUnicast(MSG_DATA_REPORT_ACK, reporting_device);
        delay(50); //To make sure ack is sent correctly. 5ms is too small. 10 works fine.

        return_received = true; //To avoid processing the same report more than once in case it is received multiple times due to retries and ACK failures.
        uint8_t index = SHORT_MAC_LEN+1; // Variable "index" is used to go through all the payload.
        uint8_t num_measures = data[index++];

        if(num_measures*6>LEN_DATA-SHORT_MAC_LEN-4){
            // x5 because every measure takes 5 bytes.
            Serial.println("DATA REPORT RECEIVED IS TOO LONG");
            return;
        }

        for(int i=0; i<num_measures;i++){

            uint8_t short_addr_origin = data[index++];
            uint8_t short_addr_dest = data[index++];

            uint16_t distance_cm;
            memcpy(&distance_cm, data+index, 2);
            index += 2;
            float distance_m = (float)distance_cm/100;
            int16_t _rxPower; //Already has a sign (int, not uint)
            memcpy(&_rxPower, data+index, 2);
            index += 2;
            float rxPower = (float)_rxPower/100;

            logMeasure(short_addr_origin,short_addr_dest,distance_m,rxPower);

        }
    }

    _wait_for_return = false; // To restart the timer next time state is WAIT_FOR_RETURN.
    
    state = TOKEN_HANDOFF_STATE;    // To send the token to any unvisited nodes 
    
}

void showData(){

    bool inactive_measures_exist = false;

    Serial.println("\n--------------------------- DATA REPORT ---------------------------");
    
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
    
    resetMeasures();
    
}

void showPlottingData(){
    /*This is used to plot the distances in real time using a python app, which can be found inside the example's folder.
    This function converts the data into a JSON format. Said python app reads the serial monitor & plots the results. */
 

    StaticJsonDocument<1024> json_doc; //Saves up 1024 Bytes of the stack to show the JSON

    //The JSON will have the next structure: 
    /*
    
    {
        "time_between_cycles": (indicates the number of ms it took to complete the current cycle)
        "measure":[ ("[ indicates the value of this field is a list of other fields")
            {
                "from": (origin short address header)
                "to":   (destiny short address header)
                "dist": (distance value)
                "rxpwr": (measured RX power in each measure)
            },
            {
                "from":
                "to":  
                "dist":
                "rxpwr":
            }      
        ]
    }
    
    */

    //1 I save the elapsed time between prints
    unsigned long time_between_prints = current_time - last_plot_timestamp;
    json_doc["time_between_cycles"] = time_between_prints;
    last_plot_timestamp = current_time;

    //2: I create the field "measures":
    JsonArray measures_array = json_doc.createNestedArray("measures");

    for(int i = 0; i<amount_measurements;i++){
        if(measurements[i].active){
            
            //3: I save each valid measure in a new object inside the "measures" field

            JsonObject measure = measures_array.createNestedObject();

            // To save the short addresses: 1 char for each character, and 1 for the ending value (\0)
            char origin[3];
            char destiny[3];

            sprintf(origin,"%02X", measurements[i].short_addr_origin);
            sprintf(destiny,"%02X", measurements[i].short_addr_dest);

            measure["from"] = origin;
            measure["to"] = destiny;
            measure["dist"] = measurements[i].distance;
            measure["rxpwr"] = measurements[i].rxPower;

        }
    }

    Serial.print("JSON_DATA:");
    serializeJson(json_doc,Serial);
    Serial.println();

}


void resetMeasures(){ 
    for(int i = 0;i<amount_measurements;i++){
        measurements[i].active = false; //This way, only prints active measures next time showData() is called
    }
}

/*LOOP*/

void loop(){

    DW1000Ranging.loop();
    current_time = millis();

    if(state == DISCOVERY){
        
        if(discovery_previously_done){
            //Only repeats discovery every UPDATE_DISCOVERY_ATTEMPTS cycles
            discovery_attempts++;

            if(DEBUG_COORDINATOR){
                Serial.print("Discovery attempt: "); Serial.print(discovery_attempts); 
                Serial.print("/");Serial.print(UPDATE_DISCOVERY_ATTEMPTS);
            }
            if(discovery_attempts<UPDATE_DISCOVERY_ATTEMPTS){
                if(DEBUG_COORDINATOR){
                    Serial.println(" No need to re-discover."); 
                }
                state = COORDINATOR_RANGING; 
                return;
            }

            if(DEBUG_COORDINATOR) Serial.print("\nRepeating discovery --> ");
            _discovery = false;
            discovery_previously_done = false;
            discovery_attempts = 0;
        }

        if(!_discovery){
            
            if(DEBUG_COORDINATOR){Serial.println("DISCOVERING:\n");}
            _discovery = true;
            nodes_discovered = false;
            discovery_start = current_time;
            DW1000Ranging.setRangingMode(DW1000RangingClass::DISCOVERY);
            activateRanging();
        }

        if(_discovery && current_time - discovery_start >= DISCOVERY_PERIOD){

            _discovery = false;
            if(nodes_discovered){
                discovery_previously_done = true;
                state = COORDINATOR_RANGING;
                if(DEBUG_COORDINATOR){
                    Serial.print("\nNodes have been found. Now --> Coordinator ranging\n");
                }
            }
            
            else{
                if(DEBUG_COORDINATOR){Serial.println("No nodes detected. Back to discovering");}
                discovery_start = current_time;
            }
        }
    }

    else if(state == COORDINATOR_RANGING){


        if(!_coordinator_ranging){
            
            _coordinator_ranging = true;
            DW1000Ranging.setRangingMode(DW1000RangingClass::UNICAST);
            activateRanging();
            ranging_device_index = -1; //Set at -1 so that when doing active_polling_index++, the first index is 0.
            if(DEBUG_COORDINATOR){Serial.print("\nCOORDINATOR RANGING:");}
        }
        ranging_device_index++;


        if(ranging_device_index < amount_devices){
            // There still are devices to poll
            if(DEBUG_COORDINATOR){
                Serial.print("\nUnicast Polling with device: ");
                Serial.print(ranging_device_index+1); 
                Serial.print("/"); Serial.print(amount_devices); Serial.print(" --> ");
            }

            if(Existing_devices[ranging_device_index].active == true){
                Existing_devices[ranging_device_index].range_pending = true;
                num_retries = 0; //Before starting polling, set retries to 0 to avoid carrying previous retry attempt counts.
                transmitUnicast(MSG_POLL_UNICAST); 
                state = WAIT_UNICAST_RANGE;
            }

            else{

                if(DEBUG_COORDINATOR){
                    Serial.print("Skipped Polling with an inactive device: ");
                    Serial.println(Existing_devices[ranging_device_index].short_addr,HEX);
                }
                //Do nothing --> Next loop, state will still be unicast master ranging, and the index will increase, trying the next device.
            }

        }

        else{
            //All devices have been targeted with the unicast poll. Now, coordinator has to send the token to the closes node.
            _coordinator_ranging = false;
           
            stopRanging();
            if(DEBUG_COORDINATOR){Serial.print("\nCoordinator Ranging ended. ");}
            state = TOKEN_HANDOFF_STATE;
        }
    }

    else if(state == WAIT_UNICAST_RANGE){ 

        if(!_wait_unicast_range){
            _wait_unicast_range = true;
            wait_unicast_range_start = current_time;
            if(DEBUG_COORDINATOR){
                Serial.print("Waiting for unicast range from: [");
                Serial.print(Existing_devices[ranging_device_index].short_addr,HEX); Serial.println("] ... ");
            }
        }

        else if(current_time - wait_unicast_range_start >= WAITING_TIME){
            
            _wait_unicast_range = false;
            retryTransmission(MSG_POLL_UNICAST);
        }
    }
    
    else if(state == TOKEN_HANDOFF_STATE){

        if(DEBUG_COORDINATOR) Serial.println("\n\nTOKEN HANDOFF starts:");
        stopRanging();
        
        token_target_address = getNextHop();

        if(token_target_address == -1){
            Serial.println("ALL NODES VISITED. Cycle finished. Moving on to END_CYCLE\n\n");
            state = END_CYCLE;
            return;
        }

        else{
            DW1000Device* token_target_device = DW1000Ranging.searchDeviceByShortAddHeader(token_target_address);
            transmitUnicast(MSG_TOKEN_HANDOFF,token_target_device);
            delay(50);

            state = WAIT_TOKEN_HANDOFF_ACK;
            _wait_token_handoff_ack = false;
            num_retries = 0; //To clear previous retry attempts.
        }
        
    }

    else if(state == WAIT_TOKEN_HANDOFF_ACK){

        if(!_wait_token_handoff_ack){
            _wait_token_handoff_ack = true;
            wait_token_handoff_ack_start = current_time;
            if(DEBUG_COORDINATOR){
                Serial.print("Waiting for token handoff ACK from: [");
                Serial.print(token_target_address,HEX); Serial.println("] ... ");
            }
        }

        else if(current_time - wait_token_handoff_ack_start >= WAITING_TIME){
            _wait_token_handoff_ack = false;
            retryTransmission(MSG_TOKEN_HANDOFF);
        }
    }

    else if(state == WAIT_FOR_RETURN){

        if(!_wait_for_return){
            
            _wait_for_return = true;
            return_received = false;
            wait_for_return_start = current_time;
            
            if(DEBUG_COORDINATOR){Serial.println("\nWAIT FOR RETURN: ");}
        }

        else if(current_time - wait_for_return_start >= WAIT_FOR_RETURN_TIMEOUT){
            _wait_for_return = false;
            if(DEBUG_COORDINATOR){
                Serial.print("Return TIMEOUT. Child lost: ["); Serial.print(token_target_address,HEX);
                Serial.println("] Trying on next one...");
            }

            // Sets the "lost child" as inactive and continues with next one:

            int dev_idx = searchDevice(token_target_address);
            if(dev_idx != -1) Existing_devices[dev_idx].active = false;


            state = TOKEN_HANDOFF_STATE;
        }
    }   

    else if(state == END_CYCLE){

        if(PLOTTING) showPlottingData();
        showData();

        num_retries = 0;
        if(DEBUG_COORDINATOR){
            Serial.print("\nEnd of cycle. Restarting process... ");
        }

        cycle_id++;
        if(cycle_id > 250){
            //If the counter is about to overflow, restarts its value
            cycle_id = 0;
        }

        DW1000Ranging.setOwnCycleId(cycle_id);
       
        _discovery = false;
        state = DISCOVERY;
    }
}