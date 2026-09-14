/****************************************************************************
 * app/huangshan_hal/huangshan_hal.c
 ****************************************************************************/

#include "huangshan_hal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/input/buttons.h>
#include <nuttx/ioexpander/gpio.h>
#include <nuttx/sensors/lsm6dsl.h>
#include <nuttx/timers/pwm.h>
#include <nuttx/video/fb.h>

static int hs_read_full(int fd, void *buffer, size_t length)
{
  uint8_t *ptr = (uint8_t *)buffer;
  size_t done = 0;

  while (done < length)
    {
      ssize_t nread = read(fd, ptr + done, length - done);
      if (nread < 0)
        {
          return -errno;
        }

      if (nread == 0)
        {
          return -EIO;
        }

      done += (size_t)nread;
    }

  return 0;
}

static int hs_write_full(int fd, const void *buffer, size_t length)
{
  const uint8_t *ptr = (const uint8_t *)buffer;
  size_t done = 0;

  while (done < length)
    {
      ssize_t nwritten = write(fd, ptr + done, length - done);
      if (nwritten < 0)
        {
          return -errno;
        }

      if (nwritten == 0)
        {
          return -EIO;
        }

      done += (size_t)nwritten;
    }

  return 0;
}

int hs_i2c_open(struct hs_i2c_s *bus, unsigned int busno,
                uint32_t frequency)
{
  char path[20];

  if (bus == NULL || frequency == 0)
    {
      return -EINVAL;
    }

  bus->fd = -1;
  snprintf(path, sizeof(path), "/dev/i2c%u", busno);
  bus->fd = open(path, O_RDWR);
  if (bus->fd < 0)
    {
      return -errno;
    }

  bus->frequency = frequency;
  return 0;
}

void hs_i2c_close(struct hs_i2c_s *bus)
{
  if (bus != NULL && bus->fd >= 0)
    {
      close(bus->fd);
      bus->fd = -1;
    }
}

static int hs_i2c_transfer(struct hs_i2c_s *bus, struct i2c_msg_s *messages,
                           size_t count)
{
  struct i2c_transfer_s transfer;

  if (bus == NULL || bus->fd < 0 || messages == NULL || count == 0)
    {
      return -EINVAL;
    }

  transfer.msgv = messages;
  transfer.msgc = count;
  if (ioctl(bus->fd, I2CIOC_TRANSFER, (unsigned long)(uintptr_t)&transfer)
      < 0)
    {
      return -errno;
    }

  return 0;
}

int hs_i2c_write(struct hs_i2c_s *bus, uint16_t address,
                 const void *data, size_t length)
{
  struct i2c_msg_s message;

  if (bus == NULL || data == NULL || length == 0 || address > 0x3ff)
    {
      return -EINVAL;
    }

  memset(&message, 0, sizeof(message));
  message.frequency = bus->frequency;
  message.addr = address;
  message.buffer = (uint8_t *)(uintptr_t)data;
  message.length = length;
  return hs_i2c_transfer(bus, &message, 1);
}

int hs_i2c_read(struct hs_i2c_s *bus, uint16_t address,
                void *data, size_t length)
{
  struct i2c_msg_s message;

  if (bus == NULL || data == NULL || length == 0 || address > 0x3ff)
    {
      return -EINVAL;
    }

  memset(&message, 0, sizeof(message));
  message.frequency = bus->frequency;
  message.addr = address;
  message.flags = I2C_M_READ;
  message.buffer = data;
  message.length = length;
  return hs_i2c_transfer(bus, &message, 1);
}

int hs_i2c_write_read(struct hs_i2c_s *bus, uint16_t address,
                      const void *wdata, size_t wlength,
                      void *rdata, size_t rlength)
{
  struct i2c_msg_s messages[2];

  if (bus == NULL || wdata == NULL || rdata == NULL || wlength == 0 || rlength == 0 ||
      address > 0x3ff)
    {
      return -EINVAL;
    }

  memset(messages, 0, sizeof(messages));
  messages[0].frequency = bus->frequency;
  messages[0].addr = address;
  messages[0].flags = I2C_M_NOSTOP;
  messages[0].buffer = (uint8_t *)(uintptr_t)wdata;
  messages[0].length = wlength;
  messages[1].frequency = bus->frequency;
  messages[1].addr = address;
  messages[1].flags = I2C_M_READ;
  messages[1].buffer = rdata;
  messages[1].length = rlength;
  return hs_i2c_transfer(bus, messages, 2);
}

