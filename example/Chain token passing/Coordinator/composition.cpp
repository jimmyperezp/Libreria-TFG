/*COMPOSITION ANALYSIS*/
// See composition.h for the explanation of the whole process.

#include "composition.h"


void initComposition(CompositionState &state){
	state.current.length = 0;
	state.consolidated = false;
	state.startup_cycles = 0;
	state.candidate.length = 0;
	state.candidate_cycles = 0;
}


bool chainsEqual(const Chain &a, const Chain &b){
	if(a.length != b.length) return false;
	for(int i = 0; i < a.length; i++){
		if(a.ids[i] != b.ids[i]) return false;
	}
	return true;
}


int chainIndexOf(const Chain &chain, uint8_t id){
	for(int i = 0; i < chain.length; i++){
		if(chain.ids[i] == id) return i;
	}
	return -1;
}


Chain buildObservedChain(uint8_t coordinator, int16_t coordinator_son, const Measurement* measures, int num_measures){

	Chain chain;
	chain.length = 0;
	chain.ids[chain.length++] = coordinator;

	if(coordinator_son < 0) return chain;	// Token wasn't accepted by anyone: only the coordinator is observed

	int16_t next = coordinator_son;

	while(next >= 0 && chain.length < MAX_CHAIN_LENGTH){

		chain.ids[chain.length++] = (uint8_t)next;
		uint8_t current = (uint8_t)next;
		next = -1;

		// Each node reports only its measure with its son, so the next device is the OTHER END of the active
		// measure that involves 'current' and a device not yet in the chain.
		// The direction of the measure is NOT used: the coordinator stores each pair once (logMeasure searches
		// both directions), so a measure reported as B3->B2 may be stored as B2->B3 from a previous cycle.
		// The coordinator's own measures are skipped: its son is known from the token handoff ACK.
		for(int i = 0; i < num_measures; i++){
			if(!measures[i].active) continue;
			uint8_t a = measures[i].short_addr_origin;
			uint8_t b = measures[i].short_addr_dest;
			if(a == coordinator || b == coordinator) continue;

			uint8_t other;
			if(a == current) other = b;
			else if(b == current) other = a;
			else continue;

			if(chainIndexOf(chain, other) != -1) continue;	// Already in the chain (e.g. the parent)
			next = other;
			break;
		}
	}
	return chain;
}


ChainComparison compareChains(const Chain &stored, const Chain &observed){

	int missing = 0;
	for(int i = 0; i < stored.length; i++){
		if(chainIndexOf(observed, stored.ids[i]) == -1) missing++;
	}

	int extra = 0;
	for(int i = 0; i < observed.length; i++){
		if(chainIndexOf(stored, observed.ids[i]) == -1) extra++;
	}

	if(missing > 0) return CHAIN_MISSING;		// Also when there are new devices at the same time (prudence)
	if(extra > 0)   return CHAIN_NEW_DEVICES;
	if(chainsEqual(stored, observed)) return CHAIN_EQUAL;
	return CHAIN_REORDERED;
}


CompositionEvent updateComposition(CompositionState &state, const Chain &observed, ChainComparison &comparison){

	// First cycle: whatever is observed is the first composition
	if(state.current.length == 0){
		state.current = observed;
		comparison = CHAIN_NEW_DEVICES;
		state.startup_cycles = 1;
		return COMP_EVENT_FIRST;
	}

	comparison = compareChains(state.current, observed);
	bool is_change = (comparison == CHAIN_NEW_DEVICES || comparison == CHAIN_REORDERED);


	// Startup window: open composition. Changes that don't lose devices are taken straight away.
	if(!state.consolidated){

		state.startup_cycles++;
		CompositionEvent event = COMP_EVENT_NONE;

		if(is_change){
			state.current = observed;
			event = COMP_EVENT_UPDATED_STARTUP;
		}

		if(state.startup_cycles >= COMPOSITION_WINDOW_CYCLES){
			state.consolidated = true;
			event = COMP_EVENT_CONSOLIDATED;
		}
		return event;
	}


	// Normal operation: a change must persist during a whole window before being adopted.
	if(is_change){

		if(state.candidate_cycles > 0 && chainsEqual(state.candidate, observed)){
			state.candidate_cycles++;
		}
		else{
			state.candidate = observed;
			state.candidate_cycles = 1;
			if(COMPOSITION_WINDOW_CYCLES > 1) return COMP_EVENT_CANDIDATE_STARTED;
		}

		if(state.candidate_cycles >= COMPOSITION_WINDOW_CYCLES){
			state.current = state.candidate;
			state.candidate.length = 0;
			state.candidate_cycles = 0;
			return COMP_EVENT_ADOPTED;
		}
		return COMP_EVENT_NONE;
	}

	// EQUAL or MISSING: if there was a candidate, it didn't persist
	if(state.candidate_cycles > 0){
		state.candidate.length = 0;
		state.candidate_cycles = 0;
		return COMP_EVENT_CANDIDATE_DISCARDED;
	}
	return COMP_EVENT_NONE;
}


const char* comparisonName(ChainComparison comparison){
	switch(comparison){
		case CHAIN_EQUAL:       return "equal";
		case CHAIN_MISSING:     return "missing devices";
		case CHAIN_NEW_DEVICES: return "new devices";
		case CHAIN_REORDERED:   return "reordered";
	}
	return "?";
}
