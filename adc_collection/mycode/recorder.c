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

#define RECORDER_LINE_BUF_SIZE   768U   /* 32 channels per CSV row (was 384 for 16) */
#define RECORDER_PATH_BUF_SIZE   32U
#define RECORDER_NAME_BUF_SIZE   16U
#define RECORDER_SD_RETRY_MS     5000U
#define RECORDER_CACHE_MAX_LINES 32U
#define RECORDER_CACHE_MAX_SECONDS 1800UL

/* Ring-overwrite: when free space drops below this floor, the oldest data
   file (smallest YYMMDD name) is deleted to make room — like a dashcam. The
   day's currently-open file is never evicted. */
#define RECORDER_MIN_FREE_KB     (20U * 1024U)   /* 20 MiB headroom */
#define RECORDER_EVICT_MAX_LOOPS 64U             /* safety cap per write */

static const AppConfigImage *s_config = NULL;

/* Per-scan buffer: gets the values from the just-completed sample round.
   It is written as an "S" row only when averaging is disabled and the
   configured record interval elapses. */
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
/* Set whenever Recorder_FlushCachedLines actually wrote bytes to the card.
   Drained by Recorder_TakeWroteSinceLast so the main loop can flash an
   indicator LED without instrumenting every internal flush site. */
static uint8_t  s_wrote_since_last = 0U;
static uint8_t  s_write_error_since_last = 0U;
static char     s_line_cache[RECORDER_CACHE_MAX_LINES][RECORDER_LINE_BUF_SIZE];
static uint8_t  s_line_cache_count = 0U;

/* Cached capacity (KiB), refreshed after each flush so the Modbus layer can
   report it without triggering a full FAT scan on every register read. */
static uint32_t s_total_kb = 0U;
static uint32_t s_free_kb = 0U;

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

static void Recorder_ClearLineCache(void)
{
  s_line_cache_count = 0U;
}

