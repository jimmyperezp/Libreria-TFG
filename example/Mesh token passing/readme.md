# Mesh networks: Token passing

> <img src="https://cdn.worldvectorlogo.com/logos/arduino-1.svg" alt="arduino" align = "right" width="60" height="60"/> This code is an upgrade from the [*Chain token passing*](/example/Chain%20token%20passing) example. It solves the necessity to have the boards in a straight line, being able to explore multiple "branches" thanks to the use of a network cycle ID.

<br>

### The need for this "upgrade"

In the chain token passing example, the boards are expected to be placed in a somewhat linear form. This means the token is passed "downwards" by simply passing it to the closest node.  
However, if the layout is not linear, then that system is useless. 

Let's imagine a node layout like the seen in the following image: 


<p align="center">
<img src="https://github.com/jimmyperezp/Libreria-TFG/blob/main/extras/images/Mesh%20Network%20layout.png" alt="Mesh Network node layout" width="600" height="600"/>

If the same logic is applied, the coordinator could pass the token to B1 (let's imagine that is the closest node), but instead of following that yellow branch, B1 might have B5 closer, therefore passing the token to that other branch. The nodes B2 and B3 would not receive the token in this situation.  

Also, if the token is only passed to the closest node, it would explore a full branch (or jump between several ones) until reaching the end. Then, it would return to the coordinator following the same path but backwards. This means that no matter what, the unexplored nodes would remain unexplored. 


<br>

### Implemented solution (Cycle ID)

In order to achieve a good functioning of the system, the chosen solution has been to keep track of the cycle in which the coordinator is currently in.  
This has some implications: 
- Every device will try to pass the token to all of those that it can reach. 
- Inside the token handoff payload, the coordinator's cycle ID is codified. When any device receives a token, it extracts that byte. Then it decides whether to use it or not: 
    - If the received ID is greater than the one already saved by the device, then it accepts it, sends a token handoff ACK and updates its own_cycle_id value.
    - If the received ID is the same than the one it currently has, then it rejects the token, and sends a token handoff NACK.   
    Going back to the previous example seen in the image, this could happen if: The coordinator passes the token to B1. B1 then sees B3 and B5, and passes the token to both of them (one at a time, and waits for the return from each one before moving on to the next).  
    Then, B1 sends the report to C1. After, C1 would try to pass the token to B5, but it already received it from B1. This would mean that C1 would be trying to send a token with a cycle ID same as the one that B5 already has. Then B5 sends a NACK and C1 moves on to try to pass it to the next node it has seen.
    
- Once the coordinator has tried to send the token to all of the devices it sees during its discovery, then it shows the data collected.


<br>

### Changes made

In order to achieve the explained functioning, the following updates have been made to the library: 

- The token handoff callback now receives as a parameter the *cycle_id*, extracted from the payload of the received TOKEN_HANDOFF message.
- The DW1000Device objects now have the getters and setters to handle each one's cycle_id. This way, when receiving a NACK or an ACK, then the board's can update the other's value.
- The coordinator is the only one that calls the setOwnCycleID with a variable changed inside its local code (every cycle, this counter is increased and updated).



----
Author: Jaime Pérez  
Last modified: 05/04/2026  

<img src="https://github.com/jimmyperezp/Programacion_de_sistemas/blob/main/logo%20escuela.png" align="right" alt="logo industriales" width="300" height="80"/> 















