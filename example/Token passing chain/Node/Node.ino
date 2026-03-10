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
#define DEVICE_ADDR "B2:00:5B:D5:A9:9A:E2:9C" 
uint8_t own_short_addr = 0; 
uint16_t Adelay = 16580;

#define DEBUG_SLAVE true

/*Structs to handle devices & measurements*/
#define MAX_MEASURES 200
Measurement measurements[MAX_MEASURES];
int amount_measurements = 0;

ExistingDevice Existing_devices[MAX_DEVICES];
uint8_t amount_devices = 0;

uint8_t nodes_indexes[MAX_DEVICES];
static bool nodes_discovered = false;
uint8_t amount_nodes = 0;


/*Message types used in unicast transmissions*/
enum UnicastMessageType{
    MSG_POLL_UNICAST,
    MSG_TOKEN_HANDOFF,
    MSG_TOKEN_HANDOFF_ACK,
    MSG_TOKEN_HANDOFF_NACK,
    MSG_RETURN_TO_PARENT,
    MSG_DATA_REPORT_ACK
};

/*Node's FSM states*/
enum node_state{
    IDLE,
    DISCOVERY,
    RANGING,
    WAIT_FOR_RANGE,
    TOKEN_HANDOFF_STATE,
    WAIT_TOKEN_HANDOFF_ACK,
    RETURN_TO_PARENT,
    WAIT_RETURN_TO_PARENT_ACK,
    WAIT_FOR_RETURN
};
node_state state = IDLE;


/*Time management*/
unsigned long current_time = 0;
const unsigned long WAITING_TIME = 800;

/*Retry messages management*/
#define MAX_RETRIES 2
unsigned long last_retry = 0;
uint8_t num_retries = 0;

/*state = DISCOVERY*/
static bool _discovery = false;
unsigned long discovery_start = 0;
const unsigned long DISCOVERY_PERIOD = 700;

//To only re-discovery after a certain number of cycles: 
#define UPDATE_DISCOVERY_ATTEMPTS 2
static bool discovery_previously_done = false;
uint8_t discovery_attempts = 0;


/*State = RANGING*/
static bool _ranging = false;
int8_t ranging_device_index = -1; // To go through all devices when ranging via unicast.


/*state = WAIT_FOR_RANGE*/
static bool _wait_for_range = false;
unsigned long wait_for_range_start = 0;

/*Token Control*/
static bool i_have_token = false; 
int16_t token_target_address = -1; 
uint8_t parent_address = 0; //Device that sent the token to this node. The data return will be sent to this address. 


/*MODE SWITCHING control*/
static bool _switch_to_initiator_pending = false; //Used inside tokenHandoff. Checks if has to switch or not (need to send ack before switching)
static bool device_is_initiator = false;
unsigned long being_initiator_start = 0;
const unsigned long INITIATOR_TIMEOUT = 5000;

/*state = WAIT_TOKEN_HANDOFF_ACK*/
static bool _wait_token_handoff_ack = false;
unsigned long wait_token_handoff_ack_start = 0;

/*state = RETURN_TO_PARENT*/

/*satet = WAIT_RETURN_TO_PARENT_ACK*/
static bool _wait_return_to_parent_ack = false;
unsigned long wait_return_to_parent_ack_start = 0;

/*state = WAIT_FOR_RETURN*/
static bool _wait_for_return = false;
static bool return_received = false; // To avoid processing the same report more than once in case it is received multiple times due to retries and ACK failures.
unsigned long wait_for_return_start = 0;
const unsigned long WAITING_RETURN_TIME = 5000;


/*Function prototypes*/
//transmitUnicast presents errors as one of the parameters is set as null
void transmitUnicast(uint8_t message_type, DW1000Device* explicit_target = nullptr);

/*CODE*/

void setup(){

    Serial.begin(115200);
    delay(1000); // 1 sec to launch the serial monitor

    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI); // SPI bus start
    DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); // DW1000 Start

    DW1000.setAntennaDelay(Adelay);

    attachCallbacks();

    DW1000Ranging.startAsResponder(DEVICE_ADDR,DW1000.MODE_1, false,NODE);

    own_short_addr = getOwnShortAddress();
    // I save the own_short_addr after the device has been set up propperly
}