static uint8_t Recorder_TargetCacheLines(void)
{
  uint32_t sample_s;
  uint32_t target_s;
  uint32_t lines;

  if (s_config == NULL)
  {
    return 1U;
  }

  sample_s = s_config->sample_interval_sec;
  if (sample_s == 0UL)
  {
    sample_s = 1UL;
  }

  target_s = s_config->record_interval_sec;
  if (target_s == 0UL)
  {
    target_s = sample_s;
  }
  if (target_s > RECORDER_CACHE_MAX_SECONDS)
  {
    target_s = RECORDER_CACHE_MAX_SECONDS;
  }
  if (target_s < sample_s)
  {
    target_s = sample_s;
  }

  lines = (target_s + sample_s - 1UL) / sample_s;
  if (lines < 1UL)
  {
    lines = 1UL;
  }
  if (lines > RECORDER_CACHE_MAX_LINES)
  {
    lines = RECORDER_CACHE_MAX_LINES;
  }

  return (uint8_t)lines;
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

static uint8_t Recorder_IsLeapYear(uint16_t year)
{
  if ((year % 400U) == 0U)
  {
    return 1U;
  }
  if ((year % 100U) == 0U)
  {
    return 0U;
  }
  return ((year % 4U) == 0U) ? 1U : 0U;
}

static uint8_t Recorder_DaysInMonth(uint16_t year, uint8_t month)
{
  static const uint8_t days_table[12] =
  {
    31U, 28U, 31U, 30U, 31U, 30U,
    31U, 31U, 30U, 31U, 30U, 31U
  };

  if ((month < 1U) || (month > 12U))
  {
    return 31U;
  }
  if ((month == 2U) && (Recorder_IsLeapYear(year) != 0U))
  {
    return 29U;
  }
  return days_table[month - 1U];
}

static uint32_t Recorder_DateToDays(uint16_t year, uint8_t month, uint8_t day)
{
  uint32_t days = 0UL;
  uint16_t y;
  uint8_t m;

  for (y = 2000U; y < year; y++)
  {
    days += (Recorder_IsLeapYear(y) != 0U) ? 366UL : 365UL;
  }
  for (m = 1U; m < month; m++)
  {
    days += Recorder_DaysInMonth(year, m);
  }

  return days + (uint32_t)(day - 1U);
}

static void Recorder_DaysToDate(uint32_t days, AppRtcDateTime *date)
{
  uint16_t year = 2000U;
  uint8_t month = 1U;

  if (date == NULL)
  {
    return;
  }

  for (;;)
  {
    uint16_t year_days = (Recorder_IsLeapYear(year) != 0U) ? 366U : 365U;
    if (days < year_days)
    {
      break;
    }
    days -= year_days;
    year++;
  }

  for (;;)
  {
    uint8_t month_days = Recorder_DaysInMonth(year, month);
    if (days < month_days)
    {
      break;
    }
    days -= month_days;
    month++;
  }

  date->year = year;
  date->month = month;
  date->day = (uint8_t)(days + 1U);
  date->hour = 0U;
  date->minute = 0U;
  date->second = 0U;
}

static uint8_t Recorder_BuildFileName(char *out, uint32_t out_size)
{
  AppRtcDateTime now;
  AppRtcDateTime file_date;
  uint16_t span_days = APP_CONFIG_FILE_SPAN_MIN_DAYS;
  uint32_t days;
  uint32_t group_start_days;

  if (AppRtc_GetDateTime(&now) == 0U)
  {
    return 0U;
  }

  file_date = now;
  if (s_config != NULL)
  {
    span_days = s_config->file_span_days;
  }
  if (span_days < APP_CONFIG_FILE_SPAN_MIN_DAYS)
  {
    span_days = APP_CONFIG_FILE_SPAN_MIN_DAYS;
  }
  if (span_days > APP_CONFIG_FILE_SPAN_MAX_DAYS)
  {
    span_days = APP_CONFIG_FILE_SPAN_MAX_DAYS;
  }

  if (span_days > 1U)
  {
    days = Recorder_DateToDays(now.year, now.month, now.day);
    group_start_days = (days / (uint32_t)span_days) * (uint32_t)span_days;
    Recorder_DaysToDate(group_start_days, &file_date);
  }

  /* 8.3-compatible: YYMMDD.EXT */
  (void)snprintf(out, out_size, "%02u%02u%02u.%s",
                 (unsigned)(file_date.year % 100U),
                 (unsigned)file_date.month,
                 (unsigned)file_date.day,
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

/* True if name looks like a recorder data file: "YYMMDD.EXT" — six leading
   digits then a dot. Other files (SD_TEST.CSV, user files) are left alone. */
static uint8_t Recorder_IsDataFile(const char *name)
{
  if (name == NULL)
  {
    return 0U;
  }
  for (uint8_t i = 0U; i < 6U; i++)
  {
    if ((name[i] < '0') || (name[i] > '9'))
    {
      return 0U;
    }
  }
  return (name[6] == '.') ? 1U : 0U;
}

/* Scan the data directory for the oldest data file (lexicographically
   smallest YYMMDD name == earliest date), skipping the file currently being
   written so we never delete today's open log. Returns 1 and fills oldest[]
   if one is found. */
static uint8_t Recorder_FindOldestDataFile(const char *current_name,
                                           char *oldest, uint32_t oldest_size)
{
  DIR dir;
  FILINFO fno;
  uint8_t found = 0U;

  if (f_opendir(&dir, SDPath) != FR_OK)
  {
    return 0U;
  }

  for (;;)
  {
    if (f_readdir(&dir, &fno) != FR_OK)
    {
      break;
    }
    if (fno.fname[0] == '\0')
    {
      break;
    }
    if ((fno.fattrib & (AM_DIR | AM_HID | AM_SYS)) != 0U)
    {
      continue;
    }
    if (Recorder_IsDataFile(fno.fname) == 0U)
    {
      continue;
    }
    if ((current_name != NULL) && (strcmp(fno.fname, current_name) == 0))
    {
      continue; /* never evict the file we're writing right now */
    }

    if ((found == 0U) || (strcmp(fno.fname, oldest) < 0))
    {
      (void)snprintf(oldest, oldest_size, "%s", fno.fname);
      found = 1U;
    }
  }

  (void)f_closedir(&dir);
  return found;
}

/* Refresh the cached capacity figures. Best-effort; leaves cache untouched on
   error so the last good reading is still reported. */
static void Recorder_RefreshCapacity(void)
{
  uint32_t total = 0U;
  uint32_t freekb = 0U;
  if (Storage_GetCapacityKB(&total, &freekb) == FR_OK)
  {
    s_total_kb = total;
    s_free_kb = freekb;
  }
}

/* Ring-overwrite: while free space is below the floor, delete the oldest data
   file. Stops when enough space is freed, when only the current day's file
   remains, or after a safety cap. */
static void Recorder_EnsureFreeSpace(const char *current_name)
{
  for (uint32_t loop = 0U; loop < RECORDER_EVICT_MAX_LOOPS; loop++)
  {
    Recorder_RefreshCapacity();

    if ((s_total_kb == 0U) || (s_free_kb >= RECORDER_MIN_FREE_KB))
    {
      return; /* capacity unknown, or enough room */
    }

    char oldest[RECORDER_NAME_BUF_SIZE];
    char path[RECORDER_PATH_BUF_SIZE];

    if (Recorder_FindOldestDataFile(current_name, oldest, sizeof(oldest)) == 0U)
    {
      return; /* nothing left to evict (only today's file remains) */
    }

    Recorder_BuildPath(path, sizeof(path), oldest);
    if (f_unlink(path) != FR_OK)
    {
      return; /* give up on error; next cycle may retry */
    }
  }
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

static void Recorder_GetChannelName(uint8_t channel, char *out, uint32_t out_size)
{
  uint32_t i = 0U;

  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  out[0] = '\0';

  if ((s_config != NULL) && (channel < APP_CONFIG_CHANNEL_COUNT) &&
      (s_config->channel_names[channel][0] != '\0') &&
      (AppConfig_IsValidChannelName(s_config->channel_names[channel]) != 0U))
  {
    while ((s_config->channel_names[channel][i] != '\0') &&
           ((i + 1U) < out_size))
    {
      out[i] = s_config->channel_names[channel][i];
      i++;
    }
    out[i] = '\0';
    return;
  }

  (void)snprintf(out, out_size, "CH%u", (unsigned)channel);
}

static FRESULT Recorder_WriteHeader(FIL *file)
{
  char header[RECORDER_LINE_BUF_SIZE];
  char name[APP_CONFIG_CHANNEL_NAME_LEN + 1U];
  UINT written = 0U;
  UINT length;
  FRESULT result;
  uint32_t offset = 0U;

  offset = Recorder_AppendString(header, sizeof(header), offset, "type,timestamp");
  for (uint8_t ch = 0U; ch < APP_CONFIG_CHANNEL_COUNT; ch++)
  {
    Recorder_GetChannelName(ch, name, sizeof(name));
    offset = Recorder_AppendString(header, sizeof(header), offset, ",");
    offset = Recorder_AppendString(header, sizeof(header), offset, name);
    offset = Recorder_AppendString(header, sizeof(header), offset, "_uV");
  }
  offset = Recorder_AppendString(header, sizeof(header), offset, "\r\n");
  (void)offset;

  length = (UINT)strlen(header);
  result = f_write(file, header, length, &written);
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

static FRESULT Recorder_FlushLines(char lines[][RECORDER_LINE_BUF_SIZE], uint8_t count)
{
  char file_name[RECORDER_NAME_BUF_SIZE];
  char path[RECORDER_PATH_BUF_SIZE];
  FIL file;
  UINT written = 0U;
  UINT length;
  FRESULT result;
  char crlf[3];

  if ((lines == NULL) || (count == 0U))
  {
    return FR_OK;
  }

  if (Recorder_BuildFileName(file_name, sizeof(file_name)) == 0U)
  {
    return FR_INVALID_NAME;
  }

  result = Recorder_EnsureMounted();
  if (result != FR_OK)
  {
    return result;
  }

  /* Ring-overwrite: free room before opening, evicting oldest files but never
     the one we're about to append to. */
  Recorder_EnsureFreeSpace(file_name);

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

  for (uint8_t i = 0U; i < count && result == FR_OK; i++)
  {
    uint32_t line_len = (uint32_t)strlen(lines[i]);
    length = (UINT)line_len;
    result = f_write(&file, lines[i], length, &written);
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
    }
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

  if (result == FR_OK)
  {
    Recorder_RefreshCapacity();
  }

  return result;
}

static FRESULT Recorder_FlushCachedLines(void)
{
  FRESULT result;

  if (s_line_cache_count == 0U)
  {
    return FR_OK;
  }

  result = Recorder_FlushLines(s_line_cache, s_line_cache_count);
  if (result == FR_OK)
  {
    Recorder_ClearLineCache();
    s_wrote_since_last = 1U;
  }
  else
  {
    s_write_error_since_last = 1U;
  }

  return result;
}

static FRESULT Recorder_QueueLine(const char *line, uint8_t force_flush)
{
  FRESULT result = FR_OK;
  uint8_t target_lines;

  if (line == NULL)
  {
    return FR_INVALID_PARAMETER;
  }

  target_lines = Recorder_TargetCacheLines();

  if (s_line_cache_count >= RECORDER_CACHE_MAX_LINES)
  {
    result = Recorder_FlushCachedLines();
    if (result != FR_OK)
    {
      return result;
    }
  }

  (void)snprintf(s_line_cache[s_line_cache_count],
                 RECORDER_LINE_BUF_SIZE,
                 "%s",
                 line);
  s_line_cache_count++;

  if ((force_flush != 0U) || (s_line_cache_count >= target_lines))
  {
    result = Recorder_FlushCachedLines();
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
    Recorder_RefreshCapacity();
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

/* Called after every completed sample round.
 * average_enable=1: only writes an "A" row when the record interval elapses.
 * average_enable=0: the main scheduler samples at record_interval_sec, so
 * each completed scan is written as an "S" row immediately. Per-scan rows are
 * no longer logged while averaging is enabled. */
void Recorder_OnScanComplete(void)
{
  char timestamp[APP_RTC_TIMESTAMP_SIZE];
  char line[RECORDER_LINE_BUF_SIZE];
  FRESULT fr;
  uint8_t have_ts = 0U;
  uint32_t interval_ms;
  uint32_t elapsed;

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

  if (s_config->average_enable == 0U)
  {
    if ((have_ts != 0U) && (Recorder_HasData(s_scan_count) != 0U))
    {
      Recorder_BuildDataLine(line, sizeof(line), 'S', timestamp,
                             s_scan_uv, s_scan_count);
      fr = Recorder_QueueLine(line, 1U);
      if (fr != FR_OK)
      {
        s_sd_ok = 0U;
      }
      else
      {
        s_sd_ok = 1U;
      }
    }
    else
    {
      fr = Recorder_FlushCachedLines();
      if (fr != FR_OK)
      {
        s_sd_ok = 0U;
      }
    }

    Recorder_ClearScan();
    Recorder_ClearAcc();
    s_last_record_tick = HAL_GetTick();
    return;
  }

  interval_ms = s_config->record_interval_sec * 1000UL;
  if (interval_ms == 0UL)
  {
    interval_ms = 1000UL;
  }

  elapsed = HAL_GetTick() - s_last_record_tick;
  if ((s_line_cache_count != 0U) && (elapsed < interval_ms))
  {
    fr = Recorder_FlushCachedLines();
    if (fr != FR_OK)
    {
      s_sd_ok = 0U;
    }
    else
    {
      s_sd_ok = 1U;
    }
  }

  if (elapsed >= interval_ms)
  {
    if (s_config->average_enable != 0U)
    {
      if ((have_ts != 0U) && (Recorder_HasData(s_acc_count) != 0U))
      {
        Recorder_BuildDataLine(line, sizeof(line), 'A', timestamp,
                                s_acc_uv, s_acc_count);
        fr = Recorder_QueueLine(line, 1U);
        if (fr != FR_OK)
        {
          s_sd_ok = 0U;
        }
        else
        {
          s_sd_ok = 1U;
        }
      }
      else
      {
        fr = Recorder_FlushCachedLines();
        if (fr != FR_OK)
        {
          s_sd_ok = 0U;
        }
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

  Recorder_ClearScan();
}

uint8_t Recorder_Flush(void)
{
  FRESULT fr;

  if (s_line_cache_count == 0U)
  {
    return s_sd_ok;
  }

  fr = Recorder_FlushCachedLines();
  if (fr == FR_OK)
  {
    s_sd_ok = 1U;
    return 1U;
  }

  s_sd_ok = 0U;
  return 0U;
}

uint8_t Recorder_HasPendingWrite(void)
{
  return (s_line_cache_count != 0U) ? 1U : 0U;
}

uint8_t Recorder_TakeWroteSinceLast(void)
{
  uint8_t value = s_wrote_since_last;
  s_wrote_since_last = 0U;
  return value;
}

uint8_t Recorder_TakeWriteErrorSinceLast(void)
{
  uint8_t value = s_write_error_since_last;
  s_write_error_since_last = 0U;
  return value;
}

void Recorder_InvalidateMount(void)
{
  s_sd_mounted = 0U;
  /* Reset the retry throttle so Recorder_EnsureMounted will try immediately
     on the next write attempt instead of waiting RECORDER_SD_RETRY_MS. */
  s_last_mount_attempt_tick = 0U;
}

uint8_t Recorder_NextScanWillFlush(void)
{
  uint32_t interval_ms;
  uint32_t elapsed;

  if (s_config == NULL || s_config->run_enable == 0U)
  {
    return 0U;
  }

  /* Already-cached lines waiting to be flushed → always write. */
  if (s_line_cache_count != 0U)
  {
    return 1U;
  }

  if (s_config->average_enable == 0U)
  {
    return 1U;
  }

  /* Either averaging mode writes exactly when record_interval_sec elapses:
     A-row if averaging is enabled, otherwise one S-row from the latest scan. */
  if (s_record_tick_inited != 0U)
  {
    interval_ms = s_config->record_interval_sec * 1000UL;
    if (interval_ms == 0UL)
    {
      interval_ms = 1000UL;
    }
    elapsed = HAL_GetTick() - s_last_record_tick;
    if (elapsed >= interval_ms)
    {
      return 1U;
    }
    /* Cover the case where the upcoming scan itself crosses the boundary:
       at scan-complete time, elapsed will be ~elapsed + sample_interval. */
    if ((elapsed + (s_config->sample_interval_sec * 1000UL)) >= interval_ms)
    {
      return 1U;
    }
  }

  return 0U;
}

uint8_t Recorder_GetSdOk(void)
{
  return s_sd_ok;
}

void Recorder_GetCapacityKB(uint32_t *total_kb, uint32_t *free_kb)
{
  if (total_kb != NULL)
  {
    *total_kb = s_total_kb;
  }
  if (free_kb != NULL)
  {
    *free_kb = s_free_kb;
  }
}
