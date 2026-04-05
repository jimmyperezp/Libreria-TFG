# Hub & Spoke Coordination

><img src="https://cdn.worldvectorlogo.com/logos/arduino-1.svg" alt="arduino" align = "right" width="60" height="60"/> This example centralizes the system's measurements in a coordinator. It does this while using a hub & spoke (or star) topology

> This example has maximum distance limitations. The next one, [Token passing chain](/example/Token%20passing%20chain) is an ampliation of this example.


<br>

### Hub & Spoke coordination

This is the topology used in this example:



<p align="center">
<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/extras/images/Coordinator-centered%20topology.png" alt="Coordinator-centered topology" width="300" height="300"/>
<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/extras/images/Hub%20%26%20Spoke%20Topology.png" alt="Hub & Spoke topology" width="350" height="350"/>


<p align="center"> (Both images show a coordinator-centered topologies. The right one shows a hub & spoke topology by definition)
<p>

Really, this topology consists on any layout in which the coordinator "speaks" with all of the devices it reaches and none other.


<br>

### FSM and system's procedure

The coordinator follows the next structure:

1. The coordinator starts ranging via unicast. It discovers all of the devices it can reach, and targets them via unicast, one by one, to measure the distance between them   

2. After measuring with them all, then the coordinator starts with the *initiator Handoff*. This next step consists on the coordinator "allowing" each of the devices it knows to become an "*initiator*" for a period of time.  

3. The nodes (or slaves) receive the mode switch order from the coordinator (or master). Then, they become initiators and replicate the process previously done by the coordinator: they discover, and then range via unicast with their known devices. 

4. Once the nodes (or slaves) finish with all of their rangings, then they let the master know by sending it their data report. Alternatively, if the master reaches a waiting timeout, then it forces the slaves to turn back to responders. This way, there is only one initiator at a time in the system, avoiding message colissions.

5. This process is repeated until the master has passed the initiator handoff to all of its known devices. 

6. After receiving the last report, the master (or coordinator) shows the collected results and resets them for following cycles.

<br>

### Limitations

As explained in the system's procedure, the coordinator hands off the turn to become initiator to only the devices it knows.  

Later, these can measure with devices that the master doesn't reach, but they can't hand them the initiator turn.   

This clearly presents a maximum measurable distance limitation. In order to solve this, the nodes should be more "intelligent", being able to pass the initiator turn to devices they reach, without caring if the coordinator sees them or not.  

This is precisely what is achieved in the next example. See [Token Passing Chain](/example/Token%20passing%20chain)


<br><br>
------------
Author: Jaime Pérez Pérez  
Last modified: 04/04/2026  

<img src="https://github.com/jimmyperezp/Programacion_de_sistemas/blob/main/logo%20escuela.png" align="right" alt="logo industriales" width="300" height="80"/> 


