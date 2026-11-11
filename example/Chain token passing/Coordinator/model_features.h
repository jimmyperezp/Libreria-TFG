/*MODEL FEATURES

This code (.h and .cpp) has the goal of creating the same format that the TinyML uses, 
but locally in the coordinator instead than on the PC using python. It must be created using the same order
as the training CSVs.

It turns the cycle's raw data (which can be seen in the monitor using the RAW_DATA print) into said format.

This format is: 

	1: For each LINK (c_w1,c_w2,c_w3,etc) (not pairs, links)
		- [seen][unseen_cycles][deviation_m][acc_x_diff]

	2: For each two-hop PAIR: 
		- [seen][unseen_cycles][deviation_m]


	Whatever wasn't received is set to 0. The reference distances (deviation_m) and unseen counters (unseen_cycles) come from the 
	link_references code.
*/

#pragma once

#include "link_references.h"

#define RAW_VEHICLES 5						// Fixed positions: coordinator + 4 nodes (c, w1, w2, w3, w4)
#define RAW_LINKS (RAW_VEHICLES - 1)		// Links between consecutive positions: c_w1, w1_w2, w2_w3, w3_w4
#define RAW_PAIRS (RAW_VEHICLES - 2)		// Two-hop pairs (grandson): c_w2, w1_w3, w2_w4
#define NUM_FEATURES 25						// The total number of "columns" that the model will need.


struct RawRow {
	/*Saves all of the received data in this cycle. This is used to export the raw data CSVs that will later be processed
	to be valid for the model. The grabador_serie.py reads the monitor until it sees the "RAW_DATA" header (shown thanks to
	the printRawData function), and exports all of the data to the raw csv.

	Later on, whenever the TinyML model is used, the computeFeatures() function comes to this rawData and extracts only the 
	values that the model needs. For example, the RX Power, if received in the data report type, is not used by the model, it 
	simply enters the raw CSV to keep track of everything received/known.
	*/

	uint32_t cycle;							//Simply keeps a count. Not used/needed by the model. 
	unsigned long time_ms;					// millis() when ending the cycle.
	unsigned long duration_ms;

	bool    position_used[RAW_VEHICLES];	// false if the composition has fewer vehicles than positions
	uint8_t ids[RAW_VEHICLES];				// Device in each position

	bool  link_seen[RAW_LINKS];				// Link i joins positions i and i+1
	float link_distance[RAW_LINKS];			// [m]

	bool  pair_seen[RAW_PAIRS];				// Pair i joins positions i and i+2
	float pair_distance[RAW_PAIRS];			// [m]

	bool  imu_received[RAW_VEHICLES];
	float acc[RAW_VEHICLES][2];				// [m/s^2]  x, y (the only axes that travel in the report)
};


// Computes the 25 features. 'references' must already be updated with this cycle (updateLinkReference).
void computeFeatures(const RawRow &row, LinkReferenceTable &references, float features[NUM_FEATURES]);

// Name of feature i (same names as the training CSV columns)
const char* featureName(int i);
