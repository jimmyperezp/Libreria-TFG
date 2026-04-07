# AI Integration

> <img src="https://raw.githubusercontent.com/devicons/devicon/master/icons/python/python-original.svg" alt="python" width="80" height="80" align = "right"/> This python script reads the Serial monitor & extracts the measurements collected after each cycle. It then saves them in a .csv file, which is used to train a tinyML model.

<br>

### Context and expected use

In order to train a ML model to help me detect possible decouplings or integrity losses, I need a series of datasets taken in different situations.  
To do so, the [Chain token passing example](/example/Chain%20token%20passing) has been modified in order to show a specific output in the serial monitor. Said output starts with the header: "AI_DATA", and simply shows the measurements collected that cycle in a csv format. 

To see this output, the defines section of the [*Coordinator.ino*](/example/Chain%20token%20passing/Coordinator/Coordinator.ino) must set the AI_INTEGRATION as true: 

```c++
#define AI_INTEGRATION true
```


Once this is done, the coordinator's output is the following: 

<p align = center>
AI_DATA:  
Cycle_ID , Time since last print, origin address, destiny address, distance, RX power, Active? 
<p>

- Cycle ID: Indicates the cycle that the coordinator is in. This allows the model to differenciate each cycle from the previous, as well as to collect all of the measurements made in each cycle.
- Time between prints. It may be redundant to show this in every line of the output, but it is necessary. It shows the time taken between cycles. The model will use this value to understand if there has been high UWB traffic, or if the devices have been working on a high number of retries, taking up more time to finish the cycle.
- Origin and destiny short addresses: They indicate the devices involved in that measurement.
- Distance and RX power: Values measured between both devices indicated
- Active? : This last value indicates if the connection between both devices has been active this cycle. This lets the model know of any missing communications.


For example, a possible output could be the following: 

AI_DATA:

0, 494 , C1 , B2 , 12.54 , -89.17 ,1  
0, 494 , C1 , B3 , 41.96 , -95.62 ,1  
0, 494 , B2 , B3 , 33.57 , -91.36 ,1  
0, 494 , B3 , B4 , 24.56 , -90.77 ,1  

AI_DATA:

1, 742 , C1 , B2 , 12.72 , -89.26 ,1  
1, 742 , C1 , B3 , -999  , -999   ,0  
1, 742 , B2 , B3 , 33.55 , -91.44 ,1  
1, 742 , B3 , B4 , 24.60 , -90.57 ,1  

AI_DATA:

2, 1023 , C1 , B2 , 12.67 , -89.01 ,1  
2, 1023 , C1 , B3 , -999  , -999   ,0  
2, 1023 , B2 , B3 , 33.56 , -91.35 ,1  
2, 1023 , B3 , B4 , -999  , -999   ,0  


<br>

### Data collector functioning

This sript works in a similar way to the [RX Distance Data collector](/extras/RX-Distance%20relation). It is comoposed of 5 main blocks: 

<br>

#### 1:  Accessing the port & saving the file: 

```python
# Serial monitor setup:
SERIAL_PORT = 'COM9'      # Update with the coordinator's serial port
BAUD_RATE = 115200        
# IMPORTANT: Change the output file name with the experiment done (NLOS, LOS, etc)
OUTPUT_FILE = 'extras/AI_integration/tinyML data sets/tinyML_data.csv'
```

As mentioned in the comments, it is key to update the output file depending on the experiment made.  
For example, if the data collected is for devices in LOS, then name it tinyML_data_LOS.csv, and so on.

<br>

#### 2: Opening the serial monitor & reading each line

```python
def main():
    try:
        
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        
        
        with open(OUTPUT_FILE, mode='w', newline='') as file:
            writer = csv.writer(file)
            writer.writerow(["Cycle ID","Cycle time [mS]","Origin Addr","Destiny Addr", "Distance (m)", "RX Power (dBm)","Active"])
            
            recording = False
            
            # Data colection loop
            while True:
                if ser.in_waiting > 0:
                    try:
                        
                        line = ser.readline().decode('utf-8').strip() # The full line is read and saved here
                    
                        if line == ("AI_DATA:"):
                            recording = True
                            continue # Goes to the next 'while' iteration (to read the next lines)
```
<br><br>

This block of code starts by accessing the serial monitor (ser). Then it opens the file indicated as output: 

```python
with open(OUTPUT_FILE, mode='w', newline='') as file: # From now on, 'file' is the OUTPUT_FILE indicated in the header section
```

After accessing the serial port and opening the output file, the script reads every line shown in the serial monitor: 

```python
line = ser.readline().decode('utf-8').strip() 
```

"Line" contains the full line as a plain text. The next step is to see if that line is the seached header: 

```python
if line == ("AI_DATA:"):
    recording = True
    continue 
```

When this happens, then the program continues to the neext loop iteration, where the line is converted from plain text to a CSV format

<br>

#### 3: Converting the lines into CSV format

```python
if recording and "," in line: 
    data_elements = line.split(",")
```

If recording == true (The AI_DATA header has previously been read), then, for every comma found in the line, separate the text using commas.  
This means turning an entry like this: "[0,393,C1,B2,34.87,-76.43,1]"  
Into a trully CSV line: "['0','393','C1','B2','34.87','-76.43','1']


<br>

#### 4: Writing the converted line into the output file

```python
if len(data_elements) == 7:
    writer.writerow(data_elements)
    file.flush()
else:
    recording = False
```

If the data_elements array (that contains the line in CSV format) has a length of 7 elements (correct), then directly writes it in the file (the output file, as indicated in section 2).  
However, if the length is not 7, then there has been some kind of problem. This is handled by setting the recording as false, therefore skipping the current log until reading "AI_DATA" again.


<br>

#### 5: Handling errors 

If there has been any kind of error while opening the serial port or the file, then the error is shown for the user to be aware of it. 


```python
                    except Exception as e:
                        print(f"Error while reading data: {e}")
                        
  
    except serial.SerialException as e:
        print(f"Error with the serial port: {e}")
    except KeyboardInterrupt:
        print("\nUser aborted the program.")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()


```

With this section of code, the program is finished. 

<br>

### Saving the results

The output files are indicated to be saved inside the [TinyML data sets](/extras/AI%20Integration/tinyML%20data%20sets/) folder. After finishing the experiments, the user can upload them directly from this folder into the tinyML generator used. 


<br>

### Edge impulse

TODO --> Explain the use of Edge Impulse (the tinyML generator that I'm using)


<br><br>

------
<img src="https://github.com/jimmyperezp/Programacion_de_sistemas/blob/main/logo%20escuela.png" align="right" alt="logo industriales" width="300" height="80"/>   
Author: Jaime Pérez  
Last modified: 07/04/2026  
