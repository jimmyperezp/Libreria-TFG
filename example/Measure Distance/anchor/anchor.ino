//1 Tag y 1 Anchor: 
// ANCHOR SETUP

#include <SPI.h>
#include "DW1000Ranging.h"
#include "DW1000.h"

// Los 2 bytes de la izquierda son la short address.
// NOMENCLATURA: A para Anchors, B para Tags
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
  delay(1000); // Le doy 1 segundo para el monitor serie

  // Inicializo la configuración: 

    // 1: Inicializo el bus SPI con los pines indicados antes.
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

    // 2: Inicializo el DW1000
  DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); //Reset, CS, IRQ pin


  // Establezco el antenna delay indicado antes: 
  // Internamente, escribirá el valor que le paso en un registro concreto del DW1000
  DW1000.setAntennaDelay(Adelay);

  DW1000Ranging.attachNewRange(newRange);
  //Esto es, básicamente: Cuando se termine una medición, lanza la función newRange.
  DW1000Ranging.attachNewDevice(newDevice);
  DW1000Ranging.attachInactiveDevice(inactiveDevice);

  //inicio la placa como ANCHOR. los parámetros son: 
  // dirección - modo de funcionamiento - ¿Tomar dirección aleatoria? 
  // La dirección aleatoria sirve para pruebas "rapidas". Así evito tener que estar cambiando todas las direcciones.
  
  DW1000Ranging.startAsResponder(DEVICE_ADDR, DW1000.MODE_LONGDATA_RANGE_LOWPOWER, false);
  // DW1000Ranging.startAsResponder(DEVICE_ADDR, DW1000.MODE_SHORTDATA_FAST_LOWPOWER);
  // DW1000Ranging.startAsResponder(DEVICE_ADDR, DW1000.MODE_LONGDATA_FAST_LOWPOWER);
  // DW1000Ranging.startAsResponder(DEVICE_ADDR, DW1000.MODE_SHORTDATA_FAST_ACCURACY);
  // DW1000Ranging.startAsResponder(DEVICE_ADDR, DW1000.MODE_LONGDATA_FAST_ACCURACY);
  // DW1000Ranging.startAsResponder(DEVICE_ADDR, DW1000.MODE_LONGDATA_RANGE_ACCURACY);
}

void loop()
{
  DW1000Ranging.loop();
}

void newRange()
{

  //Muestro desde qué dispositivo se ha hecho el range
  Serial.print(" Desde: ");
  Serial.print(DW1000Ranging.getDistantDevice()->getShortAddress(), HEX);
  // La función getDistantDevice() devuelve el objeto "device" con el que se ha hecho ranging. 
  // A este device puedo pedirle métodos como: getShortAddress, getAddress, getRange, etc.

  Serial.print("\t Distancia: ");
  Serial.print(DW1000Ranging.getDistantDevice()->getRange());
  Serial.print(" m");
  Serial.print("\t RX power: ");
  Serial.println(DW1000Ranging.getDistantDevice()->getRXPower());
}

void newDevice(DW1000Device *device){

  // La librería DW1000 lanza este callback cuando detecta una comunicación desde un device con shortAdress distinta a las que ha visto hasta entonces.
  Serial.print("Device added: ");
  Serial.println(device->getShortAddress(), HEX);
}

void inactiveDevice(DW1000Device *device){

  //Dentro de DWdevice.h, se define el inactivity_time como 1s.
  // Si pasa ese tiempo sin señal de un dispositivo que ya había registrado antes, lo considero inactivo. 
  Serial.print("Delete inactive device: ");
  Serial.println(device->getShortAddress(), HEX);
}