void attachCallbacks(){

    DW1000Ranging.attachNewRange(newRange);
    DW1000Ranging.attachNewDevice(newDevice);
    DW1000Ranging.atttachDiscoveredDevice(discoveredDevice);
    DW1000Ranging.attachInactiveDevice(inactiveDevice);   

    DW1000Ranging.attachTokenHandoff(tokenHandoff);
    DW1000Ranging.attachTokenHandoffAck(tokenHandoffAck);
    DW1000Ranging.attachTokenHandoffNack(tokenHandoffNack);

    DW1000Ranging.attachAggregatedDataReport(aggregatedDataReport);  
    DW1000Ranging.attachDataReportAck(dataReportAck);
}

uint8_t getOwnShortAddress() {
    byte* sa = DW1000Ranging.getCurrentShortAddress();
    return (uint8_t)sa[0];
}

void resetFSMVariables(){

    /*In case a cycle is aborted or a initiator timeout happens*/
    num_retries = 0;
    _discovery = false;
    _ranging = false; 
    _wait_for_range = false;
    _wait_token_handoff_ack = false;
    _wait_for_return = false;
    return_received = false;
    _wait_return_to_parent_ack = false;
    
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
            Serial.println("Pending (discovered via blink)");
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

    if(DEBUG_SLAVE){
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
                }  
                else{ Existing_devices[i].is_node = false;}
            }
            return;
        }
    }

    //If code reaches this point, the device wasn't previously registered. I add it to the list

    if (amount_devices >= MAX_DEVICES) {
        if (DEBUG_SLAVE) {
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
            if(examined_node_short_addr_header == parent_address) continue; 

            if((Existing_devices[searchDevice(examined_node_short_addr_header)].cycle_id) == DW1000RangingClass.getOwnCycleId()){
                //If the examined node has the same cycle ID as me, then it has received the token from someone else. I skip it.
                continue;
            }

                for(int j = 0; j<amount_devices; j++){
                    if(Existing_devices[j].short_addr == examined_node_short_addr_header && Existing_devices[j].is_node && Existing_devices[j].active){
                        // In order to examine a device, it must: 
                        // 1) be a node 2) Be active 3) have an existing & active measure with the coordinator.

                        if(measurements[i].distance < min_distance){
                            min_distance_updated = true;
                            min_distance = measurements[i].distance;
                            closest_node_short_addr_header = examined_node_short_addr_header;
                        }
                        
                    }
                }
            }       
        }
    if(min_distance_updated) return closest_node_short_addr_header;
    else return -1;

}



void tokenHandoff(uint8_t incoming_cycle_id){

    uint8_t requesting_address = 0;
    DW1000Device* requesting_device = DW1000Ranging.getDistantDevice();
    if(requesting_device) requesting_address = requesting_device->getShortAddressHeader();
    else requesting_address = 0;
    uint8_t own_cycle_id = DW1000Ranging.getOwnCycleId();

    if(DEBUG_SLAVE){
        Serial.print("\nTOKEN RECEIVED from: [");
        Serial.print(requesting_address,HEX);
        Serial.print("].Incoming Cycle: "); Serial.print(incoming_cycle_id);
        Serial.print("Current Cycle: "); Serial.println(own_cycle_id);
    }
    

    if(i_have_token){

        //In case my ACK was lost before, i re-send a mercy Ack
        if(DEBUG_SLAVE) Serial.print("But I already have the token. Sending mercy ACK... ");
        transmitUnicast(MSG_TOKEN_HANDOFF_ACK,requesting_device);

    }

    if(incoming_cycle_id == own_cycle_id){

        if(requesting_address == parent_address){

            if(DEBUG_SLAVE) Serial.print("It's my parent retrying. Sending mercy ACK... ");
            transmitUnicast(MSG_TOKEN_HANDOFF_ACK,requesting_device);
            delay(50);
            return;
        }
        
        else{
            if(DEBUG_SLAVE) Serial.print("Not my parent with same cycleID. Rejecting token... ");
            transmitUnicast(MSG_TOKEN_HANDOFF_NACK,requesting_device);
            delay(50);
        }
    }

    /*Here, the cycle ID is different than mine. Must mean that the coordinator has started a new one. 
    I stop whatever I'm doing and join this new cycle*/
    

    if(state != IDLE){
        //If I was busy doing something else, i abort it and join the new Cycle
        if(DEBUG_SLAVE){
            Serial.print("But currently busy with state: "); 
            printState();
            Serial.println(". Aborting current task & joining new cycle...");
        }
        resetFSMVariables();
    }


    parent_address = requesting_address; // I save the parent address to know where to send the return data when the time comes.

    DW1000RangingClass.setOwnCycleId(incoming_cycle_id);
    Existing_devices[searchDevice(requesting_address)].cycle_id = incoming_cycle_id;

    transmitUnicast(MSG_TOKEN_HANDOFF_ACK,requesting_device);
    delay(50); //Time to send the token handoff ack. Without this, the parent rarely receives the ack. 5ms is too small. 10 works fine

    if(!(i_have_token)) i_have_token = true;

    switchToInitiator();
    state = DISCOVERY;
    
   
}

