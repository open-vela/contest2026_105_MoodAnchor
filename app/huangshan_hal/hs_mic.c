/****************************************************************************
 * app/huangshan_hal/hs_mic.c
 *
 * On-board MEMS microphone via the SiFli AUDCODEC HAL.
 *
 * The capture sequence follows the sequence documented by SiFli for the RX
 * path (docs/source/zh_CN/hal/audcodec.md):
 *
 *   HAL_AUDCODEC_Init
 *   HAL_AUDCODEC_Config_RChanel
 *   HAL_AUDCODEC_Config_ADCPath_Volume
 *   HAL_AUDCODEC_Receive_DMA
 *   HAL_AUCODEC_Refgen_Init
 *   HAL_AUDCODEC_Config_Analog_ADCPath
 *   __HAL_AUDCODEC_LP_ENABLE          <- always last
 *
 * The audio PLL is deliberately left alone: the 16 kHz entry of SiFli's
 * sample rate table runs off the 48 MHz crystal, so bf0_enable_pll() is not
 * needed and the clock tree stays as the rest of the system expects it.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/* The AUDCODEC HAL needs the chip-side CMSIS environment (SOC_BF0_HCPU,
 * SF32LB52X, core_cm33.h) which only the kernel arch target provides, so the
 * capture engine lives in vendor/sifli/chips/sf32lb52/sf32lb_mic.c.  This
 * file is the application-facing wrapper plus the loudness mapping.
 */

#include "sf32lb_mic.h"
#include "hs_mic.h"



/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hs_mic_start(void)
{
  return sf32lb_mic_start();
}

void hs_mic_stop(void)
{
  sf32lb_mic_stop();
}

bool hs_mic_ready(void)
{
  return sf32lb_mic_ready();
}

/* Loudness is logarithmic, so the mapping is done in octaves rather than on
 * the raw magnitude.  A linear scale would spend most of its travel on the
 * top few dB and leave speech squashed against the bottom of the meter.
 *
 * The window is 6.64 octaves wide, which is 40 dB - about the range from a
 * quiet room to a raised voice - starting at a mean magnitude of roughly 50.
 * Working in octaves also means the curve stays sensible whatever gain the
 * codec is left at, so it does not have to be recalibrated.
 */

#define HS_MIC_LOG2_AT_ZERO  1444    /* log2(50) in 1/256 units */
#define HS_MIC_LOG2_SPAN     1700    /* 6.64 octaves in 1/256 units */

/****************************************************************************
 * Name: hs_mic_log2_q8
 *
 * Description:
 *   log2 of a positive integer, in 1/256 units, by linear interpolation
 *   between powers of two.  Accurate to better than 0.1 octave, which is far
 *   finer than a loudness meter needs.
 *
 ****************************************************************************/

static int hs_mic_log2_q8(int32_t value)
{
  int     msb = 0;
  int32_t base;

  while ((value >> (msb + 1)) > 0)
    {
      msb++;
    }

  base = (int32_t)1 << msb;

  return (msb << 8) + (int)(((value - base) << 8) / base);
}

int hs_mic_level(void)
{
  int mean = sf32lb_mic_mean();
  int level;

  if (mean <= 0)
    {
      return 0;
    }

  level = (hs_mic_log2_q8(mean) - HS_MIC_LOG2_AT_ZERO) * 100
          / HS_MIC_LOG2_SPAN;

  if (level < 0)
    {
      level = 0;
    }

  return level > 100 ? 100 : level;
}

int hs_mic_mean(void)
{
  return sf32lb_mic_mean();
}

int hs_mic_set_volume(int db)
{
  return sf32lb_mic_set_volume(db);
}

int hs_mic_peak(void)
{
  return sf32lb_mic_peak();
}

uint32_t hs_mic_blocks(void)
{
  return sf32lb_mic_blocks();
}

/****************************************************************************
 * Name: hs_mic_service
 *
 * Description:
 *   Called from a normal thread context to fold a completed DMA block into
 *   the published level.  Kept out of the interrupt so the arithmetic never
 *   runs with interrupts disabled.
 *
 ****************************************************************************/

void hs_mic_service(void)
{
  sf32lb_mic_service();
}

void hs_mic_dump(void)
{
  sf32lb_mic_dump();
}
