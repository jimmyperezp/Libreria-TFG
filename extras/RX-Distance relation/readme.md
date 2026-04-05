# RX Power - Distance relation

> This set of "extras" takes a set of RX-Distance values measured between two devices. Afterwards, it plots (using the matlab file) the results, comparing them to the theoretical (or ideal) curve.

### Context & use

In order to understand how stable certain measurements are, this set of files aims to get the relationship between the RX power and distance measured between two devices.   
In order to do so, three different files are used: the data (.csv), the data collector (.py) and the plotter (.m).   

The result is the following graph:
 



### Data collection

The first step is to upload the code to the boards. In order for the program to work, the coordinator's serial monitor must show a specific header: "RX_DIST_DATA", followed by the RX Power & Distance values.  
To do so, the [Token passing chain](../../example/Token%20passing%20chain) example is prepared to show this valid output, as long as the user makes sure the define section is prepared to perform the plotting: 

```c++
#define PLOTTING true
```

Once the serial monitor shows the results that I'm interested in, the python file extracts the RX power and distance displayed.  
Then, it saves these data into a specific *.csv* file, as indicated here: 

```python
OUTPUT_FILE = 'extras/RX-Distance relation/rx_distance_data.csv'
```

<br><br>
Once the python file is running and the boards have the running code, then the next step is to walk around with the boards, collecting a lot of distances and rx powers to perform an accurate graph. 

After moving around and collecting some data, the *.csv* file is built up with all of the collected values.  



### Data plotting

When reaching this point, the boards have performed the measurements, and the python script has collected the data and saved it in the *.csv* file. Now, it's time for the plotting.  

The next step is to open Matlab and run the "rx_distance_plotter.m" script. Inside it, the program starts by opening the correct file, and extracting the RX powers and the distances to two separate valid arrays: 

```matlab
file = 'rx_distance_data.csv';
data = readtable(file);

distances = data{:, 1}; %1st colum is distance in meters.
rx_powers = data{:, 2}; % 2nd is the RX power in dBm.
```

Then, it filters all of the measurements that show a negative distance, in order to plot only the (X,Y) values that are greater than 0.

```matlab
valid_indexes = distances > 0;  % Only takes distances >0 
distances = distances(valid_indexes);
rx_powers = rx_powers(valid_indexes);
```

And finaly, it scatter plots the collected measurements (one dot for each individual measure)

```matlab
figure('Name', 'RX Power vs Distance','Color','w');
scatter(distances,rx_powers,15,'b','filled','MarkerFaceAlpha',0.5,'DisplayName',' Experimental data');
```


<br><br>
### Ideal Curve (Friis Law)

In order to validate the collected measures, I now plot the ideal curve that shows the RX Power - Distance relationship.   

This curve responds to the Frii's Law: 

$$
P_r [dbM] = P_t + G - L -20·log_{10}(\frac{4·\pi·f_c·d}{c})  
$$

Being 
$$
20·log_{10}(\frac{4·\pi·f_c·d}{c}) = FSPL
$$

Taking logarithms, 


$$
FSPL = 20·log_{10}(d) + 20·log_{10}(f) + 20·log_{10}(\frac{4·\pi}{c})
$$


From this ecuation: 

- $P_r$ is the received signal power
- $P_t$ is the transmitted power from the DW1000
- $G$ Includes the antenna gains of the transmitting and receiving antennas, as well as any other gain from external amplifiers
- $L$ includes any PCB, cable, connector and other losses in the system
- $c$ is the speed of light (~$3·10^8$ m/s)
- $f_c$ is the center frecuency for the channel used, expressed in Hertz
- $d$ is the distance in meters between the transmitter and receiver


(This definitions and formula are as seen in the [APS023 Part 1 - Transmit power Calibration and management](https://www.qorvo.com/products/d/da008453). See section 1.3)


<br>
Knowing all of the equation's elements, now I calculate:  

- Using channel 5, $f_c = 500  MHz$
- $P_t = -41,3 \frac{dBm}{MHz} + 10·log_{10}(f_c) = -41,3 + 27 = -14,3 dBm$ 
- FSPL = -27,55 dBm


The final result, and the plotted ideal curve is the following: 

$$
P_r = -63 - 20·log_{10}(d) 
$$

This curve is plotted over 500 values for "d" (x) from x = 0 to x = 150.  
The code that does that is: 

```matlab
ideal_distances = linspace(1,110,500);
ideal_rx_powers = -63.0 - 20*log10(ideal_distances);
plot(ideal_distances,ideal_rx_powers,'r','LineWidth',1.5,'DisplayName',' Ideal (Friis Law)');
```

<br><br>


### Results

<p align="center">
<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/extras/images/RX%20Power%20-%20distance%20correlation.png" alt="RX Power - Distance relation" width="1000" height="1500"/>



<br><br><br>
-----------
Author: Jaime Pérez  
Last modified: 04/04/2026

<img src="https://github.com/jimmyperezp/Programacion_de_sistemas/blob/main/logo%20escuela.png" align="right" alt="logo industriales" width="300" height="80"/> 
