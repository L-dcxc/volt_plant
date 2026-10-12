#include "app_config.h"

#include <stddef.h>
#include <stdio.h>

#define APP_CONFIG_CONTROL_DEFAULT_INTERVAL_S 600UL
#define APP_CONFIG_CONTROL_DEFAULT_ON_DURATION_S 30UL

static void AppConfig_Clear(AppConfigImage *config)
{
  uint8_t *bytes;
  uint32_t i;

  bytes = (uint8_t *)config;
  for (i = 0U; i < (uint32_t)sizeof(AppConfigImage); i++)
  {
    bytes[i] = 0U;
  }
}

static void AppConfig_SetChannel(AppChannelConfig *channel,
                                 uint8_t enable,
                                 uint8_t mode,
                                 uint8_t positive_input,
                                 uint8_t negative_input,
                                 uint8_t sensor_type,
                                 uint32_t range_uv,
                                 uint8_t gain)
{
  if (channel == NULL)
  {
    return;
  }

  channel->enable = enable;
  channel->mode = mode;
  channel->positive_input = positive_input;
  channel->negative_input = negative_input;
  channel->sensor_type = sensor_type;
  channel->gain = gain;
  channel->filter_mode = APP_CONFIG_DEFAULT_ADC_FILTER;
  channel->range_uv = range_uv;
  channel->calib_offset_uv = 0L;
  channel->calib_scale_ppm = APP_CONFIG_DEFAULT_SCALE_PPM;
  channel->warmup_ms = 0UL;
}

static void AppConfig_SetControl(AppControlOutputConfig *control,
                                 uint8_t output_id,
                                 uint32_t phase_offset_sec)
{
  if (control == NULL)
  {
    return;
  }

  control->enable = 0U;
  control->output_id = output_id;
  control->interval_sec = APP_CONFIG_CONTROL_DEFAULT_INTERVAL_S;
  control->on_duration_sec = APP_CONFIG_CONTROL_DEFAULT_ON_DURATION_S;
  control->phase_offset_sec = phase_offset_sec;
}

static uint8_t AppConfig_IsLeapYear(uint16_t year)
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

static uint8_t AppConfig_DaysInMonth(uint16_t year, uint8_t month)
{
  static const uint8_t days_table[12] =
  {
    31U, 28U, 31U, 30U, 31U, 30U,
    31U, 31U, 30U, 31U, 30U, 31U
  };

  if ((month < 1U) || (month > 12U))
  {
    return 0U;
  }
  if ((month == 2U) && (AppConfig_IsLeapYear(year) != 0U))
  {
    return 29U;
  }
  return days_table[month - 1U];
}

static uint8_t AppConfig_IsValidDateTime(uint16_t year,
                                         uint8_t month,
                                         uint8_t day,
                                         uint8_t hour,
                                         uint8_t minute,
                                         uint8_t second)
{
  uint8_t max_day;

  if ((year < 2000U) || (year > 2099U))
  {
    return 0U;
  }

  max_day = AppConfig_DaysInMonth(year, month);
  if ((max_day == 0U) || (day < 1U) || (day > max_day))
  {
    return 0U;
  }

  if ((hour > 23U) || (minute > 59U) || (second > 59U))
  {
    return 0U;
  }

  return 1U;
}

static void AppConfig_SetDefaultChannelName(AppConfigImage *config, uint32_t channel)
{
  if ((config == NULL) || (channel >= APP_CONFIG_CHANNEL_COUNT))
  {
    return;
  }

  (void)snprintf(config->channel_names[channel],
                 APP_CONFIG_CHANNEL_NAME_LEN + 1U,
                 "CH%lu",
                 (unsigned long)channel);
}

void AppConfig_LoadDefaults(AppConfigImage *config)
{
  uint32_t i;

  if (config == NULL)
  {
    return;
  }

  AppConfig_Clear(config);

  config->magic = APP_CONFIG_MAGIC;
  config->version = APP_CONFIG_VERSION;
  config->size = (uint16_t)sizeof(AppConfigImage);
  config->sequence = 0UL;
  config->crc32 = 0UL;

  config->device_id = APP_CONFIG_DEFAULT_DEVICE_ID;
  config->modbus_addr = APP_CONFIG_DEFAULT_MODBUS_ADDR;
  config->run_enable = 0U;
  config->average_enable = 1U;
  config->file_format = APP_FILE_FORMAT_CSV;

  config->uart_baudrate = APP_CONFIG_DEFAULT_UART_BAUDRATE;
  config->sample_interval_sec = APP_CONFIG_DEFAULT_SAMPLE_INTERVAL_S;
  config->record_interval_sec = APP_CONFIG_DEFAULT_RECORD_INTERVAL_S;
  config->file_span_days = APP_CONFIG_FILE_SPAN_MIN_DAYS;
  config->record_start_enable = 0U;
  config->record_start_year = 2026U;
  config->record_start_month = 1U;
  config->record_start_day = 1U;
  config->record_start_hour = 0U;
  config->record_start_minute = 0U;
  config->record_start_second = 0U;

  config->adc_vref_mv = APP_CONFIG_DEFAULT_ADC_VREF_MV;
  config->adc_default_gain = APP_CONFIG_DEFAULT_ADC_GAIN;
  config->adc_default_filter = APP_CONFIG_DEFAULT_ADC_FILTER;

  for (i = 0U; i < APP_CONFIG_CHANNEL_COUNT; i++)
  {
    AppConfig_SetChannel(&config->channels[i],
                         1U,
                         APP_CHANNEL_MODE_SINGLE_ENDED,
                         (uint8_t)(i % APP_CONFIG_CHANNELS_PER_ADC),
                         APP_ADC_INPUT_AVSS,
                         APP_SENSOR_TYPE_CUSTOM,
                         APP_CONFIG_2V_RANGE_UV,
                         APP_CONFIG_DEFAULT_ADC_GAIN);
    AppConfig_SetDefaultChannelName(config, i);
  }

  /* Default policy: expose all AD7124 AIN0~AIN15 as enabled single-ended
     channels. Differential pairing, sensor type, range and gain are left for
     the customer configuration layer to decide. */
  for (i = 0U; i < APP_CONFIG_CONTROL_COUNT; i++)
  {
    AppConfig_SetControl(&config->controls[i], (uint8_t)i, i * 5UL);
  }
}

