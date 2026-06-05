#include "app_config.h"

#include <stddef.h>

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

  config->adc_vref_mv = APP_CONFIG_DEFAULT_ADC_VREF_MV;
  config->adc_default_gain = APP_CONFIG_DEFAULT_ADC_GAIN;
  config->adc_default_filter = APP_CONFIG_DEFAULT_ADC_FILTER;

  for (i = 0U; i < APP_CONFIG_CHANNEL_COUNT; i++)
  {
    AppConfig_SetChannel(&config->channels[i],
                         1U,
                         APP_CHANNEL_MODE_SINGLE_ENDED,
                         (uint8_t)i,
                         APP_ADC_INPUT_AVSS,
                         APP_SENSOR_TYPE_CUSTOM,
                         APP_CONFIG_2V_RANGE_UV,
                         APP_CONFIG_DEFAULT_ADC_GAIN);
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

  if ((config->sample_interval_sec == 0UL) ||
      (config->record_interval_sec == 0UL) ||
      (config->record_interval_sec < config->sample_interval_sec))
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
