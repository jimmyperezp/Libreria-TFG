/*MODEL FEATURES (See model_features.h for understanding each function.)*/

#include "model_features.h"

static const char* FEATURE_NAMES[NUM_FEATURES] = {
	"link_c_w1_seen",  "link_c_w1_unseen_cycles",  "link_c_w1_deviation_m",  "link_c_w1_acc_x_diff_ms2",
	"link_w1_w2_seen", "link_w1_w2_unseen_cycles", "link_w1_w2_deviation_m", "link_w1_w2_acc_x_diff_ms2",
	"link_w2_w3_seen", "link_w2_w3_unseen_cycles", "link_w2_w3_deviation_m", "link_w2_w3_acc_x_diff_ms2",
	"link_w3_w4_seen", "link_w3_w4_unseen_cycles", "link_w3_w4_deviation_m", "link_w3_w4_acc_x_diff_ms2",
	"hop2_c_w2_seen",  "hop2_c_w2_unseen_cycles",  "hop2_c_w2_deviation_m",
	"hop2_w1_w3_seen", "hop2_w1_w3_unseen_cycles", "hop2_w1_w3_deviation_m",
	"hop2_w2_w4_seen", "hop2_w2_w4_unseen_cycles", "hop2_w2_w4_deviation_m"
};

const char* featureName(int i){
	if(i < 0 || i >= NUM_FEATURES) return "?";
	return FEATURE_NAMES[i];
}


void computeFeatures(const RawRow &row, LinkReferenceTable &references, float features[NUM_FEATURES]){

	int f = 0;

	// Links
	for(int i = 0; i < RAW_LINKS; i++){

		bool positions_exist = row.position_used[i] && row.position_used[i+1];
		LinkReference* ref = positions_exist ? findLinkReference(references, row.ids[i], row.ids[i+1]) : nullptr;
		bool seen = positions_exist && row.link_seen[i];

		// seen
		features[f++] = seen ? 1.0f : 0.0f;

		// unseen_cycles (a position without device counts as never seen: the cap)
		features[f++] = (ref != nullptr) ? (float)ref->unseen_cycles : (float)UNSEEN_CYCLES_CAP;

		// deviation_m (0 if not seen, or if the reference isn't usable yet)
		if(seen && ref != nullptr && linkReferenceReady(*ref)) features[f++] = row.link_distance[i] - ref->reference;
		else features[f++] = 0.0f;

		// acc_x_diff_ms2 = rear - front (0 if the link isn't seen or one IMU is missing)
		if(seen && row.imu_received[i] && row.imu_received[i+1]) features[f++] = row.acc[i+1][0] - row.acc[i][0];
		else features[f++] = 0.0f;
	}

	// Two-hop pairs (positions i and i+2): same rules as the links, without acceleration
	for(int i = 0; i < RAW_PAIRS; i++){

		bool positions_exist = row.position_used[i] && row.position_used[i+2];
		LinkReference* ref = positions_exist ? findLinkReference(references, row.ids[i], row.ids[i+2]) : nullptr;
		bool seen = positions_exist && row.pair_seen[i];

		features[f++] = seen ? 1.0f : 0.0f;
		features[f++] = (ref != nullptr) ? (float)ref->unseen_cycles : (float)UNSEEN_CYCLES_CAP;
		if(seen && ref != nullptr && linkReferenceReady(*ref)) features[f++] = row.pair_distance[i] - ref->reference;
		else features[f++] = 0.0f;
	}
}
