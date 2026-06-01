#include "recorder.h"

#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "fatfs.h"
#include "stm32l4xx_hal.h"
#include "app_rtc.h"
#include "storage.h"

/* DAT format currently shares the CSV layout (extension only). Define a
   binary record format here once the spec stabilises. */

#define RECORDER_LINE_BUF_SIZE   256U
#define RECORDER_PATH_BUF_SIZE   32U
#define RECORDER_NAME_BUF_SIZE   16U
#define RECORDER_SD_RETRY_MS     5000U

static const AppConfigImage *s_config = NULL;

/* Per-scan buffer: gets the values from the just-completed sample round and
   is flushed as an "S" row every scan. */
static int64_t  s_scan_uv[APP_CONFIG_CHANNEL_COUNT];
static uint32_t s_scan_count[APP_CONFIG_CHANNEL_COUNT];

/* Running window: keeps accumulating across scans until the record interval
   elapses, then is flushed as an "A" row and cleared. */
static int64_t  s_acc_uv[APP_CONFIG_CHANNEL_COUNT];
static uint32_t s_acc_count[APP_CONFIG_CHANNEL_COUNT];

static uint32_t s_last_record_tick = 0U;
static uint8_t  s_record_tick_inited = 0U;
static uint32_t s_last_mount_attempt_tick = 0U;
static uint8_t  s_sd_mounted = 0U;
static uint8_t  s_sd_ok = 0U;

static void Recorder_ClearScan(void)
{
  memset(s_scan_uv, 0, sizeof(s_scan_uv));
  memset(s_scan_count, 0, sizeof(s_scan_count));
}

static void Recorder_ClearAcc(void)
{
  memset(s_acc_uv, 0, sizeof(s_acc_uv));
  memset(s_acc_count, 0, sizeof(s_acc_count));
}

static const char *Recorder_ExtensionFor(uint8_t file_format)
{
  switch (file_format)
  {
    case APP_FILE_FORMAT_DAT: return "DAT";
    case APP_FILE_FORMAT_TXT: return "TXT";
    case APP_FILE_FORMAT_CSV:
    default:                  return "CSV";
  }
}

static uint8_t Recorder_BuildFileName(char *out, uint32_t out_size)
{
  AppRtcDateTime now;

  if (AppRtc_GetDateTime(&now) == 0U)
  {
    return 0U;
  }

  /* 8.3-compatible: YYMMDD.EXT */
  (void)snprintf(out, out_size, "%02u%02u%02u.%s",
                 (unsigned)(now.year % 100U),
                 (unsigned)now.month,
                 (unsigned)now.day,
                 Recorder_ExtensionFor(s_config != NULL ? s_config->file_format
                                                       : (uint8_t)APP_FILE_FORMAT_CSV));
  return 1U;
}

static void Recorder_BuildPath(char *path, uint32_t path_size, const char *file_name)
{
  uint32_t index = 0U;
  uint32_t name_index = 0U;

  if ((path == NULL) || (path_size == 0U))
  {
    return;
  }

  while ((SDPath[index] != '\0') && (index < (path_size - 1U)))
  {
    path[index] = SDPath[index];
    index++;
  }

  if ((file_name != NULL) && (file_name[0] != '/') && (index < (path_size - 1U)))
  {
    path[index] = '/';
    index++;
  }

  while ((file_name != NULL) && (file_name[name_index] != '\0')
         && (index < (path_size - 1U)))
  {
    path[index] = file_name[name_index];
    index++;
    name_index++;
  }

  path[index] = '\0';
}

static uint32_t Recorder_AppendString(char *buf, uint32_t buf_size,
                                      uint32_t offset, const char *text)
{
  uint32_t i = 0U;

  if ((buf == NULL) || (text == NULL) || (offset >= buf_size))
  {
    return offset;
  }

  while ((text[i] != '\0') && ((offset + 1U) < buf_size))
  {
    buf[offset] = text[i];
    offset++;
    i++;
  }
  buf[offset] = '\0';
  return offset;
}

/* Ensures s_sd_mounted is 1 when possible; throttles retries to avoid
   hammering FatFs every sample after a real failure. */
