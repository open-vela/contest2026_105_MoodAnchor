/****************************************************************************
 * app/huangshan_hal/hs_ppg.c
 *
 * Streaming PPG analysis for the MAX30102: DC removal, envelope tracking,
 * beat detection, heart rate and SpO2.
 *
 * The approach is deliberately small enough to run on the target without a
 * floating point unit in the hot path:
 *
 *   1. the DC level of each channel follows a one-pole low pass
 *   2. what is left after subtracting it is the pulsatile component
 *   3. an envelope tracker over that component yields two thresholds
 *   4. a beat is a rise through the upper threshold followed by a fall
 *      through the lower one, timed at the peak
 *   5. the heart rate is the median of the recent beat-to-beat intervals,
 *      which throws away a missed or doubled beat
 *   6. SpO2 comes from the classic R ratio of normalised AC amplitudes
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <string.h>

#include "hs_ppg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The envelope decays by this fraction of its own spread per sample.  At
 * 100 Hz a value of 2000 gives a time constant of roughly 20 seconds, which
 * is slow enough not to chase the pulse itself but quick enough to follow a
 * finger being pressed harder or lifted.
 */

#define HS_PPG_ENV_DECAY      2000

/* Fraction of the envelope spread used for the two detection thresholds.
 * The gap between them is the hysteresis that stops noise around a single
 * level from being counted as a string of beats.
 */

