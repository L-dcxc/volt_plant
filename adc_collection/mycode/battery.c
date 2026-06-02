#include "battery.h"

#include "adc.h"
#include "main.h"
#include "stm32l4xx_ll_adc.h"

/* Resistor divider: VBAT -> R19(10k) -> node -> R23(10k) -> GND.
   node = VBAT * R23/(R19+R23) = VBAT/2, so VBAT = node * (R19+R23)/R23. */
#define BAT_DIV_NUM   (10U + 10U)   /* R19 + R23 */
#define BAT_DIV_DEN   (10U)         /* R23       */

#define BAT_STABILIZE_MS   2U       /* let the divider/cap settle after EN */
#define BAT_ADC_TIMEOUT_MS 10U

static uint16_t s_vbat_mv = 0U;
static uint16_t s_vdda_mv = 0U;

static void Battery_EnableDivider(uint8_t on)
{
  HAL_GPIO_WritePin(BAT_ADC_EN_GPIO_Port, BAT_ADC_EN_Pin,
                    (on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Configure ADC1 for a single channel and run one software conversion.
   Returns 1 and writes *out_raw on success. */
static uint8_t Battery_SampleChannel(uint32_t channel, uint32_t sampling_time,
                                     uint16_t *out_raw)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  sConfig.Channel = channel;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = sampling_time;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    return 0U;
  }

  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    return 0U;
  }
  if (HAL_ADC_PollForConversion(&hadc1, BAT_ADC_TIMEOUT_MS) != HAL_OK)
  {
    (void)HAL_ADC_Stop(&hadc1);
    return 0U;
  }

  *out_raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
  (void)HAL_ADC_Stop(&hadc1);
  return 1U;
}

void Battery_Init(void)
{
  Battery_EnableDivider(0U);   /* keep divider off until we sample */
  s_vbat_mv = 0U;
  s_vdda_mv = 0U;
}

uint8_t Battery_Update(void)
{
  uint16_t raw_vref = 0U;
  uint16_t raw_bat = 0U;
  uint32_t vdda_mv;
  uint32_t node_mv;
  uint32_t vbat_mv;
  uint8_t ok;

  /* 1) Internal reference: needs the VREFINT path enabled in the ADC common
        registers. Long sampling time per the datasheet. */
  ADC_Common_TypeDef *common = __LL_ADC_COMMON_INSTANCE(ADC1);
  LL_ADC_SetCommonPathInternalCh(common,
      LL_ADC_GetCommonPathInternalCh(common) | LL_ADC_PATH_INTERNAL_VREFINT);
  HAL_Delay(1U); /* VREFINT startup */

  ok = Battery_SampleChannel(ADC_CHANNEL_VREFINT, ADC_SAMPLETIME_247CYCLES_5,
                             &raw_vref);
  if ((ok == 0U) || (raw_vref == 0U))
  {
    return 0U;
  }

  /* Actual VDDA from the factory VREFINT calibration. */
  vdda_mv = __LL_ADC_CALC_VREFANALOG_VOLTAGE(raw_vref, LL_ADC_RESOLUTION_12B);

  /* 2) Battery divider node on IN1, with the P-MOS path enabled. */
  Battery_EnableDivider(1U);
  HAL_Delay(BAT_STABILIZE_MS);

  ok = Battery_SampleChannel(ADC_CHANNEL_1, ADC_SAMPLETIME_247CYCLES_5,
                             &raw_bat);
  Battery_EnableDivider(0U);
  if (ok == 0U)
  {
    return 0U;
  }

  /* node voltage in mV using the runtime VDDA, then undo the /2 divider. */
  node_mv = __LL_ADC_CALC_DATA_TO_VOLTAGE(vdda_mv, raw_bat, LL_ADC_RESOLUTION_12B);
  vbat_mv = (node_mv * BAT_DIV_NUM) / BAT_DIV_DEN;

  s_vdda_mv = (uint16_t)vdda_mv;
  s_vbat_mv = (uint16_t)((vbat_mv > 0xFFFFU) ? 0xFFFFU : vbat_mv);
  return 1U;
}

uint16_t Battery_GetMilliVolts(void)
{
  return s_vbat_mv;
}

uint16_t Battery_GetVddaMilliVolts(void)
{
  return s_vdda_mv;
}
