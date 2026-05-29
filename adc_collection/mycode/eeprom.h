#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EEPROM_24C64_I2C_ADDR_7BIT        0x50U
#define EEPROM_24C64_I2C_ADDR_HAL         (EEPROM_24C64_I2C_ADDR_7BIT << 1U)
#define EEPROM_24C64_SIZE_BYTES           8192U
#define EEPROM_24C64_PAGE_SIZE_BYTES      32U
#define EEPROM_24C64_MEM_ADDR_SIZE        I2C_MEMADD_SIZE_16BIT
#define EEPROM_DEFAULT_TIMEOUT_MS         100U
#define EEPROM_READY_TRIALS               20U

typedef struct
{
  I2C_HandleTypeDef *hi2c;
  uint16_t device_address;
  uint32_t timeout_ms;
} EepromHandle;

void Eeprom_Init(EepromHandle *dev, I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef Eeprom_IsReady(EepromHandle *dev);
HAL_StatusTypeDef Eeprom_Read(EepromHandle *dev, uint16_t address, uint8_t *data, uint16_t length);
HAL_StatusTypeDef Eeprom_Write(EepromHandle *dev, uint16_t address, const uint8_t *data, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif
