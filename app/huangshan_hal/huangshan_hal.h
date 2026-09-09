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
#define HS_PWM_DEVICE            "/dev/pwm0"
#define HS_LCD_DEVICE            "/dev/fb0"
#define HS_BLE_DEVICE            "/dev/ttyHCI0"

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

struct hs_buttons_s
{
  int fd;
  uint32_t supported;
};

int hs_buttons_open(struct hs_buttons_s *buttons, const char *devpath);
void hs_buttons_close(struct hs_buttons_s *buttons);
int hs_buttons_read(struct hs_buttons_s *buttons, uint32_t *state);

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
