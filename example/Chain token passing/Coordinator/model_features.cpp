/*MODEL FEATURES*/
// See model_features.h. Every rule here mirrors calculo_columnas.py.

#include "model_features.h"

static const char* FEATURE_NAMES[NUM_FEATURES] = {
	"duracion_ciclo_ms",
	"enlace_c_v1_visto",  "enlace_c_v1_ciclos_sin_ver",  "enlace_c_v1_desviacion_m",  "enlace_c_v1_dif_acel_x_ms2",
	"enlace_v1_v2_visto", "enlace_v1_v2_ciclos_sin_ver", "enlace_v1_v2_desviacion_m", "enlace_v1_v2_dif_acel_x_ms2",
	"enlace_v2_v3_visto", "enlace_v2_v3_ciclos_sin_ver", "enlace_v2_v3_desviacion_m", "enlace_v2_v3_dif_acel_x_ms2",
	"enlace_v3_v4_visto", "enlace_v3_v4_ciclos_sin_ver", "enlace_v3_v4_desviacion_m", "enlace_v3_v4_dif_acel_x_ms2",
	"v1_dev_media_acel_y_ms2", "v1_dev_media_acel_z_ms2", "v1_dev_media_giro_x_rads", "v1_dev_media_giro_y_rads", "v1_dev_media_giro_z_rads",
	"v2_dev_media_acel_y_ms2", "v2_dev_media_acel_z_ms2", "v2_dev_media_giro_x_rads", "v2_dev_media_giro_y_rads", "v2_dev_media_giro_z_rads",
	"v3_dev_media_acel_y_ms2", "v3_dev_media_acel_z_ms2", "v3_dev_media_giro_x_rads", "v3_dev_media_giro_y_rads", "v3_dev_media_giro_z_rads",
	"v4_dev_media_acel_y_ms2", "v4_dev_media_acel_z_ms2", "v4_dev_media_giro_x_rads", "v4_dev_media_giro_y_rads", "v4_dev_media_giro_z_rads"
};

const char* featureName(int i){
	if(i < 0 || i >= NUM_FEATURES) return "?";
	return FEATURE_NAMES[i];
}


// Value of one of the 5 "relative" axes (acel_y, acel_z, giro_x, giro_y, giro_z) of vehicle v
static float relativeAxis(const RawRow &row, int v, int axis){
	switch(axis){
		case 0: return row.acc[v][1];
		case 1: return row.acc[v][2];
		case 2: return row.gyro[v][0];
		case 3: return row.gyro[v][1];
		default: return row.gyro[v][2];
	}
}


void computeFeatures(const RawRow &row, LinkReferenceTable &references, float features[NUM_FEATURES]){

	int f = 0;
	features[f++] = (float)row.duration_ms;

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

	// Wagons: deviation from the mean of the OTHER received vehicles
	for(int v = 1; v < RAW_VEHICLES; v++){
		for(int axis = 0; axis < 5; axis++){

			float sum_others = 0.0f;
			int n_others = 0;
			for(int o = 0; o < RAW_VEHICLES; o++){
				if(o == v || !row.imu_received[o]) continue;
				sum_others += relativeAxis(row, o, axis);
				n_others++;
			}

			if(row.imu_received[v] && n_others > 0) features[f++] = relativeAxis(row, v, axis) - sum_others / n_others;
			else features[f++] = 0.0f;
		}
	}
}
