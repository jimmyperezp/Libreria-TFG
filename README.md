># Library "TFG Jaime Pérez"
<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/extras/images/tfg-banner.png" alt="" width="600" height="300"/> 




<img src="https://github.com/HQarroum/awesome-iot/blob/master/iot-logo.png" alt="IoT" width="40" height="40"/> <img src="https://github.com/jimmyperezp/jimmyperezp/blob/main/cpp.svg" alt="c++" width="40" height="40"/> <img src="https://cdn.worldvectorlogo.com/logos/arduino-1.svg" alt="arduino" width="40" height="40"/> <img src="https://upload.wikimedia.org/wikipedia/commons/c/cd/PlatformIO_logo.svg" alt="platformIO" width="40" height="40"/>

> This library is a personal modification of the DW1000 chips library, used for UWB communication.   


Original Library (MakerFabs): [DW1000 Library](https://github.com/jremington/UWB-Indoor-Localization_Arduino/tree/main/DW1000_library) 

Previous library version : [Update by @Pizzo00](https://github.com/jremington/UWB-Indoor-Localization_Arduino/tree/main/DW1000_library_pizzo00)

This updated version:  [Changes made](#changes-made)

### Index:

 
1. [This project's goal](#goal-of-the-project)
2. [UWB. Definitions and TWR](#previous-concepts-uwb-to-measure-distance-twr)
3. [Background and context](#background-and-context)
4. [Changes made](#changes-made)
    - [Renaming](#1-renaming)
    - [New Methods](#2-new-methods-in-dw1000rangingcpp)
    - [Slaves ignore messages directed to the master](#3-slaves-ignore-message-types-that-are-directed-towards-the-master)
    - [Reducing dataReport Payload](#4-reducing-datareport-payload-shortaddresses-distance-and-rx-power-lengths)
    - [Eliminating devices from own list](#5-slaves-dont-forget-the-master-or-the-tags)
    - [Board types control](#6-board-types-control)
5. [Library's examples](#librarys-examples)
6. [Hardware](#hardware-used)
    - [ESP32 Wroom32](#esp32-wroom32)
    - [STM32 Nucleo F429ZI](#stm32-nucleo-f429zi)
    - [DW1000 module](#dw1000----uwb-chip)
    - [DWS1000](#dws1000)
7. [Programming the boards](#programming-the-boards)
    - [Arduino IDE](#arduino-ide)
    - [PlatformIO](#platformio)
        - [Coding the Nucleo F429ZI : Pin definitions](#coding-the-nucleo-f429zi)
8. [Project status](#project-status)

<br><br>

### Goal of the project

This is a 'Trabajo Final de grado (TFG)'. It focuses on the possible use of UWB (**Ultra Wide Band**) technology in railway systems.    

The code developed aims to achieve a coordination between multiple devices in the same network. The ultimate goal is that a 'coordinator' device knows all of the needed distances to guarantee it knows the complete train length, its composition and integrity.
 
Specifically, this repository contains a library developed in C++ used to perform said tasks. 


<br></br>

### Previous concepts: UWB to measure distance. TWR.

There is a few key concepts to be familiar with before advancing onto the code: 

**1: Measuring distances: TWR**  

In order to measure the distance between 2 UWB devices, this library uses the Two Way Ranging protocole, which can be seen in the following image:
<p align="center">
<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/extras/images/Protocolo%20TWR.%20Mensajes.png" alt="Imagen explicativa TWR" width="800" height="800"/>
<p/>

This method consists in one of the devices starting the communication (doing the *polling*), while the other limits to answer to this call. 

1. The polling device (from now on, *initiator*), keeps sending this initial message until "someone" answers. 
2. If a communication is set, the responding device (*responder*) sends back the instant in which the polling request was received, as well as the instant in which this response was sent.  
3. Finally, the initiator sends back a final message, with the timestamp in which the response message was received, and the timestamp in which this final message was sent. 

This way, the *responder* knows all of the timestamps needed to calculate the ToF (time of flight) of the communication all around.
Doing a simple division using the speed in which the messages travel, the distance between the two devices is obtained. 

This process is known as Two Way Ranging (TWR)


Clearly, the library is prepared to range with more than one device simultaneously, so the process escalates a bit. To check how these poll messages are built, check the *DW1000Ranging.cpp* file. 

<br><br>

### Background and context:

When working on ESP32-UWB modules, I found that the communication options among these devices was limited while using the original library. 

One of my goals when using said devices was to centralize the data collected in one coordinator device. This way, I could have access to all of the system's data while having to check multiple modules.  
The applications this had were endless. In my case, my primary goal was to develop a system based on UWB communication to monitor and check the state of trains that use this technology. By centralizing the data, all of the information could be easily accesed by any point in the train desired, or even sent via WiFi to a remote controlling station. 

With this objective in mind, I started working on adding this functionality to the previous version of the library.  


<br><br>

### Major changes made on this library's version

To see a brief summary of the main upgrades/modifications made to the previous version, see the [Main upgrades made to the library](pending)

🚧 **PENDING** --> Link this doc to a document inside the repo explaining the biggest upgrades I've made.

<br><br>


### Library's examples

The examples found in the library are: 

1. [*1-1 Distance*](/example/1-1%20Distance) (measures distance between two boards (Initiator-Responder) using TWR)

2. [*2D Positioning*](/example/2D%20Positioning/): launches a python app to plot the position of 2 anchors and 1 tag in real time

3. [*Hub & Spoke Coordination*](/example/Hub%20&%20Spoke%20coordination). Centralizes the system's data in a coordinator, using a hub & spoke (or star) topology.

4. [*Chain Token Passing*](/example/Chain%20token%20passing/): Ampliation of previous example. It also centralizes the measurements in the coordinator, but using a token passing topology. 

5. [*Mesh network Token Passing*](/example/Mesh%20token%20passing). Upgrade of previous example. Set to explore mesh networks, not only linear-layout systems.

<br><br>


<br><br>

### Hardware used
During the development of this project, different boards have been used. The reason behind using multiple different boards was to maximize the system's ability to work cross-platform, as well as to check the code ran in different "ecosystems", all while using the same UWB chip (DW1000).  


#### DW1000 --> UWB Chip
<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/extras/images/Chip%20DW1000.jpeg" alt="DW1000" width="300" height="200" align ="right"/>

This chip is the brain behind all of this project. It is in charge of transmitting and receiving UWB messages. The main documents to work with are the following: 

- [*DW1000 User Manual*](https://www.qorvo.com/products/d/da007967)
- [*DW1000 Datasheet*](https://www.qorvo.com/products/d/da007946)
- [*Transmit Power calibration and management*](https://www.qorvo.com/products/d/da008453)
- [*Antenna delay calibration for the DW1000 & similar products*](https://www.qorvo.com/products/d/da008449)
- [*Channel effects on comunication range*](https://www.qorvo.com/products/d/da008440)
- [*Maximizing range in DW1000 based products*](https://www.qorvo.com/products/d/da008450)


To see all available documents, visit [Qorvo: DW1000 documentation](https://www.qorvo.com/products/p/DW1000#documents)
<br><br>

#### ESP32 Wroom32
<img src="https://github.com/Makerfabs/Makerfabs-ESP32-UWB/blob/main/md_pic/front.jpg?raw=true" alt="esp32 wroom32" width="300" height="200" align="right"/> 

Firstly, I've been using the ESP32-Wroom32, developed by MakerFabs. These have the DW1000 Chip built in. 
<br><br>

[ESP32-UWB MakerFabs](https://www.makerfabs.com/esp32-uwb-ultra-wideband.html?srsltid=AfmBOoptL7z67ua57v7tP1AYSjEUQVG0_JfwDDH6NKWy50RSJLR1hWZG)  



These boards run using an ESP32 microcontroller. 
- [ESP32 Wroom Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf)

<br><br>

#### STM32: Nucleo F429ZI

<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/extras/images/STM32%20Nucleo-F429ZI.png" alt="NUCLEO F429ZI" width="400" height="200" align ="right"/>

I have also used some STM32 Nucleo-F429ZI boards. These however don't have their own DW1000 chip. Therefore, a shield that had said chip was neccesary. The option used was the [DWS1000](#dws1000)


[STM32 NUCLEO F429ZI](https://www.st.com/en/evaluation-tools/nucleo-f429zi.html)



<br><br>




#### DWS1000

<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/extras/images/DWS1000.png" alt="DWS1000" width="200" height="300" align ="right"/>

This is an Arduino form-factor shileld that can be connected into the Nucleo boards using their 'Zio' connectors, which are Arduino prepared

Clearly, in order to use this shield correctly, the code must have the appropiate pin definitions and initialization methods declared. These changes are stated below: [Coding the nucleo f429ZI](#coding-the-nucleo-f429zi)

[DWS1000 Product Details - Qorvo](https://eu.mouser.com/ProductDetail/Qorvo/DWS1000?qs=TiOZkKH1s2Q1L44eotOGgw%3D%3D&srsltid=AfmBOorhnLmnjISl3sGqW7M3cWGCVUakZJ3guFfhfWJLezhSb1S3dcf_)   
[DWS1000 Product Brief](https://eu.mouser.com/datasheet/3/1081/1/dws1000productbriefv10.pdf) 


<br><br>
<br><br>


### Programming the boards

#### Arduino IDE

> The arduino IDE was used to program the ESP32-Wroom32 Boards.  

<img src="https://cdn.worldvectorlogo.com/logos/arduino-1.svg" alt="arduino" width="100" height="100" align ="right"/>  

Taking into consideration that the ESP32 Boards do not have a debugger built in them, this functionality was not critical to choosing an IDE to develop the code.  
The chosen option was using the Arduino IDE, given the fact that it allows to see multiple serial monitors at once. This feature was key to debugging and seeing what messages were getting through and which weren't

##### **Setting up the Arduino IDE**
After downloading & installing the ArduinoIDE from the developer's website, there's a couple of steps to follow in order to use the ArduinoIDE with the ESP32 Wroom32 boards: 

1. Install the ESP32 Boards. Go to tools >> Board >> Board Manager >>  Search for "ESP32".  
Download & install the *espressif Systems* boards.

2. Add the needed libraries for these boards: 
    - Add the Adafruit_SSD1306 libraries. To do this, go to sketch >> Inlcude Library >> Manage Libraries >> Search for Adafruit_SSD1306 & install.
    - Add this repository's library (TFG Jaime Perez). Simply download the repo as a .Zip Library. Then, go to sketch >> Include Library >> Add a .Zip library >> Chose the recently downloaded file.

3. All ready to use!!



<br><br>

#### PlatformIO

> The VsCode extension PlatformIO was used to program the Nucleo F429ZI boards

<img src="https://upload.wikimedia.org/wikipedia/commons/c/cd/PlatformIO_logo.svg" alt="platformIO" width="100" height="100" align = "right"/>

These boards do include a debugger in them. Clearly, this is a feature worth taking into consideration.  
To maximize this board's capacities, the selected option was using the VsCode extension *PlatformIO*  

This tool also supports the esp32 boards, so it can be used to program all of the devices. 
However, its main limitation is the availability of only one serial monitor. If the intended application needs only one SM, then this software is clearly the best option.

PlatformIO's setup is made via the *platformio.ini* file created inside the project. In it, the following parameters must be declared and specified:

<div align="center">

| Parameters to include in the *ini* file| |
|---|---|
| • Environments              | • Library dependencies |
| • Boards| • Monitor speed |
| • Framework                 | • Upload protocol |

</div>

For example, the *platformio.ini* file in my case was the following: 

```ini 
[platformio]
default_envs = nucleo_master

[env]
framework = arduino
monitor_speed = 115200
lib_extra_dirs = C:/Users/(route to where you have the library)
lib_deps = TFG_Jaime_Perez (this is the name of the library)

[nucleo_base]
platform = ststm32
board = nucleo_f429zi
build_flags =   
    -D nucleo_f429zi

[esp32_base]
platform = espressif32
board = esp32doit-devkit-v1 


[env:nucleo_master]
extends = nucleo_base
build_src_filter = -<*> +<centralizar_N_slaves/master.cpp>

```

The **Key** declaration is the library dependency. This way, any change I make locally on the computer in which I'm developing the library is taken into consideration when uploading code to the boards using platformIO.  
Make sure this dependency is well declared.

For more examples and details on the *.ini* file's configuration, check the [PlatformIO ini official guide](https://docs.platformio.org/en/latest/projectconf/index.html)


##### Coding the Nucleo F429ZI

It is clear then, that the stm32 boards have been programmed using platformIO. However, as mentioned before, some code changes must be taken into consideration when uploading the code directly to them.  
Basically, the SPI and pin definitions previously defined for the esp32 boards is no longer valid.  

The following code includes the definition for both boards. The developer must define one or the other when uploading the code: 

```c++

#define STM_NUCLEO_F429ZI 
//#define ESP32_WROOM32

#ifdef ESP32_WROOM32
    //Board's pins definitions:
    #define SPI_SCK 18
    #define SPI_MISO 19
    #define SPI_MOSI 23
   
    const uint8_t PIN_RST = 27; // reset pin
    const uint8_t PIN_IRQ = 34; // irq pin
    const uint8_t PIN_SS = 4;   // spi select pin

#endif

#ifdef STM_NUCLEO_F429ZI
    #define SPI_SCK D13
    #define SPI_MISO D12
    #define SPI_MOSI D11
    #define DW_CS D10

    #define PIN_RST D7 // reset pin
    #define PIN_IRQ D8 // irq pin
    
#endif

```

Once the pins have been declared, the SPI and DW1000 must be initialized. To do so, here's the code: 

```c++

// In the header files (.h):
void initHardware();

// In the .cpp files: 

void setup(){

    (...)
    initHardware();
    (...)

}

void initHardware(){

    void initHardware(){

    #ifdef ESP32_WROOM32
        SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI); // SPI bus start
        DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); // DW1000 Start
    #endif

    #ifdef STM_NUCLEO_F429ZI
        //init the configuration
        SPI.setMOSI(SPI_MOSI);
        SPI.setMISO(SPI_MISO);
        SPI.setSCLK(SPI_SCK);
        SPI.begin();

        DW1000Ranging.initCommunication(PIN_RST, DW_CS, PIN_IRQ); //Reset, CS, IRQ pin
    #endif

}
}



```



<br><br>
### Project status: 

🟢 The library is currently active. 


<br><br>
---------
Author: Jaime Pérez   
Last update: 13/03/2026
<img src="https://github.com/jimmyperezp/Programacion_de_sistemas/blob/main/logo%20escuela.png" align="right" alt="logo industriales" width="300" height="80"/>  
****