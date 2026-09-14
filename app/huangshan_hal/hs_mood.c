/****************************************************************************
 * app/huangshan_hal/hs_mood.c
 *
 * Multi-sensor arousal fusion.  See hs_mood.h for why it is rule based.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hs_mood.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Smoothed per-sensor scores.  These are what make "sustained" mean
 * something: an instantaneous spike is diluted by the filter, so the score
 * only climbs when the condition keeps being true.
 */

static int g_imu_smooth;
static int g_mic_smooth;

/* Remaining updates for which arousal stays reported once it has been
 * latched.  Counts down, so it doubles as the debounce timer.
 */

static int g_hold;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int mood_clamp100(int value)
{
  if (value < 0)
    {
      return 0;
    }

  return value > 100 ? 100 : value;
}

/****************************************************************************
 * Name: mood_isqrt
 *
 * Description:
 *   Integer square root by Newton's method.  The accelerometer magnitude is
 *   wanted only to pick a point on a 0..100 scale, so an exact root is
 *   plenty and libm stays out of the image.
 *
 ****************************************************************************/

static int32_t mood_isqrt(int32_t value)
{
  int32_t x;
  int32_t y;

  if (value <= 0)
    {
      return 0;
    }

  x = value;
  y = (x + 1) / 2;

  while (y < x)
    {
      x = y;
      y = (x + value / x) / 2;
    }

  return x;
}

/****************************************************************************
 * Name: mood_imu_score
 *
 * Description:
 *   How violently the watch is being moved.
 *
 *   The acceleration magnitude is compared against 1 g rather than against
 *   each axis separately, so the reading does not depend on which way up the
 *   watch happens to be.  The absolute deviation is used because a drop -
 *   which removes g for a moment - is just as much motion as a shake.
 *
 *   Rotation is added on top, scaled so that a tenth of a degree per second
 *   counts the same as a milli-g of deviation.
 *
 ****************************************************************************/

static int mood_imu_score(const struct hs_mood_input_s *in, int *gyro_mag)
{
  int32_t ax = in->accel_mg[0];
  int32_t ay = in->accel_mg[1];
  int32_t az = in->accel_mg[2];
  int32_t gx = in->gyro_dps10[0];
  int32_t gy = in->gyro_dps10[1];
  int32_t gz = in->gyro_dps10[2];
  int32_t deviation;
  int32_t rotation;
  int32_t activity;

  deviation = mood_isqrt(ax * ax + ay * ay + az * az) - 1000;

  if (deviation < 0)
    {
      deviation = -deviation;
    }

  rotation = mood_isqrt(gx * gx + gy * gy + gz * gz);

  if (gyro_mag != NULL)
    {
      *gyro_mag = (int)rotation;
    }

  activity = deviation + rotation;

  return mood_clamp100((int)(activity * 100 / HS_MOOD_IMU_FULL));
}

/****************************************************************************
 * Name: mood_mic_score
 *
 * Description:
 *   How loud it is, on the microphone's own relative 0..100 scale.  Levels
 *   below the floor count as nothing at all, so room noise never contributes
 *   to the fusion.
 *
 ****************************************************************************/

static int mood_mic_score(const struct hs_mood_input_s *in)
{
  if (in->mic_level <= HS_MOOD_MIC_FLOOR)
    {
      return 0;
    }

  return mood_clamp100((in->mic_level - HS_MOOD_MIC_FLOOR) * 100 /
                       (HS_MOOD_MIC_FULL - HS_MOOD_MIC_FLOOR));
}

/****************************************************************************
 * Name: mood_gsr_score
 *
 * Description:
 *   How far the skin conductance has moved above its resting baseline.
 *
 *   The sensor drives the electrodes with a constant current, so a rise in
 *   conductance shows up as a *fall* in voltage; the caller has already
 *   turned that round, and a positive delta here means "more aroused".
 *   Only that direction counts - a hand that has been resting and is now
 *   drying out is not an emotional event.
 *
 *   Two bands count as full scale, which leaves room for the band the user
 *   adjusts on screen to sit in the middle of the range.
 *
 ****************************************************************************/

static int mood_gsr_score(const struct hs_mood_input_s *in)
{
  int32_t band;

  if (!in->gsr_ready || in->gsr_delta <= 0)
    {
      return 0;
    }

  band = in->gsr_band > 0 ? in->gsr_band : 1;

  return mood_clamp100((int)(in->gsr_delta * 100 / (band * 2)));
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void hs_mood_init(void)
{
  g_imu_smooth = 0;
  g_mic_smooth = 0;
  g_hold       = 0;
}

void hs_mood_update(const struct hs_mood_input_s *in,
                    struct hs_mood_result_s *out)
{
  int instant;
  int fused;
  int agreeing;

  if (in == NULL || out == NULL)
    {
      return;
    }

  /* IMU and microphone are smoothed as leaky integrators over a few seconds.
   * The skin conductance is not: the hardware and the module itself already
   * have time constants far longer than this loop, so filtering it again
   * would only add lag.
   */

  instant = mood_imu_score(in, &out->gyro_mag_dps10);
  g_imu_smooth += (instant - g_imu_smooth) / 3;

  instant = mood_mic_score(in);
  g_mic_smooth += (instant - g_mic_smooth) / 2;

  out->imu_score = g_imu_smooth;
  out->mic_score = g_mic_smooth;
  out->gsr_score = mood_gsr_score(in);
  out->gsr_ready = in->gsr_ready;

  out->imu_positive = out->imu_score >= HS_MOOD_POSITIVE;
  out->mic_positive = out->mic_score >= HS_MOOD_POSITIVE;
  out->gsr_positive = out->gsr_score >= HS_MOOD_POSITIVE;

  agreeing = (out->imu_positive ? 1 : 0) + (out->mic_positive ? 1 : 0) +
             (out->gsr_positive ? 1 : 0);

  out->agreeing = agreeing;

  fused = (out->gsr_score * HS_MOOD_W_GSR + out->imu_score * HS_MOOD_W_IMU +
           out->mic_score * HS_MOOD_W_MIC) / 100;

  out->confidence = fused;

  /* Two conditions, not one.  The fused figure says "this is intense"; the
   * agreement count says "more than one kind of sensor noticed".  Requiring
   * both is what stops a single over-eager sensor from raising the flag, and
   * it is the whole point of fusing.
   */

  if (fused >= HS_MOOD_FUSED_THRESHOLD && agreeing >= HS_MOOD_MIN_AGREEING)
    {
      g_hold = HS_MOOD_HOLD_UPDATES;
    }
  else if (g_hold > 0)
    {
      g_hold--;
    }

  out->agitated = g_hold > 0;
}