static int hs_max30102_reg_write(struct hs_max30102_s *sensor,
                                 uint8_t reg, uint8_t value)
{
  uint8_t data[2] = { reg, value };
  return hs_i2c_write(&sensor->i2c, sensor->address, data, sizeof(data));
}

static int hs_max30102_reg_read(struct hs_max30102_s *sensor,
                                uint8_t reg, uint8_t *value)
{
  return hs_i2c_write_read(&sensor->i2c, sensor->address, &reg, 1,
                           value, 1);
}

int hs_max30102_open(struct hs_max30102_s *sensor, unsigned int busno)
{
  uint8_t part_id;
  int ret;

  if (sensor == NULL)
    {
      return -EINVAL;
    }

  memset(sensor, 0, sizeof(*sensor));
  sensor->i2c.fd = -1;
  sensor->address = HS_MAX30102_I2C_ADDRESS;
  ret = hs_i2c_open(&sensor->i2c, busno, HS_I2C_DEFAULT_FREQUENCY);
  if (ret < 0)
    {
      return ret;
    }

  ret = hs_max30102_reg_read(sensor, 0xff, &part_id);
  if (ret < 0)
    {
      hs_max30102_close(sensor);
      return ret;
    }

  sensor->part_id = part_id;

  /* A genuine MAX30102/MAX30105 reports 0x15 here, but many low-cost boards
   * carry MAX30102-compatible parts whose ID registers stay 0x00 (or return a
   * vendor value) while the standard register map still works.  Anything that
   * acknowledges on the bus is therefore accepted; the ID is kept so callers
   * can log it, and a mismatched revision register is tolerated as well. */
  (void)hs_max30102_reg_read(sensor, 0xfe, &sensor->rev_id);

  /* Reset, disable interrupts, clear FIFO pointers, then select SpO2 mode.
   *
   * Register 0x08 is FIFO_CONFIG (not FIFO_DATA).  The previous code wrote
   * 0x4f to 0x07 and programmed 0x09/0x0a with swapped meanings, which could
   * leave the part in multi-LED mode and produce only a startup sample.
   * Use no averaging, no rollover, 100 samples/s, 411 us pulse width and the
   * 4096 nA ADC range.  FIFO polling is intentional; INT may remain open.
   */
  ret = hs_max30102_reg_write(sensor, 0x0a, 0x40);
  if (ret >= 0)
    {
      /* RESET is self-clearing; allow the oscillator and FIFO state to
       * settle before programming the remaining registers. */
      usleep(10000);
    }
  if (ret >= 0) ret = hs_max30102_reg_write(sensor, 0x02, 0x00);
  if (ret >= 0) ret = hs_max30102_reg_write(sensor, 0x03, 0x00);
  if (ret >= 0) ret = hs_max30102_reg_write(sensor, 0x04, 0x00);
  if (ret >= 0) ret = hs_max30102_reg_write(sensor, 0x05, 0x00);
  if (ret >= 0) ret = hs_max30102_reg_write(sensor, 0x06, 0x00);

  /* Register 0x08 is FIFO_CONFIG: no sample averaging, FIFO rollover
   * enabled, almost-full threshold at 15.  Rollover matters because the
   * application drains the FIFO from a thread that can be delayed: without
   * it a full FIFO simply stops producing samples.
   */

  if (ret >= 0) ret = hs_max30102_reg_write(sensor, 0x08, 0x1f);
  if (ret >= 0) ret = hs_max30102_reg_write(sensor, 0x09, 0x03);
  if (ret >= 0) ret = hs_max30102_reg_write(sensor, 0x0a, 0x27);
  if (ret >= 0) ret = hs_max30102_reg_write(sensor, 0x0c, 0x24);
  if (ret >= 0) ret = hs_max30102_reg_write(sensor, 0x0d, 0x24);
  if (ret < 0)
    {
      hs_max30102_close(sensor);
      return ret;
    }

  sensor->initialized = true;
  (void)part_id;
  return 0;
}

