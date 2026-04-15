# TinyML data collector (python script)

import serial
import csv

# This code accesses the serial port to read the measured distance & RX power data shown by the coordinator.

# Serial monitor setup:
SERIAL_PORT = 'COM4'      # Update with the coordinator's serial port
BAUD_RATE = 115200        
# IMPORTANT: Change the output file name with the experiment done (NLOS, LOS, etc)
OUTPUT_FILE = 'extras/AI Integration/tinyML data sets/confirmacion_funcionalidad.csv'

def main():
    try:    
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)   
        with open(OUTPUT_FILE, mode='w', newline='') as file:

            print("Recording...\nPress CTRL+C to abort the program")
            writer = csv.writer(file)
            header = ["Timestamp mS","Time this cycle"]

            for i in range(6):
                header.extend([f"Link {i} active", f"Link {i} distance" , f"Link {i} RX Power"])
            writer.writerow(header)
          
            # Data colection loop
            while True:

                if ser.in_waiting > 0:
                    try:

                        line = ser.readline().decode('utf-8').strip() # The full line is read and saved here
                        
                        if line.startswith("AI_DATA:"):
                            clean_line = line.replace("AI_DATA:","")
                            data_elements = clean_line.split(",")

                            if len(data_elements) == 20:
                                    writer.writerow(data_elements)
                                    file.flush() # Save the line in the output file directly.

                            else:
                                print(f"Wrong number of elements in the line ({len(data_elements)} Elements)")

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