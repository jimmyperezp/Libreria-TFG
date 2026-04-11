import serial
import csv

# This code accesses the serial port to read the measured distance & RX power data shown by the coordinator. 
# The data is shown in the serial monitor as "RX_DIST_DATA:distance1,rx_power1;distance2,rx_power2;...".
# The code writes the data to a CSV file with two columns: "Distance (m)" and "RX Power (dBm)". Each row corresponds to a single measurement.


# Serial monitor setup:
SERIAL_PORT = 'COM9'      # Update with the coordinator's serial port
BAUD_RATE = 115200        
# IMPORTANT: Change the output file name with the experiment done (NLOS, LOS, etc)
OUTPUT_FILE = 'extras/AI Integration/tinyML data sets/train_OK_LOS_data.csv'


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

                        if recording and "," in line: 
                            #Every time there is a ',', then, split the line and save that 'chunk'
                            # Then, separate each 'chunk' (element) by commas.
                            data_elements = line.split(",")
                            # data_elements has turned the full line as text to something like this: 
                            # ['cycle id' , ' time' , 'Origin addr', 'destiny addr', 'distance', 'RX power', 'Is active?']



                            if len(data_elements) == 7:
                                writer.writerow(data_elements)
                                file.flush() # Save the line in the output file directly.
                            else:
                                recording = False
                                # If more than 7 elements have been read, then there has been an error. 
                                # I stop recording & wait for next time the serial monitor shows the "AI_DATA" header.
                            
                          
                    except Exception as e:
                        print(f"Error while reading data: {e}")
                        
  
    except serial.SerialException as e:
        print(f"Error with the serial port: {e}")
    except KeyboardInterrupt:
        print("\nUser aborted the program.")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()


if __name__ == '__main__':
    main()