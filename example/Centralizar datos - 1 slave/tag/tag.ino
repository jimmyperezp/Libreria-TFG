// TAG (Will only act as a responder.)

#include <SPI.h>
#include "DW1000Ranging.h"
#include "DW1000.h"

#define SPI_SCK 18
#define SPI_MISO 19
#define SPI_MOSI 23
#define DW_CS 4

// connection pins
const uint8_t PIN_RST = 27; // reset pin
const uint8_t PIN_IRQ = 34; // irq pin
const uint8_t PIN_SS = 4;   // spi select pin

// TAG antenna delay defaults to 16384

#define DEVICE_ADDR "C1:00:22:EA:82:60:3B:9C"

void setup(){
  
  Serial.begin(115200);
  delay(1000);

  //init the configuration
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); //Reset, CS, IRQ pin

  // start as tag, do not assign random short address
  DW1000Ranging.startAsResponder(DEVICE_ADDR, DW1000.MODE_1, false,TAG);
}

void loop(){
  DW1000Ranging.loop();
}