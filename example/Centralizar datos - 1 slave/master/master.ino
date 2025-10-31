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
uint8_t own_short_addr = 0; 
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
unsigned long data_report_request = 0;
unsigned long last_retry = 0;
unsigned long ranging_begin = 0;

uint8_t state = 1;
#define MASTER_RANGING 1
#define SLAVE_RANGING 2
#define MODE_SWITCH 3
#define DATA_REPORT 4
#define WAIT_SLAVE 5

static bool master_ranging = false;
static bool seen_first_range = false;
static bool stop_ranging_requested = false;
static bool slave_ranging = false;
static bool mode_switch_pending = false;

static bool switch_to_initiator = false;
static bool data_report_pending = false;
static bool slave_is_responder = true;
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

    

    DW1000Ranging.attachDataReport(DataReport);
    DW1000Ranging.attachModeSwitchAck(ModeSwitchAck);
    

    DW1000Ranging.startAsInitiator(DEVICE_ADDR,DW1000.MODE_1, false,MASTER_ANCHOR);

    own_short_addr = getOwnShortAddress();

    state = WAIT_SLAVE;
    slave_is_responder = true;
}


uint16_t getOwnShortAddress() {
    byte* sa = DW1000Ranging.getCurrentShortAddress();
    //return ((uint16_t)sa[0] << 8) | sa[1];
    return (uint8_t)sa[0];
}


int searchDevice(uint8_t own_sa,uint8_t dest_sa){
    
    for (int i=0 ; i < amount_measurements ; i++){

        if ((measurements[i].short_addr_origin == own_sa)&&(measurements[i].short_addr_dest == dest_sa)) {
            return i; 
            // If found, returns the index
        }
    }
    return -1; // if not, returns -1
}


void registerDevice(DW1000Device *device){


    Existing_devices[amount_devices].short_addr = device->getShortAddressHeader();
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


    DW1000Device* target = DW1000Ranging.searchDeviceByShortAddHeader(Existing_devices[getSlaveIndex()].short_addr);

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

        else if(message_type == MSG_DATA_REQUEST){

            if(DEBUG){Serial.println("Data report solicitado por UNICAST");}    
            DW1000Ranging.transmitDataRequest(target);
            
            waitForResponse(timeout);

        }
    }

    else{
        if(DEBUG){Serial.println("No encontrado el dispositivo destino. Transmisión no enviada.");}
    }

}


