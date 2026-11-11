/*MODEL FEATURES (See model_features.h for understanding each function.)*/

#include "model_features.h"

static const char* FEATURE_NAMES[NUM_FEATURES] = {
	"enlace_c_v1_visto",  "enlace_c_v1_ciclos_sin_ver",  "enlace_c_v1_desviacion_m",  "enlace_c_v1_dif_acel_x_ms2",
	"enlace_v1_v2_visto", "enlace_v1_v2_ciclos_sin_ver", "enlace_v1_v2_desviacion_m", "enlace_v1_v2_dif_acel_x_ms2",
	"enlace_v2_v3_visto", "enlace_v2_v3_ciclos_sin_ver", "enlace_v2_v3_desviacion_m", "enlace_v2_v3_dif_acel_x_ms2",
	"enlace_v3_v4_visto", "enlace_v3_v4_ciclos_sin_ver", "enlace_v3_v4_desviacion_m", "enlace_v3_v4_dif_acel_x_ms2",
	"salto2_c_v2_visto",  "salto2_c_v2_ciclos_sin_ver",  "salto2_c_v2_desviacion_m",
	"salto2_v1_v3_visto", "salto2_v1_v3_ciclos_sin_ver", "salto2_v1_v3_desviacion_m",
	"salto2_v2_v4_visto", "salto2_v2_v4_ciclos_sin_ver", "salto2_v2_v4_desviacion_m"
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

		// visto
		features[f++] = seen ? 1.0f : 0.0f;

		// ciclos_sin_ver (a position without device counts as never seen: the cap)
		features[f++] = (ref != nullptr) ? (float)ref->unseen_cycles : (float)UNSEEN_CYCLES_CAP;

		// desviacion_m (0 if not seen, or if the reference isn't usable yet)
		if(seen && ref != nullptr && linkReferenceReady(*ref)) features[f++] = row.link_distance[i] - ref->reference;
		else features[f++] = 0.0f;

		// dif_acel_x_ms2 = rear - front (0 if the link isn't seen or one IMU is missing)
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
