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

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/input/buttons.h>
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
  uint16_t x;
  uint16_t y;
  int ret;

  if (!hs_lcd_valid(lcd))
    {
      return -EINVAL;
    }

  for (y = 0; y < lcd->yres; y++)
    {
      for (x = 0; x < lcd->xres; x++)
        {
          ret = hs_lcd_pixel(lcd, x, y, color);
          if (ret < 0)
            {
              return ret;
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
  total = (size_t)payload_length + sizeof(header);
  if (total > event_size)
    {
      return -EMSGSIZE;
    }

  memcpy(event, header, sizeof(header));
  ret = hs_read_full(ble->fd, (uint8_t *)event + sizeof(header),
                     payload_length);
  if (ret < 0)
    {
      return ret;
    }

  if (event_length != NULL)
    {
      *event_length = total;
    }

  return 0;
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

  /* Command Complete: 04 0e len 01 opcode-lo opcode-hi status ... */
  if (length < 7 || event[1] != 0x0e || event[3] != 1 ||
      event[4] != (opcode & 0xff) || event[5] != (opcode >> 8))
    {
      return -EPROTO;
    }

  return event[6] == 0 ? 0 : -(EIO + event[6]);
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
      return ret;
    }

  ret = hs_ble_set_advertising_parameters(ble, 0x00a0, 0x00a0);
  if (ret < 0)
    {
      return ret;
    }

  ret = hs_ble_set_advertising_data(ble, data, (uint8_t)(name_length + 5));
  if (ret < 0)
    {
      return ret;
    }

  return hs_ble_set_advertising(ble, true);
}

int hs_ble_reset(struct hs_ble_s *ble)
{
  uint8_t event[16];
  size_t length;

  return hs_ble_command(ble, 0x0c03, NULL, 0, event, sizeof(event), &length);
}