void hs_max30102_close(struct hs_max30102_s *sensor)
{
  if (sensor != NULL)
    {
      if (sensor->initialized && sensor->i2c.fd >= 0)
        {
          (void)hs_max30102_reg_write(sensor, 0x0a, 0x40);
        }

      hs_i2c_close(&sensor->i2c);
      sensor->initialized = false;
    }
}

int hs_max30102_read_sample(struct hs_max30102_s *sensor,
                            struct hs_max30102_sample_s *sample)
{
  uint8_t write_ptr;
  uint8_t read_ptr;
  uint8_t fifo[6];
  struct timespec ts;
  int ret;

  if (sensor == NULL || sample == NULL || !sensor->initialized)
    {
      return -EINVAL;
    }

  ret = hs_max30102_reg_read(sensor, 0x04, &write_ptr);
  if (ret < 0)
    {
      return ret;
    }

  ret = hs_max30102_reg_read(sensor, 0x06, &read_ptr);
  if (ret < 0)
    {
      return ret;
    }

  if (write_ptr == read_ptr)
    {
      return -EAGAIN;
    }

  ret = hs_i2c_write_read(&sensor->i2c, sensor->address, (uint8_t[]){0x07},
                          1, fifo, sizeof(fifo));
  if (ret < 0)
    {
      return ret;
    }

  sample->red = ((uint32_t)fifo[0] << 16 | (uint32_t)fifo[1] << 8 |
                 fifo[2]) & 0x3ffff;
  sample->ir = ((uint32_t)fifo[3] << 16 | (uint32_t)fifo[4] << 8 |
                fifo[5]) & 0x3ffff;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  sample->timestamp_ms = (uint32_t)(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull);
  return 0;
}

int hs_adc_open(struct hs_adc_s *adc, const char *devpath)
{
  if (adc == NULL)
    {
      return -EINVAL;
    }

  adc->fd = -1;
  adc->fd = open(devpath != NULL ? devpath : HS_ADC_DEVICE, O_RDONLY);
  return adc->fd < 0 ? -errno : 0;
}

void hs_adc_close(struct hs_adc_s *adc)
{
  if (adc != NULL && adc->fd >= 0)
    {
      close(adc->fd);
      adc->fd = -1;
    }
}

int hs_adc_read(struct hs_adc_s *adc, uint8_t channel, int32_t *value)
{
  struct adc_msg_s samples[8];
  ssize_t nbytes;
  size_t count;
  size_t i;

  if (adc == NULL || adc->fd < 0 || value == NULL)
    {
      return -EINVAL;
    }

  /* SF32LB52 uses a software-triggered, single-channel ADC.  Clear any
   * stale sample, trigger VBAT conversion, then consume the resulting mV
   * value from the upper-half FIFO. */

  if (ioctl(adc->fd, ANIOC_RESET_FIFO, 0) < 0)
    {
      return -errno;
    }

  if (ioctl(adc->fd, ANIOC_TRIGGER, 0) < 0)
    {
      return -errno;
    }

  nbytes = read(adc->fd, samples, sizeof(samples));
  if (nbytes < 0)
    {
      return -errno;
    }

  if (nbytes == 0 || (nbytes % sizeof(struct adc_msg_s)) != 0)
    {
      return -EIO;
    }

  count = (size_t)nbytes / sizeof(struct adc_msg_s);
  for (i = 0; i < count; i++)
    {
      if (samples[i].am_channel == channel)
        {
          *value = samples[i].am_data;
          return 0;
        }
    }

  return -ENODATA;
}

static uint16_t hs_gsr_mv_to_raw10(int32_t adc_mv)
{
  if (adc_mv <= 0)
    {
      return 0;
    }

  if (adc_mv >= 3300)
    {
      return HS_GSR_RAW10_MAX;
    }

  return (uint16_t)(((int64_t)adc_mv * HS_GSR_RAW10_MAX + 1650) / 3300);
}

int hs_gsr_open(struct hs_gsr_s *gsr)
{
  int ret;

  if (gsr == NULL)
    {
      return -EINVAL;
    }

  memset(gsr, 0, sizeof(*gsr));
  gsr->adc.fd = -1;
  ret = hs_adc_open(&gsr->adc, HS_ADC_GSR_DEVICE);
  return ret;
}

void hs_gsr_close(struct hs_gsr_s *gsr)
{
  if (gsr != NULL)
    {
      hs_adc_close(&gsr->adc);
    }
}

