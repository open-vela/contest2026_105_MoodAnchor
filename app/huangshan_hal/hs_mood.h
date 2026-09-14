/****************************************************************************
 * app/huangshan_hal/hs_mood.h
 *
 * Multi-sensor arousal ("agitated") fusion.
 *
 * Three independent sensors each produce a 0..100 confidence that something
 * arousing is happening:
 *
 *   IMU   - sustained violent motion, measured as the deviation of the
 *           acceleration magnitude from 1 g plus the rotation rate
 *   Mic   - sustained loudness, from the relative level the microphone
 *           driver already publishes
 *   GSR   - skin conductance having left the personal resting band
 *
 * The three are then combined, but the result is only reported as agitated
 * when the fused confidence is high AND at least two of the three sensors
 * agree.  That agreement rule is what makes it "multi-sensor": a single
 * sensor - however loud - cannot raise the flag on its own, which is what
 * keeps a dropped watch, a slammed door or a loose electrode from being
 * reported as an emotional state.
 *
 * This is a rule-based fusion, deliberately not the frozen per-modality
 * models.  Those were trained on unrelated datasets (PAMAP2 for motion,
 * WESAD for EDA) and their own README states that no end-to-end accuracy can
 * be claimed from them; a transparent, tunable rule set is the honest option
 * until there is synchronised data from this device to fit against.
 *
 * Nothing here reads hardware: the caller fills in a snapshot and gets a
 * result back, so the whole decision is a pure function of its inputs.
 *
 ****************************************************************************/

#ifndef __APP_HUANGSHAN_HAL_HS_MOOD_H
#define __APP_HUANGSHAN_HAL_HS_MOOD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Sensor scores at or above this count as "this sensor sees something". */

#define HS_MOOD_POSITIVE         50

/* Fused confidence needed to report arousal, in percent. */

#define HS_MOOD_FUSED_THRESHOLD  55

/* How many of the three sensors have to agree. */

#define HS_MOOD_MIN_AGREEING     2

/* Once arousal has been reported it stays reported for at least this many
 * updates, so a state that the phone turns into a notification cannot
 * flicker on and off around the threshold.
 */

#define HS_MOOD_HOLD_UPDATES     5

/* Weights used to combine the three scores.  They sum to 100.  The skin
 * conductance gets the largest share because it is the only one of the three
 * that measures autonomic arousal directly; motion and loudness are both
 * easily produced without any emotional cause.
 */

#define HS_MOOD_W_GSR            40
#define HS_MOOD_W_IMU            30
#define HS_MOOD_W_MIC            30

/* Full-scale activity for the IMU, in the same arbitrary unit the score is
 * computed from: |accel| deviation from 1 g (mg) + 10 x rotation (dps).
 * Standing still sits near 20, walking near 1200, shaking the watch several
 * thousand.
 */

#define HS_MOOD_IMU_FULL         3000

/* Loudness on the microphone's relative 0..100 scale that counts as fully
 * loud, and the level below which it counts as quiet.
 */

#define HS_MOOD_MIC_FULL         85
#define HS_MOOD_MIC_FLOOR        45

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* One snapshot of everything the fusion needs.
 *
 * The caller does the hardware-specific interpretation and hands over a
 * decision-ready snapshot: it knows whether the electrodes are on skin and
 * what the resting baseline is, so the threshold for "not worn" lives in one
 * place rather than being duplicated here.
 */

struct hs_mood_input_s
{
  bool    gsr_ready;        /* pads on skin AND a baseline has been captured */
  int32_t gsr_delta;        /* baseline - current, mV.  Positive means the */
                            /* conductance rose, which is the arousing way */
  int32_t gsr_band;         /* tolerance band in mV */

  int     mic_level;        /* relative loudness, 0..100 */

  int16_t accel_mg[3];      /* acceleration, milli-g */
  int16_t gyro_dps10[3];    /* rotation, tenths of a degree/s, per axis */
};

struct hs_mood_result_s
{
  bool agitated;            /* the headline this is all for */
  int  confidence;          /* fused confidence, 0..100 */

  int  imu_score;           /* per-sensor scores, 0..100 */
  int  mic_score;
  int  gsr_score;

  bool imu_positive;        /* per-sensor verdicts */
  bool mic_positive;
  bool gsr_positive;

  int  agreeing;            /* how many sensors agreed, 0..3 */
  bool gsr_ready;           /* a baseline exists and the pads are on skin */

  /* The rotation magnitude the IMU score was computed from.  Returned so the
   * caller can forward it in the telemetry packet without repeating the
   * square root.
   */

  int gyro_mag_dps10;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: hs_mood_init
 *
 * Description:
 *   Clear the smoothing and hold state.  Call once before the first update.
 *
 ****************************************************************************/

void hs_mood_init(void);

/****************************************************************************
 * Name: hs_mood_update
 *
 * Description:
 *   Fold one snapshot into the fusion and return the current verdict.
 *
 *   The function keeps internal smoothing state, so it must be called at a
 *   steady rate - once per second is what the caller uses.  Calling it at a
 *   different rate changes the effective time constants.
 *
 ****************************************************************************/

void hs_mood_update(const struct hs_mood_input_s *in,
                    struct hs_mood_result_s *out);

#endif /* __APP_HUANGSHAN_HAL_HS_MOOD_H */