static FRESULT Recorder_EnsureMounted(void)
{
  FRESULT result;

  if (s_sd_mounted != 0U)
  {
    return FR_OK;
  }

  if ((s_last_mount_attempt_tick != 0U) &&
      ((HAL_GetTick() - s_last_mount_attempt_tick) < RECORDER_SD_RETRY_MS))
  {
    return FR_NOT_READY;
  }

  s_last_mount_attempt_tick = HAL_GetTick();
  result = Storage_Mount();
  if (result == FR_OK)
  {
    s_sd_mounted = 1U;
  }
  return result;
}

/* Compose a CSV line: type,timestamp,CH0_uV,...,CH15_uV.
   row_type 'S' = single-scan sample, 'A' = window average.
   sums[]/counts[] hold the data to emit; for 'S' rows pass the per-scan
   buffer, for 'A' rows pass the accumulator window. Channels with count==0
   or disabled produce empty fields (",,"). */
static void Recorder_BuildDataLine(char *line, uint32_t line_size,
                                   char row_type, const char *timestamp,
                                   const int64_t *sums, const uint32_t *counts)
{
  uint32_t offset = 0U;
  char num[16];
  char tag[2];

  tag[0] = row_type;
  tag[1] = '\0';
  offset = Recorder_AppendString(line, line_size, offset, tag);
  offset = Recorder_AppendString(line, line_size, offset, ",");
  offset = Recorder_AppendString(line, line_size, offset, timestamp);

  for (uint8_t ch = 0U; ch < APP_CONFIG_CHANNEL_COUNT; ch++)
  {
    offset = Recorder_AppendString(line, line_size, offset, ",");

    if ((s_config != NULL) &&
        (s_config->channels[ch].enable != 0U) &&
        (counts[ch] > 0U))
    {
      int64_t value = sums[ch] / (int64_t)counts[ch];
      (void)snprintf(num, sizeof(num), "%ld", (long)value);
      offset = Recorder_AppendString(line, line_size, offset, num);
    }
  }
}

static uint8_t Recorder_HasData(const uint32_t *counts)
{
  for (uint8_t ch = 0U; ch < APP_CONFIG_CHANNEL_COUNT; ch++)
  {
    if (counts[ch] > 0U) return 1U;
  }
  return 0U;
}

static FRESULT Recorder_WriteHeader(FIL *file)
{
  static const char *kHeader =
      "type,timestamp,CH0_uV,CH1_uV,CH2_uV,CH3_uV,CH4_uV,CH5_uV,CH6_uV,CH7_uV,"
      "CH8_uV,CH9_uV,CH10_uV,CH11_uV,CH12_uV,CH13_uV,CH14_uV,CH15_uV\r\n";
  UINT written = 0U;
  UINT length = (UINT)strlen(kHeader);
  FRESULT result;

  result = f_write(file, kHeader, length, &written);
  if (result != FR_OK)
  {
    return result;
  }
  if (written != length)
  {
    return FR_DISK_ERR;
  }
  return FR_OK;
}

static FRESULT Recorder_FlushLine(const char *line)
{
  char file_name[RECORDER_NAME_BUF_SIZE];
  char path[RECORDER_PATH_BUF_SIZE];
  FIL file;
  UINT written = 0U;
  UINT length;
  FRESULT result;
  uint32_t line_len;
  char crlf[3];

  if (Recorder_BuildFileName(file_name, sizeof(file_name)) == 0U)
  {
    return FR_INVALID_NAME;
  }

  result = Recorder_EnsureMounted();
  if (result != FR_OK)
  {
    return result;
  }

  Recorder_BuildPath(path, sizeof(path), file_name);

  result = f_open(&file, path, FA_WRITE | FA_OPEN_APPEND);
  if (result != FR_OK)
  {
    /* Open failed -> assume mount is stale; force remount next time */
    s_sd_mounted = 0U;
    return result;
  }

  if (f_size(&file) == 0U)
  {
    result = Recorder_WriteHeader(&file);
    if (result != FR_OK)
    {
      (void)f_close(&file);
      return result;
    }
  }

  line_len = (uint32_t)strlen(line);
  length = (UINT)line_len;
  result = f_write(&file, line, length, &written);
  if (result == FR_OK && written != length)
  {
    result = FR_DISK_ERR;
  }

  if (result == FR_OK)
  {
    crlf[0] = '\r';
    crlf[1] = '\n';
    crlf[2] = '\0';
    result = f_write(&file, crlf, 2U, &written);
    if (result == FR_OK && written != 2U)
    {
      result = FR_DISK_ERR;
    }
  }

  if (result == FR_OK)
  {
    result = f_sync(&file);
  }

  if (f_close(&file) != FR_OK && result == FR_OK)
  {
    result = FR_DISK_ERR;
  }

  return result;
}

