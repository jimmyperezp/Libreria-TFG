/*COMPOSITION ANALYSIS*/
// See composition.h for the explanation of the whole process.

#include "composition.h"


void initComposition(CompositionState &state){
	// Called on start. Sets everything to 0 and false.
	state.current.length = 0;
	state.consolidated = false;
	state.startup_cycles = 0;
	state.candidate.length = 0;
	state.candidate_cycles = 0;
}


bool chainsEqual(const Chain &a, const Chain &b){
	//Goes through both id arrays. If all of the positions are equal, returns true.
	if(a.length != b.length) return false;
	for(int i = 0; i < a.length; i++){
		if(a.ids[i] != b.ids[i]) return false;
	}
	return true;
}


int chainIndexOf(const Chain &chain, uint8_t id){
	// Searches for the position inside the IDs array of a certain given ID.
	// The chain received is set as constant so that it can't be modified.
	for(int i = 0; i < chain.length; i++){
		if(chain.ids[i] == id) return i;
	}
	return -1;
}


Chain buildObservedChain(uint8_t coordinator, int16_t coordinator_son, const uint8_t* order, int order_length){

	Chain chain;
	chain.length = 0;
	chain.ids[chain.length++] = coordinator;

	// No report this cycle: the only node known is the one that accepted the coordinator's token (if any)
	if(order_length <= 0){
		if(coordinator_son >= 0) chain.ids[chain.length++] = (uint8_t)coordinator_son;
		return chain;
	}

	// The report already gives the nodes in the order of the chain: they are copied as they are.
	// A repeated address (malformed report) is skipped so that the chain never contains a device twice.
	for(int i = 0; i < order_length && chain.length < MAX_CHAIN_LENGTH; i++){
		if(chainIndexOf(chain, order[i]) != -1) continue;
		chain.ids[chain.length++] = order[i];
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
