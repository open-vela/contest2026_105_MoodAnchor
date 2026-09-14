/****************************************************************************
 * app/huangshan_hal/mood_anchor_main.c
 *
 * MoodAnchor - openvela native application (LVGL).
 *
 * This is the application form of the project: instead of drawing into the
 * framebuffer by hand, it runs on the openvela graphics framework
 * (CONFIG_GRAPHICS_LVGL + CONFIG_LV_USE_NUTTX) exactly like the official
 * packages/demos applications (mini_memo / bandx):
 *
 *   lv_init() -> lv_nuttx_dsc_init() -> lv_nuttx_init() -> lv_nuttx_uv_loop()
 *
 * The three pages show the MoodAnchor sensor set:
 *
 *   MOOD   : skin conductance (GSR / PA28 per /dev/adc1) + mood index
 *   VITALS : heart rate and SpO2 (MAX30102 on /dev/i2c1 @0x57)
 *   MOTION : accelerometer / gyroscope / temperature (LSM6DSL on /dev/i2c1)
 *
 * Sensor access goes through the existing huangshan_hal C library, so the
 * data path stays in native code.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
#include <uv.h>
#endif

#include "huangshan_hal.h"
#include "hs_ble.h"
#include "hs_ppg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MA_PAGE_MOOD       0
#define MA_PAGE_VITALS     1
#define MA_PAGE_MOTION     2
#define MA_PAGE_LINK       3
#define MA_PAGE_COUNT      4

#define MA_REFRESH_MS      500

#define MA_COLOR_BG        0x101418
#define MA_COLOR_CARD      0x1c2530
#define MA_COLOR_TEXT      0xe8eef5
#define MA_COLOR_MUTED     0x8fa3b8
#define MA_COLOR_ACCENT    0x31c48d

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_tileview;
static lv_obj_t *g_tiles[MA_PAGE_COUNT];
static lv_obj_t *g_dots[MA_PAGE_COUNT];

/* Mood page */

static lv_obj_t *g_lbl_mood_value;
static lv_obj_t *g_lbl_mood_state;
static lv_obj_t *g_lbl_gsr_mv;

/* Vitals page */

static lv_obj_t *g_lbl_hr;
static lv_obj_t *g_lbl_spo2;
static lv_obj_t *g_lbl_vitals_note;

/* Motion page */

static lv_obj_t *g_lbl_accel;
static lv_obj_t *g_lbl_gyro;
static lv_obj_t *g_lbl_temp;

/* Link page (BLE switch) */

static lv_obj_t *g_sw_ble;
static lv_obj_t *g_lbl_ble_state;
static lv_obj_t *g_lbl_ble_info;

/* Sensor state */

static struct hs_gsr_s      g_gsr;
static struct hs_imu_s      g_imu;
static struct hs_max30102_s g_max;
static struct hs_adc_s      g_batt;
static struct hs_vibration_s g_vib;
static struct hs_buttons_s  g_btn;

static bool g_gsr_open;
static bool g_imu_open;
static bool g_max_open;
static bool g_batt_open;
static bool g_vib_open;
static bool g_btn_open;

/* PPG pipeline.  The MAX30102 produces 100 samples per second and needs to
 * be drained at that rate for the beat detector to see the pulse waveform,
 * which the 2 Hz UI timer can never do.  A dedicated thread therefore owns
 * the sensor and the algorithm; the UI and the BLE data thread only read the
 * published results.
 */

#define MA_PPG_ABSENT   0       /* sensor not on the bus */
#define MA_PPG_WAITING  1       /* present, no usable pulse yet */
#define MA_PPG_OK       2       /* measuring */

static struct hs_ppg_s  g_ppg;
static volatile bool    g_ppg_run;
static volatile int     g_ppg_state = MA_PPG_ABSENT;
static volatile int     g_ppg_hr;
static volatile int     g_ppg_spo2;
static volatile int     g_ppg_quality;
static pthread_t        g_ppg_thread;

/* Latest samples */

static int32_t g_gsr_mv;
static bool    g_gsr_valid;

static uint32_t g_hr_bpm;
static uint32_t g_spo2;
static bool     g_vitals_valid;

/* Latest samples published to BLE (written by the LVGL thread, read by the
 * BLE data thread; plain volatiles are enough for these scalars).
 */

static volatile int32_t  g_ble_gsr_mv;
static volatile uint8_t  g_ble_gsr_ok;
static volatile uint8_t  g_ble_hr;
static volatile uint8_t  g_ble_spo2;
static volatile uint8_t  g_ble_vitals_ok;

static bool        g_ble_data_run;
static pthread_t   g_ble_thread;

/* BLE bring-up state machine driven by the on-screen switch */