int hs_gsr_read(struct hs_gsr_s *gsr, struct hs_gsr_sample_s *sample)
{
  int ret;
  if (gsr == NULL || sample == NULL || gsr->adc.fd < 0)
    {
      return -EINVAL;
    }

  memset(sample, 0, sizeof(*sample));
  ret = hs_adc_read(&gsr->adc, HS_ADC_GSR_CHANNEL, &sample->adc_mv);
  if (ret < 0)
    {
      return ret;
    }

  sample->raw10 = hs_gsr_mv_to_raw10(sample->adc_mv);
  return 0;
}

int hs_imu_open(struct hs_imu_s *imu)
{
  if (imu == NULL)
    {
      return -EINVAL;
    }

  imu->fd = -1;
  imu->started = false;
  imu->fd = open(HS_IMU_DEVICE, O_RDONLY);
  if (imu->fd < 0)
    {
      return -errno;
    }

  if (ioctl(imu->fd, SNIOC_START, 0) < 0)
    {
      int err = errno;
      close(imu->fd);
      imu->fd = -1;
      return -err;
    }

  imu->started = true;
  return 0;
}

void hs_imu_close(struct hs_imu_s *imu)
{
  if (imu != NULL)
    {
      if (imu->fd >= 0 && imu->started)
        {
          (void)ioctl(imu->fd, SNIOC_STOP, 0);
        }

      if (imu->fd >= 0)
        {
          close(imu->fd);
        }

      imu->fd = -1;
      imu->started = false;
    }
}

int hs_imu_read(struct hs_imu_s *imu, struct hs_imu_sample_s *sample)
{
  struct lsm6dsl_sensor_data_s data;

  if (imu == NULL || sample == NULL || imu->fd < 0 || !imu->started)
    {
      return -EINVAL;
    }

  memset(&data, 0, sizeof(data));
  if (ioctl(imu->fd, SNIOC_LSM6DSLSENSORREAD,
            (unsigned long)(uintptr_t)&data) < 0)
    {
      return -errno;
    }

  sample->accel_x_mg = data.x_data;
  sample->accel_y_mg = data.y_data;
  sample->accel_z_mg = data.z_data;
  sample->gyro_x_mdps = data.g_x_data;
  sample->gyro_y_mdps = data.g_y_data;
  sample->gyro_z_mdps = data.g_z_data;
  sample->temperature_c = (int16_t)data.temperature;
  sample->timestamp = data.timestamp;
  return 0;
}

int hs_mic_open(struct hs_mic_s *mic, uint32_t sample_rate)
{
  if (mic == NULL || sample_rate == 0)
    {
      return -EINVAL;
    }

  mic->fd = open(HS_MIC_DEVICE, O_RDONLY | O_NONBLOCK);
  if (mic->fd < 0)
    {
      mic->fd = -1;
      return -errno;
    }

  mic->sample_rate = sample_rate;
  mic->channels = 1;
  return 0;
}

void hs_mic_close(struct hs_mic_s *mic)
{
  if (mic != NULL && mic->fd >= 0)
    {
      close(mic->fd);
      mic->fd = -1;
    }
}

ssize_t hs_mic_read(struct hs_mic_s *mic, int16_t *samples,
                    size_t sample_count)
{
  ssize_t bytes;

  if (mic == NULL || samples == NULL || sample_count == 0 || mic->fd < 0)
    {
      errno = EINVAL;
      return -1;
    }

  bytes = read(mic->fd, samples, sample_count * sizeof(int16_t));
  if (bytes < 0)
    {
      return -1;
    }

  return bytes / (ssize_t)sizeof(int16_t);
}

int hs_buttons_open(struct hs_buttons_s *buttons, const char *devpath)
{
  btn_buttonset_t supported = 0;

  if (buttons == NULL)
    {
      return -EINVAL;
    }

  buttons->fd = -1;
  buttons->supported = 0;
  buttons->fd = open(devpath != NULL ? devpath : HS_BUTTONS_DEVICE,
                     O_RDONLY | O_NONBLOCK);
  if (buttons->fd < 0)
    {
      return -errno;
    }

  if (ioctl(buttons->fd, BTNIOC_SUPPORTED,
            (unsigned long)(uintptr_t)&supported) < 0)
    {
      int err = errno;
      close(buttons->fd);
      buttons->fd = -1;
      return -err;
    }

  buttons->supported = (uint32_t)supported;
  return 0;
}

