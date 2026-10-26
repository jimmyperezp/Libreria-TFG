/*COMPOSITION ANALYSIS*/

// Keeps track of the train's composition: which devices form the train and in which order.
//
// Every token passing cycle reveals the chain: the coordinator knows which node accepted its token
// (its son), and every node reports ONLY its measure with its own son. Following those measures,
// the coordinator rebuilds the order observed in the cycle (e.g. C1 > B2 > B3 > B4).
//
// That observed chain is compared with the stored composition:
//   - EQUAL       --> nothing to do.
//   - MISSING     --> some stored devices were not observed. It's an observation (loss of communication
//                     or rupture), NEVER a composition change. The composition is kept.
//   - NEW_DEVICES --> all stored devices are there, plus new ones (a wagon was coupled).
//   - REORDERED   --> same devices, different order.
//
// Startup: the composition is open during the first COMPOSITION_WINDOW_CYCLES cycles. Any observed
//          chain that doesn't lose devices (NEW_DEVICES / REORDERED) replaces it straight away.
//          Afterwards, the composition is consolidated.
// Normal operation: NEW_DEVICES / REORDERED open a candidate. If the same candidate is observed during
//          COMPOSITION_WINDOW_CYCLES consecutive cycles, it is adopted. Otherwise it is discarded.
//
// This module doesn't use the radio: it only receives the measures of the cycle. It can be tested on a PC.

#pragma once

#include "DW1000Ranging.h"   // Measurement struct & MAX_DEVICES

#define MAX_CHAIN_LENGTH MAX_DEVICES
#define COMPOSITION_WINDOW_CYCLES 30	// Startup window & cycles a change must persist before being adopted


struct Chain {
	uint8_t ids[MAX_CHAIN_LENGTH];	// Short address headers, from the coordinator (ids[0]) to the tail
	uint8_t length;
};

enum ChainComparison {
	CHAIN_EQUAL,
	CHAIN_MISSING,
	CHAIN_NEW_DEVICES,
	CHAIN_REORDERED
};

enum CompositionEvent {
	COMP_EVENT_NONE,
	COMP_EVENT_FIRST,				// First composition obtained (first cycle)
	COMP_EVENT_UPDATED_STARTUP,		// Changed during the startup window
	COMP_EVENT_CONSOLIDATED,		// Startup window finished
	COMP_EVENT_CANDIDATE_STARTED,	// A change was observed in normal operation. Waiting for it to persist
	COMP_EVENT_CANDIDATE_DISCARDED,	// The change didn't persist
	COMP_EVENT_ADOPTED				// The change persisted for a whole window and was adopted
};

struct CompositionState {
	Chain current;					// Stored composition
	bool consolidated;
	uint16_t startup_cycles;		// Cycles elapsed in the startup window

	Chain candidate;				// Change waiting to be adopted (normal operation)
	uint16_t candidate_cycles;		// Consecutive cycles the candidate has been observed
};


// Sets the state to "no composition yet".
void initComposition(CompositionState &state);

// Rebuilds the chain observed in this cycle.
//   coordinator      --> coordinator's short address header
//   coordinator_son  --> node that accepted the coordinator's token in this cycle (-1: none)
//   measures         --> this cycle's measures (only the active ones are used)
Chain buildObservedChain(uint8_t coordinator, int16_t coordinator_son, const Measurement* measures, int num_measures);

ChainComparison compareChains(const Chain &stored, const Chain &observed);

// Updates the stored composition with this cycle's observed chain. Returns what happened.
// 'comparison' returns the comparison of the observed chain against the composition BEFORE the update.
CompositionEvent updateComposition(CompositionState &state, const Chain &observed, ChainComparison &comparison);

bool chainsEqual(const Chain &a, const Chain &b);
int  chainIndexOf(const Chain &chain, uint8_t id);	// Position of 'id' in the chain, or -1
const char* comparisonName(ChainComparison comparison);
