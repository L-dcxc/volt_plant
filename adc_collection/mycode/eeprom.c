#include "eeprom.h"

static uint8_t Eeprom_IsRangeValid(uint16_t address, uint16_t length)
{
  uint32_t end_address;

  if (length == 0U)
  {
    return 1U;
  }

  end_address = (uint32_t)address + (uint32_t)length;
  if (end_address > EEPROM_24C64_SIZE_BYTES)
  {
    return 0U;
  }

  return 1U;
}

void Eeprom_Init(EepromHandle *dev, I2C_HandleTypeDef *hi2c)
{
  if (dev == NULL)
  {
    return;
  }

  dev->hi2c = hi2c;
  dev->device_address = EEPROM_24C64_I2C_ADDR_HAL;
  dev->timeout_ms = EEPROM_DEFAULT_TIMEOUT_MS;
}

HAL_StatusTypeDef Eeprom_IsReady(EepromHandle *dev)
{
  if ((dev == NULL) || (dev->hi2c == NULL))
  {
    return HAL_ERROR;
  }

  return HAL_I2C_IsDeviceReady(dev->hi2c,
                               dev->device_address,
                               EEPROM_READY_TRIALS,
                               dev->timeout_ms);
}

HAL_StatusTypeDef Eeprom_Read(EepromHandle *dev, uint16_t address, uint8_t *data, uint16_t length)
{
  if ((dev == NULL) || (dev->hi2c == NULL) || (data == NULL))
  {
    return HAL_ERROR;
  }

  if (Eeprom_IsRangeValid(address, length) == 0U)
  {
    return HAL_ERROR;
  }

  if (length == 0U)
  {
    return HAL_OK;
  }

  return HAL_I2C_Mem_Read(dev->hi2c,
                          dev->device_address,
                          address,
                          EEPROM_24C64_MEM_ADDR_SIZE,
                          data,
                          length,
                          dev->timeout_ms);
}

HAL_StatusTypeDef Eeprom_Write(EepromHandle *dev, uint16_t address, const uint8_t *data, uint16_t length)
{
  uint16_t current_address;
  uint16_t remaining;
  const uint8_t *current_data;
  HAL_StatusTypeDef status;

  if ((dev == NULL) || (dev->hi2c == NULL) || (data == NULL))
  {
    return HAL_ERROR;
  }

  if (Eeprom_IsRangeValid(address, length) == 0U)
  {
    return HAL_ERROR;
  }

  current_address = address;
  current_data = data;
  remaining = length;

  while (remaining > 0U)
  {
    uint16_t page_offset;
    uint16_t page_space;
    uint16_t write_size;

    page_offset = (uint16_t)(current_address % EEPROM_24C64_PAGE_SIZE_BYTES);
    page_space = (uint16_t)(EEPROM_24C64_PAGE_SIZE_BYTES - page_offset);
    write_size = (remaining < page_space) ? remaining : page_space;

    status = HAL_I2C_Mem_Write(dev->hi2c,
                               dev->device_address,
                               current_address,
                               EEPROM_24C64_MEM_ADDR_SIZE,
                               (uint8_t *)current_data,
                               write_size,
                               dev->timeout_ms);
    if (status != HAL_OK)
    {
      return status;
    }

    status = Eeprom_IsReady(dev);
    if (status != HAL_OK)
    {
      return status;
    }

    current_address = (uint16_t)(current_address + write_size);
    current_data = &current_data[write_size];
    remaining = (uint16_t)(remaining - write_size);
  }

  return HAL_OK;
}