void hs_buttons_close(struct hs_buttons_s *buttons)
{
  if (buttons != NULL && buttons->fd >= 0)
    {
      close(buttons->fd);
      buttons->fd = -1;
    }
}

int hs_buttons_read(struct hs_buttons_s *buttons, uint32_t *state)
{
  btn_buttonset_t sample;
  ssize_t nbytes;

  if (buttons == NULL || buttons->fd < 0 || state == NULL)
    {
      return -EINVAL;
    }

  nbytes = read(buttons->fd, &sample, sizeof(sample));
  if (nbytes < 0)
    {
      return -errno;
    }

  if (nbytes != sizeof(sample))
    {
      return -EIO;
    }

  *state = (uint32_t)sample;
  return 0;
}

int hs_vibration_open(struct hs_vibration_s *vibration)
{
  if (vibration == NULL)
    {
      return -EINVAL;
    }

  vibration->fd = open(HS_VIBRATION_DEVICE, O_RDWR);
  vibration->enabled = false;
  if (vibration->fd < 0)
    {
      vibration->fd = -1;
      return -errno;
    }

  /* Leave the output in the safe low state after opening. */
  if (ioctl(vibration->fd, GPIOC_WRITE, 0) < 0)
    {
      int err = errno;
      close(vibration->fd);
      vibration->fd = -1;
      return -err;
    }

  return 0;
}

void hs_vibration_close(struct hs_vibration_s *vibration)
{
  if (vibration != NULL)
    {
      if (vibration->fd >= 0)
        {
          (void)ioctl(vibration->fd, GPIOC_WRITE, 0);
          close(vibration->fd);
          vibration->fd = -1;
        }

      vibration->enabled = false;
    }
}

int hs_vibration_set(struct hs_vibration_s *vibration, bool enabled)
{
  if (vibration == NULL || vibration->fd < 0)
    {
      return -EINVAL;
    }

  if (ioctl(vibration->fd, GPIOC_WRITE, enabled ? 1 : 0) < 0)
    {
      return -errno;
    }

  vibration->enabled = enabled;
  return 0;
}

bool hs_vibration_is_enabled(const struct hs_vibration_s *vibration)
{
  return vibration != NULL && vibration->enabled;
}

int hs_pwm_open(struct hs_pwm_s *pwm, const char *devpath)
{
  if (pwm == NULL)
    {
      return -EINVAL;
    }

  pwm->fd = -1;
  pwm->fd = open(devpath != NULL ? devpath : HS_PWM_DEVICE, O_RDWR);
  return pwm->fd < 0 ? -errno : 0;
}

void hs_pwm_close(struct hs_pwm_s *pwm)
{
  if (pwm != NULL && pwm->fd >= 0)
    {
      (void)hs_pwm_stop(pwm);
      close(pwm->fd);
      pwm->fd = -1;
    }
}

int hs_pwm_set(struct hs_pwm_s *pwm, uint32_t frequency,
               uint8_t duty_percent)
{
  struct pwm_info_s info;

  if (pwm == NULL || pwm->fd < 0 || frequency == 0 || duty_percent > 100)
    {
      return -EINVAL;
    }

  memset(&info, 0, sizeof(info));
  info.frequency = frequency;
  /* Match the NuttX PWM example's fixed-point convention: 1%..100%
   * correspond to (percentage / 100) of a 16.16 unsigned fraction, while
   * zero is reserved for a fully disabled output. */
  info.duty = duty_percent == 0 ? 0 :
              b16divi(uitoub16(duty_percent) - 1, 100);
  info.cpol = PWM_CPOL_HIGH;
  info.dcpol = PWM_DCPOL_LOW;

  if (ioctl(pwm->fd, PWMIOC_SETCHARACTERISTICS,
            (unsigned long)(uintptr_t)&info) < 0)
    {
      return -errno;
    }

  if (ioctl(pwm->fd, PWMIOC_START, 0) < 0)
    {
      return -errno;
    }

  pwm->frequency = frequency;
  pwm->duty_percent = duty_percent;
  return 0;
}

int hs_pwm_stop(struct hs_pwm_s *pwm)
{
  if (pwm == NULL || pwm->fd < 0)
    {
      return -EINVAL;
    }

  if (ioctl(pwm->fd, PWMIOC_STOP, 0) < 0)
    {
      return -errno;
    }

  return 0;
}