void Recorder_Init(const AppConfigImage *config)
{
  s_config = config;
  Recorder_ClearScan();
  Recorder_ClearAcc();
  s_last_record_tick = HAL_GetTick();
  s_record_tick_inited = 1U;
  s_last_mount_attempt_tick = 0U;
  s_sd_mounted = 0U;
  s_sd_ok = 0U;

  if (Recorder_EnsureMounted() == FR_OK)
  {
    s_sd_ok = 1U;
  }
}

void Recorder_ResetTiming(void)
{
  Recorder_ClearScan();
  Recorder_ClearAcc();
  s_last_record_tick = HAL_GetTick();
  s_record_tick_inited = 1U;
}

void Recorder_OnChannelSample(uint8_t channel, int32_t voltage_uv)
{
  if (channel >= APP_CONFIG_CHANNEL_COUNT)
  {
    return;
  }
  s_scan_uv[channel] += (int64_t)voltage_uv;
  s_scan_count[channel]++;
  s_acc_uv[channel] += (int64_t)voltage_uv;
  s_acc_count[channel]++;
}

/* Called after every completed sample round (every sample_interval_sec).
 * Always writes an "S" row with this scan's data. If averaging is enabled
 * and the record interval has elapsed since the last "A" row, additionally
 * writes an "A" row and resets the averaging window. */
void Recorder_OnScanComplete(void)
{
  char timestamp[APP_RTC_TIMESTAMP_SIZE];
  char line[RECORDER_LINE_BUF_SIZE];
  FRESULT fr;
  uint8_t have_ts = 0U;

  if (s_config == NULL)
  {
    return;
  }

  if (s_record_tick_inited == 0U)
  {
    s_last_record_tick = HAL_GetTick();
    s_record_tick_inited = 1U;
  }

  if (AppRtc_FormatTimestamp(timestamp, sizeof(timestamp)) != 0U)
  {
    have_ts = 1U;
  }

  /* 1) Sample row — every completed scan. */
  if (have_ts != 0U && Recorder_HasData(s_scan_count) != 0U)
  {
    Recorder_BuildDataLine(line, sizeof(line), 'S', timestamp,
                            s_scan_uv, s_scan_count);
    fr = Recorder_FlushLine(line);
    s_sd_ok = (fr == FR_OK) ? 1U : 0U;
  }
  Recorder_ClearScan();

  /* 2) Average row — when averaging is enabled and the record interval has
        elapsed. Advances last_record_tick by exact interval_ms instead of
        snapping to HAL_GetTick(), so the cadence does not drift by the
        sample interval. */
  if (s_config->average_enable != 0U)
  {
    uint32_t interval_ms = s_config->record_interval_sec * 1000UL;
    if (interval_ms == 0UL)
    {
      interval_ms = 1000UL;
    }

    uint32_t elapsed = HAL_GetTick() - s_last_record_tick;
    if (elapsed >= interval_ms)
    {
      if (have_ts != 0U && Recorder_HasData(s_acc_count) != 0U)
      {
        Recorder_BuildDataLine(line, sizeof(line), 'A', timestamp,
                                s_acc_uv, s_acc_count);
        fr = Recorder_FlushLine(line);
        if (fr != FR_OK)
        {
          s_sd_ok = 0U;
        }
      }
      Recorder_ClearAcc();

      /* Advance by full intervals; if we lagged badly (e.g. SD stall, paused
         sampling), resync to now to avoid a burst of catch-up rows. */
      if (elapsed >= (interval_ms * 2UL))
      {
        s_last_record_tick = HAL_GetTick();
      }
      else
      {
        s_last_record_tick += interval_ms;
      }
    }
  }
}

uint8_t Recorder_GetSdOk(void)
{
  return s_sd_ok;
}
