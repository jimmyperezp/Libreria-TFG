# Anchor-Tag Distance


> <img src="https://cdn.worldvectorlogo.com/logos/arduino-1.svg" alt="arduino" align = "right" width="60" height="60"/>First test in the development of this project. This code allows the user to check the boards are working correctly, as well as the fundamentals begind the UWB communication between Anchor & Tag.





## Procedure: 

The first step is **calibrating** the boards. The goal is to establish a correct *Antenna Delay* in every anchor. This way, the measurements made are more accurate.

- [DW1000 Antenna Delay Calibration](https://www.decawave.com/wp-content/uploads/2018/10/APS014_Antennna-Delay-Calibration_V1.2.pdf)

 To do so, these are the instructions to follow, step by step:
 

 
1. Place Anchor & Tag at a known distance (after doing this process quite a few times, my personal recommendation is to place them at distances greater than 1m) 

2. Upload the code *Tag.ino* onto the tag

3. Upload the *Anchor_autocalibrate.ino* to the Anchor. 
    - Inside this code, the user must indicate the distance at which the calibration is being made. (the distance at which the boards are placed).  
    This value goes into the following variable: 
    ```C
    float this_anchor_target_distance = (distance in m);
    ```


4. Once both boards have their corresponding codes running on them, hit the reset button on both. 

5. The Anchor's serial monitor will desplay the *Antenna Delay* measured at the given distance. This is the parameter needed to calibrate the boards. 

6. Finally, this value is taken to the *MD_anchor.ino* code. 
    - The measurement is stored at the variable *Adelay*:
    ```C
    uint16_t Adelay = (Value measured during calibration);
    ```


Once the *Antenna Delay* is known for each anchor, the system is ready to measure the distances betwueen both boards.

<br></br>


-------------
Author: Jaime Pérez Pérez  
Modified last: 22/12/2025