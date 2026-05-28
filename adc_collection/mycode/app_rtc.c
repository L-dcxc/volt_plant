#include "app_rtc.h"

#include "main.h"
#include "rtc.h"

static uint8_t app_rtc_initialized = 0U;

static void AppRtc_EnableBackupAccess(void)
{
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
}

static uint8_t AppRtc_IsLeapYear(uint16_t year)
{
  if ((year % 400U) == 0U)
  {
    return 1U;
  }
  if ((year % 100U) == 0U)
  {
    return 0U;
  }
  if ((year % 4U) == 0U)
  {
    return 1U;
  }

  return 0U;
}

static uint8_t AppRtc_DaysInMonth(uint16_t year, uint8_t month)
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

  if ((month == 2U) && (AppRtc_IsLeapYear(year) != 0U))
  {
    return 29U;
  }

  return days_table[month - 1U];
}

static uint8_t AppRtc_IsValidDateTime(const AppRtcDateTime *date_time)
{
  uint8_t max_day;

  if (date_time == NULL)
  {
    return 0U;
  }

  if ((date_time->year < 2000U) || (date_time->year > 2099U))
  {
    return 0U;
  }

  if ((date_time->month < 1U) || (date_time->month > 12U))
  {
    return 0U;
  }

  max_day = AppRtc_DaysInMonth(date_time->year, date_time->month);
  if ((date_time->day < 1U) || (date_time->day > max_day))
  {
    return 0U;
  }

  if ((date_time->hour > 23U) || (date_time->minute > 59U) || (date_time->second > 59U))
  {
    return 0U;
  }

  return 1U;
}

static uint8_t AppRtc_CalculateWeekDay(uint16_t year, uint8_t month, uint8_t day)
{
  static const uint8_t month_offsets[12] =
  {
    0U, 3U, 2U, 5U, 0U, 3U,
    5U, 1U, 4U, 6U, 2U, 4U
  };
  uint32_t y = year;
  uint32_t week_day;

  if (month < 3U)
  {
    y--;
  }

  week_day = (y + (y / 4U) - (y / 100U) + (y / 400U) + month_offsets[month - 1U] + day) % 7U;
  if (week_day == 0U)
  {
    return RTC_WEEKDAY_SUNDAY;
  }

  return (uint8_t)week_day;
}

static void AppRtc_WriteTwoDigits(char *buffer, uint32_t index, uint8_t value)
{
  buffer[index] = (char)('0' + (value / 10U));
  buffer[index + 1U] = (char)('0' + (value % 10U));
}

void AppRtc_Init(void)
{
  AppRtcDateTime default_time;

  AppRtc_EnableBackupAccess();
  if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == APP_RTC_BKP_MARKER)
  {
    app_rtc_initialized = 1U;
    return;
  }

  default_time.year = APP_RTC_DEFAULT_YEAR;
  default_time.month = APP_RTC_DEFAULT_MONTH;
  default_time.day = APP_RTC_DEFAULT_DAY;
  default_time.hour = APP_RTC_DEFAULT_HOUR;
  default_time.minute = APP_RTC_DEFAULT_MINUTE;
  default_time.second = APP_RTC_DEFAULT_SECOND;

  (void)AppRtc_SetDateTime(&default_time);
}

uint8_t AppRtc_SetDateTime(const AppRtcDateTime *date_time)
{
  RTC_TimeTypeDef rtc_time = {0};
  RTC_DateTypeDef rtc_date = {0};

  if (AppRtc_IsValidDateTime(date_time) == 0U)
  {
    return 0U;
  }

  rtc_time.Hours = date_time->hour;
  rtc_time.Minutes = date_time->minute;
  rtc_time.Seconds = date_time->second;
  rtc_time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  rtc_time.StoreOperation = RTC_STOREOPERATION_RESET;

  rtc_date.WeekDay = AppRtc_CalculateWeekDay(date_time->year, date_time->month, date_time->day);
  rtc_date.Month = date_time->month;
  rtc_date.Date = date_time->day;
  rtc_date.Year = (uint8_t)(date_time->year - 2000U);

  if (HAL_RTC_SetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK)
  {
    return 0U;
  }

  if (HAL_RTC_SetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK)
  {
    return 0U;
  }

  AppRtc_EnableBackupAccess();
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, APP_RTC_BKP_MARKER);
  app_rtc_initialized = 1U;

  return 1U;
}

uint8_t AppRtc_GetDateTime(AppRtcDateTime *date_time)
{
  RTC_TimeTypeDef rtc_time = {0};
  RTC_DateTypeDef rtc_date = {0};

  if (date_time == NULL)
  {
    return 0U;
  }

  if (app_rtc_initialized == 0U)
  {
    AppRtc_Init();
  }

  if (HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK)
  {
    return 0U;
  }

  if (HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK)
  {
    return 0U;
  }

  date_time->year = (uint16_t)rtc_date.Year + 2000U;
  date_time->month = rtc_date.Month;
  date_time->day = rtc_date.Date;
  date_time->hour = rtc_time.Hours;
  date_time->minute = rtc_time.Minutes;
  date_time->second = rtc_time.Seconds;

  return 1U;
}

uint8_t AppRtc_FormatTimestamp(char *buffer, uint32_t buffer_size)
{
  AppRtcDateTime now;
  uint16_t year;

  if ((buffer == NULL) || (buffer_size < APP_RTC_TIMESTAMP_SIZE))
  {
    return 0U;
  }

  if (AppRtc_GetDateTime(&now) == 0U)
  {
    return 0U;
  }
  year = now.year;

  buffer[0] = (char)('0' + ((year / 1000U) % 10U));
  buffer[1] = (char)('0' + ((year / 100U) % 10U));
  buffer[2] = (char)('0' + ((year / 10U) % 10U));
  buffer[3] = (char)('0' + (year % 10U));
  buffer[4] = '-';
  AppRtc_WriteTwoDigits(buffer, 5U, now.month);
  buffer[7] = '-';
  AppRtc_WriteTwoDigits(buffer, 8U, now.day);
  buffer[10] = ' ';
  AppRtc_WriteTwoDigits(buffer, 11U, now.hour);
  buffer[13] = ':';
  AppRtc_WriteTwoDigits(buffer, 14U, now.minute);
  buffer[16] = ':';
  AppRtc_WriteTwoDigits(buffer, 17U, now.second);
  buffer[19] = '\0';

  return 1U;
}

DWORD AppRtc_GetFatTime(void)
{
  AppRtcDateTime now;
  DWORD fat_time;

  if (AppRtc_GetDateTime(&now) == 0U)
  {
    return 0U;
  }

  fat_time = ((DWORD)(now.year - 1980U) << 25U)
           | ((DWORD)now.month << 21U)
           | ((DWORD)now.day << 16U)
           | ((DWORD)now.hour << 11U)
           | ((DWORD)now.minute << 5U)
           | ((DWORD)(now.second / 2U));

  return fat_time;
}
