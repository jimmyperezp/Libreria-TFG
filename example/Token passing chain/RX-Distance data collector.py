import serial
import csv

# Serial monitor setup:
SERIAL_PORT = 'COM9'      # Update with the coordinator's serial port
BAUD_RATE = 115200        
OUTPUT_FILE = 'example/Token passing chain/rx_distance_data.csv'


def main():
    try:
        
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        
        
        with open(OUTPUT_FILE, mode='w', newline='') as file:
            writer = csv.writer(file)
            writer.writerow(["Distance (m)", "RX Power (dBm)"])
            
            
            # Data colection loop
            while True:
                if ser.in_waiting > 0:
                    try:
                        
                        line = ser.readline().decode('utf-8').strip()
                    
                        if line.startswith("RX_DIST_DATA:"):
                            data_str = line.replace("RX_DIST_DATA:", "")
                            measurements = data_str.split(";")
                            
                            for measure in measurements:
                                if measure != "": 
                                    distance, rx_power = measure.split(",")
                                    
                                    # --- SECCIÓN 5: ESCRITURA ---
                                    writer.writerow([distance, rx_power])
                                    
                                    
                            file.flush() # Forzar volcado a disco
                          
                    except Exception as e:
                        print(f"Error while reading data: {e}")
                        
  
  
    except KeyboardInterrupt:
        print("\nUser aborted the program.")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()


if __name__ == '__main__':
    main()