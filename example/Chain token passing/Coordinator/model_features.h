/*MODEL FEATURES*/

// C version of extras/AI Integration/scripts/calculo_columnas.py.
// Turns this cycle's raw row (the same data printed as RAW_DATA) into the 37 inputs of the model,
// in exactly the same order as the training CSVs.
//
//   duracion_ciclo_ms
//   For each link (c_v1, v1_v2, v2_v3, v3_v4):
//       visto, ciclos_sin_ver, desviacion_m, dif_acel_x_ms2
//   For each wagon (v1..v4), in acel_y, acel_z, giro_x, giro_y, giro_z:
//       dev_media = value - mean of the OTHER received vehicles (coordinator included)
//
// Whatever wasn't received is 0 (its absence is already given by visto / ciclos_sin_ver).
// Reference distances and unseen counters come from link_references (they are per pair of devices).
//
// This module doesn't use the radio. It can be tested on a PC.

#pragma once

#include "link_references.h"

#define RAW_VEHICLES 5			// Fixed positions: coordinator + 4 nodes (c, v1, v2, v3, v4)
#define RAW_LINKS (RAW_VEHICLES - 1)
#define NUM_FEATURES 37


// Everything observed in one cycle, mapped to the fixed positions of the composition.
// It is what gets printed as RAW_DATA and what the features are computed from.
struct RawRow {
	uint32_t cycle;
	unsigned long time_ms;
	unsigned long duration_ms;

	bool    position_used[RAW_VEHICLES];	// false if the composition has fewer vehicles than positions
	uint8_t ids[RAW_VEHICLES];				// Device in each position

	bool  link_seen[RAW_LINKS];				// Link i joins positions i and i+1
	float link_distance[RAW_LINKS];			// [m]
	float link_rx_power[RAW_LINKS];			// [dBm]

	bool  imu_received[RAW_VEHICLES];
	float acc[RAW_VEHICLES][3];				// [m/s^2]  x, y, z
	float gyro[RAW_VEHICLES][3];			// [rad/s]  x, y, z
};


// Computes the 37 features. 'references' must already be updated with this cycle (updateLinkReference).
void computeFeatures(const RawRow &row, LinkReferenceTable &references, float features[NUM_FEATURES]);

// Name of feature i (same names as the training CSV columns)
const char* featureName(int i);
