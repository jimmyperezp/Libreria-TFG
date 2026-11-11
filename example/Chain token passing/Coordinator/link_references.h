/*LINK REFERENCES

For every pair of devices, this saves a reference value for the distance between them. 
The TinyML model will use that reference distancec to calculate how off the link is (desviacion_m = distance measured - reference).
This way, the model doesn't learn "21m is OK", but "variations of around 3cm are normal".

The Pairs include every link of the composition (consecutive devices, e.g. B2-B3) and every two-hop pair (a node with its
grandson, e.g. B1-B3).

In order to save these reference measurements,

	- Each pair is calibrated during LINK_REF_SAMPLES (defined on top of this file. Currently, 30 calibration measures).
	- The calibration counts measures, not cycles. A cycle in which the pair isn't seen, doesn't count.
	- While calibrating, the reference is the median of he measures received so far (state "calibrating n/30").
	- Once reaching LINK_REF_SAMPLES, then the reference is fixed and never changes again. 
	- The references are stored by pair (for example, between B2 and B3). This way, if the composition changes, the pairs 
	  that still exist keep their reference value, and only the new ones need to calibrate.

The link_references (.h and .cpp) also keep, for every pair, how many consecutive cycles it has gone unseen (ciclos_sin_ver, capped at
UNSEEN_CYCLES_CAP), which is another input of the model.
*/

#pragma once

#include "composition.h"

#define LINK_REF_SAMPLES 30			// Number of measures used to calibrate each reference.
#define LINK_REF_MIN_SAMPLES 3		// The minimum of references needed to calibrate a measure. Below 3, then it isn't used (deviation = 0).
#define UNSEEN_CYCLES_CAP 10		// The (ciclos_sin_ver) has a limit so that the model doesn't give it too much weight.
#define MAX_LINK_REFS 12			// Pairs stored. When full, pairs not in the composition are reused


struct LinkReference {
	bool in_use;						// Simply informs if this reference is being used.
	//This is useful because the LinkReferenceTable created right after (which is an array of references) has MAX_LINK_REFS positions. 
	//Checking if each one is in use allows the program to use that slot or go to the next one.
	uint8_t a, b;						// Short addresses of the pair of devices (order doesn't matter)
	float samples[LINK_REF_SAMPLES];	// Stores all of the calibration measures used for each pair. It's modified inside updateLinkReference().
	uint8_t num_samples;				// Keeps a count of how many have been received.
	float reference;					// Median of the samples (provisional until fixed)
	bool fixed;							// Informs if the samples have reached LINK_REF_SAMPLES and so, the reference is fixed.
	uint8_t unseen_cycles;				// Consecutive cycles without a measure (capped)
};

struct LinkReferenceTable {
	LinkReference refs[MAX_LINK_REFS]; 
	//Creates an array of linkreferences. Each item of the array will be the reference value of a pair of the system.
};


void initLinkReferences(LinkReferenceTable &table);
//It sets the "in_use" variable of the reference to false in all of the pairs.

LinkReference* findLinkReference(LinkReferenceTable &table, uint8_t a, uint8_t b);
// Receives the full table of references, and the short addresses to look for. Returns the full reference or nullptr if it isn't found

void updateLinkReference(LinkReferenceTable &table, uint8_t a, uint8_t b, bool seen, float distance, const Chain &composition);
/*
This function is called inside updateCompositionLinks(), inside processcycle(). It is called every cycle for every pair.
Updates the references table with this cycle's observation. It receives as parameters:

	- The full references table as a pointer so that its memory can be written into.
	- The short addresses of the devices involved. 
	- This isn't only executed when the pair is seen, but in every cycle. This means that the "seen" variable is needed.
	- Distance: the measured distance in this cycle. If seen is false, then this isn't attended.
	- Composition: this function internally calls createLinkReference if the pair is unknown. The composition is needed if the 
	  references table is full, to see what pairs no longer exist so that they can be erased.
*/

bool linkReferenceReady(const LinkReference &ref);
// Checks if the reference received is ready (this can be true if it's fixed or has at least LINK_REF_MIN_SAMPLES samples)

float medianOf(const float* values, int n);
// Median as pandas computes it (even number of values --> mean of the two central ones)
