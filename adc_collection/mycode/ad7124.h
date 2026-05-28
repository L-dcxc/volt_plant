#ifndef __AD7124_H__
#define __AD7124_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define AD7124_REG_STATUS       0x00U
#define AD7124_REG_ADC_CONTROL  0x01U
#define AD7124_REG_DATA         0x02U
#define AD7124_REG_IO_CONTROL1  0x03U
#define AD7124_REG_IO_CONTROL2  0x04U
#define AD7124_REG_ID           0x05U
#define AD7124_REG_ERROR        0x06U
#define AD7124_REG_ERROR_EN     0x07U
#define AD7124_REG_MCLK_COUNT   0x08U
#define AD7124_REG_CHANNEL_0    0x09U
#define AD7124_REG_CHANNEL_15   0x18U
#define AD7124_REG_CONFIG_0     0x19U
#define AD7124_REG_FILTER_0     0x21U
#define AD7124_REG_OFFSET_0     0x29U
#define AD7124_REG_GAIN_0       0x31U

#define AD7124_DEVICE_ID_MASK   0xF0U
#define AD7124_DEVICE_ID_VALUE  0x10U
#define AD7124_STATUS_RDY       0x80U
#define AD7124_STATUS_CH_MASK   0x0FU
#define AD7124_ADC_CONTROL_DATA_STATUS 0x0400U
#define AD7124_BIPOLAR_ZERO_CODE 0x800000UL
#define AD7124_BIPOLAR_FULL_SCALE 8388608L
#define AD7124_DEFAULT_VREF_MV  2048L
#define AD7124_DEFAULT_GAIN     1U
#define AD7124_AIN15            0x0FU
#define AD7124_AIN_AVSS         0x11U
#define AD7124_CHANNEL_ENABLE   0x8000U
#define AD7124_CHANNEL_SETUP0   0x0000U

typedef struct
{
  SPI_HandleTypeDef *hspi;
  uint32_t timeout;
} AD7124_HandleTypeDef;

void AD7124_BoardPowerOn(uint32_t delay_ms);
void AD7124_BoardPowerOff(void);
HAL_StatusTypeDef AD7124_Init(AD7124_HandleTypeDef *dev, SPI_HandleTypeDef *hspi);
HAL_StatusTypeDef AD7124_Reset(AD7124_HandleTypeDef *dev);
HAL_StatusTypeDef AD7124_ReadRegister(AD7124_HandleTypeDef *dev, uint8_t reg, uint32_t *value, uint8_t size);
HAL_StatusTypeDef AD7124_WriteRegister(AD7124_HandleTypeDef *dev, uint8_t reg, uint32_t value, uint8_t size);
HAL_StatusTypeDef AD7124_ReadID(AD7124_HandleTypeDef *dev, uint8_t *id);
uint8_t AD7124_IsDeviceID(uint8_t id);
HAL_StatusTypeDef AD7124_ReadStatus(AD7124_HandleTypeDef *dev, uint8_t *status);
HAL_StatusTypeDef AD7124_ReadAdcControl(AD7124_HandleTypeDef *dev, uint16_t *control);
HAL_StatusTypeDef AD7124_ReadError(AD7124_HandleTypeDef *dev, uint32_t *error);
HAL_StatusTypeDef AD7124_ConfigDefaultChannel0(AD7124_HandleTypeDef *dev);
HAL_StatusTypeDef AD7124_ConfigAin15SingleEnded(AD7124_HandleTypeDef *dev);
HAL_StatusTypeDef AD7124_WaitDataReady(AD7124_HandleTypeDef *dev, uint32_t timeout_ms);
HAL_StatusTypeDef AD7124_ReadData(AD7124_HandleTypeDef *dev, uint32_t *raw_data);
HAL_StatusTypeDef AD7124_ReadDataWithStatus(AD7124_HandleTypeDef *dev, uint32_t *raw_data, uint8_t *status);
HAL_StatusTypeDef AD7124_ReadSample(AD7124_HandleTypeDef *dev, uint32_t *raw_data, int32_t *signed_data, uint8_t *status, uint32_t timeout_ms);
uint8_t AD7124_StatusToChannel(uint8_t status);
int32_t AD7124_BipolarCodeToSigned(uint32_t raw_data);
int32_t AD7124_BipolarCodeToMicrovolts(uint32_t raw_data, int32_t vref_mv, uint8_t gain);

#ifdef __cplusplus
}
#endif

#endif
