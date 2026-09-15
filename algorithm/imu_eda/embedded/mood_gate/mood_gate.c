#include "mood_gate.h"

#include <math.h>
#include <string.h>

static float mood_mean(const float *x, unsigned int n)
{
  float s = 0.0f;
  unsigned int i;
  for (i = 0; i < n; i++) s += x[i];
  return s / (float)n;
}

static float mood_std(const float *x, unsigned int n, float mean)
{
  float s = 0.0f;
  unsigned int i;
  for (i = 0; i < n; i++) { float d = x[i] - mean; s += d * d; }
  return sqrtf(s / (float)n);
}

static float mood_magnitude(float x, float y, float z)
{
  return sqrtf(x * x + y * y + z * z);
}

static float mood_sigmoid(float x)
{
  if (x > 30.0f) return 1.0f;
  if (x < -30.0f) return 0.0f;
  return 1.0f / (1.0f + expf(-x));
}

static float mood_probability(const float *raw, const float *center,
                              const float *personal_scale,
                              const float *global_mean,
                              const float *global_scale,
                              const float *weight, unsigned int n, float bias)
{
  float score = bias;
  unsigned int i;
  for (i = 0; i < n; i++)
    {
      float local = (raw[i] - center[i]) / (personal_scale[i] + 1e-6f);
      score += ((local - global_mean[i]) / (global_scale[i] + 1e-6f)) * weight[i];
    }
  return mood_sigmoid(score);
}

static void mood_sort(float *values, unsigned int n)
{
  unsigned int i;
  for (i = 1; i < n; i++)
    {
      float value = values[i];
      unsigned int j = i;
      while (j > 0 && values[j - 1] > value) { values[j] = values[j - 1]; j--; }
      values[j] = value;
    }
}

static void mood_finish_calibration(float *cal, unsigned int rows,
                                    unsigned int columns, float *center,
                                    float *scale)
{
  float values[MOOD_CALIBRATION_WINDOWS];
  unsigned int i, j;
  for (j = 0; j < columns; j++)
    {
      for (i = 0; i < rows; i++) values[i] = cal[i * columns + j];
      mood_sort(values, rows);
      center[j] = values[rows / 2];
      for (i = 0; i < rows; i++) values[i] = fabsf(cal[i * columns + j] - center[j]);
      mood_sort(values, rows);
      scale[j] = values[rows / 2] * 1.4826f;
      if (scale[j] < 1e-6f) scale[j] = 1.0f;
    }
}

static void mood_imu_features(const mood_gate_t *gate, float out[MOOD_IMU_FEATURES])
{
  float acc[MOOD_IMU_WINDOW_SAMPLES];
  float gyro[MOOD_IMU_WINDOW_SAMPLES];
  float difference_sum = 0.0f;
  unsigned int i;
  for (i = 0; i < MOOD_IMU_WINDOW_SAMPLES; i++)
    {
      acc[i] = mood_magnitude(gate->ax[i], gate->ay[i], gate->az[i]);
      gyro[i] = mood_magnitude(gate->gx[i], gate->gy[i], gate->gz[i]);
      if (i > 0) difference_sum += fabsf(acc[i] - acc[i - 1]);
    }
  {
    float am = mood_mean(acc, MOOD_IMU_WINDOW_SAMPLES);
    float gm = mood_mean(gyro, MOOD_IMU_WINDOW_SAMPLES);
    float acc_min = acc[0], acc_max = acc[0], gyro_max = gyro[0];
    float acc_energy = 0.0f, gyro_energy = 0.0f;
    for (i = 0; i < MOOD_IMU_WINDOW_SAMPLES; i++)
      {
        if (acc[i] < acc_min) acc_min = acc[i];
        if (acc[i] > acc_max) acc_max = acc[i];
        if (gyro[i] > gyro_max) gyro_max = gyro[i];
        acc_energy += acc[i] * acc[i];
        gyro_energy += gyro[i] * gyro[i];
      }
    out[0] = am;
    out[1] = mood_std(acc, MOOD_IMU_WINDOW_SAMPLES, am);
    out[2] = acc_max;
    out[3] = acc_max - acc_min;
    out[4] = sqrtf(acc_energy / MOOD_IMU_WINDOW_SAMPLES);
    out[5] = acc_energy / MOOD_IMU_WINDOW_SAMPLES;
    out[6] = difference_sum / (MOOD_IMU_WINDOW_SAMPLES - 1u);
    out[7] = gm;
    out[8] = mood_std(gyro, MOOD_IMU_WINDOW_SAMPLES, gm);
    out[9] = gyro_max;
    out[10] = gyro_energy / MOOD_IMU_WINDOW_SAMPLES;
  }
}

static void mood_eda_features(const mood_gate_t *gate, float out[MOOD_EDA_FEATURES])
{
  float diffs[MOOD_EDA_WINDOW_SAMPLES - 1u];
  float range_min = gate->eda[0], range_max = gate->eda[0];
  float energy = 0.0f, abs_diff = 0.0f;
  unsigned int i;
  for (i = 0; i < MOOD_EDA_WINDOW_SAMPLES; i++)
    {
      energy += gate->eda[i] * gate->eda[i];
      if (gate->eda[i] < range_min) range_min = gate->eda[i];
      if (gate->eda[i] > range_max) range_max = gate->eda[i];
      if (i > 0) { diffs[i - 1] = gate->eda[i] - gate->eda[i - 1]; abs_diff += fabsf(diffs[i - 1]); }
    }
  {
    float mean = mood_mean(gate->eda, MOOD_EDA_WINDOW_SAMPLES);
    float diff_mean = mood_mean(diffs, MOOD_EDA_WINDOW_SAMPLES - 1u);
    out[0] = mean;
    out[1] = mood_std(gate->eda, MOOD_EDA_WINDOW_SAMPLES, mean);
    out[2] = range_max - range_min;
    out[3] = (gate->eda[MOOD_EDA_WINDOW_SAMPLES - 1u] - gate->eda[0]) / 5.0f;
    out[4] = sqrtf(energy / MOOD_EDA_WINDOW_SAMPLES);
    out[5] = abs_diff / (MOOD_EDA_WINDOW_SAMPLES - 1u);
    out[6] = mood_std(diffs, MOOD_EDA_WINDOW_SAMPLES - 1u, diff_mean);
  }
}

