/****************************************************************************
 * app/huangshan_hal/huangshan_hal.h
 *
 * Application-facing hardware interface for the LCKFB Huangshan Pi
 * (SiFli SF32LB52).  The implementation uses the standard NuttX device
 * interfaces exported by the official vendor_sifli BSP.
 ****************************************************************************/

#ifndef __APPS_HUANGSHAN_HAL_H
#define __APPS_HUANGSHAN_HAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define HS_I2C_DEFAULT_FREQUENCY 400000u
#define HS_ADC_DEVICE            "/dev/adc0"
#define HS_ADC_GSR_DEVICE        "/dev/adc1"
#define HS_ADC_VBAT_CHANNEL      5
#define HS_ADC_GSR_CHANNEL       0
#define HS_BUTTONS_DEVICE        "/dev/buttons"
#define HS_VIBRATION_DEVICE      "/dev/gpio3"
#define HS_PWM_DEVICE            "/dev/pwm0"
#define HS_LCD_DEVICE            "/dev/fb0"
#define HS_BLE_DEVICE            "/dev/ttyHCI0"
#define HS_IMU_DEVICE            "/dev/lsm6dsl0"
/* NuttX audio capture endpoint used when the SF32LB52 AUDCODEC backend is
 * enabled.  The board's MEMS microphone is analog (MIC_BIAS/MIC_ADC_IN), so
 * it must be exposed by the audio driver as PCM; it is not a GPADC channel. */
#define HS_MIC_DEVICE            "/dev/audio/pcm0c"

struct hs_i2c_s
{
  int fd;
  uint32_t frequency;
};

int hs_i2c_open(struct hs_i2c_s *bus, unsigned int busno,
                uint32_t frequency);
void hs_i2c_close(struct hs_i2c_s *bus);
int hs_i2c_write(struct hs_i2c_s *bus, uint16_t address,
                 const void *data, size_t length);
int hs_i2c_read(struct hs_i2c_s *bus, uint16_t address,
                void *data, size_t length);
int hs_i2c_write_read(struct hs_i2c_s *bus, uint16_t address,
                      const void *wdata, size_t wlength,
                      void *rdata, size_t rlength);

struct hs_adc_s
{
  int fd;
};

int hs_adc_open(struct hs_adc_s *adc, const char *devpath);
void hs_adc_close(struct hs_adc_s *adc);
int hs_adc_read(struct hs_adc_s *adc, uint8_t channel, int32_t *value);

/* Grove GSR/皮电 interface.  This is a small, synchronous API intended for
 * application code; it does not create a sampling thread or claim the ADC
 * device globally.  A caller owns one hs_gsr_s instance and requests samples
 * from its own worker/task.  Values are engineering/debug estimates only,
 * not medical measurements or emotion diagnoses. */
#define HS_GSR_RAW10_MAX 1023u

struct hs_gsr_sample_s
{
  int32_t adc_mv;             /* ADC input voltage, millivolts */
  uint16_t raw10;             /* normalized 0..1023 reading */
};

struct hs_gsr_s
{
  struct hs_adc_s adc;
};

int hs_gsr_open(struct hs_gsr_s *gsr);
void hs_gsr_close(struct hs_gsr_s *gsr);
int hs_gsr_read(struct hs_gsr_s *gsr, struct hs_gsr_sample_s *sample);

/* LSM6DS3TR-C/LSM6DSL accelerometer and gyroscope interface.  The values
 * are already scaled by the NuttX driver: acceleration is mg, angular rate
 * is mdps, temperature is degrees Celsius, and timestamp is the sensor's
 * 24-bit sample counter (returned in the low 16 bits by the driver ABI). */
struct hs_imu_sample_s
{
  int16_t accel_x_mg;
  int16_t accel_y_mg;
  int16_t accel_z_mg;
  int16_t gyro_x_mdps;
  int16_t gyro_y_mdps;
  int16_t gyro_z_mdps;
  int16_t temperature_c;
  uint16_t timestamp;
};

struct hs_imu_s
{
  int fd;
  bool started;
};

