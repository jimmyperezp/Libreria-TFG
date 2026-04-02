# Token Passing Chain

> <img src="https://cdn.worldvectorlogo.com/logos/arduino-1.svg" alt="arduino" align = "right" width="60" height="60"/> Upgrade from the hub & spoke topology to a linear token passing chain.  
This example centralizes the collected data in the coordinator after giving all of the system's devices the chance to range themselves.

<br><br>  



### Example's Goal

The previous example (Hub & Spoke coordination) had a main limitation: the maximum measurable length.  
The coordinator handed off the initiator turn to only the devices it reached. Therefore, any devices placed further along, outside of the coordinator's reach, were never handed the opportunity to start ranging.  
This results in a big distance limitation: The measured length was, at maximum, the sum of coordinator-node + node-node (Two "connections" at best).  


<p align="center">
<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/images/Coordinator-centered%20topology.png" alt="Coordinator-centered topology" width="300" height="300"/>
<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/images/Hub%20%26%20Spoke%20Topology.png" alt="Hub & Spoke topology" width="350" height="350"/>


<p align="center"> (Both images show a coordinator-centered topologies. The right one shows a hub & spoke topology by definition)
<p>


In a real train, the number of wagons can clearly be greater than 2. That's why this new solution was developed.    


This example solves said problem by increasing the node's capabilities. Previously, the nodes were limited to range and, once they finished (or received a mode switch message by the coordinator), returned their data to the coordinator.  
The "updated" functioning for the nodes includes the ability to hand off the turn to start ranging to further nodes along the way. From now on, this handoff is referred to as: *passing the token*.  

This presents a big challenge: the nodes must save their "parent" (the device that sent them the token) and pass the token to the closest "son" they can reach.  
Every time the token is sent, the sender device "turns off" its ranging;  it won't answer to polls from the ranging devices. This way, the device that has the token will only get answers from devices that are even further along the line. The token will only be sent forward, preventing it from going backwards or getting lost in the chain of devices.

The token travels "downwards" the line of devices. Once it reaches a node that doesn't discover any more devices, then it has reached the end, and said device is the TAIL of the train.  
The tail then starts building up the data report. It includes its measurements and passes them to the parent device. Afterwards, the parent adds up its measurements and keeps on the data cascade until it reaches the coordinator. 
<br>
<p align="center">
<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/images/Token%20handoff%20%26%20report.png" alt="Token passing topology" width="800" height="600"/>



<br><br>
### FSM

This example's functioning is the previously described. The FSM follows the steps shown in the following diagram:  

<p align="center">
<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/images/Token%20passing%20FSM.png" alt="Token passing topology" width="300" height="400"/>

This is a simple diagram showing the main, common states for both types of boards (coordinator and nodes). However, it is important to understand a few key concepts: 
- The token handoff is made to the closest node. This is decided by comparing the measurements previously done in the ranging state. 
- If a device doensn't answer to either polls or to the token handoff, then it is marked as inactive, and won't be targeted for ranging or token handoffs until the next cycle.
- All of the messages transmitted are sent via unicast (polling, token handoffs, token handoff acks, data reports & data report acks).
- After reaching the "Wait for return state", the coordinator and node reach different states. 
    - Nodes: After receiving a data report from their "son", nodes add up their measurementsa and send the report to their parent (They move on to a different state not shown in the diagram: "Return to parent" & "Wait for return to parent ACK").
    - Coordinator: Once it receives the report, then it passes onto showing the received data
- If, while being on the wait for return state, no returns are received, then the line is cut at that point, and the devices act as if they were the tail: 
    - Nodes send their measurements to their parents, without waiting for returns from their childs
    - The coordinator moves on to showing the data collected in this cycle (its own measurements). 




<br><br>
### WIP: Pending changes / possible upgrades

The biggest challenge faced while developing this example has been to achieve complete cycles in under 250 mS.
Currently, under good conditions (LOS), cycles take up about ~400-500 mS. This shows a big improvement from previous versions, but still has room to improve.

Even though this is a much more difficult task to target, in this version, after reaching a wait for return timeout, the line is "cut" at the waiting point. All of the measurements collected by the devices placed afterwards are discarted and ignored. This could be improved or dealt in a different way.

<br><br>
------

Author: Jaime Pérez Pérez  
Last updated: 02/04/2026
<img src="https://github.com/jimmyperezp/Programacion_de_sistemas/blob/main/logo%20escuela.png" align="right" alt="logo industriales" width="300" height="80"/> 
