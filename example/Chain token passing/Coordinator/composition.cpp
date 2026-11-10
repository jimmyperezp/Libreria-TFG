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
	
	// This function is only called from inside "updateComposition".
	// Stored is the saved composition: supposed to be valid. 
	// Observed is the one seen this cycle: now I have to check it.

	int missing = 0;
	for(int i = 0; i < stored.length; i++){
		if(chainIndexOf(observed, stored.ids[i]) == -1) missing++;
		// I check if all of the stored devices are present in the observed chain. If not, then it's missing.
	}

	int extra = 0;
	for(int i = 0; i < observed.length; i++){
		if(chainIndexOf(stored, observed.ids[i]) == -1) extra++;
		// The other way: I see if all of the seen this cycle existed in the stored. If not, then it's new.
	}

	if(missing > 0) return CHAIN_MISSING;		
	if(extra > 0)   return CHAIN_NEW_DEVICES;
	if(chainsEqual(stored, observed)) return CHAIN_EQUAL;
	return CHAIN_REORDERED;
}


CompositionEvent updateComposition(CompositionState &state, const Chain &observed, ChainComparison &comparison){

	/*Receives: 
		1: State: This includes the current chain, the flags to know if it's consolidated, etc.
		2: observed: The chain seen this cyle
		3: 	the buffer to where the comparison is going to be stored.

	Returns: the event of this composition. See posible defined events in composition.h
	*/

	if(state.current.length == 0){
		//The first cycle? (The length is only 0 on the first cycle)
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
			state.consolidated = true; //The required number of cycles has been reached. The composition is consolidated.
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
			state.candidate = observed; //I save the candidate so that in following reads I can compare
			state.candidate_cycles = 1;
			if(COMPOSITION_WINDOW_CYCLES > 1) return COMP_EVENT_CANDIDATE_STARTED;
		}

		if(state.candidate_cycles >= COMPOSITION_WINDOW_CYCLES){
			// If the same candidate has produced the same change for COMPOSITION_WINDOW_CYCLES,
			// then the composition is consolidated.
			state.current = state.candidate;
			state.candidate.length = 0;
			state.candidate_cycles = 0;
			return COMP_EVENT_ADOPTED;
		}
		return COMP_EVENT_NONE;
	}

	// If reaching here, the chain was equal or there's a device missing. 
	// if there were previous change candidates, they didn't persist, so they're eliminated.
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
