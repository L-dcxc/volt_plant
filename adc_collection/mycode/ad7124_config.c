#include "ad7124.h"
#include <string.h>

/* Helper: Map gain value to PGA bits (0-7 for gains 1,2,4,8,16,32,64,128) */
static uint8_t GainToPgaBits(uint8_t gain)
{
  switch (gain)
  {
    case 1:   return 0U;
    case 2:   return 1U;
    case 4:   return 2U;
    case 8:   return 3U;
    case 16:  return 4U;
    case 32:  return 5U;
    case 64:  return 6U;
    case 128: return 7U;
    default:  return 0U; /* Invalid gain, default to 1 */
  }
}

/* Apply full configuration from AppConfigImage */
HAL_StatusTypeDef AD7124_ApplyConfig(AD7124_HandleTypeDef *dev, const AppConfigImage *config)
{
  HAL_StatusTypeDef st;
  uint8_t setup_count = 0U;
  uint8_t channel_to_setup[APP_CONFIG_CHANNEL_COUNT];

  /* Setup allocation: map unique (gain, filter_mode) combinations to SETUP_0..7 */
  typedef struct {
    uint8_t gain;
    uint8_t filter_mode;
    uint8_t setup_idx;
  } SetupMapping;

  SetupMapping setups[8];
  memset(setups, 0, sizeof(setups));
  memset(channel_to_setup, 0, sizeof(channel_to_setup));

  if (dev == NULL || config == NULL)
    return HAL_ERROR;

  /* Pass 1: Allocate SETUPs for unique (gain, filter_mode) combinations */
  for (uint8_t ch = 0U; ch < APP_CONFIG_CHANNEL_COUNT; ch++)
  {
    const AppChannelConfig *ch_cfg = &config->channels[ch];

    if (ch_cfg->enable == 0U)
    {
      channel_to_setup[ch] = 0U; /* Disabled channels use SETUP0 (won't be read) */
      continue;
    }

    /* Find existing setup with same gain and filter */
    uint8_t found = 0U;
    for (uint8_t s = 0U; s < setup_count; s++)
    {
      if (setups[s].gain == ch_cfg->gain && setups[s].filter_mode == ch_cfg->filter_mode)
      {
        channel_to_setup[ch] = setups[s].setup_idx;
        found = 1U;
        break;
      }
    }

    if (found == 0U)
    {
      /* Allocate new setup */
      if (setup_count >= 8U)
        return HAL_ERROR; /* Too many unique configurations */

      setups[setup_count].gain = ch_cfg->gain;
      setups[setup_count].filter_mode = ch_cfg->filter_mode;
      setups[setup_count].setup_idx = setup_count;
      channel_to_setup[ch] = setup_count;
      setup_count++;
    }
  }

  /* Pass 2: Write CONFIG and FILTER registers for allocated SETUPs */
  for (uint8_t s = 0U; s < setup_count; s++)
  {
    uint8_t pga_bits = GainToPgaBits(setups[s].gain);

    /* CONFIG register: Bipolar=1, Burnout=0, REF_BUFP=1, REF_BUFM=1,
       AIN_BUFP=0, AIN_BUFM=0, REF_SEL=0 (internal), PGA=pga_bits */
    uint16_t config_val = 0x0800U | pga_bits; /* Bipolar + internal ref + PGA */

    st = AD7124_WriteRegister(dev, (uint8_t)(AD7124_REG_CONFIG_0 + s), config_val, 2U);
    if (st != HAL_OK)
      return st;

    /* FILTER register: Use default sinc4, FS=384 (10 SPS for 614.4kHz clock) */
    uint32_t filter_val = 0x060180U; /* Sinc4, FS=384 */
    st = AD7124_WriteRegister(dev, (uint8_t)(AD7124_REG_FILTER_0 + s), filter_val, 3U);
    if (st != HAL_OK)
      return st;
  }

  /* Pass 3: Write CHANNEL registers */
  for (uint8_t ch = 0U; ch < APP_CONFIG_CHANNEL_COUNT; ch++)
  {
    const AppChannelConfig *ch_cfg = &config->channels[ch];
    uint16_t channel_val = 0x0000U;

    if (ch_cfg->enable != 0U)
    {
      uint8_t setup_sel = channel_to_setup[ch];

      /* CHANNEL register bits (verified against reset value 0x8001 =
         Enable=1, Setup=0, AINP=AIN0, AINM=AIN1):
         [15]    Enable
         [14:12] Setup (0-7)
         [11:10] Reserved (0)
         [9:5]   AINP (positive input, 5 bits)
         [4:0]   AINM (negative input, 5 bits; AVSS = 0x11) */
      channel_val = 0x8000U | /* Enable */
                    ((uint16_t)setup_sel << 12) |
                    ((uint16_t)ch_cfg->positive_input << 5) |
                    ((uint16_t)ch_cfg->negative_input);
    }

    st = AD7124_WriteRegister(dev, (uint8_t)(AD7124_REG_CHANNEL_0 + ch), channel_val, 2U);
    if (st != HAL_OK)
      return st;
  }

  /* Write ADC_CONTROL: Enable DATA_STATUS bit */
  st = AD7124_WriteRegister(dev, AD7124_REG_ADC_CONTROL, AD7124_ADC_CONTROL_DATA_STATUS, 2U);

  return st;
}
