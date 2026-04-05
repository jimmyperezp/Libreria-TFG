//Posicionamiento 2D
// Código del ANCHOR 1:

#include <SPI.h>
#include "DW1000Ranging.h"
#include "DW1000.h"

// Device naming: the two foremost left bytes are the 'shortAddressHeader'.
// In this example, I'll use 'A' for anchors and 'B' for tags
#define DEVICE_ADDR "A1:17:5B:D5:A9:9A:E2:9C" 

// Antenna delay: calibrate and substitute with updated value
uint16_t Adelay = 16580;

// ESP32's boards pin definitions
#define SPI_SCK 18
#define SPI_MISO 19
#define SPI_MOSI 23
#define DW_CS 4

const uint8_t PIN_RST = 27; // reset pin
const uint8_t PIN_IRQ = 34; // irq pin
const uint8_t PIN_SS = 4;   // spi select pin

void setup(){

  Serial.begin(115200);
  delay(1000); // 1 second for the serial monitor to start

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); //Reset, CS, IRQ pin

  DW1000.setAntennaDelay(Adelay);

  DW1000Ranging.attachNewRange(newRange);
  DW1000Ranging.attachNewDevice(newDevice);
  DW1000Ranging.attachInactiveDevice(inactiveDevice);

  DW1000Ranging.startAsResponder(DEVICE_ADDR, DW1000.MODE_1, COORDINATOR); 
}

  void loop(){
  DW1000Ranging.loop();
}

void newRange()
{

  Serial.print("From: ");         Serial.print(DW1000Ranging.getDistantDevice()->getShortAddress(), HEX);
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