#define HS_PPG_TH_HI_NUM      3
#define HS_PPG_TH_HI_DEN      5
#define HS_PPG_TH_LO_NUM      2
#define HS_PPG_TH_LO_DEN      5

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int32_t hs_ppg_abs(int32_t value)
{
  return value < 0 ? -value : value;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void hs_ppg_reset(struct hs_ppg_s *ppg)
{
  if (ppg == NULL)
    {
      return;
    }

  memset(ppg, 0, sizeof(*ppg));
}

void hs_ppg_push(struct hs_ppg_s *ppg, uint32_t ts_ms, uint32_t red,
                 uint32_t ir)
{
  int32_t ac_ir;
  int32_t ac_red;
  int32_t span;
  int32_t hi_th;
  int32_t lo_th;

  if (ppg == NULL)
    {
      return;
    }

  ppg->samples++;

  /* 1. DC tracking.  The raw sample is at most 18 bits, so the integer
   *    arithmetic here cannot overflow even with the transient of a full
   *    scale swing.
   */

  if (ppg->dc_ir == 0 && ppg->dc_red == 0)
    {
      ppg->dc_ir  = (int32_t)ir;
      ppg->dc_red = (int32_t)red;
    }
  else
    {
      ppg->dc_ir  += ((int32_t)ir  - ppg->dc_ir)  / HS_PPG_DC_TAU;
      ppg->dc_red += ((int32_t)red - ppg->dc_red) / HS_PPG_DC_TAU;
    }

  /* 2. Pulsatile component and its magnitude estimate.  Tracking the mean
   *    absolute value instead of a true RMS avoids a square root and
   *    divides out cleanly in the SpO2 ratio.
   */

  ac_ir  = (int32_t)ir  - ppg->dc_ir;
  ac_red = (int32_t)red - ppg->dc_red;

  ppg->ac_ir  += (hs_ppg_abs(ac_ir)  - ppg->ac_ir)  / HS_PPG_DC_TAU;
  ppg->ac_red += (hs_ppg_abs(ac_red) - ppg->ac_red) / HS_PPG_DC_TAU;

  /* 3. Envelope tracker.  The extremes are recorded as they arrive and then
   *    pulled towards each other, which keeps the thresholds centred on the
   *    signal instead of latching onto an old amplitude.
   */

  if (ac_ir > ppg->peak_hi)
    {
      ppg->peak_hi    = ac_ir;
      ppg->peak_hi_ms = ts_ms;
    }

  if (ac_ir < ppg->peak_lo)
    {
      ppg->peak_lo = ac_ir;
    }

  span = ppg->peak_hi - ppg->peak_lo;

  ppg->peak_hi -= span / HS_PPG_ENV_DECAY;
  ppg->peak_lo += span / HS_PPG_ENV_DECAY;

  span = ppg->peak_hi - ppg->peak_lo;

  /* 4. Without enough modulation there is no pulse to speak of.  Drop the
   *    detector state entirely: reusing half of an old beat would produce a
   *    bogus interval when the signal comes back.
   */

  if (span < HS_PPG_MIN_SPAN)
    {
      ppg->above        = false;
      ppg->last_beat_ms = 0;
      return;
    }

  hi_th = ppg->peak_lo + (span * HS_PPG_TH_HI_NUM) / HS_PPG_TH_HI_DEN;
  lo_th = ppg->peak_lo + (span * HS_PPG_TH_LO_NUM) / HS_PPG_TH_LO_DEN;

  if (!ppg->above)
    {
      if (ac_ir > hi_th)
        {
          ppg->above = true;
        }

      return;
    }

  if (ac_ir > lo_th)
    {
      return;
    }

  /* The pulse has fallen back through the lower threshold: one beat is
   * complete.  Time it at the peak, which is far more stable than either
   * level crossing.
   */

  ppg->above = false;

  if (ppg->last_beat_ms != 0 && ppg->peak_hi_ms > ppg->last_beat_ms)
    {
      uint32_t rr = ppg->peak_hi_ms - ppg->last_beat_ms;

      if (rr >= HS_PPG_RR_MIN_MS && rr <= HS_PPG_RR_MAX_MS)
        {
          ppg->rr[ppg->rr_index] = (uint16_t)rr;
          ppg->rr_index = (ppg->rr_index + 1) % HS_PPG_RR_COUNT;

          if (ppg->rr_count < HS_PPG_RR_COUNT)
            {
              ppg->rr_count++;
            }
        }
    }

  ppg->last_beat_ms = ppg->peak_hi_ms;
}

int hs_ppg_heart_rate(const struct hs_ppg_s *ppg)
{
  uint16_t sorted[HS_PPG_RR_COUNT];
  int      count;
  int      i;
  int      j;
  uint16_t median;

  if (ppg == NULL || ppg->rr_count < HS_PPG_RR_MIN_COUNT)
    {
      return 0;
    }

  count = ppg->rr_count;
  memcpy(sorted, ppg->rr, sizeof(uint16_t) * (size_t)count);

  /* Insertion sort: the array is tiny and this runs a couple of times per
   * second at most.
   */

  for (i = 1; i < count; i++)
    {
      uint16_t key = sorted[i];

      for (j = i - 1; j >= 0 && sorted[j] > key; j--)
        {
          sorted[j + 1] = sorted[j];
        }

      sorted[j + 1] = key;
    }

  median = sorted[count / 2];

  if (median < HS_PPG_RR_MIN_MS || median > HS_PPG_RR_MAX_MS)
    {
      return 0;
    }

  return 60000 / (int)median;
}

int hs_ppg_quality(const struct hs_ppg_s *ppg)
{
  uint32_t sum = 0;
  uint32_t mean;
  uint32_t deviation = 0;
  int      count;
  int      i;
  int      quality;

  if (ppg == NULL || ppg->rr_count < HS_PPG_RR_MIN_COUNT)
    {
      return 0;
    }

  count = ppg->rr_count;

  for (i = 0; i < count; i++)
    {
      sum += ppg->rr[i];
    }

  mean = sum / (uint32_t)count;
  if (mean == 0)
    {
      return 0;
    }

  for (i = 0; i < count; i++)
    {
      uint32_t diff = ppg->rr[i] > mean ? ppg->rr[i] - mean : mean - ppg->rr[i];

      deviation += diff;
    }

  deviation /= (uint32_t)count;

  /* Mean absolute deviation as a fraction of the mean interval.  A steady
   * rhythm sits below a few percent, a jittery one well above ten.
   */

  if (deviation >= mean / 10)
    {
      return 0;
    }

  quality = (int)(100 - (deviation * 1000) / mean);
  if (quality < 0)
    {
      quality = 0;
    }

  return quality;
}

int hs_ppg_spo2(const struct hs_ppg_s *ppg)
{
  int64_t numerator;
  int64_t denominator;
  int     spo2;

  if (ppg == NULL)
    {
      return 0;
    }

  /* The ratio is only meaningful on a pulsatile signal, so require a real
   * heart rate and a modulation that clears the same threshold the beat
   * detector uses.
   */

  if (hs_ppg_heart_rate(ppg) == 0)
    {
      return 0;
    }

  if (ppg->dc_ir <= 0 || ppg->dc_red <= 0 ||
      ppg->ac_ir < HS_PPG_MIN_SPAN || ppg->ac_red < HS_PPG_MIN_SPAN)
    {
      return 0;
    }

  /* R = (AC_red / DC_red) / (AC_ir / DC_ir), evaluated as a single division
   * to keep every bit of precision.
   */

  numerator   = (int64_t)ppg->ac_red * (int64_t)ppg->dc_ir;
  denominator = (int64_t)ppg->dc_red * (int64_t)ppg->ac_ir;

  if (denominator == 0)
    {
      return 0;
    }

  /* SpO2 = 110 - 25 R, the classic empirical curve used by the reference
   * MAX30102 demos.  It is not a calibrated oximeter: a real device needs
   * per-unit coefficients from a clinical fit, so treat the result as an
   * indication of trend rather than a medical reading.
   */

  spo2 = 110 - (int)((25 * numerator) / denominator);

  if (spo2 > 100)
    {
      spo2 = 100;
    }
  else if (spo2 < 70)
    {
      spo2 = 70;
    }

  return spo2;
}
