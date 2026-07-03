#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CONFIG_MAGIC              0x504C414EUL  /* "PLAN": plant logger configuration image */
#define APP_FIRMWARE_VERSION          0x0100U       /* BCD-style: 0x0100 = v1.00 */
#define APP_CONFIG_VERSION            2U
#define APP_CONFIG_CHANNEL_COUNT      16U
#define APP_CONFIG_CONTROL_COUNT      4U
#define APP_CONFIG_RESERVED_SIZE      64U

#define APP_CONFIG_DEFAULT_DEVICE_ID          1UL
#define APP_CONFIG_DEFAULT_MODBUS_ADDR        1U
#define APP_CONFIG_DEFAULT_UART_BAUDRATE      115200UL
#define APP_CONFIG_DEFAULT_SAMPLE_INTERVAL_S  60UL
#define APP_CONFIG_DEFAULT_RECORD_INTERVAL_S  600UL
#define APP_CONFIG_DEFAULT_ADC_VREF_MV        2048U
#define APP_CONFIG_DEFAULT_ADC_GAIN           1U
#define APP_CONFIG_DEFAULT_ADC_FILTER         0U
#define APP_CONFIG_DEFAULT_SCALE_PPM          1000000L

#define APP_CONFIG_2V_RANGE_UV      2000000UL
#define APP_CONFIG_2MV_RANGE_UV     2000UL

/* AD7124 analog input selector values used by channel configuration.
   AVSS means the channel is measured against board analog ground. */
typedef enum
{
  APP_ADC_INPUT_AIN0 = 0U,
  APP_ADC_INPUT_AIN1 = 1U,
  APP_ADC_INPUT_AIN2 = 2U,
  APP_ADC_INPUT_AIN3 = 3U,
  APP_ADC_INPUT_AIN4 = 4U,
  APP_ADC_INPUT_AIN5 = 5U,
  APP_ADC_INPUT_AIN6 = 6U,
  APP_ADC_INPUT_AIN7 = 7U,
  APP_ADC_INPUT_AIN8 = 8U,
  APP_ADC_INPUT_AIN9 = 9U,
  APP_ADC_INPUT_AIN10 = 10U,
  APP_ADC_INPUT_AIN11 = 11U,
  APP_ADC_INPUT_AIN12 = 12U,
  APP_ADC_INPUT_AIN13 = 13U,
  APP_ADC_INPUT_AIN14 = 14U,
  APP_ADC_INPUT_AIN15 = 15U,
  APP_ADC_INPUT_AVSS = 17U
} AppAdcInput;

/* Single-ended and differential are represented by AINP/AINM pairs.
   Single-ended: AINM = AVSS. Differential: AINM = another AINx pin. */
typedef enum
{
  APP_CHANNEL_MODE_SINGLE_ENDED = 0U,
  APP_CHANNEL_MODE_DIFFERENTIAL = 1U
} AppChannelMode;

typedef enum
{
  APP_SENSOR_TYPE_UNUSED = 0U,
  APP_SENSOR_TYPE_TEMP_HUMIDITY = 1U,
  APP_SENSOR_TYPE_PAR = 2U,
  APP_SENSOR_TYPE_SOIL_MOISTURE = 3U,
  APP_SENSOR_TYPE_TRUNK_WATER = 4U,
  APP_SENSOR_TYPE_CUSTOM = 255U
} AppSensorType;

typedef enum
{
  APP_FILE_FORMAT_CSV = 0U,
  APP_FILE_FORMAT_DAT = 1U,
  APP_FILE_FORMAT_TXT = 2U
} AppFileFormat;

/* One logical measurement channel. It is intentionally independent from
   physical connector names so the final wiring table can be changed without
   changing the data model stored in EEPROM. */
typedef struct
{
  uint8_t enable;
  uint8_t mode;
  uint8_t positive_input;
  uint8_t negative_input;

  uint8_t sensor_type;
  uint8_t gain;
  uint8_t filter_mode;
  uint8_t reserved0;

  uint32_t range_uv;
  int32_t calib_offset_uv;
  int32_t calib_scale_ppm;
  uint32_t warmup_ms;
} AppChannelConfig;

/* Periodic control output used for relay/heater style external actions.
   phase_offset_sec allows several outputs to avoid switching at the same time. */
typedef struct
{
  uint8_t enable;
  uint8_t output_id;
  uint16_t reserved0;

  uint32_t interval_sec;
  uint32_t on_duration_sec;
  uint32_t phase_offset_sec;
} AppControlOutputConfig;

/* Versioned EEPROM image. magic/version/size identify format compatibility;
   sequence/crc32 are reserved for the later A/B slot storage layer. */
typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t sequence;
  uint32_t crc32;

  uint32_t device_id;
  uint8_t modbus_addr;
  uint8_t run_enable;
  uint8_t average_enable;
  uint8_t file_format;

  uint32_t uart_baudrate;
  uint32_t sample_interval_sec;
  uint32_t record_interval_sec;

  uint16_t adc_vref_mv;
  uint8_t adc_default_gain;
  uint8_t adc_default_filter;

  AppChannelConfig channels[APP_CONFIG_CHANNEL_COUNT];
  AppControlOutputConfig controls[APP_CONFIG_CONTROL_COUNT];

  uint8_t reserved[APP_CONFIG_RESERVED_SIZE];
} AppConfigImage;

void AppConfig_LoadDefaults(AppConfigImage *config);
uint8_t AppConfig_IsValid(const AppConfigImage *config);
uint8_t AppConfig_IsValidAdcInput(uint8_t input);
uint8_t AppConfig_IsDifferentialChannel(const AppChannelConfig *channel);
uint32_t AppConfig_GetEnabledChannelMask(const AppConfigImage *config);

#ifdef __cplusplus
}
#endif

#endif