int hs_imu_open(struct hs_imu_s *imu);
void hs_imu_close(struct hs_imu_s *imu);
int hs_imu_read(struct hs_imu_s *imu, struct hs_imu_sample_s *sample);

/* Board microphone PCM capture.  Samples are signed 16-bit mono PCM.  The
 * endpoint is intentionally kept as a standard file descriptor so upper
 * layers can consume samples without depending on SiFli HAL ABI details. */
struct hs_mic_s
{
  int fd;
  uint32_t sample_rate;
  uint8_t channels;
};

int hs_mic_open(struct hs_mic_s *mic, uint32_t sample_rate);
void hs_mic_close(struct hs_mic_s *mic);
ssize_t hs_mic_read(struct hs_mic_s *mic, int16_t *samples,
                    size_t sample_count);

struct hs_buttons_s
{
  int fd;
  uint32_t supported;
};

int hs_buttons_open(struct hs_buttons_s *buttons, const char *devpath);
void hs_buttons_close(struct hs_buttons_s *buttons);
int hs_buttons_read(struct hs_buttons_s *buttons, uint32_t *state);

/* PA42 vibration control output (exported as /dev/gpio3).  PA30 is reserved
 * for the LSM6DSL sensor LDO and must not be driven by the vibration path.
 * Drive a transistor/MOSFET input or a vibration-driver EN pin; do not
 * connect a bare motor directly to a GPIO. */
struct hs_vibration_s
{
  int fd;
  bool enabled;
};

int hs_vibration_open(struct hs_vibration_s *vibration);
void hs_vibration_close(struct hs_vibration_s *vibration);
int hs_vibration_set(struct hs_vibration_s *vibration, bool enabled);
bool hs_vibration_is_enabled(const struct hs_vibration_s *vibration);

struct hs_pwm_s
{
  int fd;
  uint32_t frequency;
  uint8_t duty_percent;
};

int hs_pwm_open(struct hs_pwm_s *pwm, const char *devpath);
void hs_pwm_close(struct hs_pwm_s *pwm);
int hs_pwm_set(struct hs_pwm_s *pwm, uint32_t frequency,
               uint8_t duty_percent);
int hs_pwm_stop(struct hs_pwm_s *pwm);

struct hs_lcd_s
{
  int fd;
  uint16_t xres;
  uint16_t yres;
  uint8_t bpp;
  uint32_t stride;
  size_t length;
  void *framebuffer;
  int power;
};

int hs_lcd_open(struct hs_lcd_s *lcd, const char *devpath);
void hs_lcd_close(struct hs_lcd_s *lcd);
int hs_lcd_fill(struct hs_lcd_s *lcd, uint32_t color);
int hs_lcd_pixel(struct hs_lcd_s *lcd, uint16_t x, uint16_t y,
                 uint32_t color);
int hs_lcd_flush(struct hs_lcd_s *lcd);

/* BLE is exposed as a standard H:4 HCI transport by the SiFli BSP.  These
 * helpers intentionally stop at HCI: GATT/GAP policy belongs to the selected
 * openvela Bluetooth host (Zblue/framework service) and is not duplicated in
 * this application layer. */

struct hs_ble_s
{
  int fd;
};

int hs_ble_open(struct hs_ble_s *ble, const char *devpath);
void hs_ble_close(struct hs_ble_s *ble);
int hs_ble_reset(struct hs_ble_s *ble);
int hs_ble_command(struct hs_ble_s *ble, uint16_t opcode,
                   const void *params, uint8_t params_length,
                   void *event, size_t event_size, size_t *event_length);
int hs_ble_set_advertising_data(struct hs_ble_s *ble,
                                const void *data, uint8_t length);
int hs_ble_set_advertising_parameters(struct hs_ble_s *ble,
                                      uint16_t interval_min,
                                      uint16_t interval_max);
int hs_ble_set_advertising(struct hs_ble_s *ble, bool enable);
int hs_ble_advertise_name(struct hs_ble_s *ble, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_HUANGSHAN_HAL_H */