int hs_lcd_open(struct hs_lcd_s *lcd, const char *devpath)
{
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
  const char *path;
  int retry;

  if (lcd == NULL)
    {
      return -EINVAL;
    }

  memset(lcd, 0, sizeof(*lcd));
  lcd->fd = -1;
  lcd->power = -1;
  path = devpath != NULL ? devpath : HS_LCD_DEVICE;

  /* The SiFli board registers /dev/fb0 from an asynchronous LCD worker.
   * Allow early shell commands to wait for that worker instead of failing
   * depending on boot timing. */

  for (retry = 0; retry < 30; retry++)
    {
      lcd->fd = open(path, O_RDWR);
      if (lcd->fd >= 0)
        {
          break;
        }

      usleep(100 * 1000);
    }

  if (lcd->fd < 0)
    {
      return -errno;
    }

  memset(&vinfo, 0, sizeof(vinfo));
  memset(&pinfo, 0, sizeof(pinfo));
  if (ioctl(lcd->fd, FBIOGET_VIDEOINFO,
            (unsigned long)(uintptr_t)&vinfo) < 0 ||
      ioctl(lcd->fd, FBIOGET_PLANEINFO,
            (unsigned long)(uintptr_t)&pinfo) < 0)
    {
      int err = errno;
      close(lcd->fd);
      lcd->fd = -1;
      return -err;
    }

  lcd->xres = vinfo.xres;
  lcd->yres = vinfo.yres;
  lcd->bpp = pinfo.bpp;
  lcd->stride = pinfo.stride;
  lcd->length = pinfo.fblen;
  /* NuttX framebuffer drivers expose the mapped memory through
   * FBIOGET_PLANEINFO.  Do not assume POSIX mmap support on the target. */
  lcd->framebuffer = pinfo.fbmem;
  if (lcd->framebuffer == NULL)
    {
      int err = errno;
      close(lcd->fd);
      lcd->fd = -1;
      lcd->framebuffer = NULL;
      return -err;
    }

  /* Make panel power explicit.  The framebuffer normally powers the LCD
   * during registration, but doing it here also recovers from an early
   * asynchronous init or a previous power-off operation. */

  if (ioctl(lcd->fd, FBIOSET_POWER, CONFIG_LCD_MAXPOWER) < 0)
    {
      int err = errno;
      close(lcd->fd);
      lcd->fd = -1;
      lcd->framebuffer = NULL;
      return -err;
    }

  if (ioctl(lcd->fd, FBIOGET_POWER,
            (unsigned long)(uintptr_t)&lcd->power) < 0)
    {
      lcd->power = -1;
    }

  usleep(20 * 1000);

  return 0;
}

void hs_lcd_close(struct hs_lcd_s *lcd)
{
  if (lcd == NULL)
    {
      return;
    }

  if (lcd->framebuffer != NULL)
    {
      lcd->framebuffer = NULL;
    }

  if (lcd->fd >= 0)
    {
      close(lcd->fd);
      lcd->fd = -1;
    }
}

static int hs_lcd_valid(const struct hs_lcd_s *lcd)
{
  return lcd != NULL && lcd->fd >= 0 && lcd->framebuffer != NULL &&
         lcd->bpp != 0;
}

int hs_lcd_pixel(struct hs_lcd_s *lcd, uint16_t x, uint16_t y,
                 uint32_t color)
{
  uint8_t *pixel;

  if (!hs_lcd_valid(lcd) || x >= lcd->xres || y >= lcd->yres)
    {
      return -EINVAL;
    }

  pixel = (uint8_t *)lcd->framebuffer + (size_t)y * lcd->stride +
          ((size_t)x * lcd->bpp) / 8;
  switch (lcd->bpp)
    {
      case 16:
        *(uint16_t *)pixel = (uint16_t)color;
        break;
      case 24:
        pixel[0] = color & 0xff;
        pixel[1] = (color >> 8) & 0xff;
        pixel[2] = (color >> 16) & 0xff;
        break;
      case 32:
        *(uint32_t *)pixel = color;
        break;
      default:
        return -ENOTSUP;
    }

  return 0;
}

