#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mood_gate_model_params.h"

#define MOOD_IMU_FS 100u
#define MOOD_IMU_WINDOW_SAMPLES 200u /* 2 seconds */
#define MOOD_EDA_FS 4u
#define MOOD_EDA_WINDOW_SAMPLES 20u /* 5 seconds */
#define MOOD_CALIBRATION_WINDOWS 120u /* one feature vector per second */

typedef void (*mood_gate_event_cb_t)(float imu_high_motion_probability,
                                     float eda_positive_probability,
                                     uint32_t timestamp_ms, void *arg);

typedef struct
{
  float ax[MOOD_IMU_WINDOW_SAMPLES];
  float ay[MOOD_IMU_WINDOW_SAMPLES];
  float az[MOOD_IMU_WINDOW_SAMPLES];
  float gx[MOOD_IMU_WINDOW_SAMPLES];
  float gy[MOOD_IMU_WINDOW_SAMPLES];
  float gz[MOOD_IMU_WINDOW_SAMPLES];
  float eda[MOOD_EDA_WINDOW_SAMPLES];
  uint16_t imu_count;
  uint16_t imu_since_window;
  uint16_t eda_count;
  uint16_t eda_since_window;

  float imu_cal[MOOD_CALIBRATION_WINDOWS][MOOD_IMU_FEATURES];
  float eda_cal[MOOD_CALIBRATION_WINDOWS][MOOD_EDA_FEATURES];
  float imu_center[MOOD_IMU_FEATURES];
  float imu_personal_scale[MOOD_IMU_FEATURES];
  float eda_center[MOOD_EDA_FEATURES];
  float eda_personal_scale[MOOD_EDA_FEATURES];
  uint16_t imu_cal_count;
  uint16_t eda_cal_count;
  bool imu_calibrated;
  bool eda_calibrated;
  bool imu_pass;
  float last_imu_probability;
  float last_eda_probability;
  uint32_t cooldown_until_ms;
  uint32_t cooldown_ms;
  mood_gate_event_cb_t event_cb;
  void *event_arg;
} mood_gate_t;

void mood_gate_init(mood_gate_t *gate, mood_gate_event_cb_t event_cb, void *arg);
bool mood_gate_calibrated(const mood_gate_t *gate);
void mood_gate_push_imu(mood_gate_t *gate, float ax, float ay, float az,
                        float gx, float gy, float gz, uint32_t timestamp_ms);
void mood_gate_push_eda(mood_gate_t *gate, float eda_us, uint32_t timestamp_ms);