void mood_gate_init(mood_gate_t *gate, mood_gate_event_cb_t callback, void *arg)
{
  memset(gate, 0, sizeof(*gate));
  gate->cooldown_ms = 30000u;
  gate->event_cb = callback;
  gate->event_arg = arg;
}

bool mood_gate_calibrated(const mood_gate_t *gate)
{
  return gate->imu_calibrated && gate->eda_calibrated;
}

void mood_gate_push_imu(mood_gate_t *gate, float ax, float ay, float az,
                        float gx, float gy, float gz, uint32_t timestamp_ms)
{
  float features[MOOD_IMU_FEATURES];
  unsigned int index;
  if (gate->imu_count < MOOD_IMU_WINDOW_SAMPLES)
    {
      index = gate->imu_count++;
    }
  else
    {
      /* Small fixed window: keeping it ordered avoids a second ring buffer. */
      memmove(gate->ax, gate->ax + 1, sizeof(gate->ax) - sizeof(float));
      memmove(gate->ay, gate->ay + 1, sizeof(gate->ay) - sizeof(float));
      memmove(gate->az, gate->az + 1, sizeof(gate->az) - sizeof(float));
      memmove(gate->gx, gate->gx + 1, sizeof(gate->gx) - sizeof(float));
      memmove(gate->gy, gate->gy + 1, sizeof(gate->gy) - sizeof(float));
      memmove(gate->gz, gate->gz + 1, sizeof(gate->gz) - sizeof(float));
      index = MOOD_IMU_WINDOW_SAMPLES - 1u;
    }
  gate->ax[index] = ax; gate->ay[index] = ay; gate->az[index] = az;
  gate->gx[index] = gx; gate->gy[index] = gy; gate->gz[index] = gz;
  if (gate->imu_count < MOOD_IMU_WINDOW_SAMPLES) return;
  if (++gate->imu_since_window < MOOD_IMU_FS) return;
  gate->imu_since_window = 0;
  mood_imu_features(gate, features);
  if (!gate->imu_calibrated)
    {
      if (gate->imu_cal_count < MOOD_CALIBRATION_WINDOWS)
        memcpy(gate->imu_cal[gate->imu_cal_count++], features, sizeof(features));
      if (gate->imu_cal_count == MOOD_CALIBRATION_WINDOWS)
        { mood_finish_calibration(&gate->imu_cal[0][0], MOOD_CALIBRATION_WINDOWS, MOOD_IMU_FEATURES, gate->imu_center, gate->imu_personal_scale); gate->imu_calibrated = true; }
      return;
    }
  gate->last_imu_probability = mood_probability(features, gate->imu_center, gate->imu_personal_scale,
      g_mood_imu_mean, g_mood_imu_scale, g_mood_imu_weight, MOOD_IMU_FEATURES, g_mood_imu_bias);
  gate->imu_pass = gate->last_imu_probability < MOOD_IMU_THRESHOLD;
  (void)timestamp_ms;
}

void mood_gate_push_eda(mood_gate_t *gate, float eda_us, uint32_t timestamp_ms)
{
  float features[MOOD_EDA_FEATURES];
  unsigned int index;
  if (gate->eda_count < MOOD_EDA_WINDOW_SAMPLES)
    {
      index = gate->eda_count++;
    }
  else
    {
      memmove(gate->eda, gate->eda + 1, sizeof(gate->eda) - sizeof(float));
      index = MOOD_EDA_WINDOW_SAMPLES - 1u;
    }
  gate->eda[index] = eda_us;
  if (gate->eda_count < MOOD_EDA_WINDOW_SAMPLES) return;
  if (++gate->eda_since_window < MOOD_EDA_FS) return;
  gate->eda_since_window = 0;
  mood_eda_features(gate, features);
  if (!gate->eda_calibrated)
    {
      if (gate->eda_cal_count < MOOD_CALIBRATION_WINDOWS)
        memcpy(gate->eda_cal[gate->eda_cal_count++], features, sizeof(features));
      if (gate->eda_cal_count == MOOD_CALIBRATION_WINDOWS)
        { mood_finish_calibration(&gate->eda_cal[0][0], MOOD_CALIBRATION_WINDOWS, MOOD_EDA_FEATURES, gate->eda_center, gate->eda_personal_scale); gate->eda_calibrated = true; }
      return;
    }
  if (!gate->imu_calibrated || !gate->imu_pass) return;
  gate->last_eda_probability = mood_probability(features, gate->eda_center, gate->eda_personal_scale,
      g_mood_eda_mean, g_mood_eda_scale, g_mood_eda_weight, MOOD_EDA_FEATURES, g_mood_eda_bias);
  if (gate->last_eda_probability >= MOOD_EDA_THRESHOLD && timestamp_ms >= gate->cooldown_until_ms)
    {
      gate->cooldown_until_ms = timestamp_ms + gate->cooldown_ms;
      if (gate->event_cb) gate->event_cb(gate->last_imu_probability, gate->last_eda_probability, timestamp_ms, gate->event_arg);
    }
}
