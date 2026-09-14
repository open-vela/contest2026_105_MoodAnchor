/****************************************************************************
 * app/huangshan_hal/hs_ppg.h
 *
 * Photoplethysmography (PPG) processing for the MAX30102.
 *
 * The sensor hands over raw RED/IR light levels.  Turning those into a heart
 * rate and an SpO2 estimate is a signal-processing job that has nothing to do
 * with the I2C transport, so it lives in its own module:
 *
 *   hs_max30102_read_sample()  ->  hs_ppg_push()  ->  hs_ppg_heart_rate()
 *                                                  ->  hs_ppg_spo2()
 *
 * The tracker is fully streaming: it keeps a handful of scalars and the last
 * few beat-to-beat intervals, never a sample buffer, so it costs a couple of
 * hundred bytes and works at any call rate.
 *
 ****************************************************************************/

#ifndef __APP_HUANGSHAN_HAL_HS_PPG_H
#define __APP_HUANGSHAN_HAL_HS_PPG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The MAX30102 is programmed for 100 samples/s in SpO2 mode (RED + IR), so
 * push() is expected to be called at that rate.  The algorithm itself does
 * not depend on it: every interval is measured from sample timestamps.
 */

#define HS_PPG_SAMPLE_HZ      100

/* Number of beat-to-beat intervals kept for the median.  Eight intervals is
 * about six seconds of signal at rest: long enough to sit through the odd
 * motion artefact, short enough to follow a real change in rate.
 */

#define HS_PPG_RR_COUNT       8

/* Time constant of the DC and AC trackers, in samples.  At 100 Hz this is
 * 1.5 s: slow enough to ignore the pulsatile component, fast enough to
 * settle within a second or two of the finger being placed.
 */

#define HS_PPG_DC_TAU         150

/* A pulse is only reported when the AC component modulates by at least this
 * many ADC counts.  The full scale is 18 bits (262143), so this is a low but
 * meaningful threshold: below it there is no finger, no perfusion, or too
 * much ambient light, and any number produced would be fiction.
 */

#define HS_PPG_MIN_SPAN       512

/* Plausible beat-to-beat interval window, in milliseconds.  This is 30 to
 * 200 beats per minute; anything outside is an artefact and is discarded.
 */

#define HS_PPG_RR_MIN_MS      300
#define HS_PPG_RR_MAX_MS      2000

/* At least this many accepted intervals are needed before a rate is
 * published: the median needs a few samples to be meaningful.
 */

#define HS_PPG_RR_MIN_COUNT   3

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct hs_ppg_s
{
  /* DC level of each channel, in raw ADC counts */

  int32_t  dc_ir;
  int32_t  dc_red;

  /* Mean absolute AC magnitude of each channel.  For a roughly sinusoidal
   * pulse this is a fixed fraction of the amplitude, which is all the SpO2
   * ratio needs.
   */

  int32_t  ac_ir;
  int32_t  ac_red;

  /* Envelope tracker driving the beat detector */

  int32_t  peak_hi;
  int32_t  peak_lo;
  uint32_t peak_hi_ms;            /* timestamp of the current peak */
  bool     above;                 /* AC is above the upper threshold */

  /* Beat-to-beat intervals in milliseconds, used as a ring buffer */

  uint16_t rr[HS_PPG_RR_COUNT];
  int      rr_count;
  int      rr_index;
  uint32_t last_beat_ms;

  uint32_t samples;               /* number of samples pushed */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: hs_ppg_reset
 *
 * Description:
 *   Clear the tracker.  Call it when the signal is known to be invalid
 *   (sensor reopened, finger removed) so stale intervals cannot be mixed
 *   with fresh ones.
 *
 ****************************************************************************/

void hs_ppg_reset(struct hs_ppg_s *ppg);

/****************************************************************************
 * Name: hs_ppg_push
 *
 * Description:
 *   Feed one RED/IR sample taken at ts_ms (a monotonic millisecond clock).
 *
 ****************************************************************************/

void hs_ppg_push(struct hs_ppg_s *ppg, uint32_t ts_ms, uint32_t red,
                 uint32_t ir);

/****************************************************************************
 * Name: hs_ppg_heart_rate
 *
 * Description:
 *   Return the median heart rate in beats per minute, or 0 while no
 *   reliable measurement is available yet.
 *
 ****************************************************************************/

int hs_ppg_heart_rate(const struct hs_ppg_s *ppg);

/****************************************************************************
 * Name: hs_ppg_spo2
 *
 * Description:
 *   Return the estimated blood oxygen saturation in percent, or 0 while no
 *   reliable measurement is available yet.
 *
 ****************************************************************************/

int hs_ppg_spo2(const struct hs_ppg_s *ppg);

/****************************************************************************
 * Name: hs_ppg_quality
 *
 * Description:
 *   Return a 0..100 confidence figure derived from the spread of the
 *   beat-to-beat intervals.  A steady rhythm scores high, a jittery one low.
 *
 ****************************************************************************/

int hs_ppg_quality(const struct hs_ppg_s *ppg);

#endif /* __APP_HUANGSHAN_HAL_HS_PPG_H */
