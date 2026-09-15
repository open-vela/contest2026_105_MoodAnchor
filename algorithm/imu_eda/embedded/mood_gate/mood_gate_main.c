#include <nuttx/config.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#include "mood_gate.h"

int mood_gate_platform_read_imu(float *ax, float *ay, float *az,
                                float *gx, float *gy, float *gz);
int mood_gate_platform_read_eda(float *eda_us);
void mood_gate_platform_on_confirm(float imu_probability, float eda_probability,
                                   uint32_t timestamp_ms);

static uint32_t now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static void on_confirm(float imu_p, float eda_p, uint32_t timestamp_ms, void *arg)
{
  (void)arg;
  printf("MOOD_EVENT imu_p=%.3f eda_p=%.3f t=%lu\n", imu_p, eda_p,
         (unsigned long)timestamp_ms);
  mood_gate_platform_on_confirm(imu_p, eda_p, timestamp_ms);
}

int mood_gate_main(int argc, char *argv[])
{
  mood_gate_t gate;
  unsigned int report_tick = 0;
  (void)argc; (void)argv;
  mood_gate_init(&gate, on_confirm, NULL);
  printf("MoodAnchor serial gate: 120 s quiet calibration, then IMU->EDA.\n");

  for (;;)
    {
      float ax, ay, az, gx, gy, gz, eda;
      uint32_t timestamp_ms = now_ms();
      if (mood_gate_platform_read_imu(&ax, &ay, &az, &gx, &gy, &gz) == 0)
        mood_gate_push_imu(&gate, ax, ay, az, gx, gy, gz, timestamp_ms);
      if (mood_gate_platform_read_eda(&eda) == 0)
        mood_gate_push_eda(&gate, eda, timestamp_ms);
      if (++report_tick >= 100u)
        {
          report_tick = 0;
          printf("cal=%d imu_pass=%d imu_p=%.3f eda_p=%.3f\n",
                 mood_gate_calibrated(&gate), gate.imu_pass,
                 gate.last_imu_probability, gate.last_eda_probability);
        }
      usleep(10000); /* poll at 100 Hz; EDA adapter should decimate to 4 Hz */
    }
  return 0;
}
