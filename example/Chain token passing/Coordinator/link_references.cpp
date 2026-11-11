/*LINK REFERENCES*/
// See link_references.h

#include "link_references.h"


void initLinkReferences(LinkReferenceTable &table){
	//Sets the "in_use" variable of all references to false. (the table is "empty")
	for(int i = 0; i < MAX_LINK_REFS; i++){
		table.refs[i].in_use = false;
	}
}


LinkReference* findLinkReference(LinkReferenceTable &table, uint8_t a, uint8_t b){
	//Goes through all the reference table to see if any of the entries is formed by a+b or b+a
	for(int i = 0; i < MAX_LINK_REFS; i++){
		LinkReference &r = table.refs[i];
		if(!r.in_use) continue;
		if((r.a == a && r.b == b) || (r.a == b && r.b == a)) return &r;
	}
	return nullptr;
}


static bool pairInComposition(const Chain &composition, uint8_t a, uint8_t b){
	// In use: consecutive devices (link) or devices two positions apart (two-hop pair)
	for(int step = 1; step <= 2; step++){
		for(int i = 0; i + step < composition.length; i++){
			uint8_t x = composition.ids[i], y = composition.ids[i+step];
			if((x == a && y == b) || (x == b && y == a)) return true;
		}
	}
	return false;
}


static LinkReference* createLinkReference(LinkReferenceTable &table, uint8_t a, uint8_t b, const Chain &composition){

	LinkReference* slot = nullptr;

	// 1) A free entry
	for(int i = 0; i < MAX_LINK_REFS && slot == nullptr; i++){
		if(!table.refs[i].in_use) slot = &table.refs[i];
	}
	// 2) If full, reuse a pair that is no longer part of the composition. 
	for(int i = 0; i < MAX_LINK_REFS && slot == nullptr; i++){
		if(!pairInComposition(composition, table.refs[i].a, table.refs[i].b)) slot = &table.refs[i];
	}
	if(slot == nullptr) return nullptr;	// All entries belong to the composition (more links than MAX_LINK_REFS)

	slot->in_use = true;
	slot->a = a;
	slot->b = b;
	slot->num_samples = 0; //On creation, the number of samples is 0.
	slot->reference = 0.0f;
	slot->fixed = false;
	slot->unseen_cycles = 0;
	return slot;
}


float medianOf(const float* values, int n){
	//Calculates the median (instead of the average to avoid possible wrong measures)
	if(n <= 0) return 0.0f;

	float sorted[LINK_REF_SAMPLES];
	for(int i = 0; i < n; i++) sorted[i] = values[i];

	// Insertion sort: at most 30 values
	for(int i = 1; i < n; i++){
		float key = sorted[i];
		int j = i - 1;
		while(j >= 0 && sorted[j] > key){ sorted[j+1] = sorted[j]; j--; }
		sorted[j+1] = key;
	}

	if(n % 2 == 1) return sorted[n / 2];
	return 0.5f * (sorted[n/2 - 1] + sorted[n/2]);
}


void updateLinkReference(LinkReferenceTable &table, uint8_t a, uint8_t b, bool seen, float distance, const Chain &composition){

	LinkReference* ref = findLinkReference(table, a, b);
	//First, check if the reference exists. If not, it creates it.
	if(ref == nullptr) ref = createLinkReference(table, a, b, composition);
	if(ref == nullptr) return; //If it can't be created, then the process is ended.

	if(!seen){
		if(ref->unseen_cycles < UNSEEN_CYCLES_CAP) ref->unseen_cycles++;
		return;
	}

	ref->unseen_cycles = 0;

	if(!ref->fixed){
		ref->samples[ref->num_samples++] = distance;
		ref->reference = medianOf(ref->samples, ref->num_samples);
		if(ref->num_samples >= LINK_REF_SAMPLES) ref->fixed = true;
	}
}


bool linkReferenceReady(const LinkReference &ref){
	//A reference is ready if it's fixed or has at least the minimum number of samples.
	return ref.fixed || ref.num_samples >= LINK_REF_MIN_SAMPLES;
}
