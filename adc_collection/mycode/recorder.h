#ifndef RECORDER_H
#define RECORDER_H

#include <stdint.h>
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Periodic sampling -> averaging -> SD card line writer.
 *
 * Call sequence from main loop after each AD7124 round:
 *   for each sampled channel:
 *     Recorder_OnChannelSample(ch, voltage_uv);
 *   Recorder_OnScanComplete();
 *
 * Recorder decides when to flush a row to SD based on average_enable +
 * record_interval_sec. Filename rotates per day (YYMMDD.CSV/DAT/TXT, 8.3
 * format since FatFs LFN is disabled). */

void Recorder_Init(const AppConfigImage *config);
void Recorder_OnChannelSample(uint8_t channel, int32_t voltage_uv);
void Recorder_OnScanComplete(void);
void Recorder_ResetTiming(void);
uint8_t Recorder_GetSdOk(void);

#ifdef __cplusplus
}
#endif

#endif /* RECORDER_H */
