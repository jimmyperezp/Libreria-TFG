# 2D Positioning
><img src="https://cdn.worldvectorlogo.com/logos/arduino-1.svg" alt="arduino" width="80" height="80" align = "right"/>This example plots in real time the relative position between 1 tag and 2 anchors. To do it, the code launches a program in Python that receives the measured distances between tag & anchors.   


><img src="https://raw.githubusercontent.com/devicons/devicon/master/icons/python/python-original.svg" alt="python" width="80" height="80" align = "right"/>Said Python program can be found here: [MakerFabs 2D Positioning demo](https://www.instructables.com/ESP32-UWB-Indoor-Positioning-Test/)


<br>

### Used Hardware
1 Tag & 2 Anchors. The communication between them is made following the same protocol explained in the 'Measure Distance' example: [Measure Distance](link_to_measure_distance)


### Functionality 

The biggest update is that the ESP's WiFi module is now used.  
The tag is the board connected to the WiFi. This is because it is the tag's position that is plotted using the anchor's positions as fixed, and known beforehand.
Moreover, the tag receives the distance to each of the anchors, while each anchor only knows its distance to the tag. 


To connect it, firstly the program's header has to be modified:  

```C
const char *ssid = "ssid"; 
const char *password = "password";  
const char *host = "IPv4";  // CMD --> ipconfig --> read and copy IPv4
WiFiClient client;
```

After connected, the functions declared in *link.h* and *link.cpp* are used. In each loop cycle, the measured data is send via WiFi to the PC. 
Finally, using the Pyhton's app, that data is read and decodified to obtain the measured distances between devices.

### Results

The Python's app plots the results, displaying them as it can be seen in this image:

<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/example/Posicionamiento%202D/foto%20app%20posicionamiento%202D.jpg" alt="2D positioning app image." width="700">


The tag (plotted as a blue circle) will move as the tag board is moved too.  
To get better results, the following considerations must be taken into account: 
1. Anchors should be placed at a distance of about 3 meters.
2. Anchors should be calibrated before launching this program to improve the measured distances. 
2. When working on small distances, measures present some noise, and are less accurate. 



<br><br>




-------------
Autor: Jaime Pérez  
Última modificación: 31/08/2025  
