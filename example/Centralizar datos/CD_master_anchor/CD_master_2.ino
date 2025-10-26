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


uint8_t state = 1;
#define MASTER_RANGING 1
#define SLAVES_RANGING 2
#define MODE_SWITCH 3
#define DATA_REPORT 4
#define WAIT_SLAVES 5


unsigned long current_time = 0; 
unsigned long ranging_begin = 0;
unsigned long slaves_ranging_begin = 0;

unsigned long mode_switch_started = 0;
unsigned long data_report_started = 0;
unsigned long last_retry = 0;

uint8_t num_retries = 0;
//Todo: si voy a usar el mismo tiempo, podría unificar en una sola variable
const unsigned long waiting_time = 500;
const unsigned long retry_time = 500;
const unsigned long ranging_period = 500;
const unsigned long timeout = 50;


static bool slaves_active = false;
static bool slaves_are_ranging = false;
static bool wait_slaves = false;
uint8_t amount_slaves = 0;

static bool master_ranging = false;
static bool seen_first_range = false;
static bool stop_ranging_requested = false;

static bool mode_switch_pending = false;
static bool switch_to_initiator = false;

static bool mode_switch_done = false;
uint8_t mode_switch_acks_received = 0;
uint8_t expected_mode_switches = 0;

static bool data_report_pending = false;
uint8_t expected_data_reports = 0;
uint8_t data_reports_received = 0;
 

uint8_t MSG_DATA_REQUEST = 1;
uint8_t MSG_MODE_SWITCH = 2;



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

    state = WAIT_SLAVES;
    
}