uint8_t AppConfig_IsValidAdcInput(uint8_t input)
{
  if (input <= APP_ADC_INPUT_AIN15)
  {
    return 1U;
  }

  if (input == APP_ADC_INPUT_AVSS)
  {
    return 1U;
  }

  return 0U;
}

uint8_t AppConfig_IsValidChannelName(const char *name)
{
  uint32_t i;

  if (name == NULL)
  {
    return 0U;
  }

  for (i = 0U; i <= APP_CONFIG_CHANNEL_NAME_LEN; i++)
  {
    char ch = name[i];
    if (ch == '\0')
    {
      return 1U;
    }

    if (i >= APP_CONFIG_CHANNEL_NAME_LEN)
    {
      return 0U;
    }

    if (((uint8_t)ch < 0x20U) || ((uint8_t)ch > 0x7EU) ||
        (ch == ',') || (ch == '"'))
    {
      return 0U;
    }
  }

  return 0U;
}

uint8_t AppConfig_IsDifferentialChannel(const AppChannelConfig *channel)
{
  if (channel == NULL)
  {
    return 0U;
  }

  return (channel->mode == APP_CHANNEL_MODE_DIFFERENTIAL) ? 1U : 0U;
}

uint32_t AppConfig_GetEnabledChannelMask(const AppConfigImage *config)
{
  uint32_t mask = 0UL;
  uint32_t i;

  if (config == NULL)
  {
    return 0UL;
  }

  for (i = 0U; i < APP_CONFIG_CHANNEL_COUNT; i++)
  {
    if (config->channels[i].enable != 0U)
    {
      mask |= (1UL << i);
    }
  }

  return mask;
}

uint8_t AppConfig_IsValid(const AppConfigImage *config)
{
  uint32_t i;

  if (config == NULL)
  {
    return 0U;
  }

  if ((config->magic != APP_CONFIG_MAGIC) ||
      (config->version != APP_CONFIG_VERSION) ||
      (config->size != (uint16_t)sizeof(AppConfigImage)))
  {
    return 0U;
  }

  if ((config->modbus_addr == 0U) || (config->modbus_addr > 247U))
  {
    return 0U;
  }

  if (config->average_enable > 1U)
  {
    return 0U;
  }

  if ((config->sample_interval_sec == 0UL) ||
      (config->record_interval_sec == 0UL))
  {
    return 0U;
  }

  if ((config->average_enable != 0U) &&
      (config->record_interval_sec < config->sample_interval_sec))
  {
    return 0U;
  }

  if ((config->file_span_days < APP_CONFIG_FILE_SPAN_MIN_DAYS) ||
      (config->file_span_days > APP_CONFIG_FILE_SPAN_MAX_DAYS) ||
      (config->record_start_enable > 1U))
  {
    return 0U;
  }

  if ((config->record_start_enable != 0U) &&
      (AppConfig_IsValidDateTime(config->record_start_year,
                                 config->record_start_month,
                                 config->record_start_day,
                                 config->record_start_hour,
                                 config->record_start_minute,
                                 config->record_start_second) == 0U))
  {
    return 0U;
  }

  if ((config->adc_vref_mv == 0U) || (config->adc_default_gain == 0U))
  {
    return 0U;
  }

  if (config->file_format > APP_FILE_FORMAT_TXT)
  {
    return 0U;
  }

  for (i = 0U; i < APP_CONFIG_CHANNEL_COUNT; i++)
  {
    const AppChannelConfig *channel = &config->channels[i];

    if (AppConfig_IsValidChannelName(config->channel_names[i]) == 0U)
    {
      return 0U;
    }

    if (channel->enable == 0U)
    {
      continue;
    }

    if ((AppConfig_IsValidAdcInput(channel->positive_input) == 0U) ||
        (AppConfig_IsValidAdcInput(channel->negative_input) == 0U) ||
        (channel->positive_input == channel->negative_input) ||
        (channel->gain == 0U) ||
        (channel->range_uv == 0UL) ||
        (channel->calib_scale_ppm == 0L))
    {
      return 0U;
    }

    if (channel->mode == APP_CHANNEL_MODE_SINGLE_ENDED)
    {
      if (channel->negative_input != APP_ADC_INPUT_AVSS)
      {
        return 0U;
      }
    }
    else if (channel->mode == APP_CHANNEL_MODE_DIFFERENTIAL)
    {
      if (channel->negative_input == APP_ADC_INPUT_AVSS)
      {
        return 0U;
      }
    }
    else
    {
      return 0U;
    }
  }

  for (i = 0U; i < APP_CONFIG_CONTROL_COUNT; i++)
  {
    const AppControlOutputConfig *control = &config->controls[i];

    if (control->output_id >= APP_CONFIG_CONTROL_COUNT)
    {
      return 0U;
    }

    if (control->enable == 0U)
    {
      continue;
    }

    if ((control->interval_sec == 0UL) ||
        (control->on_duration_sec == 0UL) ||
        (control->on_duration_sec > control->interval_sec))
    {
      return 0U;
    }
  }

  return 1U;
}
