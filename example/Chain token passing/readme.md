# Token Passing Chain

> <img src="https://cdn.worldvectorlogo.com/logos/arduino-1.svg" alt="arduino" align="right" width="60" height="60"/> Linear token passing network: every device gets the chance to range, and all of the measurements are gathered in the coordinator in every cycle.  
On top of that, the coordinator analyzes the **composition of the train** (which wagons form it and in which order) and prepares the inputs of the **TinyML model** that classifies the state of the train.

<br>

### Index

1. [Example's goal](#examples-goal)
2. [FSM](#fsm)
3. [Data report](#data-report)
4. [Train composition](#train-composition)
5. [Link references](#link-references)
6. [Model inputs and AI prediction](#model-inputs-and-ai-prediction)
7. [Files of the example](#files-of-the-example)
8. [Configuration and serial outputs](#configuration-and-serial-outputs)
9. [WIP: pending changes / possible upgrades](#wip-pending-changes--possible-upgrades)

<br><br>

### Example's goal

The previous example (Hub & Spoke coordination) had a main limitation: the maximum measurable length.  
The coordinator handed off the initiator turn only to the devices it reached. Any device placed further along, outside of the coordinator's reach, never got the chance to range.  
This results in a big distance limitation: the measured length was, at most, coordinator-node + node-node (two "connections").

<p align="center">
<img src="../../extras/images/Coordinator-centered%20topology.png" alt="Coordinator-centered topology" width="300" height="300"/>
<img src="../../extras/images/Hub%20%26%20Spoke%20Topology.png" alt="Hub & Spoke topology" width="350" height="350"/>
</p>

<p align="center">(Both images show coordinator-centered topologies. The right one is a hub & spoke topology by definition)</p>

<br>

In a real train, the number of wagons can clearly be greater than 2. That's why this solution was developed.

It increases the capabilities of the nodes. Before, they could only range and, once they finished, send their data back to the coordinator.  
Now, nodes can also hand off the turn to range to further nodes along the way. From now on, this handoff is called *passing the token*.

The nodes must save their "parent" (the device that sent them the token) and pass the token to the closest "son" they can reach.  
Every time the token is sent, the sender "turns off" its ranging: it won't answer the polls of the device that has the token. This way, that device only gets answers from devices further along the line. The token only moves forward: it can't go backwards or get lost in the chain.

The token travels "down" the line. When it reaches a node that doesn't discover any more devices, that node is the TAIL of the train.  
The tail then builds the data report with its own data and sends it to its parent. Each parent adds its own data and passes the report on, until it reaches the coordinator.

<br>
<p align="center">
<img src="../../extras/images/Token%20handoff%20%26%20report.png" alt="Token passing topology" width="800" height="600"/>
</p>

<br><br>

### FSM

The FSM follows the steps shown in the following diagram:

<p align="center">
<img src="../../extras/images/Token%20passing%20FSM%20(1).png" alt="Token passing FSM" width="300" height="400"/>
</p>

This diagram shows the main states shared by both types of boards (coordinator and nodes). A few key points:

- The token is handed off to the closest node, chosen by comparing the measurements done in the ranging state.
- If a device doesn't answer the polls or the token handoff, it is marked as inactive, and it won't be targeted for ranging or token handoffs until the next cycle.
- Every message is sent via unicast (polls, token handoffs, token handoff ACKs, data reports and data report ACKs).
- After the "Wait for return" state, the coordinator and the nodes follow different paths:
    - Nodes: after receiving the data report from their son, they add their own data and send the report to their parent (states "Return to parent" and "Wait for return to parent ACK", not shown in the diagram).
    - Coordinator: once it receives the report, it processes the cycle (composition, references, model inputs) and shows the results.
- If no report arrives while waiting for the return, the line is cut at that point and the devices act as if they were the tail:
    - Nodes send their own data to their parents, without waiting for their sons.
    - The coordinator processes the cycle with its own measurements only.

<br><br>

### Data report

The report type is chosen by the coordinator and travels with the token (`REPORT_TYPE` in *Coordinator.ino*). Currently, the only type is **`DATA_REPORT_MODEL`**, designed to feed the TinyML model. Its format is defined in *src/DW1000Ranging.h*:

```
[MAC (9)][type (1)][N (1)] + N node blocks

Node block: [address (1)][control (1)][son: address (1) + distance (2)][grandson: address (1) + distance (2)][acc x (2)][acc y (2)]
```

- There is **one block per node**. Each node puts its own block first, in front of the blocks received from its son. This way, the report reaches the coordinator **in the order of the train**: `[W1][W2][W3][W4]`.
- Each block carries up to **two measures**:
    - **Son**: the distance to the node that accepted this node's token.
    - **Grandson**: the distance to the son of its son, if this node could measure with it. It adds redundancy: if a link fails, its two ends are still related through the two-hop measures.
- The control byte says how many measures the block has (0 for the tail, 1 with only the son, 2 with son and grandson). Blocks are 6, 9 or 12 bytes long.
- Distances travel in cm (`uint16`) and accelerations in mm/s² (`int16`), saturated to their ranges.
- **Acceleration** (x and y): read by `readImu()` in *Node.ino*. The current boards don't have an IMU, so it is 0 for now; when the sensor is added, only that function has to be filled in.

<br><br>

### Train composition

> Files: *composition.h* / *composition.cpp*

Every cycle reveals the chain. As the report arrives in the order of the train, the coordinator reads the order of the nodes from it (`chain_order`) and builds the **observed chain**: `C1 > B1 > B2 > B3 > B4`. If no report arrives, the observed chain is the coordinator and the node that accepted its token (if any).

The observed chain is compared with the **stored composition**:

| Result | Meaning | Action |
|---|---|---|
| `EQUAL` | Same devices, same order | Nothing |
| `MISSING` | Some stored devices were not observed | It's an **observation** (loss of communication or a rupture), **never** a composition change: the composition is kept |
| `NEW_DEVICES` | Every stored device is there, plus new ones | A wagon was coupled: candidate change |
| `REORDERED` | Same devices, different order | Candidate change |

How the stored composition is updated:

- **Startup**: during the first `COMPOSITION_WINDOW_CYCLES` cycles (30), the composition is open. Any observed chain that doesn't lose devices replaces it straight away. Afterwards, the composition is **consolidated**.
- **Normal operation**: a `NEW_DEVICES` or `REORDERED` chain opens a **candidate**. If the same candidate is observed during `COMPOSITION_WINDOW_CYCLES` consecutive cycles, it is **adopted**. Otherwise, it is discarded.

This module doesn't use the radio: it only receives the order of each cycle, so it doesn't depend on the report format and can be tested on a PC.

<br><br>

### Link references

> Files: *link_references.h* / *link_references.cpp*

The model doesn't receive raw distances, but how far each one is from its usual value (`deviation_m = measured distance - reference`). This way, it doesn't learn "21 m is OK", but "variations of a few cm are normal", and it works with any wagon length.

A reference is kept for **every pair of devices** of the composition: each link (consecutive devices, e.g. B2-B3) and each two-hop pair (a node with its grandson, e.g. B1-B3).

- **Calibration**: each pair uses its first `LINK_REF_SAMPLES` measures (30). It counts measures, not cycles: a cycle in which the pair isn't seen doesn't count.
- While calibrating, the reference is the **median** of the measures received so far. It is used once there are at least `LINK_REF_MIN_SAMPLES` (3); before that, the deviation is 0.
- After 30 measures, the reference is **fixed** and never changes again.
- References are stored **by pair of devices**, not by position. If the composition changes, the pairs that still exist keep their reference, and only the new ones calibrate.
- For every pair, the module also counts how many consecutive cycles it has gone unseen (`unseen_cycles`, capped at `UNSEEN_CYCLES_CAP` = 10), another input of the model.

The same calibration is applied by the Python scripts that build the training datasets (*extras/AI Integration*), so the model receives the same values on the board as in training.

<br><br>

### Model inputs and AI prediction

> Files: *model_features.h* / *model_features.cpp* · Model: *src/tinyML_inferencing.h* (Edge Impulse export)

Each cycle, the coordinator turns its data into the **25 inputs** of the model, in the same order as the training CSVs:

- **Raw row** (`RawRow`): the observation of the cycle placed in fixed positions (`c`, `w1`…`w4`: coordinator + 4 wagons), with each link, each two-hop pair and the acceleration of each vehicle. It can be printed as a CSV row (`RAW_DATA:`), which *grabador_serie.py* (*extras/AI Integration*) reads to build the raw datasets.
- **Features**: computed from the raw row and the link references:
    - For each link (`c_w1`, `w1_w2`, `w2_w3`, `w3_w4`): `seen`, `unseen_cycles`, `deviation_m`, `acc_x_diff_ms2` (difference of longitudinal acceleration between the two vehicles).
    - For each two-hop pair (`c_w2`, `w1_w3`, `w2_w4`): `seen`, `unseen_cycles`, `deviation_m`.
    - Whatever wasn't received is 0.

With `AI_DEPLOYMENT` enabled, the coordinator runs the model with these inputs every cycle and prints the predicted state of the train. The first time, it checks that the model expects exactly 25 inputs; if not, predictions are disabled.

**Order of each cycle** (`processCycle()`, at the end of the cycle):

1. Composition: build the observed chain and update the stored composition.
2. References: update the reference and `unseen_cycles` of every pair of the composition.
3. Raw row and the 25 model inputs.
4. Serial outputs.
5. AI prediction (if enabled).

Steps 2, 3 and 5 only exist with `DATA_REPORT_MODEL` (`USE_TINYML_MODEL`); the composition works with any report type.

<br><br>

### Files of the example

| File | Content |
|---|---|
| *Coordinator/Coordinator.ino* | Coordinator FSM: discovery, ranging, token handoff, waiting for the report, cycle processing and serial outputs |
| *Coordinator/composition.h / .cpp* | Train composition: observed chain, comparison and update of the stored composition |
| *Coordinator/link_references.h / .cpp* | Reference distance and unseen cycles of every pair of devices |
| *Coordinator/model_features.h / .cpp* | Raw row in fixed positions and the 25 model inputs |
| *Node/Node.ino* | Node FSM: ranging, token handoff to its son, building its block and forwarding the report to its parent |
| *src/DW1000Ranging.h / .cpp* (library) | Messages, `DATA_REPORT_MODEL` format, encoding and decoding |
| *src/tinyML_inferencing.h* and Edge Impulse folders (library) | Exported TinyML model |

<br><br>

### Configuration and serial outputs

At the top of *Coordinator.ino*:

| Define | Default | What it does |
|---|---|---|
| `REPORT_TYPE` | `DATA_REPORT_MODEL` | Report type requested to the nodes |
| `READABLE_OUTPUT` | `true` | Each cycle in a readable format: node blocks, IMU data and inactive measures |
| `COMPOSITION_OUTPUT` | `true` | Composition of the train and state of each link (with its reference) |
| `RAW_DATA_OUTPUT` | `true` | One CSV row per cycle (`RAW_DATA:`) to build datasets |
| `FEATURES_OUTPUT` | `false` | The 25 model inputs of each cycle |
| `AI_DEPLOYMENT` | `false` | Runs the TinyML model every cycle and prints the prediction |
| `KAMADA_KAWAI_PLOTTING` | `false` | JSON output for the Kamada-Kawai plotter (*extras*) |
| `RX_DISTANCE_PLOTTING` | `false` | CSV output for the RX power-distance study (*extras*) |
| `DEBUG_COORDINATOR` | `false` | Debug messages of the FSM |

Composition, calibration and model constants are at the top of their *.h* files (`COMPOSITION_WINDOW_CYCLES`, `LINK_REF_SAMPLES`, `LINK_REF_MIN_SAMPLES`, `UNSEEN_CYCLES_CAP`). The calibration values must match the ones used by the Python scripts to build the datasets.

<br><br>

### WIP: pending changes / possible upgrades

- **Cycle time**: the target is a complete cycle in under 250 ms. Under good conditions (LOS) cycles take about 400-500 ms.
- **Line cut**: after a wait-for-return timeout, the line is cut at that point and the measures of the devices placed afterwards are lost for that cycle. This could be handled differently.
- **IMU**: the current boards don't have one, so the accelerations travel as 0. The model needs them to work properly.
- **Model size**: the model inputs use fixed positions for 5 vehicles (coordinator + 4 wagons). The composition and references work with any length, but longer trains need a model with more inputs.
- **References table**: `MAX_LINK_REFS` (12) is enough for 5 devices. For longer trains it must grow (each device adds up to two pairs).

<br><br>
------

Author: Jaime Pérez Pérez  
Last updated: 18/11/2026
<img src="https://github.com/jimmyperezp/Programacion_de_sistemas/blob/main/logo%20escuela.png" align="right" alt="logo industriales" width="300" height="80"/>
