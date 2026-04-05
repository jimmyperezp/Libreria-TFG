/*1 Initiator - 1 Responder*/
// This example measures the distance between 1 initiator and 1 responder. It does it by using the TWR (Two Way Ranging) protocol. 

#include <SPI.h>
#include "DW1000Ranging.h"
#include "DW1000.h"

// Device naming: the two foremost left bytes are the 'shortAddressHeader'.
// In this example, I'll use 'A1' for the initiator, and 'B1' for the responder. 
#define DEVICE_ADDR "A1:00:5B:D5:A9:9A:E2:9C"

// Antenna Delay: Sustituir con el valor obtenido en la calibracion
uint16_t Adelay = 16580;

//Definiciones de Pines de la placa usada:
#define SPI_SCK 18
#define SPI_MISO 19
#define SPI_MOSI 23
#define DW_CS 4

const uint8_t PIN_RST = 27; // reset pin
const uint8_t PIN_IRQ = 34; // irq pin
const uint8_t PIN_SS = 4;   // spi select pin

void setup(){

  Serial.begin(115200);
  delay(1000); // Gives the program 1 second to start the serial monitor. 

  /*1: Init configuration*/

    // 1: Sets up the SPI bus. It requires the SCK, MISO and MOSI pins to be defined.
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

    // 2: DW1000 start
  DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); //Reset, CS, IRQ pin


  // 3: Save the antenna delay measured with the initiator_delay_calibration file.
  // Internally, it saves this value in a specific refister of the DW1000, so it only needs to be done once.
  DW1000.setAntennaDelay(Adelay);

  DW1000Ranging.attachNewRange(newRange); // When a full TWR exchange has been completed, the library calls function newRange (defined later on)
  DW1000Ranging.attachNewDevice(newDevice); // Same as newRange, but for when detecting & losing devices. 
  DW1000Ranging.attachInactiveDevice(inactiveDevice);

    // 4: Starts the board giving it a TWR 'role' (initiator or responder). 

  // The startAs functions receive: device address - mode of operation - board type (coordinator, node or tag)

  DW1000Ranging.startAsInitiator(DEVICE_ADDR, DW1000.MODE_1,COORDINATOR);
  
  /*The different possible modes are: 
    DW1000.MODE_1 110kbs, 64 MHz PRF, 128-symbols preamble
    DW1000.MODE_2 850kbs, 64 MHz PRF, 128-symbols preamble
    DW1000.MODE_3 6800kbs, 64 MHz PRF, 128-symbols preamble
    DW1000.MODE_4 110kbs, 16 MHz PRF, 128-symbols preamble
    DW1000.MODE_5 850kbs, 16 MHz PRF, 128-symbols preamble
    DW1000.MODE_6 6800kbs, 16 MHz PRF, 128-symbols preamble
  */
}

void loop()
{
  DW1000Ranging.loop();
}

void newRange()
{
  //Called after having completed a measure. Now, I can show the results in the serial monitor:
  Serial.print(" From: ");  Serial.print(DW1000Ranging.getDistantDevice()->getShortAddressHeader(), HEX);
  // getDistantDevice gives the device with which the comunication was last made. Then, I can ask for its short address, range, RX power, etc.

  Serial.print("\t Distance: ");  Serial.print(DW1000Ranging.getDistantDevice()->getRange());  Serial.print(" m");
  Serial.print("\t RX power: ");  Serial.println(DW1000Ranging.getDistantDevice()->getRXPower());

}

void newDevice(DW1000Device *device){

  Serial.print("Device added: ");
  Serial.println(device->getShortAddress(), HEX);
}

void inactiveDevice(DW1000Device *device){

  Serial.print("Delete inactive device: ");
  Serial.println(device->getShortAddress(), HEX);
}