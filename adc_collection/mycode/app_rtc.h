#ifndef APP_RTC_H
#define APP_RTC_H

#include <stdint.h>
#include "ff.h"

#define APP_RTC_TIMESTAMP_SIZE 20U
#define APP_RTC_BKP_MARKER 0x4231U
#define APP_RTC_DEFAULT_YEAR 2026U
#define APP_RTC_DEFAULT_MONTH 5U
#define APP_RTC_DEFAULT_DAY 28U
#define APP_RTC_DEFAULT_HOUR 14U
#define APP_RTC_DEFAULT_MINUTE 56U
#define APP_RTC_DEFAULT_SECOND 0U

typedef struct
{
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} AppRtcDateTime;

void AppRtc_Init(void);
uint8_t AppRtc_SetDateTime(const AppRtcDateTime *date_time);
uint8_t AppRtc_GetDateTime(AppRtcDateTime *date_time);
uint8_t AppRtc_FormatTimestamp(char *buffer, uint32_t buffer_size);
DWORD AppRtc_GetFatTime(void);

#endif
