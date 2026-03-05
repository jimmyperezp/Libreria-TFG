import serial
import json
import matplotlib.pyplot as plt
import networkx as nx
import sys

# ==========================================
# Reading the serial port
# ==========================================

SERIAL_PORT = 'COM9'  # Change & introduce coordinator's serial port.
BAUD_RATE = 115200

try:
    #tries to read the serial port at the baud rate indicated.
    #timeout is the max time to read the whole monitor once the reading has started.
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
except Exception as e:
    
    print(f"ðŸš¨ ERROR: Port {SERIAL_PORT} could not be opened")
    print(f"Detail: {e}")
    sys.exit(1)


# ==========================================
# MATPLOTLIB config. Sets up the graphics
# ==========================================

plt.ion() # sets interactive mode ON. This tells matplotlib to not block the serial monitor while printing
fig, axis = plt.subplots(figsize=(12, 3))
fig.canvas.manager.set_window_title('Train Integrity')

def update_plot(json_string):
    """
    Function in charge of the math & drawing. Uses the Kamada-Kawai filter.
    Turns the JSON into a Graph, applies Kamada-Kawai in 1D & refreshes window.

    """

    axis.clear()   # Cleans previous plot
    G = nx.Graph() # Creates an empty NetworkX object
    
    # 1. Reads & decodes the read JSON
    try:
        data = json.loads(json_string)
        # Buscamos la clave 'measures' que definimos en C++
        measures = data.get("measures", []) 
        
        if not measures:
            return # the list comes empty. Nothing to draw this time
            
        for measure in measures:
            node_from = measure["from"]
            node_to = measure["to"]
            dist = measure["dist"]
            
            G.add_edge(node_from, node_to, weight=dist)
            
    except json.JSONDecodeError:
        print("âš ï¸ ERROR: KSON not decodified propperly.")
        return

    # No nodes --> Nothing to draw
    if len(G.nodes) == 0:
        return

    # 2. Kamada Kawai algorithm 
    try:
        # dim=1 forces the output to be a straight line
        pos_1d_raw = nx.kamada_kawai_layout(G, weight='weight', dim=1)
    
    except Exception as e:
        print(f"âš ï¸ Math error in the algorithm {e}")
        return
        

    # 3: Turning the JSON arrays into X,Y coordinates.
    # To make it a straight line, sets Y = 0
    pos = {node: (coords[0], 0) for node, coords in pos_1d_raw.items()}


    # 4. Draw all the elements on the screen
    
    # Nodes
    nx.draw_networkx_nodes(G, pos, axis=axis, node_color='#4CAF50', node_size=900, edgecolors='black')
    
    # Links between nodes (UWB Connection)
    nx.draw_networkx_edges(G, pos, ax=axis, width=3, edge_color='#607D8B')
    
    # Nodes texts (MAC short addresses)
    nx.draw_networkx_labels(G, pos, ax=axis, font_size=10, font_weight="bold", font_color='white')
    
    # Show the distances as text (placed over the lines)
    edge_labels = {(u, v): f"{d['weight']:.2f}m" for u, v, d in G.edges(data=True)}
    nx.draw_networkx_edge_labels(G, pos, edge_labels=edge_labels, axis=axis, font_size=9)

    # 5. Axis aesthetics:

    axis.set_title("Active measurements between nodes", fontweight='bold')
    axis.set_yticks([]) # Eliminates the ticks in Y-axis (not needed)
    
    # Adjusts X-axis zoom so that all the measurements fit in the screen.
    x_values = [coords[0] for coords in pos.values()]
    margin = 5 # A bit of margin on both sides
    axis.set_xlim(min(x_values) - margin, max(x_values) + margin)
    
    # Finally, draws and refreshes the screen
    plt.draw()
    plt.pause(0.001)



# ==========================================
# LOOP (in charge of reading the serial monitor)
# ==========================================

print("ðŸ“¡Reading serial monitor...")
print("--------------------------------------------------")

try:
    while True:
        
        plt.pause(0.01)
        
       
        if ser.in_waiting > 0:
            
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            
            if line.startswith("JSON_DATA:"):
                json_string = line.replace("JSON_DATA:", "") #Eliminates the prefix to keep only the "true" JSON
                update_plot(json_string)
            
            elif line != "":
                print(f"[ESP32] {line}")
                
except KeyboardInterrupt:
    # Capturamos CTRL+C para un cierre limpio
    print("\nClosing COM...")
    ser.close()
    plt.close('all')