void tokenHandoffAck(){

    if(!(state == WAIT_TOKEN_HANDOFF_ACK)) return;
    uint8_t origin_short_addr = DW1000Ranging.getDistantDevice()->getShortAddressHeader();

    if(!(origin_short_addr == token_target_address)){ // If the ACK received is not from the target of the token handoff, it is ignored.

        if(DEBUG_SLAVE){
            Serial.print("Token Handoff ACK received from ["); Serial.print(origin_short_addr,HEX);
            Serial.print("] but expected from ["); Serial.print(token_target_address,HEX); Serial.println("]. ACK ignored");
        }

        return;
    }

    // If code reaches here, the ACK received is valid.    

    if(DEBUG_SLAVE){
        Serial.print("Token handoff ACK received from: [");
        Serial.print(origin_short_addr,HEX);
        Serial.println("]. Token handoff completed. ");
    }

    Existing_devices[searchDevice(origin_short_addr)].token_handoff_pending = false;
    i_have_token = false; 
    switchToResponder();
    state = WAIT_FOR_RETURN;

    
}

void TokenHandoffNack(){

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

    uint8_t own_cycle_id = DW1000RangingClass.getOwnCycleId();
    if(DEBUG_COORDINATOR){
        Serial.print("Token passed to ["); Serial.print(origin_short_addr,HEX); Serial.println("] REJECTED. "); 
    
    }
    Existing_devices[searchDevice(origin_short_addr)].token_handoff_pending = false;
    Existing_devices[searchDevice(origin_short_addr)].cycle_id = own_cycle_id;
    origin_device->setCycleId(own_cycle_id); //To keep both lists updated with same values

    state = TOKEN_HANDOFF_STATE; //If this one was rejected, then I evaluate next
}

void printState(){
    switch(state){
        case IDLE:
            Serial.print("IDLE");
            break;
        case DISCOVERY:
            Serial.print("DISCOVERY");
            break;
        case RANGING:
            Serial.print("RANGING");
            break;
        case WAIT_FOR_RANGE:
            Serial.print("WAIT_FOR_RANGE");
            break;
        case TOKEN_HANDOFF_STATE:
            Serial.print("TOKEN_HANDOFF");
            break;
        case WAIT_TOKEN_HANDOFF_ACK:
            Serial.print("WAIT_TOKEN_HANDOFF_ACK");
            break;
        case RETURN_TO_PARENT:
            Serial.print("RETURN_TO_PARENT");
            break;
        case WAIT_RETURN_TO_PARENT_ACK:
            Serial.print("WAIT_RETURN_TO_PARENT_ACK");
            break;
        case WAIT_FOR_RETURN:
            Serial.print("WAIT_FOR_RETURN");
            break;
    }
}


/*MODE SWITCHING*/

void switchToInitiator(){

    if(DEBUG_SLAVE) {Serial.print("MODE SWITCH --> now: Inititiator... ");}
    device_is_initiator = true; 
    being_initiator_start = current_time; 
    DW1000Ranging.startAsInitiator(DEVICE_ADDR, DW1000.MODE_1, false, NODE);
    delay(50);

    
}

void switchToResponder(){

    if(DEBUG_SLAVE){Serial.print("\nMODE SWITCH --> now: Responder...");}
    device_is_initiator = false;
    DW1000Ranging.startAsResponder(DEVICE_ADDR, DW1000.MODE_1, false, NODE);
    delay(50);
   
}


/*RANGES & LOGGING*/

