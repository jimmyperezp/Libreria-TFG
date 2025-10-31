/*This tests consists on checking the ranging after doing a mode switch from the master to the slave.*/

/* Code runs good when using 1 master, 1 slave and 1 tag.
Flags and states are not adapted to using more than those devices */


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

#define DEBUG false

#define IS_MASTER true
#define DEVICE_ADDR "A1:00:5B:D5:A9:9A:E2:9C" 
uint16_t own_short_addr = 0; 
uint16_t Adelay = 16580;

#define MAX_DEVICES 5
Measurement measurements[MAX_DEVICES];
uint8_t amount_measurements = 0;

ExistingDevice Existing_devices[MAX_DEVICES];
uint8_t amount_devices = 0;

unsigned long current_time = 0; 
const unsigned long ranging_period = 500;
const unsigned long timeout = 50;

unsigned long slave_ranging_begin =0;
unsigned long mode_switch_request = 0;

unsigned long last_retry = 0;
unsigned long ranging_begin = 0;

uint8_t state = 1;
#define MASTER_RANGING 1
#define MODE_SWITCH 2
#define WAIT_SLAVE 3
#define WAIT 4

static bool master_ranging = false;
static bool seen_first_range = false;
static bool stop_ranging_requested = false;
static bool slave_is_responder = true;
static bool mode_switch_pending = false;

static bool switch_to_initiator = false;

static bool test_started = false;

static bool slave_disconnected = true;
static bool waiting_slaves = false;

uint8_t MSG_DATA_REQUEST = 1;
uint8_t MSG_MODE_SWITCH = 2;

uint8_t slave_position = 0;
uint8_t num_retries = 0;


/* CODE */


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

    DW1000Ranging.attachModeSwitchAck(ModeSwitchAck);
    

    DW1000Ranging.startAsInitiator(DEVICE_ADDR,DW1000.MODE_1, false,MASTER_ANCHOR);

    own_short_addr = getOwnShortAddress();

    state = WAIT_SLAVE;
    
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
        slave_position = amount_devices;
        slave_disconnected = false;
        if(DEBUG){Serial.println("Este es un slave.");}
        
    }
    else{ 
        Existing_devices[amount_devices].is_slave_anchor = false;
    }

    Existing_devices[amount_devices].is_responder = true;
    Existing_devices[amount_devices].active = true;
    
    
    amount_devices ++;
}


uint8_t getSlaveIndex(){

    for (int i = 0; i < amount_devices; i++){

        if(Existing_devices[i].is_slave_anchor == true){
            return i;
        }
        
    }
    return -1;
}


bool isSlaveResponder(){

    for(int i = 0; i< amount_devices; i++){

        if(Existing_devices[i].is_slave_anchor){

            return Existing_devices[i].is_responder;
        }
    }

}


void transmitUnicast(uint8_t message_type){

    
    DW1000Device* target = DW1000Ranging.searchDistantDevice(Existing_devices[getSlaveIndex()].byte_short_addr);

    if(target){

        if(message_type == MSG_MODE_SWITCH){

            if(mode_switch_pending){
                slave_position = getSlaveIndex();

                if(DEBUG){
                    Serial.print("Solicitado el cambio de ");
                    Serial.print(Existing_devices[slave_position].short_addr,HEX);
                    Serial.print(switch_to_initiator ?  " a initiator" : " a responder");
                    Serial.println(" por unicast");
                }

                DW1000Ranging.transmitModeSwitch(switch_to_initiator,target);
                waitForResponse(timeout);


                
            }

        }

        
    }

}


void waitForResponse(uint16_t waiting_time){

    uint32_t t0 = millis(); 
    if(DEBUG){Serial.println("Esperando para el Ack");}

    
    while((uint32_t)(millis()-t0)<waiting_time){

        DW1000Ranging.loop();
        if (!mode_switch_pending) break;
        
        
    }
    return;

}


void retryTransmission(uint8_t message_type){

    transmitUnicast(message_type);
    last_retry = current_time;
    num_retries = num_retries +1;

    if(num_retries == 5){

        num_retries = 0;
                                                  

        if(message_type == MSG_MODE_SWITCH){
            
            
            mode_switch_pending = false;

            if(DEBUG){Serial.println("Cambio fallido. Regreso a ranging");}

        }
        
        
        slave_is_responder = isSlaveResponder();
        state =  MASTER_RANGING;
         
               
    }
}

                

void newRange(){

    uint16_t dest_sa = DW1000Ranging.getDistantDevice()->getShortAddress();
    float dist = DW1000Ranging.getDistantDevice()->getRange();
    float rx_pwr = DW1000Ranging.getDistantDevice()->getRXPower();

    logMeasure(own_short_addr,dest_sa, dist, rx_pwr);
    

    if(stop_ranging_requested){

        
        if(DEBUG){Serial.println("El ranging ha terminado");}      
        
    }
    
    if(!seen_first_range){
        seen_first_range = true;
        ranging_begin = current_time;
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
    amount_devices--;
    
    if(dest_sa == Existing_devices[getSlaveIndex()].short_addr){

        slave_disconnected = true;
        if(mode_switch_pending){
            mode_switch_pending = false;
            state = WAIT_SLAVE;
        }
        
    }
    
    
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


void activateRanging(){

    
    DW1000Ranging.setStopRanging(false);
    stop_ranging_requested = false;
    
    seen_first_range = false;
   
}


void stopRanging(){

    DW1000Ranging.setStopRanging(true);
    stop_ranging_requested = true;
    
}


void ModeSwitchAck(bool isInitiator){

    if(mode_switch_pending){ // To avoid false reads
        
        uint16_t origin_short_addr = DW1000Ranging.getDistantDevice()->getShortAddress();

        if(getSlaveIndex()>0){
            
            if(Existing_devices[getSlaveIndex()].short_addr == origin_short_addr){



                if(DEBUG){
                        Serial.print(" Cambio Realizado: ");
                        Serial.print(origin_short_addr,HEX);
                        Serial.print(" es --> ");
                        Serial.println(slave_is_responder ? "Responder" : "Initiator");
                }
            }
        }

    }
}
              



void loop(){

    DW1000Ranging.loop();
    current_time = millis();

       

    if(state == WAIT_SLAVE){

        if(!waiting_slaves){
            waiting_slaves = true;
            activateRanging();
            Serial.println("Esperando a detectar un esclavo para iniciar el ranging");
        }
        else{
        
            if(!slave_disconnected){
               Serial.println("Esclavo detectado. Comienza el ciclo");
               waiting_slaves = false;
               state = MASTER_RANGING;
            }
        }
    }



    else if(state==MASTER_RANGING){

        if(!test_started){

            test_started = true;
            ranging_begin = current_time;
            activateRanging();
        }

        if(seen_first_range && current_time - ranging_begin >= ranging_period){
        
            if(DEBUG){Serial.println("Master termina de hacer ranging");}
            stopRanging();
            state = MODE_SWITCH;
        }
    }

    else if(state == MODE_SWITCH){

        //only changes to initiator
        
        if(!mode_switch_pending){
        
            mode_switch_pending = true;
            mode_switch_request = current_time;
            switch_to_initiator = true;
            transmitUnicast(MSG_MODE_SWITCH);
        }

        if(mode_switch_pending && current_time - mode_switch_request >= 500 ){

            if(current_time - last_retry >= 500){

                if(DEBUG){Serial.println("reintentando el mode switch...");}
                retryTransmission(MSG_MODE_SWITCH);
            }

        }
    }

    else if(state == WAIT){}
}