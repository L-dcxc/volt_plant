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
uint8_t Recorder_Flush(void);
/* Non-zero if Recorder_Flush has data queued that would cause an actual SD
   write on the next call. Use to drive a write-activity indicator. */
uint8_t Recorder_HasPendingWrite(void);
/* Returns 1 (and clears the flag) if the recorder performed any internal SD
   write since the last call to this function. Lets the main loop blink a
   "write happened" indicator without instrumenting every flush path. */
uint8_t Recorder_TakeWroteSinceLast(void);
uint8_t Recorder_TakeWriteErrorSinceLast(void);
/* Tell the recorder its mount/cache went away (e.g. main cut SD card power
   for sleep). Next OnScanComplete / Flush will attempt a fresh mount. */
void Recorder_InvalidateMount(void);
/* Non-zero if the next Recorder_OnScanComplete call is expected to trigger
   an actual SD write (S-row cached past flush target, A-row interval due,
   etc.). Call BEFORE the scan, while the SD card is still powered off, to
   decide whether to spend the ~300 ms (and ~50 mA peak) of powering the
   card up for this round. False positives are fine (extra power-up); false
   negatives lose a write so be conservative. */
uint8_t Recorder_NextScanWillFlush(void);
void Recorder_ResetTiming(void);
uint8_t Recorder_GetSdOk(void);

/* Last-known SD capacity in KiB, cached by the recorder after each flush.
   Either pointer may be NULL. Both read 0 until the card is first mounted. */
void Recorder_GetCapacityKB(uint32_t *total_kb, uint32_t *free_kb);

#ifdef __cplusplus
}
#endif

#endif /* RECORDER_H */