uint16_t getOwnShortAddress() {
    byte* sa = DW1000Ranging.getCurrentShortAddress();
    return ((uint16_t)sa[0] << 8) | sa[1];
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


void registerDevice(DW1000Device *device){


    Existing_devices[amount_devices].short_addr = device->getShortAddress();
    memcpy(Existing_devices[amount_devices].byte_short_addr, device->getByteShortAddress(), 2);
    uint8_t board_type = device->getBoardType();

    if(board_type == SLAVE_ANCHOR){
        Existing_devices[amount_devices].is_slave_anchor = true;
        slaves_active = true;
        
    }
    else{ 
        Existing_devices[amount_devices].is_slave_anchor = false;
    }

    Existing_devices[amount_devices].is_responder = true;
    Existing_devices[amount_devices].active = true;
    
    
    amount_devices ++;
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
    
    for(int i = 0; i<amount_devices; i++){

        Existing_devices[i].active = false;
        if(Existing_devices[i].is_slave_anchor){
            amount_slaves--;
            if(amount_slaves == 0){
                slaves_active = false;
                state = WAIT_SLAVES;
            }
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




void stopRanging(){

    DW1000Ranging.setStopRanging(true);
    stop_ranging_requested = true;
    
}

void activateRanging(){

    DW1000Ranging.setStopRanging(false);
    stop_ranging_requested = false;
    seen_first_range = false;
   
}


void waitForResponse(uint16_t waiting_time){

    uint32_t t0 = millis(); 
    if(DEBUG){Serial.println("Esperando para el Ack");}

    
    while((uint32_t)(millis()-t0)<waiting_time){

        DW1000Ranging.loop();
                
    }
    return;

}

void transmitUnicast(uint8_t message_type){

    for(int i=0; i<amount_devices;i++){

        if(Existing_devices[i].is_slave_anchor == true){

            DW1000Device* target = DW1000Ranging.searchDistantDevice(Existing_devices[i].byte_short_addr);

            if(target){

                if(message_type == MSG_MODE_SWITCH){

                    if(Existing_devices[i].mode_switch_pending == true){

                        if(Existing_devices[i].is_responder == true){

                            switch_to_initiator = true;

                            if(DEBUG){

                                Serial.print("Mode switch enviado a: ");
                                Serial.print(Existing_devices[i].short_addr,HEX);
                                Serial.print("Cambio a: ");
                                Serial.println(switch_to_initiator ? " initiator." : " responder.");
                            }

                            DW1000Ranging.transmitModeSwitch(switch_to_initiator,target);
                            waitForResponse(timeout);



                        }

                        if(Existing_devices[i].is_responder == false){

                            switch_to_initiator = false;

                            if(DEBUG){

                                Serial.print("Mode switch enviado a: ");
                                Serial.print(Existing_devices[i].short_addr,HEX);
                                Serial.print("Cambio a: ");
                                Serial.println(switch_to_initiator ? " initiator." : " responder.");
                            }

                            DW1000Ranging.transmitModeSwitch(switch_to_initiator,target);
                            waitForResponse(timeout);
                            
                        }




                    }

                }

                else if(message_type == MSG_DATA_REQUEST){

                    if(Existing_devices[i].data_report_pending == true){

                        if(DEBUG){
                            Serial.print("Data request enviado a: ");
                            Serial.print(Existing_devices[i].short_addr,HEX);
                            
                        }

                        DW1000Ranging.transmitDataRequest(target);
                        
                        waitForResponse(timeout);
                        
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
                                                 

        if(message_type == MSG_MODE_SWITCH){
            
            
            if(DEBUG){Serial.println("MODE SWITCH FALLIDO");}
            modeSwitchFailed();          

        }
        else if(message_type == MSG_DATA_REQUEST){

            if(DEBUG){Serial.println("DATA REPORT FALLIDO");}
            DataReportFailed();
            
            
        }
   
    }
}



void modeSwitchFailed(){

    bool switching_to_initiator = false;

    for(int i = 0; i<amount_devices;i++){

        if(Existing_devices[i].is_slave_anchor && mode_switch_pending == true){


            Existing_devices[i].mode_switch_pending = false;
            bool switching_to_initiator = Existing_devices[i].is_responder;
            Existing_devices[i].is_responder = !Existing_devices[i].is_responder;
   
            
        }

    }

    mode_switch_pending = false;
    mode_switch_done = true;
    mode_switch_acks_received = 0;

    if(switching_to_initiator){
        state = SLAVES_RANGING;
    }
    else{
        state = DATA_REPORT;
    }
}


void DataReportFailed(){

    for(int i = 0; i<amount_devices;i++){

        if(Existing_devices[i].is_slave_anchor && data_report_pending == true){



            Existing_devices[i].data_report_pending = false;
            
            data_reports_received = 0;
            data_report_pending = false;
            
            showData();

            
        }
    }


}

void DataReport(byte* data){

    
    
    uint16_t index = SHORT_MAC_LEN + 1;
    uint16_t origin_short_addr = ((uint16_t)data[index+1] << 8) | data[index];
    
    for(int i=0;i<amount_devices;i++){

        if(Existing_devices[i].short_addr == origin_short_addr){

            Existing_devices[i].data_report_pending = false;
            data_reports_received++;

            index += 2;

            uint16_t numMeasures = data[index++];

            //First, I check if the size is OK:
            if(numMeasures*10>LEN_DATA-SHORT_MAC_LEN-4){
        
                Serial.println("The Data received is too long");
                return;
            }

            for (int i = 0; i < numMeasures; i++) {

                uint16_t destiny_short_addr = ((uint16_t)data[index] << 8) | data[index + 1];
                index += 2;

                float distance, rxPower;
                memcpy(&distance, data + index, 4); index += 4;
                memcpy(&rxPower, data + index, 4); index += 4;

                logMeasure(origin_short_addr, destiny_short_addr, distance, rxPower);
            }

            if(DEBUG){
                Serial.print("Data report recibido de: ");
                Serial.print(origin_short_addr,HEX);
            }

        }
    }
    
    
    if(data_reports_received == expected_data_reports){

        data_report_pending = false;
        showData();

        if(DEBUG){Serial.println("vuelvo a master ranging");}
        state = MASTER_RANGING;
    }
    
    


    
    
}


void ModeSwitchAck(bool isInitiator){

    if(mode_switch_pending){ // To avoid false reads
        
        uint16_t origin_short_addr = DW1000Ranging.getDistantDevice()->getShortAddress();

        
        for(int i = 0; i <amount_devices; i++){

            if(Existing_devices[i].short_addr == origin_short_addr && Existing_devices[i].is_slave_anchor && Existing_devices[i].mode_switch_pending == true){

                Existing_devices[i].mode_switch_pending = false;
                Existing_devices[i].is_responder =  !isInitiator;
                
                mode_switch_acks_received++;


            }

            if(DEBUG){
                    Serial.print(" Cambio Realizado: ");
                    Serial.print(origin_short_addr,HEX);
                    Serial.print(" es --> ");
                    Serial.println(isInitiator ? "INITIATOR" : "RESPONDER");
                }

        }

        if(mode_switch_acks_received == expected_mode_switches){


            mode_switch_acks_received = 0;
            mode_switch_pending = false;
            mode_switch_done = true;

            if(isInitiator){
                
                state = SLAVES_RANGING; 
            }
            else{
                
                state = DATA_REPORT;
            }
        }

        

    }
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

    else if (state == SLAVES_RANGING){

        if(!slaves_are_ranging){
            slaves_are_ranging = true;
            slaves_ranging_begin = current_time;
        }
        
        else{

            if(current_time - slaves_ranging_begin > ranging_period){
                slaves_are_ranging = false;
                state = MODE_SWITCH;
            }
        }

    }

    else if (state == MODE_SWITCH){

        if(!mode_switch_pending){

            mode_switch_pending = true;
            expected_mode_switches = 0;
            
            mode_switch_started = current_time;

            for(int i = 0; i<amount_devices;i++){

                if(Existing_devices[i].is_slave_anchor && Existing_devices[i].active){

                    Existing_devices[i].mode_switch_pending = true;
                    expected_mode_switches++;
                }
            }

            transmitUnicast(MSG_MODE_SWITCH);
        }

        
        if(mode_switch_pending && current_time - mode_switch_started >= waiting_time){

            if(current_time - last_retry >= retry_time){

                if(DEBUG){Serial.println("reintentando el mode switch...");}
                retryTransmission(MSG_MODE_SWITCH);

            }
            

        }
        
        
  

    }

    else if(state == DATA_REPORT){

        if(!data_report_pending){
            data_report_pending = true;
            data_report_started = current_time;

            for(int i = 0; i<amount_devices;i++){

                if(Existing_devices[i].is_slave_anchor && Existing_devices[i].active){

                    Existing_devices[i].data_report_pending = true;
                    expected_data_reports++;
                }
            }

            data_report_started = current_time;
            transmitUnicast(MSG_DATA_REQUEST);
        }

        if(data_report_pending && current_time - data_report_started >= waiting_time){

            if(current_time - last_retry >= retry_time){
                
                if(DEBUG){Serial.println("Reintentando data report...");}
                retryTransmission(MSG_DATA_REQUEST);
            }
        }



        
    }

    else if(state == WAIT_SLAVES){

        if(!wait_slaves){

            wait_slaves = true;
            mode_switch_pending = false;
            mode_switch_acks_received = 0;

            data_report_pending = false;
            data_reports_received = 0;

            for(int i = 0; i<amount_devices; i++){

                Existing_devices[i].mode_switch_pending = false;
                Existing_devices[i].data_report_pending = false;
            }

        }

        else{

            if(slaves_active = true){
                wait_slaves = false;
                state = MASTER_RANGING;
            }
        }
        
    }

}