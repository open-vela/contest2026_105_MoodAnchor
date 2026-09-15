#pragma once

#include <stdbool.h>

#define MOOD_BVP_WINDOW_SAMPLES 320  /* 5 seconds x 64 Hz */
#define MOOD_EDA_WINDOW_SAMPLES 20   /* 5 seconds x 4 Hz after downsampling */

bool mood_bvp_features(const float samples[MOOD_BVP_WINDOW_SAMPLES], float out[7]);
void mood_eda_features(const float samples[MOOD_EDA_WINDOW_SAMPLES], float out[8]);

