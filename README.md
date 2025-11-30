# Library "TFG Jaime Pérez"

<img src="https://github.com/jimmyperezp/jimmyperezp/blob/main/cpp.svg" alt="c++" width="40" height="40"/> <img src="https://cdn.worldvectorlogo.com/logos/arduino-1.svg" alt="arduino" width="40" height="40"/> <img src="https://upload.wikimedia.org/wikipedia/commons/c/cd/PlatformIO_logo.svg" alt="platformIO" width="40" height="40"/><img src="https://github.com/jimmyperezp/TFG_UWB/blob/main/logo%20UWB.png" alt="UWB Logo" width="40" height="40"/><img src="https://github.com/jimmyperezp/Programacion_de_sistemas/blob/main/logo%20escuela.png" alt="logo industriales" width="40" height="40"/>  

This library is a personal modification of the DW1000 chips library, used for UWB communication.   
<br><br>
Original Library (MakerFabs): [DW1000 Library](https://github.com/jremington/UWB-Indoor-Localization_Arduino/tree/main/DW1000_library) 
- Functions documentation: [Library API (PDF)](https://cdn.rawgit.com/thotro/arduino-dw1000/master/extras/doc/DW1000_Arduino_API_doc.pdf)

Previous library version : [Update by @Pizzo00](https://github.com/jremington/UWB-Indoor-Localization_Arduino/tree/main/DW1000_library_pizzo00)

This updated version:  [Changes made](#changes-made)


<br><br>
### Background and context:

When working on ESP32-UWB modules, I found that the communication options among these devices was limited while using the original library. 

One of my goals when using said devices was to centralize the data collected in one master anchor. This way, I could have access to all of the system's data while having to check multiple modules.  
The applications this had were endless. In my case, my primary goal was to develop a system based on UWB communication to monitor and check the state of trains that use this technology. By centralizing the data, all of the information could be easily accesed by any point in the train desired, or even sent via WiFi to a remote controlling station. 

With this objective in mind, I started working on adding this functionality to the previous version of the library.  
To do so, I renamed some of the existing methods, cleaned and organized some segments of code, and most importantly, added the neccessary bits to make this work. 

### Changes made

1: **Renaming**:  

 - Why?:
    - In the previous version, the library assumed that tags were in charge of starting the communication proccess. They were the ones in charge of doing the *polling*.  
    However, this could not be the desired behavior. For example, I wanted the master anchor to be in charge of doing said task, so that it could control the rest of the system's devices. 
- What?
     - startAsTag is now "startAsInitiator"
     - startAsAnchor is now"startAsResponder"



2: New methods:
- Why?
    - To centralize the data, firstly the master had to change the mode of operation of the slave anchors from responders to initiators. This way, All of the measurements betweeen all of the system's devices could be known.  
    Not only from the master to the rest of the modules, but also from the rest of the slave anchors to the tags. 

    - Once all of the measurements were obtained, they needed to be sent back to the master anchor.
- What?
    - 3 new message modes were added:  
        - *Mode Switch*: it indicates the targeted device to switch its behavior from initiator to responder (or viceversa)
        - *Data Request*: The master asks the slaves for their calculated measurements.
        - *Data Report*: The slaves send the master their measurements.
        - To send these messages, the methods included are: 
            - transmitModeSwitch
            - transmitDataRequest
            - transmitDataReport
    
    - The master needs to know if the slaves have done the mode switch correctly. To do so, the slave sends back an acknowledgement message.
        - transmitModeSwitchAck

    - To handle when these messages are received, I declared the neccessary callbacks, which are linked to functions in the library's examples. These are explained each examples' readme.
        - [Example #1 -> Measure Distances](https://github.com/jimmyperezp/Libreria-TFG/tree/main/example/Medir%20distancias) 
        - [Example #2 --> Plot live position in 2D](https://github.com/jimmyperezp/Libreria-TFG/tree/main/example/Posicionamiento%202D)
        - [Example #3 --> Centralize data (1 slave)](https://github.com/jimmyperezp/Libreria-TFG/tree/main/example/Centralizar%20datos%20-%201%20slave)
        - [Example #4 --> Centralize data (N slaves)](https://github.com/jimmyperezp/Libreria-TFG/tree/main/example/Centralizar%20datos%20-%20N%20slaves)

    - In case there's ever the need to turn the ranging off of a device, the library now uses this new method, which has it's own *ack* message as well.
    
        - transmitStopRanging  
        - transmitStopRangingAck
    
    
        


3: Slaves ignore message types that are directed towards the master.  
- Why?
    - The previous version of the library wasn't prepared to handle multiple devices ranging at the same time.  
    This was seen in erratic behaviors and messages not getting through. The reason behind this problem was that the slaves didn't filter the messages that weren's sent to them. Therefore, their ranging sequence failed in every single cycle.

- What? 
    - Simple. Inside the function that handles the received messages, a new "filter" was established inside the "responder" section: (in dw1000Ranging.cpp)
    ```c++
    if(ranging_enabled){
        if(_type == RESPONDER) {
            if (messageType == POLL_ACK || messageType == RANGE_REPORT || messageType == RANGE_FAILED) {
						return; 
                	}
        (...)
        }
    }
    
    ```
<br><br>


### Examples

The examples found in the library are: 

1. Medir distancias (measure distance between anchor and tag (Initiator-Responder))

2. Posicionamiento 2D: launches an app to plot the position of 2 anchors and 1 tag in real time

3. Centralizar datos: Centralizes all of the system's measures between its devices.


<br><br>
### Project status: 

The library is currently active. 


<br><br>
---------
Author: Jaime Pérez   
Last update: 30/11/2025
