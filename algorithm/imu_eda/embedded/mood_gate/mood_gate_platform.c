/* Board adapter. This file deliberately contains no guessed register values.
 * Implement the two functions below for the exact external IMU and EDA front
 * end fitted to your Huangshan Pi. The generic OpenVela board exposes ADC and
 * I2C, but it does not ship a standard /dev/imu0 node. */
#include <errno.h>
#include <stdint.h>

int mood_gate_platform_read_imu(float *ax, float *ay, float *az,
                                float *gx, float *gy, float *gz)
{
  (void)ax; (void)ay; (void)az; (void)gx; (void)gy; (void)gz;
  return -ENOSYS;
}

/* Return calibrated skin conductance in microSiemens, not raw ADC counts. */
int mood_gate_platform_read_eda(float *eda_us)
{
  (void)eda_us;
  return -ENOSYS;
}

void mood_gate_platform_on_confirm(float imu_probability, float eda_probability,
                                   uint32_t timestamp_ms)
{
  (void)imu_probability; (void)eda_probability; (void)timestamp_ms;
  /* Add vibrator/BLE notification here after BLE GATT service is enabled. */
}