void waitForResponse(uint16_t waiting_time){

    uint32_t t0 = millis(); 
    if(DEBUG){Serial.println("Esperando para el Ack");}

    
    while((uint32_t)(millis()-t0)<waiting_time){

        DW1000Ranging.loop();
        if (!mode_switch_pending) break;
        if (!data_report_pending) break;
        
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
        else if(message_type == MSG_DATA_REQUEST){

            data_report_pending = false;
            

            if(DEBUG){Serial.println("Data Report NO recibido. Regreso a ranging");}
        }
        
        slave_is_responder = isSlaveResponder();
        state =  MASTER_RANGING;
         
               
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

    uint8_t destiny_short_addr = DW1000Ranging.getDistantDevice()->getShortAddressHeader();
    float dist = DW1000Ranging.getDistantDevice()->getRange();
    float rx_pwr = DW1000Ranging.getDistantDevice()->getRXPower();

    logMeasure(own_short_addr,destiny_short_addr, dist, rx_pwr);
    

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
    Serial.println(device->getShortAddressHeader(), HEX);

    registerDevice(device);

    
}


void inactiveDevice(DW1000Device *device){

    uint8_t origin_short_addr = device->getShortAddressHeader();
    Serial.print("Lost connection with device: ");
    Serial.println(origin_short_addr, HEX);
    amount_devices--;
    
    if(origin_short_addr == Existing_devices[getSlaveIndex()].short_addr){

        slave_disconnected = true;
        if(mode_switch_pending){
            mode_switch_pending = false;
            state = WAIT_SLAVE;
        }
        else if(data_report_pending){
            data_report_pending = false;
            state = WAIT_SLAVE;
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
        
        uint8_t origin_short_addr = DW1000Ranging.getDistantDevice()->getShortAddressHeader();

        if(getSlaveIndex()>0){
            if(Existing_devices[getSlaveIndex()].short_addr == origin_short_addr){



                slave_is_responder = !isInitiator;
                mode_switch_pending = false;
                
                num_retries = 0;
                if(switch_to_initiator){
                    state = SLAVE_RANGING;
                }
                else{
                    state = DATA_REPORT;
                }

                Existing_devices[getSlaveIndex()].is_responder = slave_is_responder;


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


void DataReport(byte* data){

    
    uint8_t origin_short_addr = DW1000Ranging.getDistantDevice()->getShortAddressHeader();
    uint16_t index = SHORT_MAC_LEN+1;
    uint8_t numMeasures = data[index++];

    //First, I check if the size is OK:
    if(numMeasures*5>LEN_DATA-SHORT_MAC_LEN-4){
        // x5 because every measure takes 5 bytes.
        Serial.println("The Data received is too long");
        return;
    }

    for (int i = 0; i < numMeasures; i++) {

        uint16_t destiny_short_addr = data[index++];

        uint16_t distance_cm;
        memcpy(&distance_cm, data + index, 2); 
        index += 2;
        // Now, transforms the distance in cm to distance in meters.
        float distance = (float)distance_cm / 100.0f;

        int16_t _rxPower; // int16_t (already has a sign)
        memcpy(&_rxPower, data + index, 2); 
        index += 2; 
        // Same as before, now I transform it.
        float rxPower = (float)rxPower_tx / 100.0f;

        logMeasure(origin_short_addr, destiny_short_addr, distance, rxPower);
    }
    if(DEBUG){
        Serial.print("Data report recibido de: ");
        Serial.print(origin_short_addr,HEX);
    }
    
    if(data_report_pending){

        data_report_pending = false;
        num_retries = 0;
        
    }


    showData();
    if(DEBUG){Serial.println("Recibido el data report. Regreso a MASTER RANGING");}
    state = MASTER_RANGING;
}


void loop(){

    DW1000Ranging.loop();
    current_time = millis();

    if(state == MASTER_RANGING){

        if(!master_ranging){
            Serial.println("El master comienza a hacer ranging");
            master_ranging = true;
            activateRanging();

        }

        if(master_ranging){

            if(seen_first_range && current_time - ranging_begin >= ranging_period){

                stopRanging();
                master_ranging = false;
                state = MODE_SWITCH;
            }
        }

    }

    else if (state == SLAVE_RANGING){

        if(!slave_ranging){
            slave_ranging = true;
            slave_ranging_begin = current_time;
        }

        else{

            if(current_time - slave_ranging_begin > ranging_period){
                slave_ranging = false;
                state = MODE_SWITCH;

            }
        }

    }

    else if (state == MODE_SWITCH){

        if(!mode_switch_pending){
        
            mode_switch_pending = true;
            

            if(slave_is_responder){
                switch_to_initiator = true;  
            }
            else{
                switch_to_initiator = false;
            }
            
            mode_switch_request = current_time;
            transmitUnicast(MSG_MODE_SWITCH);
        }

        if(mode_switch_pending && current_time - mode_switch_request >= 500 ){

            if(current_time - last_retry >= 500){

                if(DEBUG){Serial.println("reintentando el mode switch...");}
                retryTransmission(MSG_MODE_SWITCH);
            }

        }



    }

    else if(state == DATA_REPORT){

        if(!data_report_pending){

            data_report_pending = true;
            data_report_request = current_time;

            transmitUnicast(MSG_DATA_REQUEST);
        }

        

        if(data_report_pending && current_time - data_report_request >= 500){

            if(current_time-last_retry >=500){ 
                
                    if(DEBUG){Serial.println("Reintentando data report...");}
                    retryTransmission(MSG_DATA_REQUEST);
                                               
                }
        }

        
    }

    else if(state == WAIT_SLAVE){

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
}