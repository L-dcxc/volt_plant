#include "ad7124.h"
#include <string.h>

#define AD7124_COMM_READ        0x40U
#define AD7124_COMM_WRITE       0x00U
#define AD7124_COMM_ADDR_MASK   0x3FU
#define AD7124_DEFAULT_TIMEOUT  100U

void AD7124_BoardPowerOn(uint32_t delay_ms)
{
  HAL_GPIO_WritePin(ADC_PWR_EN_GPIO_Port, ADC_PWR_EN_Pin, GPIO_PIN_SET);
  HAL_Delay(delay_ms);
}

void AD7124_BoardPowerOff(void)
{
  HAL_GPIO_WritePin(ADC_PWR_EN_GPIO_Port, ADC_PWR_EN_Pin, GPIO_PIN_RESET);
}

HAL_StatusTypeDef AD7124_Init(AD7124_HandleTypeDef *dev, SPI_HandleTypeDef *hspi)
{
  if ((dev == NULL) || (hspi == NULL))
  {
    return HAL_ERROR;
  }

  dev->hspi = hspi;
  dev->timeout = AD7124_DEFAULT_TIMEOUT;

  return AD7124_Reset(dev);
}

HAL_StatusTypeDef AD7124_Reset(AD7124_HandleTypeDef *dev)
{
  uint8_t reset_buf[8];
  HAL_StatusTypeDef st;

  if ((dev == NULL) || (dev->hspi == NULL))
  {
    return HAL_ERROR;
  }

  memset(reset_buf, 0xFF, sizeof(reset_buf));
  st = HAL_SPI_Transmit(dev->hspi, reset_buf, sizeof(reset_buf), dev->timeout);
  HAL_Delay(2);

  return st;
}

HAL_StatusTypeDef AD7124_ReadRegister(AD7124_HandleTypeDef *dev, uint8_t reg, uint32_t *value, uint8_t size)
{
  uint8_t tx[5] = {0};
  uint8_t rx[5] = {0};
  HAL_StatusTypeDef st;
  uint32_t v = 0U;
  uint8_t i;

  if ((dev == NULL) || (dev->hspi == NULL) || (value == NULL) || (size == 0U) || (size > 4U))
  {
    return HAL_ERROR;
  }

  tx[0] = (uint8_t)(AD7124_COMM_READ | (reg & AD7124_COMM_ADDR_MASK));
  for (i = 1U; i <= size; i++)
  {
    tx[i] = 0xFFU;
  }

  st = HAL_SPI_TransmitReceive(dev->hspi, tx, rx, (uint16_t)(size + 1U), dev->timeout);
  if (st != HAL_OK)
  {
    return st;
  }

  for (i = 0U; i < size; i++)
  {
    v = (v << 8) | rx[1U + i];
  }

  *value = v;
  return HAL_OK;
}

HAL_StatusTypeDef AD7124_WriteRegister(AD7124_HandleTypeDef *dev, uint8_t reg, uint32_t value, uint8_t size)
{
  uint8_t tx[5] = {0};
  uint8_t i;

  if ((dev == NULL) || (dev->hspi == NULL) || (size == 0U) || (size > 4U))
  {
    return HAL_ERROR;
  }

  tx[0] = (uint8_t)(AD7124_COMM_WRITE | (reg & AD7124_COMM_ADDR_MASK));
  for (i = 0U; i < size; i++)
  {
    tx[1U + i] = (uint8_t)(value >> (8U * (size - 1U - i)));
  }

  return HAL_SPI_Transmit(dev->hspi, tx, (uint16_t)(size + 1U), dev->timeout);
}

HAL_StatusTypeDef AD7124_ReadID(AD7124_HandleTypeDef *dev, uint8_t *id)
{
  uint32_t value;
  HAL_StatusTypeDef st;

  if (id == NULL)
  {
    return HAL_ERROR;
  }

  st = AD7124_ReadRegister(dev, AD7124_REG_ID, &value, 1U);
  if (st == HAL_OK)
  {
    *id = (uint8_t)value;
  }

  return st;
}

uint8_t AD7124_IsDeviceID(uint8_t id)
{
  return (((id & AD7124_DEVICE_ID_MASK) == AD7124_DEVICE_ID_VALUE) ? 1U : 0U);
}

HAL_StatusTypeDef AD7124_ReadStatus(AD7124_HandleTypeDef *dev, uint8_t *status)
{
  uint32_t value;
  HAL_StatusTypeDef st;

  if (status == NULL)
  {
    return HAL_ERROR;
  }

  st = AD7124_ReadRegister(dev, AD7124_REG_STATUS, &value, 1U);
  if (st == HAL_OK)
  {
    *status = (uint8_t)value;
  }

  return st;
}

HAL_StatusTypeDef AD7124_ReadAdcControl(AD7124_HandleTypeDef *dev, uint16_t *control)
{
  uint32_t value;
  HAL_StatusTypeDef st;

  if (control == NULL)
  {
    return HAL_ERROR;
  }

  st = AD7124_ReadRegister(dev, AD7124_REG_ADC_CONTROL, &value, 2U);
  if (st == HAL_OK)
  {
    *control = (uint16_t)value;
  }

  return st;
}

HAL_StatusTypeDef AD7124_ReadError(AD7124_HandleTypeDef *dev, uint32_t *error)
{
  return AD7124_ReadRegister(dev, AD7124_REG_ERROR, error, 3U);
}

