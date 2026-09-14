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

int hs_mic_level(void)
{
  /* Map the mean magnitude onto 0..100 with a piecewise approximation of a
   * logarithmic response, so quiet room noise sits near the bottom of the
   * scale and speech climbs quickly.
   *
   * The break points assume a mean magnitude of roughly 30 in a quiet room
   * and a few thousand for speech.  They are the one thing worth revisiting
   * once real numbers are seen on the target.
   */

  int mean = sf32lb_mic_mean();

  if (mean <= 0)
    {
      return 0;
    }

  if (mean < 50)
    {
      return mean * 40 / 50;                    /* 0 .. 40 */
    }

  if (mean < 500)
    {
      return 40 + (mean - 50) * 40 / 450;       /* 40 .. 80 */
    }

  if (mean < 3000)
    {
      return 80 + (mean - 500) * 20 / 2500;     /* 80 .. 100 */
    }

  return 100;
}

int hs_mic_mean(void)
{
  return sf32lb_mic_mean();
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