#define MA_BLE_OFF      0
#define MA_BLE_STARTING 1
#define MA_BLE_ON       2
#define MA_BLE_FAILED   3

static volatile int g_ble_state = MA_BLE_OFF;
static pthread_t    g_ble_start_thread;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void ma_ble_start_async(void);
static void ma_ble_stop(void);

/****************************************************************************
 * Name: ma_create_card
 *
 * Description:
 *   Rounded dark card used by every page.
 *
 ****************************************************************************/

/****************************************************************************
 * Name: ma_log_silent
 *
 * Description:
 *   LVGL writes its log straight to the 1 Mbps console from the render
 *   thread.  A single line costs about a millisecond, the console port is
 *   not reentrant, and the UI visibly stutters while it is busy.  The
 *   diagnostics that matter are printed by the application itself, so drop
 *   the LVGL channel completely.
 *
 ****************************************************************************/

static void ma_log_silent(lv_log_level_t level, const char *buf)
{
  (void)level;
  (void)buf;
}

static lv_obj_t *ma_create_card(lv_obj_t *parent, lv_coord_t height)
{
  lv_obj_t *card = lv_obj_create(parent);

  lv_obj_set_size(card, LV_PCT(100), height);
  lv_obj_set_style_bg_color(card, lv_color_hex(MA_COLOR_CARD), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 18, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  return card;
}

/****************************************************************************
 * Name: ma_create_caption
 ****************************************************************************/

static lv_obj_t *ma_create_caption(lv_obj_t *parent, const char *text)
{
  lv_obj_t *lbl = lv_label_create(parent);

  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(MA_COLOR_MUTED), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

  return lbl;
}

/****************************************************************************
 * Name: ma_create_value
 ****************************************************************************/

static lv_obj_t *ma_create_value(lv_obj_t *parent, const char *text,
                                 const lv_font_t *font, uint32_t color)
{
  lv_obj_t *lbl = lv_label_create(parent);

  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(lbl, font, 0);
  lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  return lbl;
}

/****************************************************************************
 * Name: ma_create_page_header
 ****************************************************************************/

static void ma_create_page_header(lv_obj_t *tile, const char *title)
{
  lv_obj_t *lbl = lv_label_create(tile);

  lv_label_set_text(lbl, title);
  lv_obj_set_style_text_color(lbl, lv_color_hex(MA_COLOR_TEXT), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 4);
}

/****************************************************************************
 * Name: ma_build_mood_page
 ****************************************************************************/

static void ma_build_mood_page(lv_obj_t *tile)
{
  lv_obj_t *card;

  ma_create_page_header(tile, "MOOD / SKIN");

  card = ma_create_card(tile, 150);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 44);
  ma_create_caption(card, "Mood index");
  g_lbl_mood_value = ma_create_value(card, "--", &lv_font_montserrat_48,
                                     MA_COLOR_ACCENT);

  card = ma_create_card(tile, 96);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 208);
  ma_create_caption(card, "Conductance");
  g_lbl_gsr_mv = ma_create_value(card, "-- mV", &lv_font_montserrat_28,
                                 MA_COLOR_TEXT);
  g_lbl_mood_state = lv_label_create(tile);
  lv_obj_set_style_text_color(g_lbl_mood_state, lv_color_hex(MA_COLOR_MUTED),
                              0);
  lv_obj_align(g_lbl_mood_state, LV_ALIGN_TOP_MID, 0, 314);
  lv_label_set_text(g_lbl_mood_state, "GSR: waiting for data");
}

/****************************************************************************
 * Name: ma_build_vitals_page
 ****************************************************************************/

static void ma_build_vitals_page(lv_obj_t *tile)
{
  lv_obj_t *card;

  ma_create_page_header(tile, "VITALS");

  card = ma_create_card(tile, 130);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 44);
  ma_create_caption(card, "Heart rate");
  g_lbl_hr = ma_create_value(card, "-- bpm", &lv_font_montserrat_48,
                             MA_COLOR_TEXT);
  lv_obj_set_style_text_color(g_lbl_hr, lv_color_hex(0xff6b81), 0);

  card = ma_create_card(tile, 130);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 186);
  ma_create_caption(card, "Blood oxygen");
  g_lbl_spo2 = ma_create_value(card, "-- %", &lv_font_montserrat_48,
                               MA_COLOR_ACCENT);

  g_lbl_vitals_note = lv_label_create(tile);
  lv_obj_set_style_text_color(g_lbl_vitals_note, lv_color_hex(MA_COLOR_MUTED),
                              0);
  lv_obj_align(g_lbl_vitals_note, LV_ALIGN_TOP_MID, 0, 328);
  lv_label_set_text(g_lbl_vitals_note, "MAX30102 on /dev/i2c1 @0x57");
}

