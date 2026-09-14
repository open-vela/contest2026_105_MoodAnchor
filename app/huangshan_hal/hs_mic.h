/****************************************************************************
 * app/huangshan_hal/hs_mic.h
 *
 * On-board MEMS microphone: loudness measurement.
 *
 * The board wires an analogue MEMS mic to the chip's AUDCODEC ADC (module
 * pins 36 MIC_BIAS / 37 MIC_ADC_IN).  That path is not routed through the
 * NuttX audio framework on this port - there is no audio driver in
 * vendor/sifli/chips/sf32lb52 - so this module drives the SiFli AUDCODEC HAL
 * directly:
 *
 *   HAL_PMU_EnableAudio / HAL_RCC_EnableModule
 *   HAL_AUDCODEC_Init + Config_RChanel + Config_Analog_ADCPath
 *   HAL_AUDCODEC_Receive_DMA  ->  ping-pong buffer
 *
 * Only one channel at 16 kHz is captured, and the result is reduced to a
 * relative loudness figure.  Absolute SPL calibration is deliberately not
 * attempted: microphone sensitivity, bias and gain all drift, so the number
 * is only meaningful against its own recent history.
 *
 ****************************************************************************/

#ifndef __APP_HUANGSHAN_HAL_HS_MIC_H
#define __APP_HUANGSHAN_HAL_HS_MIC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* 16 kHz is plenty for a loudness reading and - more importantly - its entry
 * in the SiFli sample rate table uses the crystal rather than the audio PLL,
 * which keeps the clock tree untouched.  See hs_mic_clk_config[] in hs_mic.c.
 */

#define HS_MIC_SAMPLE_RATE    16000
#define HS_MIC_RATE_INDEX     3

/* Frames per DMA half-buffer.  At 16 kHz, 512 frames is 32 ms: often enough
 * to follow speech, long enough that the per-block overhead stays small.
 */

#define HS_MIC_BLOCK_FRAMES   512
#define HS_MIC_BLOCK_MS       (HS_MIC_BLOCK_FRAMES * 1000 / HS_MIC_SAMPLE_RATE)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: hs_mic_start
 *
 * Description:
 *   Power up the AUDCODEC, configure the ADC path for the on-board mic and
 *   start DMA capture.
 *
 * Returned Value:
 *   Zero on success, a negated errno value otherwise.
 *
 ****************************************************************************/

int hs_mic_start(void);

/****************************************************************************
 * Name: hs_mic_stop
 *
 * Description:
 *   Stop DMA capture and power the codec down again.
 *
 ****************************************************************************/

void hs_mic_stop(void);

/****************************************************************************
 * Name: hs_mic_ready
 *
 * Description:
 *   True once DMA capture is running and at least one block has been seen.
 *
 ****************************************************************************/

bool hs_mic_ready(void);

/****************************************************************************
 * Name: hs_mic_level
 *
 * Description:
 *   Relative loudness of the most recent block, 0..100.
 *
 *   Derived from the RMS of the block mapped logarithmically, so a quiet
 *   room sits near 0 and speech or clapping pins the scale.  Compare it with
 *   its own recent values rather than treating it as a sound pressure level.
 *
 ****************************************************************************/

int hs_mic_level(void);

/****************************************************************************
 * Name: hs_mic_mean
 *
 * Description:
 *   Raw mean absolute sample magnitude of the most recent block, 0..32767.
 *   Reported unscaled so the mapping inside hs_mic_level() can be tuned
 *   against real numbers from the board.
 *
 ****************************************************************************/

int hs_mic_mean(void);

/****************************************************************************
 * Name: hs_mic_peak
 *
 * Description:
 *   Absolute peak sample magnitude of the most recent block, 0..32767.
 *
 ****************************************************************************/

int hs_mic_peak(void);

/****************************************************************************
 * Name: hs_mic_blocks
 *
 * Description:
 *   Number of blocks processed since hs_mic_start().  A value that never
 *   grows means the DMA is not completing, which is the first thing to check
 *   when the level stays at zero.
 *
 ****************************************************************************/

uint32_t hs_mic_blocks(void);

/****************************************************************************
 * Name: hs_mic_service
 *
 * Description:
 *   Fold a completed DMA block into the published level.  Call it from a
 *   normal thread, not from an interrupt: the analysis stays out of the ISR
 *   so it never runs with interrupts disabled.
 *
 ****************************************************************************/

void hs_mic_service(void);

#endif /* __APP_HUANGSHAN_HAL_HS_MIC_H */
