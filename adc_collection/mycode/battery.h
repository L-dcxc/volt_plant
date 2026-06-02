#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>
#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Battery monitor.
 *
 * Hardware: VBAT -> Q8 (P-MOS, gated by BAT_ADC_EN) -> R19(10k)/R23(10k)
 * divider -> PC0/ADC1_IN1. So the ADC node sees VBAT/2. BAT_ADC_EN is held
 * low normally to avoid ~VBAT/20k leakage through the divider, and pulsed
 * high only around a sample.
 *
 * VDDA (the ADC reference) is derived at runtime from the internal VREFINT
 * channel against its factory calibration, so readings stay accurate even
 * when the board runs off a battery whose rail is not exactly 3.3 V. */

void     Battery_Init(void);

/* Take one measurement (enables divider, samples VREFINT + IN1, restores
   state). Updates the cached value. Returns 1 on success, 0 on ADC error. */
uint8_t  Battery_Update(void);

/* Last good readings (0 until the first successful Battery_Update). */
uint16_t Battery_GetMilliVolts(void);   /* reconstructed VBAT in mV */
uint16_t Battery_GetVddaMilliVolts(void);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_H */