/****************************************************************************
 * Name: ma_build_motion_page
 ****************************************************************************/

static void ma_build_motion_page(lv_obj_t *tile)
{
  lv_obj_t *card;

  ma_create_page_header(tile, "MOTION");

  card = ma_create_card(tile, 110);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 44);
  ma_create_caption(card, "Acceleration (mg)");
  g_lbl_accel = ma_create_value(card, "--", &lv_font_montserrat_24,
                                MA_COLOR_TEXT);

  card = ma_create_card(tile, 110);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 166);
  ma_create_caption(card, "Angular rate (mdps)");
  g_lbl_gyro = ma_create_value(card, "--", &lv_font_montserrat_24,
                               MA_COLOR_TEXT);

  card = ma_create_card(tile, 90);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 288);
  ma_create_caption(card, "Temperature");
  g_lbl_temp = ma_create_value(card, "-- C", &lv_font_montserrat_24,
                               MA_COLOR_TEXT);
}

/****************************************************************************
 * Name: ma_ble_switch_event
 *
 * Description:
 *   Touch handler of the BLE switch on the LINK page.
 *
 ****************************************************************************/

static void ma_ble_switch_event(lv_event_t *event)
{
  lv_obj_t *sw = lv_event_get_target(event);

  if (lv_obj_has_state(sw, LV_STATE_CHECKED))
    {
      ma_ble_start_async();
    }
  else
    {
      ma_ble_stop();
    }
}

/****************************************************************************
 * Name: ma_refresh_ble_ui
 *
 * Description:
 *   Reflect the BLE state machine on the LINK page.  The switch itself is
 *   never written here, so the user stays in control of it.
 *
 ****************************************************************************/

static void ma_refresh_ble_ui(void)
{
  static int  last_state = -1;
  static bool last_peer;
  bool        peer;
  int         state = g_ble_state;

  if (g_lbl_ble_state == NULL)
    {
      return;
    }

  peer = hs_ble_gatt_peer_connected();

  /* lv_label_set_text_fmt() reallocates the string and invalidates the
   * widget on every call, even when the resulting text is identical.  This
   * runs 2x per second, so leave the widgets alone unless the state machine
   * really moved.
   */

  if (state == last_state && peer == last_peer)
    {
      return;
    }

  last_state = state;
  last_peer  = peer;

  switch (state)
    {
      case MA_BLE_STARTING:
        lv_label_set_text(g_lbl_ble_state, "STARTING");
        lv_obj_set_style_text_color(g_lbl_ble_state, lv_color_hex(0xffc14d),
                                    0);
        lv_label_set_text(g_lbl_ble_info, "Bringing up the host stack...");
        break;

      case MA_BLE_ON:
        lv_label_set_text(g_lbl_ble_state, "ON");
        lv_obj_set_style_text_color(g_lbl_ble_state, lv_color_hex(MA_COLOR_ACCENT),
                                    0);
        if (!peer)
          {
            lv_label_set_text_fmt(g_lbl_ble_info,
                                  "Advertising as %s\nwaiting for a phone",
                                  hs_ble_gatt_name());
          }
        else
          {
            lv_label_set_text(g_lbl_ble_info, "Phone connected");
          }
        break;

      case MA_BLE_FAILED:
        lv_label_set_text(g_lbl_ble_state, "FAILED");
        lv_obj_set_style_text_color(g_lbl_ble_state, lv_color_hex(0xff6b81),
                                    0);
        lv_label_set_text(g_lbl_ble_info,
                          "Host start failed, switch off\nand on to retry");
        break;

      default:
        lv_label_set_text(g_lbl_ble_state, "OFF");
        lv_obj_set_style_text_color(g_lbl_ble_state, lv_color_hex(MA_COLOR_MUTED),
                                    0);
        lv_label_set_text(g_lbl_ble_info,
                          "Tap the switch to advertise\nas a BLE peripheral");
        break;
    }
}

/****************************************************************************
 * Name: ma_build_link_page
 *
 * Description:
 *   Fourth page: the touch switch that turns the BLE peripheral on and off.
 *
 ****************************************************************************/

