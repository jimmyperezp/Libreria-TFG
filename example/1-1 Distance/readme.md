# Initiator-Responder Distance (1 on 1)


> <img src="https://cdn.worldvectorlogo.com/logos/arduino-1.svg" alt="arduino" align = "right" width="60" height="60"/>First test in the development of this project. This code simply measures the distance between 2 boards. One must be an initiator, and the other, a responder.

<br>

## Procedure: 

The first step is **calibrating** the boards. The goal is to establish a correct *Antenna Delay* in the initiator boards. This way, the measurements made are more accurate.

- [DW1000 Antenna Delay Calibration](https://www.decawave.com/wp-content/uploads/2018/10/APS014_Antennna-Delay-Calibration_V1.2.pdf)

 To do so, these are the instructions to follow, step by step:
 

 
1. Place both boards at a known distance (after doing this process quite a few times, my personal recommendation is to place them at distances greater than 1m) 

2. Upload the code *Responder.ino* onto one of the boards.

3. Upload the *Initiator_delay_calibration.ino* to the other.
    - Inside this code, the user must indicate the distance at which the calibration is being made. (the distance at which the boards are placed).  
    This value goes into the following variable: 
    ```C
    float boards_distance = (distance in m);
    ```


4. Once both boards have their corresponding codes running on them, hit the reset button on both. 

5. The Initiator's serial monitor will desplay the *Antenna Delay* measured at the given distance. This is the parameter needed to calibrate the boards. 

6. Finally, this value is taken to the *Initiator.ino* code. 
    - The measurement is stored at the variable *Adelay*:
    ```C
    uint16_t Adelay = (Value measured during calibration);
    ```


Once the *Antenna Delay* is known for each anchor, the system is ready to measure the distances betwueen both boards.

<br></br>


-------------
Author: Jaime Pérez Pérez  
Modified last: 22/12/2025  
<img src="https://github.com/jimmyperezp/Programacion_de_sistemas/blob/main/logo%20escuela.png" align="right" alt="logo industriales" width="300" height="80"/> 

