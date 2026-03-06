import serial
import json
import matplotlib.pyplot as plt
import networkx as nx
import sys

# ==========================================
# Reading the serial port
# ==========================================

SERIAL_PORT = 'COM6'  # Change & introduce coordinator's serial port.
BAUD_RATE = 115200

try:
    #tries to read the serial port at the baud rate indicated.
    #timeout is the max time to read the whole monitor once the reading has started.
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
except Exception as e:
    
    print(f"ERROR: Port {SERIAL_PORT} could not be opened")
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
    Visualización en 'Arco' (Arcoíris técnico). 
    Fuerza los arcos hacia ARRIBA y escala el dibujo para máxima claridad.
    """
    axis.clear()
    
    # Using multigraph instead of graph, to show all of the measurements
    G = nx.MultiGraph()
    update_time = 0
    
    try:
        data = json.loads(json_string)
        measures = data.get("measures", [])
        update_time = data.get("time_between_cycles",0)
        if not measures: return

        for m in measures:
            # Saves all of the measures taken by the DW1000 (without filtering)
            G.add_edge(m["from"], m["to"], weight=m["dist"],pwr = m.get("rxpwr",0))
            
    except Exception as e:
        print(f"Error en JSON: {e}")
        return

    if len(G.nodes) < 2: return

    # 1. Kamada-Kawai Algorithm
    try:
        pos_1d = nx.kamada_kawai_layout(G, weight='weight', dim=1)
        # The algorithm returns the (x,y) coordinates in which is node has to be placed in order to reduce "conflicts"
    except: return

    # Now, i sort the nodes depending on their "X" position, from lower to highest.
    # "Rank" renames their indexes, from 0 to N.    
    nodes_sorted = sorted(G.nodes(), key=lambda n: pos_1d[n][0])
    rank = {node: i for i, node in enumerate(nodes_sorted)}

    # 2. Visual Scaling --> To avoid plotting nodes too close to each other.
    VISUAL_SCALING = 20 
    pos_final = {node: (pos_1d[node][0] * VISUAL_SCALING, 0) for node in G.nodes()}

    # 3. Node and labels drawing
    nx.draw_networkx_nodes(G, pos_final, ax=axis, node_color='#4CAF50', node_size=850, edgecolors='black')
    nx.draw_networkx_labels(G, pos_final, ax=axis, font_size=10, font_weight="bold", font_color='white')


    # 4. Drawing each measure as an arch
    max_y = 0

    for u, v, key, data in G.edges(data=True, keys=True):
        dist_val = data['weight']
        rx_pwr_val = data['pwr']
        hops = abs(rank[u] - rank[v])

        base_rad = 0.25 * hops  
        extra_rad = 0.2 * key 
        rad_value = base_rad + extra_rad
        
        n_l, n_r = (u, v) if pos_final[u][0] < pos_final[v][0] else (v, u)
        
        nx.draw_networkx_edges(
            G, pos_final, 
            edgelist=[(n_l, n_r)],
            connectionstyle=f"arc3,rad=-{rad_value}", 
            ax=axis, width=2.5, edge_color='#607D8B', alpha=0.5
        )

        # Placing the distance text on the top of the archs.
        x1 = pos_final[n_l][0]
        x2 = pos_final[n_r][0]
        dx = abs(x2 - x1)
        
        x_text = (x1 + x2) / 2

        y_text = dx * (rad_value / 4.0) 
        
        if y_text > max_y: max_y = y_text

        label_str = f"{dist_val:.2f}m\n{rx_pwr_val:.1f}dBm"

        axis.text(
            x_text, y_text + 0.1, label_str, 
            size=8, color='darkblue', fontweight='bold',
            horizontalalignment='center', 
            bbox=dict(facecolor='white', alpha=0.8, edgecolor='none', pad=0.5)
        )


    # 6: Adding the time between prints text: 
    axis.text(
        0.02, 0.95, f"Cycle time: {update_time} ms", 
        transform=axis.transAxes, fontsize=10, fontweight='bold',
        verticalalignment='top', bbox=dict(boxstyle='round', facecolor='orange', alpha=0.3)
    )
    # 7. Window adjustments
    axis.set_title("Active measurements between nodes", fontweight='bold', pad=20)
    axis.set_yticks([]) # the wagons are in a straight line --> No ticks in the Y axis are needed
    
    x_vals = [p[0] for p in pos_final.values()]
    axis.set_xlim(min(x_vals) - 20, max(x_vals) + 20)
    # forces the window to start at -3 (below the nodes) and go upwards.
    axis.set_ylim(-3, max_y + 8) 
    
    plt.draw()
    plt.pause(0.001)


# ==========================================
# LOOP (in charge of reading the serial monitor)
# ==========================================

print("¡Reading serial monitor...")
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