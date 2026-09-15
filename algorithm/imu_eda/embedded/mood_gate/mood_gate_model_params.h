#pragma once

/* Exported only from E:\\WESAD\\1 clean, frozen models on 2026-09-14.
 * IMU: PAMAP2 S101--S106 source-only model, high-motion probability.
 * EDA: WESAD S2--S9 source-only model, stress/arousal probability.
 * These are two independent datasets, so these values do NOT constitute an
 * end-to-end paired IMU+EDA performance claim. */

#define MOOD_IMU_FEATURES 11
#define MOOD_EDA_FEATURES 7

#define MOOD_IMU_THRESHOLD 0.22f /* p(high-motion) >= this: veto EDA */
#define MOOD_EDA_THRESHOLD 0.68f /* p(EDA-positive) >= this: confirm */

extern const float g_mood_imu_mean[MOOD_IMU_FEATURES];
extern const float g_mood_imu_scale[MOOD_IMU_FEATURES];
extern const float g_mood_imu_weight[MOOD_IMU_FEATURES];
extern const float g_mood_imu_bias;

extern const float g_mood_eda_mean[MOOD_EDA_FEATURES];
extern const float g_mood_eda_scale[MOOD_EDA_FEATURES];
extern const float g_mood_eda_weight[MOOD_EDA_FEATURES];
extern const float g_mood_eda_bias;