int hs_lcd_fill(struct hs_lcd_s *lcd, uint32_t color)
{
  uint16_t y;

  if (!hs_lcd_valid(lcd))
    {
      return -EINVAL;
    }

  /* Fill the mapped framebuffer directly.  Calling hs_lcd_pixel() for every
   * pixel adds several bounds checks per pixel and made a full-screen clear
   * noticeably expensive on the 390x450 panel. */
  if (lcd->bpp == 16)
    {
      uint16_t *row = (uint16_t *)lcd->framebuffer;
      uint16_t value = (uint16_t)color;
      for (y = 0; y < lcd->yres; y++)
        {
          uint16_t x;
          for (x = 0; x < lcd->xres; x++)
            {
              row[x] = value;
            }

          row = (uint16_t *)((uint8_t *)row + lcd->stride);
        }
    }
  else
    {
      for (y = 0; y < lcd->yres; y++)
        {
          uint16_t x;
          for (x = 0; x < lcd->xres; x++)
            {
              (void)hs_lcd_pixel(lcd, x, y, color);
            }
        }
    }

  return hs_lcd_flush(lcd);
}

int hs_lcd_flush(struct hs_lcd_s *lcd)
{
#if defined(FBIO_UPDATE) && defined(CONFIG_FB_UPDATE)
  struct fb_area_s area;

  if (!hs_lcd_valid(lcd))
    {
      return -EINVAL;
    }

  area.x = 0;
  area.y = 0;
  area.w = lcd->xres;
  area.h = lcd->yres;
  if (ioctl(lcd->fd, FBIO_UPDATE,
            (unsigned long)(uintptr_t)&area) < 0)
    {
      return -errno;
    }
#else
  (void)lcd;
#endif

  return 0;
}

int hs_ble_open(struct hs_ble_s *ble, const char *devpath)
{
  if (ble == NULL)
    {
      return -EINVAL;
    }

  ble->fd = -1;
  ble->fd = open(devpath != NULL ? devpath : HS_BLE_DEVICE, O_RDWR);
  return ble->fd < 0 ? -errno : 0;
}

void hs_ble_close(struct hs_ble_s *ble)
{
  if (ble != NULL && ble->fd >= 0)
    {
      close(ble->fd);
      ble->fd = -1;
    }
}

int hs_ble_command(struct hs_ble_s *ble, uint16_t opcode,
                   const void *params, uint8_t params_length,
                   void *event, size_t event_size, size_t *event_length)
{
  uint8_t command[4 + 255];
  uint8_t header[3];
  uint8_t payload[255];
  uint8_t payload_length;
  size_t total;
  int ret;

  if (ble == NULL || ble->fd < 0 || event == NULL || event_size < 3 ||
      params_length > 255 || (params_length != 0 && params == NULL))
    {
      return -EINVAL;
    }

  command[0] = 0x01; /* H:4 command packet */
  command[1] = opcode & 0xff;
  command[2] = opcode >> 8;
  command[3] = params_length;
  if (params_length != 0)
    {
      memcpy(&command[4], params, params_length);
    }

  ret = hs_write_full(ble->fd, command, 4 + params_length);
  if (ret < 0)
    {
      return ret;
    }

  /* The controller may emit asynchronous LE events while a command is
   * pending.  Consume complete H:4 event packets until the response for this
   * opcode arrives, instead of treating the first event as the response. */
  for (;;)
    {
      ret = hs_read_full(ble->fd, header, sizeof(header));
      if (ret < 0)
        {
          return ret;
        }

      if (header[0] != 0x04)
        {
          return -EPROTO;
        }

      payload_length = header[2];
      ret = hs_read_full(ble->fd, payload, payload_length);
      if (ret < 0)
        {
          return ret;
        }

      if ((header[1] == 0x0e && payload_length >= 4 &&
           payload[1] == (opcode & 0xff) &&
           payload[2] == (opcode >> 8)) ||
          (header[1] == 0x0f && payload_length >= 4 &&
           payload[2] == (opcode & 0xff) &&
           payload[3] == (opcode >> 8)))
        {
          total = (size_t)payload_length + sizeof(header);
          if (total > event_size)
            {
              return -EMSGSIZE;
            }

          memcpy(event, header, sizeof(header));
          memcpy((uint8_t *)event + sizeof(header), payload,
                 payload_length);
          if (event_length != NULL)
            {
              *event_length = total;
            }

          return 0;
        }
    }
}

