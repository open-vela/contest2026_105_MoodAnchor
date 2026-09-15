#pragma once

#include <stdbool.h>

#define MOOD_BVP_FEATURES 7
#define MOOD_EDA_FEATURES 8

/* Stored in Flash after the first two-minute quiet calibration. */
typedef struct {
    float bvp_center[MOOD_BVP_FEATURES];
    float bvp_scale[MOOD_BVP_FEATURES];
    float eda_center[MOOD_EDA_FEATURES];
    float eda_scale[MOOD_EDA_FEATURES];
    float eda_weight;   /* Personal mode default: 0.70 */
    float threshold;    /* Personal decision threshold. */
    unsigned int version;
    unsigned int crc32;
} mood_profile_t;

/* Input raw window features; internally applies personal and global scaling. */
float mood_bvp_probability(const mood_profile_t *profile, const float raw[MOOD_BVP_FEATURES]);
float mood_eda_probability(const mood_profile_t *profile, const float raw[MOOD_EDA_FEATURES]);
float mood_fused_score(const mood_profile_t *profile, const float raw_bvp[MOOD_BVP_FEATURES], const float raw_eda[MOOD_EDA_FEATURES]);
bool mood_confirm(const mood_profile_t *profile, const float raw_bvp[MOOD_BVP_FEATURES], const float raw_eda[MOOD_EDA_FEATURES]);