void newRange(){

    if(_ranging == false) return;

    DW1000Device* origin_device = DW1000Ranging.getDistantDevice();

    uint8_t short_addr_origin   = origin_device->getShortAddressHeader();
    uint8_t incoming_board_type = origin_device->getBoardType();
    uint8_t incoming_cycle_id   = origin_device->getCycleId();
    
    if (Existing_devices[ranging_device_index].short_addr == short_addr_origin && Existing_devices[ranging_device_index].range_pending == true){
        Existing_devices[ranging_device_index].active = true;
        Existing_devices[ranging_device_index].range_pending = false;
        Existing_devices[ranging_device_index].cycle_id = incoming_cycle_id;
        Existing_devices[ranging_device_index].is_node = (incoming_board_type == NODE);

        _wait_for_range = false;  // To restart the timer next time state is state = WAIT_FOR_RANGE.
        state = RANGING;
    }
    else return; //If range arrives from a device with which node is not currently ranging.
    
    
    //Once code gets here, range is valid and device is registered and active. Now it can be logged.
    float dist = DW1000Ranging.getDistantDevice()->getRange();
    float rx_pwr = DW1000Ranging.getDistantDevice()->getRXPower();
    
    logMeasure(own_short_addr,short_addr_origin, dist, rx_pwr);
    
    if(DEBUG_SLAVE){
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
    int index = searchMeasure(own_sa,dest_sa);
    
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

void transmitUnicast(uint8_t message_type,DW1000Device* explicit_target){

    //All transmissions are done here. Receives the message type to send + an optional explicit target. If 2nd parameter is null, the target will be searched inside this function depending on the message type to send.
    
    if(message_type == MSG_POLL_UNICAST){

        DW1000Device* target = DW1000Ranging.searchDeviceByShortAddHeader(Existing_devices[ranging_device_index].short_addr);

        if(target){

            if(DEBUG_SLAVE){
                    Serial.print("Ranging poll transmitted to: [");
                    Serial.print(Existing_devices[ranging_device_index].short_addr,HEX);
                    Serial.println("] via unicast");
                }

            DW1000Ranging.transmitPoll(target);
            
        }

        else{
            if(DEBUG_SLAVE){Serial.println("Target Not found. Ranging poll via unicast not sent");}
            Existing_devices[ranging_device_index].range_pending = false;
            _wait_for_range = false; 
            state = RANGING; 
        }

    }
    else if(message_type == MSG_TOKEN_HANDOFF){

        DW1000Device* target = DW1000Ranging.searchDeviceByShortAddHeader(token_target_address);

        if(target){

            if(DEBUG_SLAVE){
                    Serial.print("Passing token to: [");
                    Serial.print(token_target_address,HEX);
                    Serial.print("] via unicast. ");
                }
            DW1000Ranging.transmitTokenHandoff(target);
        }

        else{
            if(DEBUG_SLAVE) Serial.print("Target Not found. Token handoff not sent");
            // Simply prints the transmission was not sent. the retryTransmission logic handles the retries and, in case of failure, moving on to the next closest node
        }
    }
    else if(message_type == MSG_TOKEN_HANDOFF_ACK){

        DW1000Device* parent = explicit_target ? explicit_target : DW1000Ranging.searchDeviceByShortAddHeader(parent_address);

        parent_address = parent->getShortAddressHeader(); //updates parent address in case it receives an explicit target different from the parent previously saved.

        if(parent){

            if(DEBUG_SLAVE){
                    Serial.print("Token Handoff ACK sent to: [");
                    Serial.print(parent_address,HEX);
                    Serial.println("] via unicast");
                }

            DW1000Ranging.transmitTokenHandoffAck(parent);
                    
        }

        else{
            if(DEBUG_SLAVE) Serial.println("Parent device not found. Sending token handoff ACK via broadcast.");
            DW1000Ranging.transmitTokenHandoffAck(nullptr);
        }

    }
    else if(message_type == MSG_TOKEN_HANDOFF_NACK){

        uint8_t target_address = explicit_target->getShortAddressHeader();
        if(explicit_target){

            if(DEBUG_SLAVE){
                Serial.print("Token Handoff NACK sent to: ["); Serial.print(target_address,HEX);
                Serial.printkn("] via unicast");
            }

            DW1000Ranging.transmitTokenHandoffNack(explicit_target);
        }

        else{

            if(DEBUG_SLAVE) Serial.println("Target Not found. Sending Token handoff Nack via broadcast.");
            DW1000Ranging.transmitTokenHandoffNack(nullptr);
        }

    }
    
    
    else if(message_type == MSG_RETURN_TO_PARENT){

        DW1000Device* parent = DW1000Ranging.searchDeviceByShortAddHeader(parent_address);

        if(parent){

            if(DEBUG_SLAVE){
                    Serial.print("Data report sent to parent: [");
                    Serial.print(parent_address,HEX);
                    Serial.print("] via unicast");
                }
            DW1000Ranging.transmitAggregatedDataReport((Measurement*)measurements, amount_measurements,parent);
        }


        else{
            if(DEBUG_SLAVE){
                Serial.print("Parent ["); 
                Serial.print(parent_address,HEX); 
                Serial.println("] not found. Data report sent via broadcast. ");
            }
            DW1000Ranging.transmitAggregatedDataReport((Measurement*)measurements, amount_measurements, nullptr);
        }

        clearMeasures(); // No matter if transmitting via unicast or broadcast, measures are "cleared" (marked as inactive) to avoid sending obsolete measures next time.
    }
    else if(message_type == MSG_DATA_REPORT_ACK){

        DW1000Device* target = explicit_target ? explicit_target : DW1000Ranging.searchDeviceByShortAddHeader(token_target_address);
        uint8_t data_report_target_address = target->getShortAddressHeader();

        if(target){

            if(DEBUG_SLAVE){
                    Serial.print("Data report ACK transmitted to: [");
                    Serial.print(data_report_target_address,HEX);
                    Serial.println("] via unicast");
                }

            DW1000Ranging.transmitDataReportAck(target);
                    
        }

        else{
            if(DEBUG_SLAVE) Serial.println("Target device not found. Sending data report ACK via broadcast.");
            DW1000Ranging.transmitDataReportAck(nullptr);
        }
    }

}

void retryTransmission(uint8_t message_type){

    if(DEBUG_SLAVE) Serial.print("Retrying... ");
    transmitUnicast(message_type);
    last_retry = current_time;
    num_retries = num_retries +1;

    if(num_retries == MAX_RETRIES){

        num_retries = 0;
                                                 

        if(message_type == MSG_POLL_UNICAST){
            
            PollUnicastFailed();
        }
        
        else if(message_type == MSG_TOKEN_HANDOFF){

            if(DEBUG_SLAVE){Serial.print("\nToken handoff failed. ");}

            TokenHandoffFailed(); 
        }
   
        else if(message_type == MSG_RETURN_TO_PARENT){

            dataReportFailed();
        }
        
    }
}

void PollUnicastFailed(){

    if(DEBUG_SLAVE){
        Serial.print("Poll via unicast with [");
        Serial.print(Existing_devices[ranging_device_index].short_addr,HEX); Serial.println("] FAILED. Moving on to next device. Back to unicast ranging.");
        
    }
    Existing_devices[ranging_device_index].range_pending = false;
    _wait_for_range = false; 
    state = RANGING; 
}

void TokenHandoffFailed(){

    if(DEBUG_SLAVE){
        Serial.print("Token handoff with [");
        Serial.print(getNextHop(),HEX); Serial.println("] FAILED. Retrying token handoff... ");
        
    }
    
    Existing_devices[searchDevice(token_target_address)].active = false;
    measurements[searchMeasure(own_short_addr,getNextHop())].active = false;
    
    // Set both measure and device as inactive, so that it is not selected again when calling getNextHop().

    state = TOKEN_HANDOFF_STATE; // Goes back to handoff. When calling getNextHop(), the failed node won't be selected (it is set as inactive), so token will go to the next closest node. 


}


/*DATA REPORTING*/

void aggregatedDataReport(byte* data){

    DW1000Device* reporting_device = DW1000Ranging.getDistantDevice();

    uint8_t reporting_node_short_addr = reporting_device->getShortAddressHeader();

    if(!(reporting_node_short_addr == token_target_address)){

        // Filters data reports that arrive from devices that weren't expected. There are 2 options: 
        // 1) Token was not passed (token_target_address == -1), so no data reports are expected.
        // 2) A report is expected, but the one that arrives comes from a different device than the token_target_address device.

        if(DEBUG_SLAVE){

            if(token_target_address == -1){
                // If token_target_address == -1 (or 0xFFFF), means the token was not passed, so NO data reports are expected. 
                // All data reports are ignored, but the mercy ack is sent to unblock the node trying to send it.
                Serial.print("\nData report received from [");
                Serial.print(reporting_node_short_addr, HEX);
                Serial.println("] but no reports were expected. Data report ignored. Sending Mercy ACK.");

            }

            else{
                //The token was passed, so the data report is expected to come from the token_target_address device.
                // If a report arrives from a different node, it is ignored, but the mercy ack is sent to avoid blocking the node trying to send it.
                Serial.print("\nData report received from [");
                Serial.print(reporting_node_short_addr, HEX);
                Serial.print("] but expected from [");
                Serial.print(token_target_address, HEX);
                Serial.println("]. Data report ignored. Sending Mercy ACK.");

            }
        }
        transmitUnicast(MSG_DATA_REPORT_ACK,reporting_device);
        delay(50);
        return;
    }

    else{ //The report comes from the valid device

        if(DEBUG_SLAVE){
            Serial.print("Data report received from: [");
            Serial.print(reporting_node_short_addr,HEX);
            Serial.print("]");
        }

        if(state == WAIT_TOKEN_HANDOFF_ACK){ //Implicit ACK reception
         
            
            /*This section is for the following case: the parent didn't receive the token Handoff ACK (THA) but the "son" did receive the TH. In this case, the "son" has finished its rangings and sent the data report back to its parent, but the parent was still busy retrying to get the THA
            Receiving a data report from the son while expecting the THA carrys whithin an implicit reception of said THA.*/

            if(DEBUG_SLAVE){
                Serial.println(" Implicit token handoff ACK reception");

            }
            //1) Manages tokenHandoffAck flags as if it was received
            Existing_devices[searchDevice(reporting_node_short_addr)].token_handoff_pending = false;
            i_have_token = false; 
            switchToResponder();

            //2) Sets the state and flags before processing the data.
            state = WAIT_FOR_RETURN;
            _wait_for_return = true;
            return_received = false;

        }

        if(_wait_for_return == true && return_received == false){ //The device is valid but I wasn't waiting for a report.
            return_received = true;
            _wait_for_return = false; // To restart the timer next time state is WAIT_FOR_RETURN.
            if(DEBUG_SLAVE) Serial.print(" Sending data report ACK and processing data...");
        }

        else if(return_received == true){ //The device is valid + I was waiting for the report
             
            if(DEBUG_SLAVE) Serial.print(" but already received before. Only need to send ACK");
            transmitUnicast(MSG_DATA_REPORT_ACK,reporting_device);
            delay(50);
            return;

        }

        else if(_wait_for_return == false){
            if(DEBUG_SLAVE) Serial.print(" but I wasn't waiting for it anymore. Sending ack of reception but ignoring data");
            transmitUnicast(MSG_DATA_REPORT_ACK,reporting_device);
            delay(50);
            return;
        }

        transmitUnicast(MSG_DATA_REPORT_ACK,reporting_device);
        delay(50);

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

    _wait_for_return = false;
    state = TOKEN_HANDOFF;

    
}

void dataReportAck(){

    uint8_t message_origin_short_addr = DW1000Ranging.getDistantDevice()->getShortAddressHeader();

    if(!(message_origin_short_addr == parent_address)) return;
    else{

        _wait_return_to_parent_ack = false; // To restart the timer next time state is WAIT_RETURN_TO_PARENT_ACK.
        num_retries = 0; // If ACK is received, retries count must be restarted for the next transmissions.
        

        state = IDLE;

        if(DEBUG_SLAVE){
            Serial.print("Data report ACK received from: [");
            Serial.print(DW1000Ranging.getDistantDevice()->getShortAddressHeader(),HEX);
            Serial.println("]. Back to IDLE");
        }
    }
}

void clearMeasures(){
    for(int i=0; i<amount_measurements; i++){
        measurements[i].active = false;
    }
}

void dataReportFailed(){

    if(DEBUG_SLAVE){
        Serial.print("\nData report to parent [");
        Serial.print(parent_address,HEX);
        Serial.println("] FAILED after max retries. Going back to IDLE");
    }
    
    state = IDLE;
}

/*Loop*/

void loop(){

    DW1000Ranging.loop();
    current_time = millis();

    if(device_is_initiator && current_time - being_initiator_start > INITIATOR_TIMEOUT){

        if(DEBUG_SLAVE){Serial.print("INITIATOR TIMEOUT!!. ");}
        switchToResponder();
        resetFSMVariables();
    }
    if(state == IDLE){
        //Simply acts as responder. Answers to polls & waits for token
        if(i_have_token) i_have_token = false;
        if(device_is_initiator){
            switchToResponder();
            if(DEBUG_SLAVE) Serial.println("Waiting for token");
        }

        activateRanging();
        
    }
    else if(state == DISCOVERY){

        if(discovery_previously_done){
            //Only repeats discovery every UPDATE_DISCOVERY_ATTEMPTS cycles
            discovery_attempts++;

            if(DEBUG_SLAVE){
                Serial.print("Discovery attempt: "); Serial.print(discovery_attempts); 
                Serial.print("/");Serial.print(UPDATE_DISCOVERY_ATTEMPTS);
            }

            if(discovery_attempts<UPDATE_DISCOVERY_ATTEMPTS){
                if(DEBUG_SLAVE){
                    Serial.println(" No need to re-discover\n"); 
                }
                state = RANGING; 
                return;
            }

            if(DEBUG_SLAVE) Serial.print("\nRepeating discovery --> ");
            _discovery = false;
            discovery_previously_done = false;
            discovery_attempts = 0;
        }


        if(!_discovery){
            Serial.println("DISCOVERING:\n");

            //Marks all devices as inactive (except the parent) to only range with the discovered (active) ones.
            for(int i = 0; i<amount_devices ; i++){ 
                if(Existing_devices[i].short_addr != parent_address) Existing_devices[i].active = false; 
            }

            _discovery = true;
            DW1000Ranging.setRangingMode(DW1000RangingClass::DISCOVERY); //Discovery is done via broadcast.
            activateRanging();

            nodes_discovered = false;
            discovery_start = current_time;
        }


        if(_discovery && current_time - discovery_start >= DISCOVERY_PERIOD){
            _discovery = false;
            discovery_previously_done = true;
            
            if(nodes_discovered){
                
                state = RANGING;
                if(DEBUG_SLAVE){Serial.print("\nNodes have been found. Now --> ");}
            }
            
            else{
                if(DEBUG_SLAVE){Serial.println("NO NODES DISCOVERED. I am the TAIL. Returning to parent\n");}
                state = RETURN_TO_PARENT;
            }
        }
    }
    else if(state == RANGING){

        if(!nodes_discovered){
            if(DEBUG_SLAVE){
                Serial.print("No nodes to range with. Returning to parent\n");
            }
            state = RETURN_TO_PARENT;
            return;
        }
        if(!_ranging){
            _ranging = true;
            DW1000Ranging.setRangingMode(DW1000RangingClass::UNICAST); // After discovery, ranging is done via unicast.
            activateRanging(); 
            ranging_device_index = -1;

            if(DEBUG_SLAVE){Serial.println("RANGING: ");}

        }
        ranging_device_index++;

        if(ranging_device_index < amount_devices){
            // There still are devices to poll


            if(DEBUG_SLAVE){
                Serial.print("\nUnicast Polling with device: ");
                Serial.print(ranging_device_index+1); 
                Serial.print("/"); Serial.print(amount_devices); Serial.print(" --> ");
            }

            //1st, checks if ranging device is parent. If so, skips it
            //This shouldn't happen, as parent does stopRanging(), but just in case

            if(Existing_devices[ranging_device_index].short_addr == parent_address){

                if(DEBUG_SLAVE){
                    Serial.print("It's my parent! Skipping ranging with [");
                    Serial.print(Existing_devices[ranging_device_index].short_addr,HEX);Serial.println("]");
                }
                return; //Exits. Next loop, ranging_device_index is increased, aiming at the next device.
            }

            if(Existing_devices[ranging_device_index].active == true){
                Existing_devices[ranging_device_index].range_pending = true;
                num_retries = 0;
                transmitUnicast(MSG_POLL_UNICAST); 
                state = WAIT_FOR_RANGE;
            }

            else{
                if(DEBUG_SLAVE){
                    Serial.print("Skipped Polling with an inactive device: ");
                    Serial.println(Existing_devices[ranging_device_index].short_addr,HEX);
                }
                // Do noting. Next loop. index will increase, polling the next device.
            }

        }

        else{
            // All known devices have been polled. Now, hands off the token to closest node.
            if(DEBUG_SLAVE){Serial.print("\nRanging Ended. Now -->  ");}
            _ranging = false;
            state = TOKEN_HANDOFF_STATE;
        }
    }
    else if(state == WAIT_FOR_RANGE){

        if(!_wait_for_range){
            _wait_for_range = true;
            wait_for_range_start = current_time;
            if(DEBUG_SLAVE){
                Serial.print("Waiting for unicast range from: [");
                Serial.print(Existing_devices[ranging_device_index].short_addr,HEX); Serial.println("] ... ");
            }
        }

        else if(current_time - wait_for_range_start >= WAITING_TIME){
            
            _wait_for_range = false; //To restart the timer next time state is WAIT_FOR_RANGE.
            retryTransmission(MSG_POLL_UNICAST);
        }
    }
    else if(state == TOKEN_HANDOFF_STATE){

        if(DEBUG_SLAVE) Serial.println("\n\nTOKEN HANDOFF starts: ");

        stopRanging();

        token_target_address = getNextHop();

        if(token_target_address == -1){
            Serial.println("I am the TAIL. Building up the return\n\n");
            state = RETURN_TO_PARENT;
        }
        else{
            state = WAIT_TOKEN_HANDOFF_ACK;
            _wait_token_handoff_ack = false;
            num_retries = 0;
            transmitUnicast(MSG_TOKEN_HANDOFF); 
            delay(50);
        }
         
    }
    else if(state == WAIT_TOKEN_HANDOFF_ACK){

        if(!_wait_token_handoff_ack){
            _wait_token_handoff_ack = true;
            wait_token_handoff_ack_start = current_time;
            if(DEBUG_SLAVE){
                Serial.print("Waiting for token handoff ACK from: [");
                Serial.print(token_target_address,HEX); Serial.println("]... ");
            }
        }

        else if(current_time - wait_token_handoff_ack_start >= WAITING_TIME){
            
            _wait_token_handoff_ack = false; // To restart the timer next time state is WAIT_TOKEN_HANDOFF_ACK.
            retryTransmission(MSG_TOKEN_HANDOFF);
        }
    }
    else if(state == WAIT_FOR_RETURN){
       
        if(!(_wait_for_return)){

            _wait_for_return = true;
            return_received = false;
            wait_for_return_start = current_time;
            
            if(DEBUG_SLAVE){
                Serial.print("\nWAIT FOR RETURN from: [");
                Serial.print(token_target_address,HEX); Serial.println("]: ");
            }
        }

        else if(current_time - wait_for_return_start >= WAITING_RETURN_TIME){
            
            _wait_for_return = false; // To restart the timer next time state is WAIT_FOR_RETURN.
            if(DEBUG_SLAVE){Serial.print("WAIT FOR RETURN TIMEOUT. Sending my report to my parent: ["); Serial.print(parent_address,HEX); Serial.println("]\n ");}
            state = RETURN_TO_PARENT;
        }
    }
    else if(state == RETURN_TO_PARENT){

        state = WAIT_RETURN_TO_PARENT_ACK;
        _wait_return_to_parent_ack = false;
        num_retries = 0;
        transmitUnicast(MSG_RETURN_TO_PARENT);
        delay(50);
        
    }     
    else if(state == WAIT_RETURN_TO_PARENT_ACK){

        if(!(_wait_return_to_parent_ack)){
            _wait_return_to_parent_ack = true;
            wait_return_to_parent_ack_start = current_time;
            if(DEBUG_SLAVE){
                Serial.print("... Waiting for data report ACK from: [");
                Serial.print(parent_address,HEX); Serial.println("]...");
            }
        }
        else if(current_time - wait_return_to_parent_ack_start >= WAITING_TIME){
            
            _wait_return_to_parent_ack = false; // To restart the timer next time state is WAIT_RETURN_TO_PARENT_ACK.
            retryTransmission(MSG_RETURN_TO_PARENT);
        }
    }
}