/****************************************************************************
 * Huangshan Pi hardware interface smoke-test command.
 ****************************************************************************/

#include "huangshan_hal.h"
#include "hs_ble.h"
#include "hs_mic.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include <nuttx/ioexpander/gpio.h>
#include <nuttx/video/fb.h>

#define HS_GSR_DEFAULT_CAL_SECONDS 30
#define HS_GSR_MAX_CAL_SECONDS     600
#define HS_GSR_WARMUP_SAMPLES      5

/* ------------------------------------------------------------------------
 * Minimal built-in 5x7 bitmap font (row bit4..bit0 = left..right pixels)
 * ------------------------------------------------------------------------ */

static const uint8_t g_font_digit[10][7] =
{
  { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e }, /* 0 */
  { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* 1 */
  { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f }, /* 2 */
  { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e }, /* 3 */
  { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 }, /* 4 */
  { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e }, /* 5 */
  { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e }, /* 6 */
  { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* 7 */
  { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e }, /* 8 */
  { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c }  /* 9 */
};

static const uint8_t g_font_alpha[26][7] =
{
  { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* A */
  { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e }, /* B */
  { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e }, /* C */
  { 0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c }, /* D */
  { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f }, /* E */
  { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 }, /* F */
  { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f }, /* G */
  { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* H */
  { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* I */
  { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c }, /* J */
  { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* K */
  { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f }, /* L */
  { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
  { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 }, /* N */
  { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* O */
  { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 }, /* P */
  { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d }, /* Q */
  { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 }, /* R */
  { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e }, /* S */
  { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* T */
  { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* U */
  { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 }, /* V */
  { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a }, /* W */
  { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 }, /* X */
  { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 }, /* Y */
  { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f }  /* Z */
};

/* A small 8x8 CJK font for the fixed labels used by the debug panel.
 * Keeping this table local avoids a font-file/UTF-8 dependency in the early
 * framebuffer diagnostic path.  Bit 7 is the leftmost pixel. */
struct hs_cn_glyph_s
{
  uint32_t codepoint;
  uint16_t rows[16];
};

static const struct hs_cn_glyph_s g_font_cn[] =
{
  { 0x60c5, { 0x0000,0x0000,0x3060,0x37fe,0x3860,0x3860,0x7ffe,0x7060,0x77fe,0xf000,0x33fc,0x3304,0x33fc,0x3304,0x33fc,0x3304 } },
  { 0x7eea, { 0x0000,0x0000,0x0000,0x1060,0x1866,0x31fe,0x31fc,0x646c,0x7fff,0x7bff,0x1860,0x31fe,0x7ffe,0x7fc6,0x00fe,0x0cc6 } },
  { 0x951a, { 0x0000,0x0000,0x0000,0x10cc,0x30cc,0x3dfe,0x7dfe,0xc0cc,0x7ccc,0x3c00,0x19fe,0x7dfe,0x7db6,0x19fe,0x19fe,0x1fb6 } },
  { 0x70b9, { 0x0000,0x0000,0x0180,0x01fe,0x01fe,0x0180,0x0180,0x3ffc,0x3ffc,0x300c,0x300c,0x3ffc,0x3ffc,0x0000,0x36cc,0x366e } },
  { 0x8c03, { 0x0000,0x0000,0x0000,0x33fe,0x3bfe,0x1b26,0x03fe,0x7326,0x7326,0x33fe,0x3306,0x33fe,0x32de,0x3ade,0x3efe,0x3ec6 } },
  { 0x8bd5, { 0x0000,0x0000,0x0000,0x0014,0x3016,0x1812,0x0bfe,0x03fe,0x7018,0x7018,0x13d8,0x13d8,0x1198,0x1198,0x159a,0x1dfb } },
  { 0x8fd0, { 0x0000,0x0000,0x0000,0x2000,0x73fc,0x1bfc,0x0000,0x6000,0x77ff,0x17ff,0x00c8,0x719c,0x718c,0x31fe,0x37fe,0x3382 } },
  { 0x884c, { 0x0000,0x0000,0x0000,0x0800,0x19fe,0x39fe,0x7000,0x6c00,0x4c00,0x1bfe,0x3bfe,0x7818,0x7818,0x5818,0x1818,0x1818 } },
  { 0x603b, { 0x0000,0x0000,0x0c30,0x0c30,0x0670,0x1ff8,0x1ff8,0x1818,0x1818,0x1ff8,0x1ff8,0x0180,0x2dcc,0x3cee,0x6c46,0x6c1b } },
  { 0x7ebf, { 0x0000,0x0000,0x1068,0x186c,0x1864,0x307e,0x37fe,0x7de0,0x7c6e,0x19fe,0x31f0,0x7c36,0x7c3e,0x003c,0x1c38,0x7cfb } },
  { 0x89e6, { 0x0000,0x0000,0x0000,0x3010,0x3410,0x3c10,0x6cfe,0x6cfe,0xfed6,0x6ad6,0x7ed6,0x6ad6,0x6afe,0x7efe,0x6214,0x6216 } },
  { 0x6478, { 0x0000,0x0000,0x0000,0x30d8,0x33fe,0x33fe,0x7cd8,0x7ffc,0x3304,0x33fc,0x3f04,0x3f04,0xfbfc,0x7060,0x37fe,0x37fe } },
  { 0x7535, { 0x0000,0x0000,0x0000,0x0180,0x0180,0x3ffc,0x3ffc,0x318c,0x318c,0x3ffc,0x3ffc,0x318c,0x318c,0x3ffc,0x3ffc,0x3183 } },
  { 0x6c60, { 0x0000,0x0000,0x0000,0x2020,0x3820,0x1b20,0x032e,0x033e,0x63fe,0x77e6,0x3726,0x0726,0x032e,0x1b2c,0x1b20,0x3303 } },
  { 0x6309, { 0x0000,0x0000,0x0000,0x3030,0x3030,0x33fe,0x7ffe,0x7f46,0x3366,0x3060,0x33ff,0x3fff,0xf8cc,0x718c,0x31f8,0x3078 } },
  { 0x952e, { 0x0000,0x0000,0x0000,0x3010,0x33fe,0x7b96,0x7916,0x417f,0x7b16,0x7ffe,0x3190,0x79fe,0x7f90,0x3390,0x337e,0x3b10 } },
  { 0x4f9b, { 0x0000,0x0000,0x0000,0x0998,0x1998,0x1998,0x37fe,0x37fe,0x7198,0xf198,0x7198,0x7198,0x37fe,0x37fe,0x3000,0x319c } },
  { 0x5145, { 0x0000,0x0000,0x0000,0x0180,0x0180,0x7ffe,0x7ffe,0x0630,0x0e38,0x3ffc,0x3ffe,0x0664,0x0660,0x0660,0x0c60,0x0c63 } },
  { 0x76ae, { 0x0000,0x0000,0x0000,0x0180,0x0180,0x3ffe,0x3ffe,0x318c,0x318c,0x3184,0x3ffc,0x3ffc,0x3618,0x3338,0x31f0,0x61e0 } },
  { 0x516d, { 0x0000,0x0000,0x0000,0x0180,0x0180,0x0180,0x0180,0x7ffe,0x7ffe,0x0000,0x0000,0x0c30,0x0c38,0x1c18,0x181c,0x380c } },
  { 0x8f74, { 0x0000,0x0000,0x0000,0x1830,0x1030,0x7e30,0x7e30,0x31fe,0x29fe,0x69b6,0x7fb6,0x7ffe,0x09fe,0x0bb6,0xffb6,0x79b6 } },
  { 0x5c4f, { 0x0000,0x0000,0x0000,0x0000,0x3ffc,0x3ffc,0x3ffc,0x3ffc,0x3318,0x3318,0x3ffe,0x3ffe,0x3310,0x3ffe,0x3ffe,0x6310 } },
  { 0x5e55, { 0x0000,0x0000,0x7ffe,0x7ffe,0x0c30,0x3ffc,0x300c,0x3ffc,0x300c,0x3ffc,0x0600,0x7ffe,0x7ffe,0x399c,0x7ffe,0x599a } },
  { 0x6307, { 0x0000,0x0000,0x0000,0x1180,0x119c,0x11fc,0x7dc0,0x7d82,0x11fe,0x11fe,0x1400,0x3dfe,0xf9fe,0x5186,0x11fe,0x11fe } },
  { 0x4ee4, { 0x0000,0x0000,0x0000,0x0180,0x03c0,0x07e0,0x0e70,0x1c38,0x7ffe,0x77ee,0x4002,0x3ffc,0x3ffc,0x030c,0x030c,0x030c } },
  { 0x9000, { 0x0000,0x0000,0x0000,0x23fc,0x73fc,0x3b0c,0x13fc,0x030c,0x03fc,0x03fc,0x7326,0x733e,0x1338,0x1378,0x17fe,0x178e } },
  { 0x51fa, { 0x0000,0x0000,0x0000,0x0180,0x0180,0x318c,0x318c,0x318c,0x318c,0x3ffc,0x3ffc,0x0180,0x318c,0x318c,0x318c,0x318c } }
  ,{ 0x6709, { 0x0000,0x0000,0x0000,0x0300,0x0700,0x7ffe,0x7ffe,0x0c00,0x0ff8,0x1ff8,0x3c18,0x7ff8,0x6ff8,0x0c18,0x0ff8,0x0ff8 } }
  ,{ 0x538b, { 0x0000,0x0000,0x0000,0x0000,0x3ffe,0x3ffe,0x20c0,0x20c0,0x20c0,0x2ffe,0x2ffe,0x20c0,0x20d8,0x60cc,0x60c4,0x60c0 } }
};

static const uint16_t *hs_cn_glyph(uint32_t codepoint)
{
  unsigned int i;

  for (i = 0; i < sizeof(g_font_cn) / sizeof(g_font_cn[0]); i++)
    {
      if (g_font_cn[i].codepoint == codepoint)
        {
          return g_font_cn[i].rows;
        }
    }

  return NULL;
}

static const uint8_t *hs_font_glyph(char c)
{
  if (c >= '0' && c <= '9')
    {
      return g_font_digit[c - '0'];
    }

  if (c >= 'A' && c <= 'Z')
    {
      return g_font_alpha[c - 'A'];
    }

  if (c >= 'a' && c <= 'z')
    {
      return g_font_alpha[c - 'a'];
    }

  switch (c)
    {
      case ' ':
        {
          static const uint8_t sp[7] = { 0, 0, 0, 0, 0, 0, 0 };
          return sp;
        }
      case ':':
        {
          static const uint8_t col[7] = { 0, 12, 12, 0, 12, 12, 0 };
          return col;
        }
      case '.':
        {
          static const uint8_t dot[7] = { 0, 0, 0, 0, 0, 12, 12 };
          return dot;
        }
      case '-':
        {
          static const uint8_t dash[7] = { 0, 0, 0, 31, 0, 0, 0 };
          return dash;
        }
      case '/':
        {
          static const uint8_t slash[7] = { 1, 2, 2, 4, 8, 8, 16 };
          return slash;
        }
      case '(':
        {
          static const uint8_t lp[7] = { 2, 4, 8, 8, 8, 4, 2 };
          return lp;
        }
      case ')':
        {
          static const uint8_t rp[7] = { 8, 4, 2, 2, 2, 4, 8 };
          return rp;
        }
      case '>':
        {
          static const uint8_t gt[7] = { 8, 4, 2, 1, 2, 4, 8 };
          return gt;
        }
      case '%':
        {
          static const uint8_t pc[7] = { 25, 26, 2, 4, 8, 11, 19 };
          return pc;
        }
      case 'x':
        {
          static const uint8_t xx[7] = { 0, 0, 17, 10, 4, 10, 17 };
          return xx;
        }
      case '+':
        {
          static const uint8_t plus[7] = { 0, 4, 4, 31, 4, 4, 0 };
          return plus;
        }
      case '_':
        {
          static const uint8_t us[7] = { 0, 0, 0, 0, 0, 0, 31 };
          return us;
        }
      case '[':
        {
          static const uint8_t lb[7] = { 14, 8, 8, 8, 8, 8, 14 };
          return lb;
        }
      case ']':
        {
          static const uint8_t rb[7] = { 14, 2, 2, 2, 2, 2, 14 };
          return rb;
        }
      case '!':
        {
          static const uint8_t ex[7] = { 12, 12, 12, 12, 12, 0, 12 };
          return ex;
        }
      case '=':
        {
          static const uint8_t eq[7] = { 0, 0, 31, 0, 31, 0, 0 };
          return eq;
        }
      default:
        return NULL;
    }
}

static void hs_lcd_char(struct hs_lcd_s *lcd, int x0, int y0, char c,
                        uint32_t color, int scale)
{
  const uint8_t *glyph = hs_font_glyph(c);
  int row;
  int col;

  if (glyph == NULL)
    {
      return;
    }

  for (row = 0; row < 7; row++)
    {
      uint8_t bits = glyph[row];

      for (col = 0; col < 5; col++)
        {
          int dx;
          int dy;

          if ((bits & (0x10 >> col)) == 0)
            {
              continue;
            }

          for (dy = 0; dy < scale; dy++)
            {
              for (dx = 0; dx < scale; dx++)
                {
                  hs_lcd_pixel(lcd,
                               (uint16_t)(x0 + col * scale + dx),
                               (uint16_t)(y0 + row * scale + dy),
                               color);
                }
            }
        }
    }
}

static void hs_lcd_cn_char(struct hs_lcd_s *lcd, int x0, int y0,
                           uint32_t codepoint, uint32_t color, int scale)
{
  const uint16_t *glyph = hs_cn_glyph(codepoint);
  int row;
  int col;

  if (glyph == NULL)
    {
      return;
    }

  for (row = 0; row < 16; row++)
    {
      for (col = 0; col < 16; col++)
        {
          int dx;
          int dy;

          if ((glyph[row] & (0x8000 >> col)) == 0)
            {
              continue;
            }

          for (dy = 0; dy < scale; dy++)
            {
              for (dx = 0; dx < scale; dx++)
                {
                  hs_lcd_pixel(lcd,
                               (uint16_t)(x0 + col * scale + dx),
                               (uint16_t)(y0 + row * scale + dy),
                               color);
                }
            }
        }
    }
}

static void hs_lcd_cn_text(struct hs_lcd_s *lcd, int x, int y,
                           const uint32_t *text, size_t count,
                           uint32_t color, int scale)
{
  size_t i;

  for (i = 0; i < count; i++)
    {
      hs_lcd_cn_char(lcd, x + (int)(i * 17 * scale), y, text[i], color,
                     scale);
    }
}

static const uint32_t g_cn_touch[] = { 0x89e6, 0x6478 };
static const uint32_t g_cn_title[] =
  { 0x60c5, 0x7eea, 0x951a, 0x70b9, 0x8c03, 0x8bd5 };
static const uint32_t g_cn_uptime[] = { 0x8fd0, 0x884c };
static const uint32_t g_cn_bus[] = { 0x603b, 0x7ebf };
static const uint32_t g_cn_key[] = { 0x6309, 0x952e };
static const uint32_t g_cn_usb[] = { 0x4f9b, 0x7535 };
static const uint32_t g_cn_charge[] = { 0x5145, 0x7535 };
static const uint32_t g_cn_supply_voltage[] =
  { 0x4f9b, 0x7535, 0x7535, 0x538b };
static const uint32_t g_cn_wired_supply[] =
  { 0x6709, 0x7ebf, 0x4f9b, 0x7535 };
static const uint32_t g_cn_battery_supply[] =
  { 0x7535, 0x6c60, 0x4f9b, 0x7535 };
static const uint32_t g_cn_gsr[] = { 0x76ae, 0x7535 };
static const uint32_t g_cn_imu[] = { 0x516d, 0x8f74 };
static const uint32_t g_cn_spo2[] = { 0x8840, 0x6c27 };

/* Addresses worth probing periodically on the panel.  A full 0x08-0x77 scan
 * must not run inside the display loop: every absent address costs an I2C
 * timeout, which would freeze the UI for many seconds. */
static const uint8_t g_probe_list[] =
{
  0x1c, 0x1e, 0x29, 0x38, 0x40, 0x44, 0x49, 0x57, 0x68, 0x69, 0x6a, 0x6b
};
static const uint32_t g_cn_screen[] = { 0x5c4f, 0x5e55 };
static const uint32_t g_cn_exit[] = { 0x6307, 0x4ee4, 0x9000, 0x51fa };

static void hs_lcd_text(struct hs_lcd_s *lcd, int x, int y,
                        const char *text, uint32_t color, int scale)
{
  int cx = x;

  while (*text != '\0')
    {
      if (*text == '\n')
        {
          cx = x;
          y += 7 * scale + 1;
          text++;
          continue;
        }

      hs_lcd_char(lcd, cx, y, *text, color, scale);
      cx += 6 * scale;
      text++;
    }
}

static void hs_i2c_format(char *buf, size_t size, const uint8_t *found,
                          int n)
{
  size_t off = 0;
  int i;

  if (n < 0)
    {
      snprintf(buf, size, "bus closed");
      return;
    }

  if (n == 0)
    {
      snprintf(buf, size, "(none)");
      return;
    }

  for (i = 0; i < n && off + 4 < size; i++)
    {
      off += (size_t)snprintf(buf + off, size - off, "%02X ", found[i]);
    }
}

/* Scan one I2C bus completely.  Returns the number of ACKing addresses, or
 * -1 when the bus could not be opened. */

static int hs_i2c_scan(struct hs_i2c_s *bus, uint8_t *found, int max)
{
  int n = 0;
  int addr;

  if (bus == NULL || bus->fd < 0)
    {
      return -1;
    }

  for (addr = 0x08; addr <= 0x77 && n < max; addr++)
    {
      uint8_t reg = 0;
      uint8_t value;

      if (hs_i2c_write_read(bus, (uint16_t)addr, &reg, 1, &value, 1) >= 0)
        {
          found[n++] = (uint8_t)addr;
        }
    }

  return n;
}

static void hs_demo_help(void)
{
  printf("huangshan_hal_demo <all|i2c|adc|power|pwm|vibration|imu|mic_once|mic_stream|lcd|lcdtest|ble|ble_adv|blehost|bleevent|blestatus|sysinfo|gsr_once|gsr_cal|gsr_stream|max30102_once|max30102_stream>\n");
  printf("  i2c: probe FT6146 at I2C1 address 0x38\n");
  printf("  adc: read the VBAT ADC channel (channel 5)\n");
  printf("  power: print USB, VBAT, charger registers and KEY2 once\n");
  printf("  pwm: output 1 kHz, 50%% on /dev/pwm0 for 2 seconds\n");
  printf("  vibration [on|off]: drive PA20 (30P-24) high/low for the vibration module\n");
  printf("  imu: read one LSM6DSL accelerometer/gyroscope sample\n");
  printf("  mic_once: read one PCM block from the board MEMS microphone\n");
  printf("  mic_stream: print microphone RMS/peak at about 20 Hz\n");
  printf("  lcd: fill the CO5300 framebuffer with blue\n");
  printf("  lcdtest: LCD color bars, checkerboard and text (Ctrl+C to exit)\n");
  printf("  ble: open HCI transport and issue HCI Reset\n");
  printf("  ble_adv [name]: advertise a BLE local name for testing\n");
  printf("  blehost [xxxx]: bring up the NuttX BLE host stack, install the GATT\n");
  printf("                  server and advertise as 是非钟-xxxx\n");
  printf("  bleevent [type] [risk] [conf] [flags]: notify one 11 byte event packet\n");
  printf("  blestatus: print the BLE host/GATT/advertising state\n");
  printf("  sysinfo: live debug panel on the LCD (Ctrl+C to exit)\n");
  printf("  i2cscan: sweep 0x08-0x77 on /dev/i2c0 and /dev/i2c1\n");
  printf("  gsr_once: read Grove GSR from PA28 (/dev/adc1) once\n");
  printf("  gsr_cal [seconds]: open-electrode calibration, default 30 seconds\n");
  printf("  gsr_stream <CAL_RAW10>: CSV at about 5 Hz; Ctrl+C to exit\n");
  printf("  max30102_once: read one MAX30102 RED/IR sample on /dev/i2c1\n");
  printf("  max30102_stream: poll MAX30102 FIFO and print RED/IR at about 25 Hz\n");
  printf("  max30102_regs: dump pulse-oximeter registers and one FIFO burst\n");
  printf("                 optional <hz> argument, e.g. 100000\n");
}

static int hs_demo_max30102_once(void)
{
  struct hs_max30102_s sensor;
  struct hs_max30102_sample_s sample;
  int ret;

  ret = hs_max30102_open(&sensor, HS_MAX30102_I2C_BUS);
  if (ret < 0)
    {
      printf("max30102: open failed on /dev/i2c%u addr=0x%02x (%d)\n",
             HS_MAX30102_I2C_BUS, HS_MAX30102_I2C_ADDRESS, ret);
      return ret;
    }

  ret = hs_max30102_read_sample(&sensor, &sample);
  if (ret < 0)
    {
      printf("max30102: sample unavailable (%d), keep sensor on finger and retry\n",
             ret);
    }
  else
    {
      printf("max30102: addr=0x%02x red=%lu ir=%lu time_ms=%lu\n",
             HS_MAX30102_I2C_ADDRESS, (unsigned long)sample.red,
             (unsigned long)sample.ir, (unsigned long)sample.timestamp_ms);
    }

  printf("max30102: part_id=0x%02x rev_id=0x%02x (0x15 = genuine MAX30102)\n",
         sensor.part_id, sensor.rev_id);

  hs_max30102_close(&sensor);
  return ret;
}

static int hs_demo_max30102_stream(void)
{
  struct hs_max30102_s sensor;
  struct hs_max30102_sample_s sample;
  int ret;
  int errors = 0;

  ret = hs_max30102_open(&sensor, HS_MAX30102_I2C_BUS);
  if (ret < 0)
    {
      printf("max30102_stream: open failed (%d)\n", ret);
      return ret;
    }

  printf("max30102: part_id=0x%02x rev_id=0x%02x (0x15 = genuine MAX30102)\n",
         sensor.part_id, sensor.rev_id);
  printf("time_ms,red,ir,status\n");
  while (1)
    {
      ret = hs_max30102_read_sample(&sensor, &sample);
      if (ret == 0)
        {
          printf("%lu,%lu,%lu,OK\n", (unsigned long)sample.timestamp_ms,
                 (unsigned long)sample.red, (unsigned long)sample.ir);
          errors = 0;
        }
      else if (ret == -EAGAIN)
        {
          errors = 0;
        }
      else
        {
          printf("0,0,0,ERROR(%d)\n", ret);
          if (++errors >= 5)
            {
              break;
            }
        }

      usleep(40000);
    }

  hs_max30102_close(&sensor);
  return ret == -EAGAIN ? 0 : ret;
}

static int hs_demo_imu(void)
{
  struct hs_imu_s imu = { .fd = -1, .started = false };
  struct hs_imu_sample_s sample;
  int ret;

  ret = hs_imu_open(&imu);
  if (ret < 0)
    {
      printf("imu: cannot open %s (%d)\n", HS_IMU_DEVICE, ret);
      return ret;
    }

  ret = hs_imu_read(&imu, &sample);
  if (ret < 0)
    {
      printf("imu: read failed (%d)\n", ret);
    }
  else
    {
      printf("imu: accel_mg=%d,%d,%d gyro_mdps=%d,%d,%d temp_c=%d ts=%u\n",
             sample.accel_x_mg, sample.accel_y_mg, sample.accel_z_mg,
             sample.gyro_x_mdps, sample.gyro_y_mdps, sample.gyro_z_mdps,
             sample.temperature_c, sample.timestamp);
    }

  hs_imu_close(&imu);
  return ret;
}

static int hs_demo_mic_sweep(void)
{
  static const int gains[] = { -60, -40, -20, 0, 10, 20, 30 };
  int ret;
  int i;

  ret = hs_mic_start();
  if (ret < 0)
    {
      printf("mic_sweep: start failed (%d)\n", ret);
      return ret;
    }

  printf("gain_db,mean,peak,level\n");

  for (i = 0; i < (int)(sizeof(gains) / sizeof(gains[0])); i++)
    {
      int tick;

      if (hs_mic_set_volume(gains[i]) < 0)
        {
          printf("%d,set failed\n", gains[i]);
          continue;
        }

      /* Let a few blocks land at the new gain.  One block is 32 ms, so this
       * is a little over three blocks' worth.
       */

      for (tick = 0; tick < 24; tick++)
        {
          hs_mic_service();
          usleep(5000);
        }

      printf("%d,%d,%d,%d\n", gains[i], hs_mic_mean(), hs_mic_peak(),
             hs_mic_level());
    }

  return OK;
}

static int hs_demo_mic_once(void)
{
  int ret;
  int i;

  ret = hs_mic_start();
  if (ret < 0)
    {
      printf("mic: start failed (%d)\n", ret);
      return ret;
    }

  /* Let a few blocks land before reporting.  One block is 32 ms. */

  for (i = 0; i < 40; i++)
    {
      hs_mic_service();
      usleep(5000);
    }

  printf("mic: blocks=%lu mean=%d peak=%d level=%d\n",
         (unsigned long)hs_mic_blocks(), hs_mic_mean(), hs_mic_peak(),
         hs_mic_level());

  hs_mic_stop();
  return OK;
}

static int hs_demo_mic_stream(void)
{
  uint32_t last = 0;
  uint32_t seen;
  int ret;

  ret = hs_mic_start();
  if (ret < 0)
    {
      printf("mic_stream: start failed (%d)\n", ret);
      return ret;
    }

  printf("blocks,mean,peak,level\n");

  for (;;)
    {
      hs_mic_service();

      seen = hs_mic_blocks();
      if (seen != last)
        {
          last = seen;
          printf("%lu,%d,%d,%d\n", (unsigned long)seen, hs_mic_mean(),
                 hs_mic_peak(), hs_mic_level());
        }

      usleep(5000);
    }

  hs_mic_stop();
  return OK;
}

static int hs_demo_gsr_once(void)
{
  struct hs_gsr_s gsr = { .adc = { .fd = -1 } };
  struct hs_gsr_sample_s sample;
  int ret;

  ret = hs_gsr_open(&gsr);
  if (ret >= 0)
    {
      ret = hs_gsr_read(&gsr, &sample);
    }

  if (ret < 0)
    {
      printf("gsr_once: failed (%d); check PA28 wiring and /dev/adc1\n", ret);
    }
  else
    {
      printf("gsr_once: adc_mv=%ld raw10=%u\n", (long)sample.adc_mv,
             sample.raw10);
    }

  hs_gsr_close(&gsr);
  return ret;
}

static int hs_demo_gsr_cal(int seconds)
{
  struct hs_gsr_s gsr = { .adc = { .fd = -1 } };
  struct hs_gsr_sample_s sample;
  int64_t total = 0;
  int samples = 0;
  int target;
  int ret;

  if (seconds < 1 || seconds > HS_GSR_MAX_CAL_SECONDS)
    {
      printf("gsr_cal: seconds must be 1..%d\n", HS_GSR_MAX_CAL_SECONDS);
      return -EINVAL;
    }

  target = seconds * 5;
  ret = hs_gsr_open(&gsr);
  if (ret < 0)
    {
      printf("gsr_cal: cannot open /dev/adc1 (%d)\n", ret);
      return ret;
    }

  printf("gsr_cal: leave electrodes open, adjust the Grove potentiometer, ");
  printf("then wait %d s (Ctrl+C aborts)\n", seconds);

  while (samples < target)
    {
      ret = hs_gsr_read(&gsr, &sample);
      if (ret < 0)
        {
          break;
        }

      total += sample.raw10;
      samples++;
      if ((samples % 5) == 0)
        {
          printf("gsr_cal: %d/%d raw10=%u\n", samples, target,
                 sample.raw10);
        }
    }

  hs_gsr_close(&gsr);
  if (ret < 0)
    {
      printf("gsr_cal: failed (%d)\n", ret);
      return ret;
    }

  if (samples == 0)
    {
      return -ENODATA;
    }

  printf("CAL_RAW10=%ld samples=%d\n", (long)((total + samples / 2) /
         samples), samples);
  return 0;
}

static const char *hs_gsr_status(uint16_t cal_raw10, uint16_t raw10,
                                 unsigned int sample_index)
{
  if (cal_raw10 == 0 || cal_raw10 >= HS_GSR_RAW10_MAX)
    {
      return "CAL_INVALID";
    }

  if (raw10 == 0 || raw10 >= HS_GSR_RAW10_MAX - 1)
    {
      return "SATURATED";
    }

  if (raw10 >= cal_raw10)
    {
      return "CAL_INVALID";
    }

  if (sample_index < HS_GSR_WARMUP_SAMPLES)
    {
      return "WARMUP";
    }

  return "OK";
}

static int hs_demo_gsr_stream(uint16_t cal_raw10)
{
  struct hs_gsr_s gsr = { .adc = { .fd = -1 } };
  struct hs_gsr_sample_s sample;
  struct timespec ts;
  int32_t ema_mv = 0;
  int32_t baseline_mv = 0;
  unsigned int count = 0;
  int ret;

  if (cal_raw10 == 0 || cal_raw10 >= HS_GSR_RAW10_MAX)
    {
      printf("gsr_stream: CAL_RAW10 must be 1..1022\n");
      return -EINVAL;
    }

  ret = hs_gsr_open(&gsr);
  if (ret < 0)
    {
      printf("gsr_stream: cannot open /dev/adc1 (%d)\n", ret);
      return ret;
    }

  printf("time_ms,adc_mv,raw10,ema_mv,resistance_ohm,delta_pct,status\n");
  while (1)
    {
      const char *status;
      int64_t resistance = -1;
      int32_t delta_pct = 0;

      ret = hs_gsr_read(&gsr, &sample);
      if (ret < 0)
        {
          break;
        }

      if (count == 0)
        {
          ema_mv = sample.adc_mv;
        }
      else
        {
          ema_mv = (ema_mv * 4 + sample.adc_mv + 2) / 5;
        }

      if (count == HS_GSR_WARMUP_SAMPLES - 1)
        {
          baseline_mv = ema_mv;
        }

      if (baseline_mv > 0)
        {
          delta_pct = (int32_t)(((int64_t)(ema_mv - baseline_mv) * 100) /
                                baseline_mv);
        }

      status = hs_gsr_status(cal_raw10, sample.raw10, count);
      if (strcmp(status, "OK") == 0 || strcmp(status, "WARMUP") == 0)
        {
          int32_t denominator = (int32_t)cal_raw10 - sample.raw10;

          if (denominator > 0)
            {
              resistance = ((int64_t)(1024 + 2 * sample.raw10) * 10000) /
                           denominator;
            }
        }

      clock_gettime(CLOCK_MONOTONIC, &ts);
      printf("%lld,%ld,%u,%ld,%lld,%ld,%s\n",
             (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000,
             (long)sample.adc_mv, sample.raw10, (long)ema_mv,
             (long long)resistance, (long)delta_pct, status);
      count++;
    }

  hs_gsr_close(&gsr);
  printf("gsr_stream: stopped (%d)\n", ret);
  return ret;
}

static int hs_demo_power(void)
{
  struct hs_i2c_s i2c1 = { .fd = -1 };
  struct hs_adc_s adc = { .fd = -1 };
  struct hs_buttons_s buttons = { .fd = -1 };
  int vbus_fd = -1;
  int32_t vbat = -1;
  uint32_t button_state = 0;
  uint8_t reg;
  uint8_t chg01 = 0;
  uint8_t chg05 = 0;
  bool vbus = false;
  int adcret;
  int buttonret;
  int chgret01;
  int chgret05;
  int vbusret;

  adcret = hs_adc_open(&adc, NULL);
  if (adcret >= 0)
    {
      /* Compensates the on-chip attenuation of the VBATS sense input, so the
       * value printed below is the pack voltage rather than half of it.
       */

      adcret = hs_battery_read_mv(&adc, &vbat);
    }

  buttonret = hs_buttons_open(&buttons, NULL);
  if (buttonret >= 0)
    {
      buttonret = hs_buttons_read(&buttons, &button_state);
      /* No queued edge is the normal idle state for the non-blocking
       * buttons device; report it as released rather than failing power. */
      if (buttonret == -EAGAIN)
        {
          buttonret = 0;
          button_state = 0;
        }
    }

  chgret01 = hs_i2c_open(&i2c1, 1, HS_I2C_DEFAULT_FREQUENCY);
  chgret05 = chgret01;
  if (chgret01 >= 0)
    {
      reg = 0x01;
      chgret01 = hs_i2c_write_read(&i2c1, 0x49, &reg, 1, &chg01, 1);
      reg = 0x05;
      chgret05 = hs_i2c_write_read(&i2c1, 0x49, &reg, 1, &chg05, 1);
    }

  vbus_fd = open("/dev/gpio1", O_RDONLY);
  vbusret = vbus_fd < 0 ? -errno :
    ioctl(vbus_fd, GPIOC_READ, (unsigned long)(uintptr_t)&vbus);
  if (vbusret < 0 && vbus_fd >= 0)
    {
      vbusret = -errno;
    }

  printf("power: USB=%s VBAT=%ldmV ADC=%d CHG49=%s R1=0x%02X R5=0x%02X KEY2=%s\n",
         vbusret >= 0 ? (vbus ? "ON" : "OFF") : "ERR",
         (long)vbat, adcret,
         (chgret01 >= 0 && chgret05 >= 0) ? "OK" : "ERR",
         chg01, chg05,
         buttonret >= 0 ? ((button_state & 1) != 0 ? "DOWN" : "UP") :
                          "ERR");

  if (vbus_fd >= 0)
    {
      close(vbus_fd);
    }

  hs_buttons_close(&buttons);
  hs_adc_close(&adc);
  hs_i2c_close(&i2c1);
  return adcret < 0 ? adcret :
         chgret01 < 0 ? chgret01 :
         chgret05 < 0 ? chgret05 :
         vbusret < 0 ? vbusret : buttonret;
}

static int hs_demo_i2c(void)
{
  struct hs_i2c_s bus = { .fd = -1 };
  uint8_t reg = 0;
  uint8_t value;
  int ret;

  ret = hs_i2c_open(&bus, 0, HS_I2C_DEFAULT_FREQUENCY);
  if (ret < 0)
    {
      printf("i2c open failed: %d\n", ret);
      return ret;
    }

  ret = hs_i2c_write_read(&bus, 0x38, &reg, 1, &value, 1);
  printf("i2c FT6146: %s (%d), value=0x%02x\n",
         ret < 0 ? "failed" : "ok", ret, value);
  hs_i2c_close(&bus);
  return ret;
}

static int hs_demo_adc(void)
{
  struct hs_adc_s adc = { .fd = -1 };
  int32_t value;
  int ret = hs_adc_open(&adc, NULL);
  if (ret >= 0)
    {
      ret = hs_adc_read(&adc, HS_ADC_VBAT_CHANNEL, &value);
      printf("adc VBAT channel %u: %s (%d), %ld mV\n",
             HS_ADC_VBAT_CHANNEL, ret < 0 ? "failed" : "ok", ret,
             (long)value);
    }
  else
    {
      printf("adc open failed: %d\n", ret);
    }

  hs_adc_close(&adc);
  return ret;
}

static int hs_demo_pwm(void)
{
  struct hs_pwm_s pwm = { .fd = -1 };
  int ret = hs_pwm_open(&pwm, NULL);
  if (ret >= 0)
    {
      ret = hs_pwm_set(&pwm, 1000, 50);
      if (ret >= 0)
        {
          sleep(2);
          ret = hs_pwm_stop(&pwm);
        }
      printf("pwm 1kHz/50%%: %s (%d)\n", ret < 0 ? "failed" : "ok", ret);
    }
  else
    {
      printf("pwm open failed: %d\n", ret);
    }

  hs_pwm_close(&pwm);
  return ret;
}

static int hs_demo_vibration(const char *mode)
{
  struct hs_vibration_s vibration = { .fd = -1 };
  bool enabled;
  int ret;

  if (mode == NULL || (strcmp(mode, "on") != 0 &&
                       strcmp(mode, "off") != 0))
    {
      printf("vibration: usage vibration on|off\n");
      return -EINVAL;
    }

  enabled = strcmp(mode, "on") == 0;
  ret = hs_vibration_open(&vibration);
  if (ret >= 0)
    {
      ret = hs_vibration_set(&vibration, enabled);
      printf("vibration: PA20=%d (%s)\n", enabled ? 1 : 0,
             ret < 0 ? "failed" : "ok");
    }
  else
    {
      printf("vibration: open %s failed (%d)\n", HS_VIBRATION_DEVICE, ret);
    }

  hs_vibration_close(&vibration);
  return ret;
}

static int hs_demo_lcd(void)
{
  struct hs_lcd_s lcd = { .fd = -1 };
  int ret = hs_lcd_open(&lcd, NULL);
  if (ret >= 0)
    {
      ret = hs_lcd_fill(&lcd, 0x001f); /* RGB565 blue */
      printf("lcd %ux%u/%u bpp power=%d: %s (%d)\n", lcd.xres, lcd.yres,
             lcd.bpp, lcd.power, ret < 0 ? "failed" : "ok", ret);
    }
  else
    {
      printf("lcd open failed: %d\n", ret);
    }

  hs_lcd_close(&lcd);
  return ret;
}

/* A display-only diagnostic.  It deliberately avoids LVGL and the touch
 * stack so a bad panel, framebuffer mapping or refresh ioctl can be isolated
 * from the rest of the application. */

static int hs_demo_lcdtest(void)
{
  struct hs_lcd_s lcd = { .fd = -1 };
  static const uint16_t colors[] =
    { 0xf800, 0xffe0, 0x07e0, 0x07ff, 0x001f, 0xf81f, 0xffff, 0x0000 };
  char line[48];
  unsigned int frame = 0;
  int ret;

  ret = hs_lcd_open(&lcd, NULL);
  if (ret < 0)
    {
      printf("lcdtest: open failed: %d\n", ret);
      return ret;
    }

  printf("lcdtest: %ux%u, %u bpp, stride=%lu, fb=%lu bytes, power=%d\n",
         lcd.xres, lcd.yres, lcd.bpp, (unsigned long)lcd.stride,
         (unsigned long)lcd.length, lcd.power);
  printf("lcdtest: color bars -> checkerboard -> text; Ctrl+C to exit\n");

  while (1)
    {
      uint16_t x;
      uint16_t y;

      /* Eight full-width bars. */
      for (y = 0; y < lcd.yres; y++)
        {
          uint16_t bar = (uint16_t)(((unsigned long)y * 8) / lcd.yres);
          uint16_t color = colors[bar < 8 ? bar : 7];
          for (x = 0; x < lcd.xres; x++)
            {
              hs_lcd_pixel(&lcd, x, y, color);
            }
        }
      hs_lcd_text(&lcd, 8, 8, "LCD TEST COLOR BARS", 0x0000, 2);
      ret = hs_lcd_flush(&lcd);
      printf("lcdtest[%u]: bars flush=%d\n", frame++, ret);
      if (ret < 0)
        {
          break;
        }
      sleep(1);

      /* Checkerboard catches swapped byte order and address-line errors. */
      for (y = 0; y < lcd.yres; y++)
        {
          for (x = 0; x < lcd.xres; x++)
            {
              uint16_t color = (((x / 16) ^ (y / 16)) & 1) ?
                               0xffff : 0x0000;
              hs_lcd_pixel(&lcd, x, y, color);
            }
        }
      snprintf(line, sizeof(line), "LCD %ux%u %ubpp", lcd.xres, lcd.yres,
               lcd.bpp);
      hs_lcd_text(&lcd, 8, 8, line, 0xf800, 2);
      ret = hs_lcd_flush(&lcd);
      printf("lcdtest[%u]: checker flush=%d\n", frame++, ret);
      if (ret < 0)
        {
          break;
        }
      sleep(1);

      hs_lcd_fill(&lcd, 0x0000);
      hs_lcd_text(&lcd, 8, 8, "MOODANCHOR LCD OK", 0x07e0, 3);
      hs_lcd_text(&lcd, 8, 40, "FB0 / CO5300", 0x07ff, 2);
      hs_lcd_text(&lcd, 8, 58, "FRAMEBUFFER + REFRESH", 0xffff, 2);
      ret = hs_lcd_flush(&lcd);
      printf("lcdtest[%u]: text flush=%d\n", frame++, ret);
      if (ret < 0)
        {
          break;
        }
      sleep(1);
    }

  hs_lcd_close(&lcd);
  return ret;
}

static int hs_demo_ble(void)
{
  struct hs_ble_s ble = { .fd = -1 };
  int ret = hs_ble_open(&ble, NULL);
  if (ret >= 0)
    {
      ret = hs_ble_reset(&ble);
      printf("ble HCI reset: %s (%d)\n", ret < 0 ? "failed" : "ok", ret);
    }
  else
    {
      printf("ble open failed: %d\n", ret);
    }

  hs_ble_close(&ble);
  return ret;
}

static int hs_demo_ble_adv(const char *name)
{
  struct hs_ble_s ble = { .fd = -1 };
  int ret = hs_ble_open(&ble, NULL);

  if (ret >= 0)
    {
      ret = hs_ble_reset(&ble);
      if (ret >= 0)
        {
          ret = hs_ble_advertise_name(&ble, name);
        }

      printf("ble advertising (%s): %s (%d)\n", name,
             ret < 0 ? "failed" : "ok", ret);

      /* Keep the HCI session alive.  The SiFli controller is deinitialised
       * when /dev/ttyHCI0 is closed, which would stop advertising immediately
       * after a one-shot command exits. */
      if (ret >= 0)
        {
          for (;;)
            {
              sleep(1);
            }
        }
    }
  else
    {
      printf("ble open failed: %d\n", ret);
    }

  hs_ble_close(&ble);
  return ret;
}

/* ------------------------------------------------------------------------
 * BLE GATT peripheral (NuttX host stack)
 * ------------------------------------------------------------------------ */

static int hs_demo_blehost(const char *suffix)
{
  int ret;

  if (hs_ble_host_ready())
    {
      printf("blehost: already running as \"%s\"\n", hs_ble_gatt_name());
      return OK;
    }

  ret = hs_ble_host_start();
  if (ret < 0)
    {
      printf("blehost: host start failed: %d\n", ret);
      return ret;
    }

  printf("blehost: host stack up\n");

  ret = hs_ble_gatt_start(suffix);
  if (ret < 0)
    {
      printf("blehost: GATT/advertising start failed: %d\n", ret);
      return ret;
    }

  printf("blehost: ready, connect with nRF Connect and subscribe to "
         "\"d38a0002\"\n");
  return OK;
}

static int hs_demo_bleevent(int argc, char *argv[])
{
  uint8_t type  = HS_BLE_EV_SELFTEST;
  uint8_t risk  = HS_BLE_RISK_MEDIUM;
  uint8_t conf  = 80;
  uint8_t flags = HS_BLE_FLAG_ACK_REQ | HS_BLE_FLAG_SIMULATED;
  int ret;

  if (!hs_ble_host_ready())
    {
      printf("bleevent: run blehost first\n");
      return -ENOTCONN;
    }

  if (argc > 2)
    {
      type = (uint8_t)strtoul(argv[2], NULL, 0);
    }

  if (argc > 3)
    {
      risk = (uint8_t)strtoul(argv[3], NULL, 0);
    }

  if (argc > 4)
    {
      conf = (uint8_t)strtoul(argv[4], NULL, 0);
    }

  if (argc > 5)
    {
      flags = (uint8_t)strtoul(argv[5], NULL, 0);
    }

  ret = hs_ble_event_notify(type, risk, conf, flags);
  if (ret < 0)
    {
      printf("bleevent: notify failed: %d\n", ret);
    }

  return ret < 0 ? ret : OK;
}

static int hs_demo_blestatus(void)
{
  const uint8_t *addr = hs_ble_host_bdaddr();
  uint8_t ctrl[HS_BLE_CTRL_MAX];
  size_t ctrllen;
  uint16_t handle;
  uint16_t interval;

  printf("host      : %s\n", hs_ble_host_ready() ? "ready" : "down");

  if (addr != NULL)
    {
      printf("bdaddr    : %02x:%02x:%02x:%02x:%02x:%02x\n",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    }

  printf("adv name  : %s\n", hs_ble_host_ready() ? hs_ble_gatt_name() : "-");

  handle   = hs_ble_host_conn_handle();
  interval = hs_ble_host_conn_interval();

  if (handle == 0xffff)
    {
      printf("connection: none\n");
    }
  else
    {
      printf("connection: handle 0x%04x, interval %.2f ms\n",
             handle, (double)interval * 1.25);
    }

  ctrllen = hs_ble_control_last(ctrl, sizeof(ctrl));
  if (ctrllen > 0)
    {
      printf("last ctrl : opcode 0x%02x, %u byte(s)\n", ctrl[0],
             (unsigned)ctrllen);
    }
  else
    {
      printf("last ctrl : none\n");
    }

  printf("event seq : %u\n", hs_ble_event_seq());
  return OK;
}

/* ------------------------------------------------------------------------
 * sysinfo: live debug panel on the LCD
 * ------------------------------------------------------------------------ */

static int hs_demo_sysinfo(void)
{
  const int safe_x = 24;
  /* Four 16px CJK glyphs at x=24 occupy through x=91.  Keep a
   * separate numeric column so values cannot overwrite the labels. */
  const int value_x = 104;
  const int dynamic_y = 20;
  const int dynamic_h = 340;
  struct hs_lcd_s lcd = { .fd = -1 };
  struct hs_i2c_s i2c0 = { .fd = -1 };
  struct hs_i2c_s i2c1 = { .fd = -1 };
  struct hs_adc_s adc = { .fd = -1 };
  struct hs_gsr_s gsr = { .adc = { .fd = -1 } };
  struct hs_buttons_s buttons = { .fd = -1 };
  struct hs_vibration_s vibration = { .fd = -1 };
  struct hs_imu_s imu = { .fd = -1, .started = false };
  struct hs_mic_s mic = { .fd = -1 };
  struct hs_max30102_s max30102 = { .i2c = { .fd = -1 } };
  int vbus_fd = -1;
  uint8_t found0[16];
  uint8_t found1[16];
  char line[64];
  unsigned int tick = 0;
  int n0 = 0;
  int n1 = 0;
  uint8_t max_id_ff = 0;
  uint8_t max_id_fe = 0;
  int max_id_ret = -ENODEV;
  int probe_idx0 = 0;
  int probe_idx1 = 0;
  int ret;
  bool first_frame = true;
  bool max_sample_valid = false;
  uint32_t max_last_red = 0;
  uint32_t max_last_ir = 0;

  ret = hs_lcd_open(&lcd, NULL);
  if (ret < 0)
    {
      printf("sysinfo: lcd open failed: %d\n", ret);
      goto out;
    }

  hs_i2c_open(&i2c0, 0, HS_I2C_DEFAULT_FREQUENCY);
  hs_i2c_open(&i2c1, 1, HS_I2C_DEFAULT_FREQUENCY);
  hs_adc_open(&adc, NULL);
  /* GSR is optional.  Keep the panel useful when the Grove module is not
   * connected or /dev/adc1 is unavailable. */
  hs_gsr_open(&gsr);
  hs_buttons_open(&buttons, NULL);
  hs_vibration_open(&vibration);
  vbus_fd = open("/dev/gpio1", O_RDONLY);
  hs_imu_open(&imu);
  hs_mic_open(&mic, 16000);
  hs_max30102_open(&max30102, HS_MAX30102_I2C_BUS);

  printf("sysinfo panel on, press Ctrl+C to exit\n");

  bool key2_pressed = false;

  while (1)
    {
      struct timespec ts;
      int32_t adcval = -1;
      struct hs_gsr_sample_s gsr_sample;
      int gsrret = -ENODEV;
      struct hs_imu_sample_s imu_sample;
      int imuret = -ENODEV;
      int micret = -ENODEV;
      int mic_rms = 0;
      int mic_peak = 0;
      int16_t mic_samples[64];
      struct hs_max30102_sample_s max_sample;
      int maxret = -ENODEV;
      uint32_t button_state = 0;
      int button_ret;
      uint8_t reg = 0;
      uint8_t touch = 0;
      uint8_t chg01 = 0;
      uint8_t chg05 = 0;
      int chgret01 = -ENODEV;
      int chgret05 = -ENODEV;
      bool vbus = false;
      int vbusret = -ENODEV;
      int y = 22;

      clock_gettime(CLOCK_MONOTONIC, &ts);

      /* Probe one candidate address per bus per frame.  A full cycle of the
       * candidate list takes about a second at the panel's refresh rate, and
       * no frame ever stalls waiting for an absent device.  Results from the
       * previous cycle stay visible until the new one completes. */
      if (i2c0.fd >= 0)
        {
          uint8_t probe_reg = 0;
          uint8_t probe_value = 0;

          if (hs_i2c_write_read(&i2c0, g_probe_list[probe_idx0],
                                &probe_reg, 1, &probe_value, 1) >= 0 &&
              n0 < (int)(sizeof(found0) / sizeof(found0[0])))
            {
              found0[n0++] = g_probe_list[probe_idx0];
            }

          if (++probe_idx0 >= (int)(sizeof(g_probe_list) /
                                    sizeof(g_probe_list[0])))
            {
              probe_idx0 = 0;
              n0 = 0;
            }
        }

      if (i2c1.fd >= 0)
        {
          uint8_t probe_reg = 0;
          uint8_t probe_value = 0;

          if (hs_i2c_write_read(&i2c1, g_probe_list[probe_idx1],
                                &probe_reg, 1, &probe_value, 1) >= 0 &&
              n1 < (int)(sizeof(found1) / sizeof(found1[0])))
            {
              found1[n1++] = g_probe_list[probe_idx1];
            }

          if (++probe_idx1 >= (int)(sizeof(g_probe_list) /
                                    sizeof(g_probe_list[0])))
            {
              probe_idx1 = 0;
              n1 = 0;
            }
        }

      /* Read the pulse-oximeter identification registers every ~3 seconds.
       * This separates a wiring/power fault (no ACK) from an unexpected chip
       * model (ACK but a part ID other than MAX30102's 0x15). */
      if ((tick % 30u) == 0u && i2c1.fd >= 0)
        {
          uint8_t idreg = 0xff;

          max_id_ff = 0;
          max_id_fe = 0;
          max_id_ret = hs_i2c_write_read(&i2c1, HS_MAX30102_I2C_ADDRESS,
                                         &idreg, 1, &max_id_ff, 1);
          if (max_id_ret >= 0)
            {
              idreg = 0xfe;
              (void)hs_i2c_write_read(&i2c1, HS_MAX30102_I2C_ADDRESS,
                                      &idreg, 1, &max_id_fe, 1);
            }
        }

      if (adc.fd >= 0)
        {
          hs_adc_read(&adc, HS_ADC_VBAT_CHANNEL, &adcval);
        }

      if (gsr.adc.fd >= 0)
        {
          gsrret = hs_gsr_read(&gsr, &gsr_sample);
        }

      if (imu.fd >= 0)
        {
          imuret = hs_imu_read(&imu, &imu_sample);
        }

      if (mic.fd >= 0)
        {
          ssize_t mic_count = hs_mic_read(&mic, mic_samples,
                                          sizeof(mic_samples) /
                                          sizeof(mic_samples[0]));
          if (mic_count > 0)
            {
              int64_t sum = 0;
              int i;
              micret = 0;
              for (i = 0; i < mic_count; i++)
                {
                  int value = mic_samples[i] < 0 ? -mic_samples[i] : mic_samples[i];
                  if (value > mic_peak)
                    {
                      mic_peak = value;
                    }
                  sum += (int64_t)value * value;
                }
              mic_rms = (int)sqrt((double)(sum / mic_count));
            }
          else if (mic_count == 0 || errno == EAGAIN)
            {
              micret = -EAGAIN;
            }
        }

      if (max30102.initialized)
        {
          maxret = hs_max30102_read_sample(&max30102, &max_sample);
          if (maxret >= 0)
            {
              /* FIFO polling can legitimately return EAGAIN between samples.
               * Keep the last valid pair visible instead of blanking the panel. */
              max_last_red = max_sample.red;
              max_last_ir = max_sample.ir;
              max_sample_valid = true;
            }
        }

      button_ret = -ENODEV;
      if (buttons.fd >= 0)
        {
          button_ret = hs_buttons_read(&buttons, &button_state);
          if (button_ret >= 0)
            {
              bool pressed = (button_state & 1u) != 0;
              if (pressed && !key2_pressed && vibration.fd >= 0)
                {
                  int vibret = hs_vibration_set(
                    &vibration, !hs_vibration_is_enabled(&vibration));
                  printf("KEY2: vibration %s (%d)\n",
                         hs_vibration_is_enabled(&vibration) ? "on" : "off",
                         vibret);
                }

              key2_pressed = pressed;
            }
        }

      if (i2c0.fd >= 0)
        {
          hs_i2c_write_read(&i2c0, 0x38, &reg, 1, &touch, 1);
        }

      /* AW32001 charger diagnostics.  The vendor BSP uses address 0x49 on
       * I2C2 (registered as /dev/i2c1).  Keep the raw register values in the
       * panel because the public board docs do not define a stable battery
       * present/status bitfield for this chip. */

      if (i2c1.fd >= 0)
        {
          reg = 0x01;
          chgret01 = hs_i2c_write_read(&i2c1, 0x49, &reg, 1, &chg01, 1);
          reg = 0x05;
          chgret05 = hs_i2c_write_read(&i2c1, 0x49, &reg, 1, &chg05, 1);
        }

      if (vbus_fd >= 0)
        {
          vbusret = ioctl(vbus_fd, GPIOC_READ,
                          (unsigned long)(uintptr_t)&vbus);
        }

      /* Clear the local framebuffer without flushing it.  Static labels are
       * drawn on every pass for simplicity, but after the first pass only the
       * numeric/value column is cleared and sent to the panel. */
      if (first_frame)
        {
          memset(lcd.framebuffer, 0, lcd.length);
        }
      else
        {
          uint16_t clear_y;
          size_t pixel_bytes = (size_t)lcd.bpp / 8;
          size_t clear_offset = (size_t)value_x * pixel_bytes;
          size_t clear_bytes = ((size_t)lcd.xres - value_x) * pixel_bytes;

          for (clear_y = dynamic_y;
               clear_y < dynamic_y + dynamic_h && clear_y < lcd.yres;
               clear_y++)
            {
              uint8_t *row = (uint8_t *)lcd.framebuffer +
                             (size_t)clear_y * lcd.stride;
              memset(row + clear_offset, 0, clear_bytes);
            }
        }

      /* Title */
      hs_lcd_cn_text(&lcd, safe_x, y, g_cn_title,
                     sizeof(g_cn_title) / sizeof(g_cn_title[0]), 0x07ff, 1);
      y += 20;

      /* Tick + uptime */
      hs_lcd_cn_text(&lcd, safe_x, y, g_cn_uptime,
                     sizeof(g_cn_uptime) / sizeof(g_cn_uptime[0]),
                     0xffff, 1);
      snprintf(line, sizeof(line), "  %06u  %lds", tick,
               (long)ts.tv_sec);
      hs_lcd_text(&lcd, value_x, y + 1, line, 0xffff, 2);
      y += 20;

      /* I2C scan results */
      hs_lcd_cn_text(&lcd, safe_x, y, g_cn_bus,
                     sizeof(g_cn_bus) / sizeof(g_cn_bus[0]), 0xffe0, 1);
      hs_lcd_text(&lcd, value_x, y + 1, "0:", 0xffe0, 2);
      hs_i2c_format(line, sizeof(line), found0, n0);
      hs_lcd_text(&lcd, value_x + 26, y + 1, line, 0xffe0, 2);
      y += 20;

      hs_lcd_cn_text(&lcd, safe_x, y, g_cn_bus,
                     sizeof(g_cn_bus) / sizeof(g_cn_bus[0]), 0xffe0, 1);
      hs_lcd_text(&lcd, value_x, y + 1, "1:", 0xffe0, 2);
      hs_i2c_format(line, sizeof(line), found1, n1);
      hs_lcd_text(&lcd, value_x + 26, y + 1, line, 0xffe0, 2);
      y += 20;

      /* MAX30102 identification: NOACK means wiring/power, a HEX part ID
       * other than 15 means the module is a different chip model. */
      hs_lcd_cn_text(&lcd, safe_x, y, g_cn_spo2,
                     sizeof(g_cn_spo2) / sizeof(g_cn_spo2[0]), 0xf81f, 1);
      if (max_id_ret < 0)
        {
          snprintf(line, sizeof(line), "NOACK(%d)", max_id_ret);
        }
      else
        {
          snprintf(line, sizeof(line), "FF=%02X FE=%02X", max_id_ff,
                   max_id_fe);
        }
      hs_lcd_text(&lcd, value_x, y + 1, line, 0xf81f, 2);
      y += 20;

      /* Touch and battery voltage.  Keep the numeric values in ASCII for
       * readability, with fixed Chinese labels beside them. */
      hs_lcd_cn_text(&lcd, safe_x, y, g_cn_touch,
                     sizeof(g_cn_touch) / sizeof(g_cn_touch[0]), 0x07e0, 1);
      snprintf(line, sizeof(line), "  0X%02X", touch);
      hs_lcd_text(&lcd, value_x, y + 1, line, 0x07e0, 2);
      y += 20;

      hs_lcd_cn_text(&lcd, safe_x, y, g_cn_supply_voltage,
                     sizeof(g_cn_supply_voltage) /
                     sizeof(g_cn_supply_voltage[0]),
                     0x07e0, 1);
      snprintf(line, sizeof(line), "%ldMV", (long)adcval);
      hs_lcd_text(&lcd, value_x, y + 1, line, 0x07e0, 2);
      y += 20;

      if (vbusret >= 0 && vbus)
        {
          hs_lcd_cn_text(&lcd, safe_x, y, g_cn_wired_supply,
                         sizeof(g_cn_wired_supply) /
                         sizeof(g_cn_wired_supply[0]), 0xff80, 1);
        }
      else if (vbusret >= 0)
        {
          hs_lcd_cn_text(&lcd, safe_x, y, g_cn_battery_supply,
                         sizeof(g_cn_battery_supply) /
                         sizeof(g_cn_battery_supply[0]), 0xff80, 1);
        }
      else
        {
          hs_lcd_cn_text(&lcd, safe_x, y, g_cn_usb,
                         sizeof(g_cn_usb) / sizeof(g_cn_usb[0]),
                         0xff80, 1);
          hs_lcd_text(&lcd, value_x, y + 1, "--", 0xff80, 2);
        }
      y += 20;

      hs_lcd_cn_text(&lcd, safe_x, y, g_cn_key,
                     sizeof(g_cn_key) / sizeof(g_cn_key[0]), 0xf81f, 1);
      snprintf(line, sizeof(line), "  %u 0X%02lX",
               (button_state & 1) != 0 ? 1u : 0u,
               (unsigned long)buttons.supported);
      hs_lcd_text(&lcd, value_x, y + 1, line, 0xf81f, 2);
      y += 20;

      hs_lcd_text(&lcd, safe_x, y, "VIB PA20:", 0xf81f, 2);
      snprintf(line, sizeof(line), " %s", hs_vibration_is_enabled(&vibration)
               ? "ON" : "OFF");
      hs_lcd_text(&lcd, value_x, y + 1, line, 0xf81f, 2);
      y += 20;

      hs_lcd_cn_text(&lcd, safe_x, y, g_cn_usb,
                     sizeof(g_cn_usb) / sizeof(g_cn_usb[0]), 0xff80, 1);
      hs_lcd_cn_text(&lcd, value_x, y, g_cn_charge,
                     sizeof(g_cn_charge) / sizeof(g_cn_charge[0]),
                     0xff80, 1);
      snprintf(line, sizeof(line), " %s %s R1:%02X R5:%02X",
               vbusret >= 0 ? (vbus ? "1" : "0") : "--",
               (chgret01 >= 0 && chgret05 >= 0) ? "1" : "--",
               chg01, chg05);
      hs_lcd_text(&lcd, value_x, y + 1, line, 0xff80, 2);
      y += 20;

      if (gsrret >= 0)
        {
          snprintf(line, sizeof(line), " %ldMV R:%04u",
                   (long)gsr_sample.adc_mv, gsr_sample.raw10);
        }
      else
        {
          snprintf(line, sizeof(line), " -- (PA28/ADC1)");
        }
      hs_lcd_cn_text(&lcd, safe_x, y, g_cn_gsr,
                     sizeof(g_cn_gsr) / sizeof(g_cn_gsr[0]), 0xfbe0, 1);
      hs_lcd_text(&lcd, value_x, y + 1, line, 0xfbe0, 2);
      y += 20;

      hs_lcd_cn_text(&lcd, safe_x, y, g_cn_imu,
                     sizeof(g_cn_imu) / sizeof(g_cn_imu[0]), 0x07ff, 1);
      if (imuret >= 0)
        {
          snprintf(line, sizeof(line), "A:%d,%d,%d",
                   imu_sample.accel_x_mg, imu_sample.accel_y_mg,
                   imu_sample.accel_z_mg);
        }
      else
        {
          snprintf(line, sizeof(line), "-- (LSM6DSL)");
        }
      hs_lcd_text(&lcd, value_x, y + 1, line, 0x07ff, 2);
      y += 20;

      hs_lcd_text(&lcd, safe_x, y, "MAX:", 0x07e0, 2);
      if (max_sample_valid)
        {
          snprintf(line, sizeof(line), "R:%lu I:%lu %s",
                   (unsigned long)max_last_red,
                   (unsigned long)max_last_ir,
                   maxret == -EAGAIN ? "WAIT" : "OK");
        }
      else if (max30102.initialized && maxret == -EAGAIN)
        {
          snprintf(line, sizeof(line), "WAIT (0X57)");
        }
      else
        {
          snprintf(line, sizeof(line), "-- (0X57)");
        }
      hs_lcd_text(&lcd, value_x, y + 1, line, 0x07e0, 2);
      y += 20;

      hs_lcd_text(&lcd, safe_x, y, "MIC:", 0x07ff, 2);
      if (micret >= 0)
        {
          snprintf(line, sizeof(line), "RMS:%d P:%d", mic_rms, mic_peak);
        }
      else
        {
          snprintf(line, sizeof(line), "-- (AUDIO)");
        }
      hs_lcd_text(&lcd, value_x, y + 1, line, 0x07ff, 2);
      y += 20;

      /* Display info */
      hs_lcd_cn_text(&lcd, safe_x, y, g_cn_screen,
                     sizeof(g_cn_screen) / sizeof(g_cn_screen[0]), 0x001f, 1);
      snprintf(line, sizeof(line), " %ux%u %ubpp", lcd.xres, lcd.yres,
               lcd.bpp);
      hs_lcd_text(&lcd, value_x, y + 1, line, 0x001f, 2);
      y += 20;

      hs_lcd_cn_text(&lcd, safe_x, y, g_cn_exit,
                     sizeof(g_cn_exit) / sizeof(g_cn_exit[0]), 0xf800, 1);
      y += 20;

      if (first_frame)
        {
          hs_lcd_flush(&lcd);
          first_frame = false;
        }
      else
        {
#if defined(FBIO_UPDATE) && defined(CONFIG_FB_UPDATE)
          struct fb_area_s area;

          /* Labels are static after the first frame.  Submit only the
           * numeric column; the LCD driver packs its full-stride rows into
           * contiguous DMA chunks for this partial area. */
          area.x = value_x;
          area.y = dynamic_y;
          area.w = lcd.xres - value_x;
          area.h = dynamic_h;
          ret = ioctl(lcd.fd, FBIO_UPDATE,
                      (unsigned long)(uintptr_t)&area);
          if (ret < 0)
            {
              /* A failed partial update must not silently freeze the panel.
               * Retry once through the normal full-frame path. */
              (void)hs_lcd_flush(&lcd);
            }
#else
          hs_lcd_flush(&lcd);
#endif
        }
      tick++;
      usleep(100000);
    }

out:
  if (vbus_fd >= 0)
    {
      close(vbus_fd);
    }

  hs_buttons_close(&buttons);
  hs_imu_close(&imu);
  hs_mic_close(&mic);
  hs_max30102_close(&max30102);
  hs_gsr_close(&gsr);
  hs_vibration_close(&vibration);
  hs_adc_close(&adc);
  hs_i2c_close(&i2c0);
  hs_i2c_close(&i2c1);
  hs_lcd_close(&lcd);
  return ret;
}

/* Full scan of both I2C buses.  Deliberately kept out of the display loop:
 * every absent address in a 0x08-0x77 sweep costs an I2C timeout, which
 * would stall the panel for many seconds. */

static int hs_demo_i2cscan(void)
{
  struct hs_i2c_s bus = { .fd = -1 };
  uint8_t found[32];
  char line[128];
  unsigned int busno;
  int ret;

  for (busno = 0; busno <= 1; busno++)
    {
      ret = hs_i2c_open(&bus, busno, HS_I2C_DEFAULT_FREQUENCY);
      if (ret < 0)
        {
          printf("i2cscan: /dev/i2c%u open failed (%d)\n", busno, ret);
          continue;
        }

      ret = hs_i2c_scan(&bus, found, (int)(sizeof(found) / sizeof(found[0])));
      hs_i2c_format(line, sizeof(line), found, ret);
      printf("i2cscan: bus %u -> %s\n", busno, line);
      hs_i2c_close(&bus);
    }

  return 0;
}

/* Dump the pulse-oximeter register file and one FIFO burst.  This is the
 * ground-truth probe for low-cost MAX30102-compatible parts: registers that
 * stay 0x00 or a read that fails reveal a chip that does not implement the
 * standard map. */

static int hs_demo_max30102_regs(uint32_t frequency)
{
  struct hs_i2c_s bus = { .fd = -1 };
  uint8_t regs[0x0c];
  uint8_t fifo[6];
  char line[64];
  uint8_t reg;
  int ret;
  int i;

  ret = hs_i2c_open(&bus, HS_MAX30102_I2C_BUS, frequency);
  if (ret < 0)
    {
      printf("max30102_regs: /dev/i2c%u open failed (%d)\n",
             HS_MAX30102_I2C_BUS, ret);
      return ret;
    }

  printf("max30102_regs: bus frequency %lu Hz\n",
         (unsigned long)bus.frequency);

  memset(regs, 0, sizeof(regs));
  for (i = 0; i <= 0x0b; i++)
    {
      uint8_t value = 0;

      reg = (uint8_t)i;
      ret = hs_i2c_write_read(&bus, HS_MAX30102_I2C_ADDRESS, &reg, 1,
                              &value, 1);
      regs[i] = (ret < 0) ? 0xee : value;
    }

  for (i = 0; i <= 0x0b; i += 4)
    {
      snprintf(line, sizeof(line), "max30102_regs: 0x%02x-0x%02x = %02x %02x "
               "%02x %02x\n", i, i + 3, regs[i], regs[i + 1], regs[i + 2],
               regs[i + 3]);
      printf("%s", line);
    }

  memset(fifo, 0, sizeof(fifo));
  reg = 0x07;
  ret = hs_i2c_write_read(&bus, HS_MAX30102_I2C_ADDRESS, &reg, 1,
                          fifo, sizeof(fifo));
  printf("max30102_regs: fifo burst (0x07) ret=%d %02x %02x %02x %02x %02x "
         "%02x\n", ret, fifo[0], fifo[1], fifo[2], fifo[3], fifo[4], fifo[5]);

  /* A plain read (no register pointer) shows whether the part responds to a
   * bare read transaction at all. */
  {
    uint8_t raw = 0;

    ret = hs_i2c_read(&bus, HS_MAX30102_I2C_ADDRESS, &raw, 1);
    printf("max30102_regs: bare read ret=%d value=0x%02x\n", ret, raw);
  }

  /* Write/read-back test: proves whether configuration writes actually land.
   * Use registers that are not touched by the driver's init sequence. */
  {
    uint8_t wbuf[2];
    uint8_t rb = 0;

    wbuf[0] = 0x0b;   /* LED1_PA */
    wbuf[1] = 0x55;
    ret = hs_i2c_write(&bus, HS_MAX30102_I2C_ADDRESS, wbuf, sizeof(wbuf));
    reg = 0x0b;
    (void)hs_i2c_write_read(&bus, HS_MAX30102_I2C_ADDRESS, &reg, 1, &rb, 1);
    printf("max30102_regs: write 0x0b=0x55 ret=%d readback=0x%02x\n", ret, rb);

    wbuf[0] = 0x0d;   /* LED2_PA */
    wbuf[1] = 0x66;
    ret = hs_i2c_write(&bus, HS_MAX30102_I2C_ADDRESS, wbuf, sizeof(wbuf));
    reg = 0x0d;
    (void)hs_i2c_write_read(&bus, HS_MAX30102_I2C_ADDRESS, &reg, 1, &rb, 1);
    printf("max30102_regs: write 0x0d=0x66 ret=%d readback=0x%02x\n", ret, rb);

    /* Put the part into SpO2 mode and read the mode back. */
    wbuf[0] = 0x09;
    wbuf[1] = 0x03;
    ret = hs_i2c_write(&bus, HS_MAX30102_I2C_ADDRESS, wbuf, sizeof(wbuf));
    reg = 0x09;
    (void)hs_i2c_write_read(&bus, HS_MAX30102_I2C_ADDRESS, &reg, 1, &rb, 1);
    printf("max30102_regs: write 0x09=0x03 ret=%d readback=0x%02x\n", ret, rb);
  }

  /* FIFO pointers: two reads one second apart show whether the part is
   * sampling.  Equal or static pointers explain missing samples. */
  {
    uint8_t wr1 = 0;
    uint8_t rd1 = 0;
    uint8_t wr2 = 0;
    uint8_t rd2 = 0;

    reg = 0x04;
    (void)hs_i2c_write_read(&bus, HS_MAX30102_I2C_ADDRESS, &reg, 1, &wr1, 1);
    reg = 0x06;
    (void)hs_i2c_write_read(&bus, HS_MAX30102_I2C_ADDRESS, &reg, 1, &rd1, 1);
    sleep(1);
    reg = 0x04;
    (void)hs_i2c_write_read(&bus, HS_MAX30102_I2C_ADDRESS, &reg, 1, &wr2, 1);
    reg = 0x06;
    (void)hs_i2c_write_read(&bus, HS_MAX30102_I2C_ADDRESS, &reg, 1, &rd2, 1);
    printf("max30102_regs: wr_ptr %02x->%02x  rd_ptr %02x->%02x\n",
           wr1, wr2, rd1, rd2);
  }

  hs_i2c_close(&bus);
  return 0;
}

int huangshan_hal_demo_main(int argc, char *argv[])
{
  const char *name;

  if (argc < 2)
    {
      hs_demo_help();
      return -EINVAL;
    }
  name = argv[1];
  if (strcmp(name, "i2c") == 0)
    {
      return hs_demo_i2c();
    }
  if (strcmp(name, "adc") == 0)
    {
      return hs_demo_adc();
    }
  if (strcmp(name, "power") == 0)
    {
      return hs_demo_power();
    }
  if (strcmp(name, "pwm") == 0)
    {
      return hs_demo_pwm();
    }
  if (strcmp(name, "vibration") == 0)
    {
      return hs_demo_vibration(argc > 2 ? argv[2] : NULL);
    }
  if (strcmp(name, "blehost") == 0)
    {
      return hs_demo_blehost(argc > 2 ? argv[2] : NULL);
    }
  if (strcmp(name, "bleevent") == 0)
    {
      return hs_demo_bleevent(argc, argv);
    }
  if (strcmp(name, "blestatus") == 0)
    {
      return hs_demo_blestatus();
    }
  if (strcmp(name, "imu") == 0)
    {
      return hs_demo_imu();
    }
  if (strcmp(name, "mic_once") == 0)
    {
      return hs_demo_mic_once();
    }
  if (strcmp(name, "mic_stream") == 0)
    {
      return hs_demo_mic_stream();
    }
  if (strcmp(name, "mic_sweep") == 0)
    {
      return hs_demo_mic_sweep();
    }
  if (strcmp(name, "mic_dbg") == 0)
    {
      (void)hs_mic_start();
      hs_mic_dump();
      return OK;
    }
  if (strcmp(name, "lcd") == 0)
    {
      return hs_demo_lcd();
    }
  if (strcmp(name, "lcdtest") == 0)
    {
      return hs_demo_lcdtest();
    }
  if (strcmp(name, "ble") == 0)
    {
      return hs_demo_ble();
    }
  if (strcmp(name, "ble_adv") == 0)
    {
      return hs_demo_ble_adv(argc > 2 ? argv[2] : "HuangshanPi");
    }
  if (strcmp(name, "sysinfo") == 0)
    {
      return hs_demo_sysinfo();
    }
  if (strcmp(name, "i2cscan") == 0)
    {
      return hs_demo_i2cscan();
    }
  if (strcmp(name, "max30102_regs") == 0)
    {
      uint32_t frequency = HS_I2C_DEFAULT_FREQUENCY;

      if (argc > 2)
        {
          frequency = (uint32_t)strtoul(argv[2], NULL, 0);
        }

      return hs_demo_max30102_regs(frequency);
    }
  if (strcmp(name, "gsr_once") == 0)
    {
      return hs_demo_gsr_once();
    }
  if (strcmp(name, "gsr_cal") == 0)
    {
      int seconds = argc > 2 ? atoi(argv[2]) : HS_GSR_DEFAULT_CAL_SECONDS;
      return hs_demo_gsr_cal(seconds);
    }
  if (strcmp(name, "gsr_stream") == 0)
    {
      char *end = NULL;
      unsigned long calibration;

      if (argc < 3)
        {
          hs_demo_help();
          return -EINVAL;
        }

      calibration = strtoul(argv[2], &end, 10);
      if (end == argv[2] || *end != '\0' || calibration > HS_GSR_RAW10_MAX)
        {
          printf("gsr_stream: CAL_RAW10 must be a decimal value in 1..1022\n");
          return -EINVAL;
        }

      return hs_demo_gsr_stream((uint16_t)calibration);
    }
  if (strcmp(name, "max30102_once") == 0)
    {
      return hs_demo_max30102_once();
    }
  if (strcmp(name, "max30102_stream") == 0)
    {
      return hs_demo_max30102_stream();
    }
  if (strcmp(name, "all") == 0)
    {
      (void)hs_demo_i2c();
      (void)hs_demo_adc();
      (void)hs_demo_pwm();
      (void)hs_demo_lcd();
      return hs_demo_ble();
    }

  hs_demo_help();
  return -EINVAL;
}