static void ma_build_link_page(lv_obj_t *tile)
{
  lv_obj_t *card;
  lv_obj_t *lbl;

  ma_create_page_header(tile, "LINK");

  card = ma_create_card(tile, 130);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 44);

  lbl = lv_label_create(card);
  lv_label_set_text(lbl, "Bluetooth");
  lv_obj_set_style_text_color(lbl, lv_color_hex(MA_COLOR_MUTED), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

  g_sw_ble = lv_switch_create(card);
  lv_obj_align(g_sw_ble, LV_ALIGN_TOP_RIGHT, 0, -4);
  lv_obj_add_event_cb(g_sw_ble, ma_ble_switch_event, LV_EVENT_VALUE_CHANGED,
                      NULL);

  g_lbl_ble_state = lv_label_create(card);
  lv_label_set_text(g_lbl_ble_state, "OFF");
  lv_obj_set_style_text_font(g_lbl_ble_state, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(g_lbl_ble_state, lv_color_hex(MA_COLOR_MUTED),
                              0);
  lv_obj_align(g_lbl_ble_state, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  g_lbl_ble_info = lv_label_create(tile);
  lv_obj_set_style_text_color(g_lbl_ble_info, lv_color_hex(MA_COLOR_MUTED), 0);
  lv_obj_set_style_text_align(g_lbl_ble_info, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(g_lbl_ble_info, LV_PCT(90));
  lv_obj_align(g_lbl_ble_info, LV_ALIGN_TOP_MID, 0, 190);
  lv_label_set_text(g_lbl_ble_info,
                    "Tap the switch to advertise\nas a BLE peripheral");
}

/****************************************************************************
 * Name: ma_build_nav
 ****************************************************************************/

static void ma_build_nav(lv_obj_t *parent)
{
  lv_obj_t *bar;
  int i;

  bar = lv_obj_create(parent);
  lv_obj_set_size(bar, LV_PCT(100), 26);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(bar, 10, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  for (i = 0; i < MA_PAGE_COUNT; i++)
    {
      g_dots[i] = lv_obj_create(bar);
      lv_obj_set_size(g_dots[i], 8, 8);
      lv_obj_set_style_radius(g_dots[i], LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(g_dots[i], 0, 0);
      lv_obj_set_style_bg_color(g_dots[i],
                                lv_color_hex(i == 0 ? MA_COLOR_ACCENT
                                                    : MA_COLOR_MUTED), 0);
    }
}

/****************************************************************************
 * Name: ma_update_dots
 ****************************************************************************/

static void ma_update_dots(uint32_t active)
{
  uint32_t i;

  for (i = 0; i < MA_PAGE_COUNT; i++)
    {
      lv_obj_set_style_bg_color(g_dots[i],
                                lv_color_hex(i == active ? MA_COLOR_ACCENT
                                                         : MA_COLOR_MUTED),
                                0);
    }
}

/****************************************************************************
 * Name: ma_tileview_event
 ****************************************************************************/

static void ma_tileview_event(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);

  if (code == LV_EVENT_VALUE_CHANGED)
    {
      lv_obj_t *tile = lv_tileview_get_tile_active(g_tileview);
      uint32_t i;

      for (i = 0; i < MA_PAGE_COUNT; i++)
        {
          if (g_tiles[i] == tile)
            {
              ma_update_dots(i);
              break;
            }
        }
    }
}

/****************************************************************************
 * Name: ma_read_gsr
 ****************************************************************************/

static void ma_read_gsr(void)
{
  static int32_t  last_mv   = -1;
  static int32_t  last_mood = -1;
  static uint32_t last_raw  = 0xffffffffu;
  struct hs_gsr_sample_s sample;
  int32_t mood;

  if (!g_gsr_open)
    {
      if (hs_gsr_open(&g_gsr) < 0)
        {
          lv_label_set_text(g_lbl_mood_state, "GSR: /dev/adc1 not available");
          return;
        }

      g_gsr_open = true;
    }

  if (hs_gsr_read(&g_gsr, &sample) < 0)
    {
      lv_label_set_text(g_lbl_mood_state, "GSR: read error");
      return;
    }

  g_gsr_mv = sample.adc_mv;
  g_gsr_valid = true;

  g_ble_gsr_mv = sample.adc_mv;
  g_ble_gsr_ok = 1;

  /* Very simple mapping: the Grove GSR output sits near mid scale when the
   * electrodes are relaxed and rises with arousal.  This is a display
   * helper, the real calibration lives in the GSR HAL.
   */

  mood = 100 - ((g_gsr_mv - 200) * 100 / 800);

  if (mood < 0)
    {
      mood = 0;
    }
  else if (mood > 100)
    {
      mood = 100;
    }

  /* A resting GSR trace repeats the same reading for many consecutive
   * samples and lv_label_set_text_fmt() reallocates + invalidates on every
   * call, so skip the update when nothing moved.
   */

  if (g_gsr_mv == last_mv && mood == last_mood && sample.raw10 == last_raw)
    {
      return;
    }

  last_mv   = g_gsr_mv;
  last_mood = mood;
  last_raw  = sample.raw10;

  lv_label_set_text_fmt(g_lbl_gsr_mv, "%" PRId32 " mV", g_gsr_mv);
  lv_label_set_text_fmt(g_lbl_mood_value, "%" PRId32, mood);
  lv_label_set_text_fmt(g_lbl_mood_state, "GSR %" PRId32 " mV  (raw %u)",
                        g_gsr_mv, sample.raw10);
}

/****************************************************************************
 * Name: ma_read_vitals
 ****************************************************************************/

static void ma_read_vitals(void)
{
  static int last_state = -1;
  static int last_hr;
  static int last_spo2;
  static int last_quality;
  int        state   = g_ppg_state;
  int        hr      = g_ppg_hr;
  int        spo2    = g_ppg_spo2;
  int        quality = g_ppg_quality;

  /* The labels are only there to mirror the pipeline state; the numbers
   * themselves are computed by ma_ppg_thread().
   */

  if (state == last_state && hr == last_hr && spo2 == last_spo2 &&
      quality == last_quality)
    {
      return;
    }

  last_state   = state;
  last_hr      = hr;
  last_spo2    = spo2;
  last_quality = quality;

  if (state == MA_PPG_ABSENT)
    {
      lv_label_set_text(g_lbl_hr, "-- bpm");
      lv_label_set_text(g_lbl_spo2, "-- %");
      lv_label_set_text(g_lbl_vitals_note,
                        "MAX30102: not detected on /dev/i2c1");
      return;
    }

  g_hr_bpm = (uint32_t)hr;
  g_spo2   = (uint32_t)spo2;
  g_vitals_valid = (state == MA_PPG_OK);

  if (state != MA_PPG_OK)
    {
      lv_label_set_text(g_lbl_hr, "-- bpm");
      lv_label_set_text(g_lbl_spo2, "-- %");
      lv_label_set_text(g_lbl_vitals_note,
                        "Place a finger on the sensor");
      return;
    }

  lv_label_set_text_fmt(g_lbl_hr, "%d bpm", hr);
  lv_label_set_text_fmt(g_lbl_spo2, "%d %%", spo2);
  lv_label_set_text_fmt(g_lbl_vitals_note, "pulse signal %d %%", quality);
}

/****************************************************************************
 * Name: ma_read_motion
 ****************************************************************************/

static void ma_read_motion(void)
{
  static uint32_t imu_tick;
  struct hs_imu_sample_s sample;

  if (!g_imu_open)
    {
      if (hs_imu_open(&g_imu) < 0)
        {
          lv_label_set_text(g_lbl_accel, "no IMU");
          return;
        }

      g_imu_open = true;
    }

  if (hs_imu_read(&g_imu, &sample) < 0)
    {
      lv_label_set_text(g_lbl_accel, "read error");
      return;
    }

  /* The raw IMU values change on every sample, so redrawing them at the full
   * refresh rate is pure waste.  Show every other sample (about 1 Hz) which
   * is more than enough for a motion readout.
   */

  if ((imu_tick++ & 1u) != 0)
    {
      return;
    }

  lv_label_set_text_fmt(g_lbl_accel, "X %d  Y %d  Z %d", sample.accel_x_mg,
                        sample.accel_y_mg, sample.accel_z_mg);
  lv_label_set_text_fmt(g_lbl_gyro, "X %d  Y %d  Z %d", sample.gyro_x_mdps,
                        sample.gyro_y_mdps, sample.gyro_z_mdps);
  lv_label_set_text_fmt(g_lbl_temp, "%d.%d C", sample.temperature_c / 10,
                        sample.temperature_c % 10);
}

/****************************************************************************
 * Name: ma_refresh_timer
 *
 * Description:
 *   Runs inside the LVGL thread, so it may touch the widgets directly.
 *
 ****************************************************************************/

static void ma_refresh_timer(lv_timer_t *timer)
{
  lv_obj_t *tile = lv_tileview_get_tile_active(g_tileview);

  (void)timer;

  ma_refresh_ble_ui();

  if (tile == NULL || tile == g_tiles[MA_PAGE_MOOD])
    {
      ma_read_gsr();
    }
  else if (tile == g_tiles[MA_PAGE_VITALS])
    {
      ma_read_vitals();
    }
  else
    {
      ma_read_motion();
    }
}

/****************************************************************************
 * Name: ma_ppg_thread
 *
 * Description:
 *   Drain the MAX30102 FIFO and run the PPG analysis.  The sensor produces
 *   100 samples per second, so the loop polls it every few milliseconds and
 *   pushes every sample it finds into the tracker; the results are published
 *   at a much lower rate, which is all a label or a BLE notification needs.
 *
 ****************************************************************************/

static FAR void *ma_ppg_thread(FAR void *arg)
{
  struct hs_max30102_sample_s sample;
  uint32_t published = 0;

  (void)arg;

  while (g_ppg_run)
    {
      int drained = 0;
      int ret;

      if (!g_max_open)
        {
          if (hs_max30102_open(&g_max, HS_MAX30102_I2C_BUS) < 0)
            {
              g_ppg_state = MA_PPG_ABSENT;
              sleep(5);
              continue;
            }

          g_max_open  = true;
          g_ppg_state = MA_PPG_WAITING;
          hs_ppg_reset(&g_ppg);
          published   = 0;
        }

      /* Take everything the FIFO has to offer, but never more than a few
       * samples per pass so a burst cannot lock the thread up.
       */

      while (g_ppg_run && drained < 4)
        {
          ret = hs_max30102_read_sample(&g_max, &sample);

          if (ret == -EAGAIN)
            {
              break;                    /* FIFO empty: the normal idle case */
            }

          if (ret < 0)
            {
              /* I2C trouble: drop the handle and rebuild it from scratch
               * rather than spinning on a dead bus.
               */

              hs_max30102_close(&g_max);
              g_max_open  = false;
              g_ppg_state = MA_PPG_ABSENT;
              break;
            }

          hs_ppg_push(&g_ppg, sample.timestamp_ms, sample.red, sample.ir);
          drained++;
        }

      /* Publish about twice per second: the UI timer runs at 2 Hz, so a
       * faster rate would only churn the labels.
       */

      if (g_ppg.samples - published >= HS_PPG_SAMPLE_HZ / 2)
        {
          int hr = hs_ppg_heart_rate(&g_ppg);

          published = g_ppg.samples;

          if (hr > 0)
            {
              g_ppg_hr      = hr;
              g_ppg_spo2    = hs_ppg_spo2(&g_ppg);
              g_ppg_quality = hs_ppg_quality(&g_ppg);
              g_ppg_state   = MA_PPG_OK;

              /* Feed the BLE characteristics from here so a phone keeps
               * receiving vitals while the VITALS page is not shown.
               */

              g_ble_hr        = (uint8_t)hr;
              g_ble_spo2      = (uint8_t)g_ppg_spo2;
              g_ble_vitals_ok = 1;
            }
          else if (g_ppg.samples > HS_PPG_SAMPLE_HZ * 3)
            {
              /* Long enough for a finger to be placed: report "no signal"
               * instead of holding on to a stale reading.
               */

              g_ppg_hr        = 0;
              g_ppg_spo2      = 0;
              g_ppg_quality   = 0;
              g_ppg_state     = MA_PPG_WAITING;
              g_ble_vitals_ok = 0;
            }
        }

      usleep(5000);
    }

  if (g_max_open)
    {
      hs_max30102_close(&g_max);
      g_max_open = false;
    }

  return NULL;
}

/****************************************************************************
 * Name: ma_ppg_start
 *
 * Description:
 *   Spawn the PPG worker.  It runs at the default priority on purpose: the
 *   sensor polling is light and must never starve the render thread.
 *
 ****************************************************************************/

static void ma_ppg_start(void)
{
  pthread_attr_t attr;

  g_ppg_run = true;

  if (pthread_attr_init(&attr) != 0)
    {
      return;
    }

  pthread_attr_setstacksize(&attr, 4096);

  if (pthread_create(&g_ppg_thread, &attr, ma_ppg_thread, NULL) == 0)
    {
      pthread_detach(g_ppg_thread);
    }

  pthread_attr_destroy(&attr);
}

/****************************************************************************
 * Name: ma_ble_data_thread
 *
 * Description:
 *   Publish the latest samples on the BLE data characteristic once per
 *   second.  It deliberately does not touch the sensors (or LVGL): the UI
 *   thread owns the hardware, this thread only forwards the scalars.
 *
 ****************************************************************************/

static FAR void *ma_ble_data_thread(FAR void *arg)
{
  (void)arg;

  while (g_ble_data_run)
    {
      uint8_t  flags  = 0;
      uint8_t  sflags = 0;
      int16_t  accel[3] = { 0, 0, 0 };
      uint8_t  battery = HS_BLE_STATUS_BAT_UNKNOWN;
      uint8_t  buttons = 0;
      bool     vib     = false;

      if (g_ble_gsr_ok)
        {
          flags  |= HS_BLE_DATA_GSR_VALID;
          sflags |= HS_BLE_STATUS_GSR_VALID;
        }

      if (g_ble_vitals_ok)
        {
          flags  |= HS_BLE_DATA_HR_VALID | HS_BLE_DATA_SPO2_VALID;
          sflags |= HS_BLE_STATUS_HR_VALID | HS_BLE_STATUS_SPO2_VALID;
        }

      /* Motion: read the IMU on demand (the sensor is also used by the
       * MOTION page, so failures are simply reported as invalid).
       */

      if (g_imu_open || hs_imu_open(&g_imu) >= 0)
        {
          struct hs_imu_sample_s sample;

          g_imu_open = true;

          if (hs_imu_read(&g_imu, &sample) >= 0)
            {
              accel[0] = sample.accel_x_mg;
              accel[1] = sample.accel_y_mg;
              accel[2] = sample.accel_z_mg;
              sflags  |= HS_BLE_STATUS_IMU_VALID;
            }
        }

      /* Battery: VBAT ADC channel, only reported as a coarse percentage. */

      if (g_batt_open || hs_adc_open(&g_batt, HS_ADC_DEVICE) >= 0)
        {
          int32_t mv = 0;

          g_batt_open = true;

          if (hs_adc_read(&g_batt, HS_ADC_VBAT_CHANNEL, &mv) >= 0 && mv > 0)
            {
              /* 3.3 V .. 4.2 V mapped to 0..100 % */

              int32_t pct = (mv - 3300) * 100 / 900;

              if (pct < 0)   { pct = 0; }
              if (pct > 100) { pct = 100; }

              battery  = (uint8_t)pct;
              sflags  |= HS_BLE_STATUS_BAT_VALID;
            }
        }

      vib = false;
      if (g_vib_open || hs_vibration_open(&g_vib) >= 0)
        {
          g_vib_open = true;
          vib        = hs_vibration_is_enabled(&g_vib);
        }

      if (vib)
        {
          sflags |= HS_BLE_STATUS_VIB_ON;
        }

      /* Buttons: bit map of the currently pressed keys (0 = none). */

      if (g_btn_open || hs_buttons_open(&g_btn, HS_BUTTONS_DEVICE) >= 0)
        {
          uint32_t state = 0;

          g_btn_open = true;

          if (hs_buttons_read(&g_btn, &state) >= 0)
            {
              buttons  = (uint8_t)(state & 0xff);
              sflags  |= HS_BLE_STATUS_BTN_VALID;
            }
        }

      hs_ble_data_notify((uint16_t)g_ble_gsr_mv, g_ble_hr, g_ble_spo2,
                         flags);
      hs_ble_status_notify((uint16_t)g_ble_gsr_mv, g_ble_hr, g_ble_spo2,
                           (sflags & HS_BLE_STATUS_IMU_VALID) ? accel : NULL,
                           battery, buttons, vib, sflags);

      /* The controller stops advertising as soon as a phone connects and
       * never resumes it by itself: re-arm it after a disconnect.
       */

      hs_ble_adv_service();

      sleep(1);
    }

  return NULL;
}

/****************************************************************************
 * Name: ma_ble_start_worker
 *
 * Description:
 *   Bring up the NuttX Bluetooth host on the SiFli controller and install
 *   the GATT database.  This runs on its own thread because the HCI
 *   handshake can block for seconds, which must never freeze the UI.
 *
 ****************************************************************************/

static FAR void *ma_ble_start_worker(FAR void *arg)
{
  pthread_attr_t attr;
  int ret;

  (void)arg;

  ret = hs_ble_host_start();
  if (ret >= 0)
    {
      ret = hs_ble_gatt_start(NULL);
    }

  if (ret < 0)
    {
      printf("MoodAnchor: BLE start failed (%d)\n", ret);
      g_ble_state = MA_BLE_FAILED;
      return NULL;
    }

  g_ble_data_run = true;

  if (pthread_attr_init(&attr) == 0)
    {
      pthread_attr_setstacksize(&attr, 4096);
      if (pthread_create(&g_ble_thread, &attr, ma_ble_data_thread, NULL) == 0)
        {
          pthread_detach(g_ble_thread);
        }

      pthread_attr_destroy(&attr);
    }

  g_ble_state = MA_BLE_ON;
  return NULL;
}

/****************************************************************************
 * Name: ma_ble_start_async
 ****************************************************************************/

static void ma_ble_start_async(void)
{
  pthread_attr_t attr;
  struct sched_param param;

  if (g_ble_state == MA_BLE_STARTING || g_ble_state == MA_BLE_ON)
    {
      return;
    }

  g_ble_state = MA_BLE_STARTING;

  if (pthread_attr_init(&attr) != 0)
    {
      g_ble_state = MA_BLE_FAILED;
      return;
    }

  pthread_attr_setstacksize(&attr, 8192);

  /* The BLE host bring-up contains synchronous commands with a 2.5 s
   * timeout.  At the same priority as the LVGL render loop the worker gets
   * starved and the sync commands time out.  Run the worker above the UI
   * priority (FIFO is fine: it sleeps in nxsem_tickwait while waiting for
   * controller responses, so the UI keeps running).
   */

  pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
  param.sched_priority = 120;
  pthread_attr_setschedparam(&attr, &param);

  if (pthread_create(&g_ble_start_thread, &attr, ma_ble_start_worker,
                     NULL) != 0)
    {
      g_ble_state = MA_BLE_FAILED;
    }
  else
    {
      pthread_detach(g_ble_start_thread);
    }

  pthread_attr_destroy(&attr);
}

/****************************************************************************
 * Name: ma_ble_stop
 *
 * Description:
 *   Stop advertising and the sample stream but keep the host stack up, so
 *   flipping the switch back on is immediate.
 *
 ****************************************************************************/

static void ma_ble_stop(void)
{
  g_ble_data_run = false;

  if (hs_ble_gatt_ready())
    {
      hs_ble_adv_enable(false);
    }

  g_ble_state = MA_BLE_OFF;
}

/****************************************************************************
 * Name: lv_nuttx_uv_loop
 *
 * Description:
 *   Drive LVGL from a libuv event loop.  The openvela LVGL port does not
 *   export lv_nuttx_uv_loop(), so each application provides it, exactly like
 *   the official lvgldemo and packages/demos/mini_memo demos.
 *
 ****************************************************************************/

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
static void lv_nuttx_uv_loop(uv_loop_t *loop, lv_nuttx_result_t *result)
{
  lv_nuttx_uv_t uv_info;
  void *data;

  uv_loop_init(loop);

  lv_memset(&uv_info, 0, sizeof(uv_info));
  uv_info.loop = loop;
  uv_info.disp = result->disp;
  uv_info.indev = result->indev;
#ifdef CONFIG_UINPUT_TOUCH
  uv_info.uindev = result->utouch_indev;
#endif

  data = lv_nuttx_uv_init(&uv_info);
  uv_run(loop, UV_RUN_DEFAULT);
  lv_nuttx_uv_deinit(&data);
}
#else
static void lv_nuttx_loop(void)
{
  for (; ; )
    {
      uint32_t idle = lv_timer_handler();

      usleep((idle ? idle : 1) * 1000);
    }
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 *
 * Description:
 *   MoodAnchor application entry point (NSH command: mood_anchor).
 *
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  lv_obj_t *screen;
  int i;

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
  uv_loop_t ui_loop;

  lv_memzero(&ui_loop, sizeof(ui_loop));
#endif

  (void)argc;
  (void)argv;

  lv_init();

  lv_nuttx_dsc_init(&info);
  lv_nuttx_init(&info, &result);

  /* The NuttX port installs its own syslog callback from inside
   * lv_nuttx_init(), so the channel can only be silenced after that call.
   * LVGL logs from the render thread straight to the 1 Mbps console and
   * every line costs roughly a millisecond there; the application prints its
   * own state changes instead.
   */

  lv_log_register_print_cb(ma_log_silent);

  if (result.disp == NULL)
    {
      LV_LOG_ERROR("MoodAnchor: display initialization failed");
      return 1;
    }

  screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(MA_COLOR_BG), 0);

  g_tileview = lv_tileview_create(screen);
  lv_obj_set_size(g_tileview, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(g_tileview, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(g_tileview, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(g_tileview, ma_tileview_event, LV_EVENT_VALUE_CHANGED,
                      NULL);

  for (i = 0; i < MA_PAGE_COUNT; i++)
    {
      g_tiles[i] = lv_tileview_add_tile(g_tileview, i, 0, LV_DIR_HOR);
      lv_obj_set_style_bg_color(g_tiles[i], lv_color_hex(MA_COLOR_BG), 0);
      lv_obj_set_style_border_width(g_tiles[i], 0, 0);
    }

  ma_build_mood_page(g_tiles[MA_PAGE_MOOD]);
  ma_build_vitals_page(g_tiles[MA_PAGE_VITALS]);
  ma_build_motion_page(g_tiles[MA_PAGE_MOTION]);
  ma_build_link_page(g_tiles[MA_PAGE_LINK]);
  ma_build_nav(screen);

  lv_timer_create(ma_refresh_timer, MA_REFRESH_MS, NULL);
  ma_refresh_timer(NULL);

  /* Start the PPG pipeline first: the beat detector needs a few seconds of
   * samples before it can report anything, and the panel should already be
   * drawing by then.
   */

  ma_ppg_start();

  /* Bring the Bluetooth peripheral up automatically so the watch is
   * discoverable right after power-up: the LINK page switch then only acts
   * as a manual off/on control.  This also keeps the device usable when the
   * panel is not lit (the phone can still connect and read the status
   * characteristic).
   */

  ma_ble_start_async();

  printf("MoodAnchor UI running (swipe for VITALS / MOTION / LINK)\n");

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
  lv_nuttx_uv_loop(&ui_loop, &result);
#else
  lv_nuttx_loop();
#endif

  return 0;
}