HAL_StatusTypeDef AD7124_ConfigDefaultChannel0(AD7124_HandleTypeDef *dev)
{
  HAL_StatusTypeDef st;

  st = AD7124_WriteRegister(dev, AD7124_REG_CHANNEL_0, 0x8001U, 2U);
  if (st != HAL_OK)
  {
    return st;
  }

  st = AD7124_WriteRegister(dev, AD7124_REG_CONFIG_0, 0x0860U, 2U);
  if (st != HAL_OK)
  {
    return st;
  }

  st = AD7124_WriteRegister(dev, AD7124_REG_FILTER_0, 0x060180U, 3U);
  if (st != HAL_OK)
  {
    return st;
  }

  return AD7124_WriteRegister(dev, AD7124_REG_ADC_CONTROL, AD7124_ADC_CONTROL_DATA_STATUS, 2U);
}

HAL_StatusTypeDef AD7124_ConfigAin15SingleEnded(AD7124_HandleTypeDef *dev)
{
  HAL_StatusTypeDef st;
  uint32_t channel_value;

  st = AD7124_WriteRegister(dev, AD7124_REG_CHANNEL_0, 0x0000U, 2U);
  if (st != HAL_OK)
  {
    return st;
  }

  channel_value = AD7124_CHANNEL_ENABLE |
                  AD7124_CHANNEL_SETUP0 |
                  ((uint32_t)AD7124_AIN15 << 5) |
                  (uint32_t)AD7124_AIN_AVSS;
  st = AD7124_WriteRegister(dev, AD7124_REG_CHANNEL_15, channel_value, 2U);
  if (st != HAL_OK)
  {
    return st;
  }

  st = AD7124_WriteRegister(dev, AD7124_REG_CONFIG_0, 0x0800U, 2U);
  if (st != HAL_OK)
  {
    return st;
  }

  st = AD7124_WriteRegister(dev, AD7124_REG_FILTER_0, 0x060180U, 3U);
  if (st != HAL_OK)
  {
    return st;
  }

  return AD7124_WriteRegister(dev, AD7124_REG_ADC_CONTROL, AD7124_ADC_CONTROL_DATA_STATUS, 2U);
}

HAL_StatusTypeDef AD7124_WaitDataReady(AD7124_HandleTypeDef *dev, uint32_t timeout_ms)
{
  uint32_t start_tick;
  uint8_t status;
  HAL_StatusTypeDef st;

  start_tick = HAL_GetTick();
  do
  {
    st = AD7124_ReadStatus(dev, &status);
    if (st != HAL_OK)
    {
      return st;
    }

    if ((status & AD7124_STATUS_RDY) == 0U)
    {
      return HAL_OK;
    }
  } while ((HAL_GetTick() - start_tick) < timeout_ms);

  return HAL_TIMEOUT;
}

HAL_StatusTypeDef AD7124_ReadData(AD7124_HandleTypeDef *dev, uint32_t *raw_data)
{
  return AD7124_ReadRegister(dev, AD7124_REG_DATA, raw_data, 3U);
}

HAL_StatusTypeDef AD7124_ReadDataWithStatus(AD7124_HandleTypeDef *dev, uint32_t *raw_data, uint8_t *status)
{
  uint32_t value;
  HAL_StatusTypeDef st;

  if ((raw_data == NULL) || (status == NULL))
  {
    return HAL_ERROR;
  }

  st = AD7124_ReadRegister(dev, AD7124_REG_DATA, &value, 4U);
  if (st != HAL_OK)
  {
    return st;
  }

  *raw_data = (value >> 8) & 0x00FFFFFFUL;
  *status = (uint8_t)value;

  return HAL_OK;
}

HAL_StatusTypeDef AD7124_ReadSample(AD7124_HandleTypeDef *dev, uint32_t *raw_data, int32_t *signed_data, uint8_t *status, uint32_t timeout_ms)
{
  HAL_StatusTypeDef st;
  uint32_t raw;
  uint8_t data_status;

  if ((raw_data == NULL) || (signed_data == NULL))
  {
    return HAL_ERROR;
  }

  st = AD7124_WaitDataReady(dev, timeout_ms);
  if (st != HAL_OK)
  {
    return st;
  }

  st = AD7124_ReadDataWithStatus(dev, &raw, &data_status);
  if (st != HAL_OK)
  {
    return st;
  }

  if (status != NULL)
  {
    *status = data_status;
  }

  *raw_data = raw & 0x00FFFFFFUL;
  *signed_data = AD7124_BipolarCodeToSigned(raw);

  return HAL_OK;
}

uint8_t AD7124_StatusToChannel(uint8_t status)
{
  return (status & AD7124_STATUS_CH_MASK);
}

int32_t AD7124_BipolarCodeToSigned(uint32_t raw_data)
{
  raw_data &= 0x00FFFFFFUL;
  return ((int32_t)raw_data - (int32_t)AD7124_BIPOLAR_ZERO_CODE);
}

int32_t AD7124_BipolarCodeToMicrovolts(uint32_t raw_data, int32_t vref_mv, uint8_t gain)
{
  int64_t code;
  int64_t uv;

  if ((vref_mv <= 0) || (gain == 0U))
  {
    return 0;
  }

  code = (int64_t)AD7124_BipolarCodeToSigned(raw_data);
  uv = code * (int64_t)vref_mv * 1000LL;
  uv /= ((int64_t)AD7124_BIPOLAR_FULL_SCALE * (int64_t)gain);

  return (int32_t)uv;
}
