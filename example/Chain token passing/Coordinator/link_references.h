/*LINK REFERENCES*/

// Keeps, for every PAIR of devices that forms (or formed) a link of the composition:
//   - its reference distance: median of its first LINK_REF_SAMPLES measures, computed on the fly
//     (state "calibrating n/30"). When the samples are complete, the reference is fixed and never changes.
//   - how many consecutive cycles it has gone unseen (capped at UNSEEN_CYCLES_CAP).
//
// References are stored by pair (e.g. B2-B3), not by position: if the composition changes, the pairs that
// still exist keep their reference, and only new pairs start calibrating. Nothing is ever blocked.
//
// This module doesn't use the radio. It can be tested on a PC.

#pragma once

#include "composition.h"

#define LINK_REF_SAMPLES 30			// Measures used to compute (and fix) each reference
#define LINK_REF_MIN_SAMPLES 3		// Below this, the provisional reference isn't used (deviation = 0)
#define UNSEEN_CYCLES_CAP 10		// Same cap as the training data (calculo_columnas.py)
#define MAX_LINK_REFS 12			// Pairs stored. When full, pairs not in the composition are reused


struct LinkReference {
	bool in_use;
	uint8_t a, b;						// Pair of devices (order doesn't matter)
	float samples[LINK_REF_SAMPLES];
	uint8_t num_samples;
	float reference;					// Median of the samples (provisional until fixed)
	bool fixed;
	uint8_t unseen_cycles;				// Consecutive cycles without a measure (capped)
};

struct LinkReferenceTable {
	LinkReference refs[MAX_LINK_REFS];
};


void initLinkReferences(LinkReferenceTable &table);

// Returns the entry of the pair (a, b), in any order. nullptr if it doesn't exist.
LinkReference* findLinkReference(LinkReferenceTable &table, uint8_t a, uint8_t b);

// Updates the pair with this cycle's observation:
//   - seen      --> unseen counter back to 0, and the distance is added to the samples if still calibrating
//   - not seen  --> unseen counter +1 (capped)
// Creates the entry if it doesn't exist. 'composition' is used to choose which entry to reuse when full.
void updateLinkReference(LinkReferenceTable &table, uint8_t a, uint8_t b, bool seen, float distance, const Chain &composition);

// True when the reference can be used (fixed, or at least LINK_REF_MIN_SAMPLES samples)
bool linkReferenceReady(const LinkReference &ref);

// Median as pandas computes it (even number of values --> mean of the two central ones)
float medianOf(const float* values, int n);