static int hs_ble_command_status(struct hs_ble_s *ble, uint16_t opcode,
                                 const void *params, uint8_t params_length)
{
  uint8_t event[16];
  size_t length;
  int ret;

  ret = hs_ble_command(ble, opcode, params, params_length,
                       event, sizeof(event), &length);
  if (ret < 0)
    {
      return ret;
    }

  /* Command Complete: 04 0e len 01 opcode-lo opcode-hi status ...
   * Command Status:   04 0f len status num opcode-lo opcode-hi */
  if ((event[1] == 0x0e &&
       (length < 7 || event[3] == 0 ||
        event[4] != (opcode & 0xff) || event[5] != (opcode >> 8))) ||
      (event[1] == 0x0f &&
       (length < 7 || event[5] != (opcode & 0xff) ||
        event[6] != (opcode >> 8))) ||
      (event[1] != 0x0e && event[1] != 0x0f))
    {
      printf("ble hci event mismatch op=0x%04x len=%lu evt=%02x data=%02x %02x %02x %02x\n",
             opcode, (unsigned long)length, event[1], event[3], event[4],
             event[5], event[6]);
      return -EPROTO;
    }

  {
    uint8_t status = event[1] == 0x0f ? event[3] : event[6];
    return status == 0 ? 0 : -(EIO + status);
  }
}

int hs_ble_set_advertising_parameters(struct hs_ble_s *ble,
                                      uint16_t interval_min,
                                      uint16_t interval_max)
{
  uint8_t params[15] =
  {
    interval_min & 0xff, interval_min >> 8,
    interval_max & 0xff, interval_max >> 8,
    0x00,                   /* connectable undirected */
    0x00,                   /* public own address */
    0x00,                   /* public direct address */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07,                   /* all three advertising channels */
    0x00                    /* allow all scanners */
  };

  if (interval_min == 0 || interval_max < interval_min)
    {
      return -EINVAL;
    }

  return hs_ble_command_status(ble, 0x2006, params, sizeof(params));
}

int hs_ble_set_advertising_data(struct hs_ble_s *ble,
                                const void *data, uint8_t length)
{
  uint8_t params[32];

  if (data == NULL || length > 31)
    {
      return -EINVAL;
    }

  memset(params, 0, sizeof(params));
  params[0] = length;
  memcpy(&params[1], data, length);
  return hs_ble_command_status(ble, 0x2008, params, sizeof(params));
}

int hs_ble_set_advertising(struct hs_ble_s *ble, bool enable)
{
  uint8_t params = enable ? 1 : 0;
  return hs_ble_command_status(ble, 0x200a, &params, sizeof(params));
}

int hs_ble_advertise_name(struct hs_ble_s *ble, const char *name)
{
  uint8_t data[31];
  size_t name_length;
  int ret;

  if (name == NULL)
    {
      return -EINVAL;
    }

  name_length = strlen(name);
  if (name_length > 24)
    {
      return -ENAMETOOLONG;
    }

  /* Flags + complete local name.  3 + (name length + 2) <= 31. */
  data[0] = 2;
  data[1] = 0x01;
  data[2] = 0x06;
  data[3] = (uint8_t)(name_length + 1);
  data[4] = 0x09;
  memcpy(&data[5], name, name_length);

  ret = hs_ble_set_advertising(ble, false);
  if (ret < 0)
    {
      printf("ble adv disable failed: %d\n", ret);
      return ret;
    }

  ret = hs_ble_set_advertising_parameters(ble, 0x00a0, 0x00a0);
  if (ret < 0)
    {
      printf("ble adv params failed: %d\n", ret);
      return ret;
    }

  ret = hs_ble_set_advertising_data(ble, data, (uint8_t)(name_length + 5));
  if (ret < 0)
    {
      printf("ble adv data failed: %d\n", ret);
      return ret;
    }

  ret = hs_ble_set_advertising(ble, true);
  if (ret < 0)
    {
      printf("ble adv enable failed: %d\n", ret);
    }

  return ret;
}

int hs_ble_reset(struct hs_ble_s *ble)
{
  uint8_t event[16];
  size_t length;

  return hs_ble_command(ble, 0x0c03, NULL, 0, event, sizeof(event), &length);
}
