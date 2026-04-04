# Chain Plotter (Kamada-Kawai Algorithm)

> <img src="https://raw.githubusercontent.com/devicons/devicon/master/icons/python/python-original.svg" alt="python logo" align = "right" width="60" height="60"/>This is an "extra" file that complements the [Token Passing Chain Example](../../example/Token%20passing%20chain).  
By running this python script, a live display showing the node's positions and distances between them is shown. To do so, the script uses the Kamada-Kawai algorithm

<br><br>
### Context & use

As mentioned in the previous summary, this Python script plots the relative positions between the system's nodes, showing the distances and RX power measured in each link.  

The final result is the shown in the next image: 

<p align="center">
<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/images/Kamada-Kawai%20plots.png" alt="Kamada-Kawai plots" width="300" height="300"/>

To achieve this functioning, the procedure is the following: 

<br><br>
### Procedure


#### 1: Showing & Reading the measured data

The script starts by accesing the serial port in which the coordinator is running (this must be updated and checked every time the script is run).  
In said serial port, the coordinator shows a specific output, which is indicated inside the *coordinator.ino* file.   
For that specific output to be shown, in the define section, the user must set:
```c++
#define PLOTTING true
```

Once this is done, the serial monitor shows a JSON type text, containing all of the needed data to plot the results. This JSON (as defined in the *coordinator.ino* file), is: 

```c++

void showJSONData(){ 
    
    //The JSON will have the next structure: 
    /*
    
    {
        "time_between_cycles": (indicates the number of ms it took to complete the current cycle)
        "measure":[ ("[ indicates the value of this field is a list of other fields")
            {
                "from": (origin short address header)
                "to":   (destiny short address header)
                "dist": (distance value)
                "rxpwr": (measured RX power in each measure)
            },
            {
                "from":
                "to":  
                "dist":
                "rxpwr":
            }      
        ]
    } */

    (...)
    

```

<br><br>
The python script reads the serial monitor until it reads a header that says: "JSON_DATA:"  
After reading that, then it extracts all of the data from each "struct" inside the JSON shown. 

Next step is running the **Kamada-Kawai Algorithm**:

#### 2: Kamada-Kawai Algorithm

This is a force-based algorithm that indicates the position in which each node must be plotted to minimize the system's energy.    

Using python's libraries to access this algorithm, I obtain as a result the (X,Y) coordinates in which each node must be plotted. To perform that calculations, see more explanations about the [Kamada-Kawai algorithm](https://cs.brown.edu/people/rtamassi/gdhandbook/chapters/force-directed.pdf)


#### 3: Plotting the nodes and showing the distance + RX data

After having the coordinates in which they must be placed, the final step is to use the matplot library and its functions (see code) to draw the nodes and their connections


<br><br>
### Limitations & possible upgrades

The current version of this code is prepared to draw the nodes in 1-D. It shows them in a straight line, including the distance and RX power measured in each link.   
To stay on 1D, I simply take into consideration the X coordinate given by the algorithm. Then, on the plotting stage, I fix the Y value to a constant in all of the nodes.      

Future upgrades should take into consideration both valuex (X and Y). This way, the results could even work on drawing curves, with certain limitations (for instance, to draw a curve, the nodes involved should see not only the following node, but the one after that too). 




<br><br>
------
Author: Jaime Pérez  
Last modified: 03/04/2026
<img src="https://github.com/jimmyperezp/Programacion_de_sistemas/blob/main/logo%20escuela.png" align="right" alt="logo industriales" width="300" height="80"/> 