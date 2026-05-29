#include "app_config_store.h"

#include <stddef.h>

#define APP_CONFIG_STORE_CRC_INIT       0xFFFFFFFFUL
#define APP_CONFIG_STORE_CRC_POLY       0xEDB88320UL

static uint16_t AppConfigStore_GetSlotAddress(AppConfigStoreSlot slot)
{
  if (slot == APP_CONFIG_STORE_SLOT_A)
  {
    return APP_CONFIG_STORE_SLOT_A_ADDR;
  }

  if (slot == APP_CONFIG_STORE_SLOT_B)
  {
    return APP_CONFIG_STORE_SLOT_B_ADDR;
  }

  return APP_CONFIG_STORE_SLOT_A_ADDR;
}

static AppConfigStoreSlot AppConfigStore_GetNextSlot(AppConfigStoreSlot active_slot)
{
  if (active_slot == APP_CONFIG_STORE_SLOT_A)
  {
    return APP_CONFIG_STORE_SLOT_B;
  }

  return APP_CONFIG_STORE_SLOT_A;
}

static uint32_t AppConfigStore_CrcUpdate(uint32_t crc, const uint8_t *data, uint32_t length)
{
  uint32_t i;
  uint8_t bit;

  for (i = 0U; i < length; i++)
  {
    crc ^= data[i];
    for (bit = 0U; bit < 8U; bit++)
    {
      if ((crc & 1UL) != 0UL)
      {
        crc = (crc >> 1U) ^ APP_CONFIG_STORE_CRC_POLY;
      }
      else
      {
        crc >>= 1U;
      }
    }
  }

  return crc;
}

static uint8_t AppConfigStore_ReadSlot(AppConfigStore *store,
                                       AppConfigStoreSlot slot,
                                       AppConfigImage *config)
{
  HAL_StatusTypeDef status;

  if ((store == NULL) || (config == NULL))
  {
    return 0U;
  }

  status = Eeprom_Read(&store->eeprom,
                       AppConfigStore_GetSlotAddress(slot),
                       (uint8_t *)config,
                       (uint16_t)sizeof(AppConfigImage));
  if (status != HAL_OK)
  {
    return 0U;
  }

  return AppConfigStore_IsCrcValid(config);
}

void AppConfigStore_Init(AppConfigStore *store, I2C_HandleTypeDef *hi2c)
{
  if (store == NULL)
  {
    return;
  }

  Eeprom_Init(&store->eeprom, hi2c);
  store->active_slot = APP_CONFIG_STORE_SLOT_NONE;
  store->load_source = APP_CONFIG_STORE_LOAD_DEFAULT;
  store->active_sequence = 0UL;
}

uint32_t AppConfigStore_CalculateCrc(const AppConfigImage *config)
{
  uint32_t crc;
  uint32_t crc_offset;
  const uint8_t *bytes;

  if (config == NULL)
  {
    return 0UL;
  }

  bytes = (const uint8_t *)config;
  crc_offset = (uint32_t)offsetof(AppConfigImage, crc32);

  crc = APP_CONFIG_STORE_CRC_INIT;
  crc = AppConfigStore_CrcUpdate(crc, bytes, crc_offset);
  crc = AppConfigStore_CrcUpdate(crc,
                                 &bytes[crc_offset + (uint32_t)sizeof(config->crc32)],
                                 (uint32_t)sizeof(AppConfigImage) - crc_offset - (uint32_t)sizeof(config->crc32));

  return crc ^ APP_CONFIG_STORE_CRC_INIT;
}

uint8_t AppConfigStore_IsCrcValid(const AppConfigImage *config)
{
  if (config == NULL)
  {
    return 0U;
  }

  if (AppConfig_IsValid(config) == 0U)
  {
    return 0U;
  }

  return (AppConfigStore_CalculateCrc(config) == config->crc32) ? 1U : 0U;
}

HAL_StatusTypeDef AppConfigStore_Load(AppConfigStore *store, AppConfigImage *config)
{
  AppConfigImage slot_a;
  AppConfigImage slot_b;
  uint8_t slot_a_valid;
  uint8_t slot_b_valid;

  if ((store == NULL) || (config == NULL))
  {
    return HAL_ERROR;
  }

  if (sizeof(AppConfigImage) > APP_CONFIG_STORE_SLOT_SIZE_BYTES)
  {
    AppConfig_LoadDefaults(config);
    store->active_slot = APP_CONFIG_STORE_SLOT_NONE;
    store->load_source = APP_CONFIG_STORE_LOAD_DEFAULT;
    store->active_sequence = 0UL;
    return HAL_ERROR;
  }

  slot_a_valid = AppConfigStore_ReadSlot(store, APP_CONFIG_STORE_SLOT_A, &slot_a);
  slot_b_valid = AppConfigStore_ReadSlot(store, APP_CONFIG_STORE_SLOT_B, &slot_b);

  if ((slot_a_valid != 0U) && (slot_b_valid != 0U))
  {
    if (slot_b.sequence > slot_a.sequence)
    {
      *config = slot_b;
      store->active_slot = APP_CONFIG_STORE_SLOT_B;
      store->load_source = APP_CONFIG_STORE_LOAD_SLOT_B;
      store->active_sequence = slot_b.sequence;
    }
    else
    {
      *config = slot_a;
      store->active_slot = APP_CONFIG_STORE_SLOT_A;
      store->load_source = APP_CONFIG_STORE_LOAD_SLOT_A;
      store->active_sequence = slot_a.sequence;
    }
    return HAL_OK;
  }

  if (slot_a_valid != 0U)
  {
    *config = slot_a;
    store->active_slot = APP_CONFIG_STORE_SLOT_A;
    store->load_source = APP_CONFIG_STORE_LOAD_SLOT_A;
    store->active_sequence = slot_a.sequence;
    return HAL_OK;
  }

  if (slot_b_valid != 0U)
  {
    *config = slot_b;
    store->active_slot = APP_CONFIG_STORE_SLOT_B;
    store->load_source = APP_CONFIG_STORE_LOAD_SLOT_B;
    store->active_sequence = slot_b.sequence;
    return HAL_OK;
  }

  AppConfig_LoadDefaults(config);
  store->active_slot = APP_CONFIG_STORE_SLOT_NONE;
  store->load_source = APP_CONFIG_STORE_LOAD_DEFAULT;
  store->active_sequence = 0UL;

  return HAL_OK;
}

HAL_StatusTypeDef AppConfigStore_Save(AppConfigStore *store, AppConfigImage *config)
{
  AppConfigStoreSlot target_slot;
  HAL_StatusTypeDef status;
  AppConfigImage verify_config;

  if ((store == NULL) || (config == NULL))
  {
    return HAL_ERROR;
  }

  if (sizeof(AppConfigImage) > APP_CONFIG_STORE_SLOT_SIZE_BYTES)
  {
    return HAL_ERROR;
  }

  config->magic = APP_CONFIG_MAGIC;
  config->version = APP_CONFIG_VERSION;
  config->size = (uint16_t)sizeof(AppConfigImage);
  config->sequence = store->active_sequence + 1UL;
  config->crc32 = 0UL;

  if (AppConfig_IsValid(config) == 0U)
  {
    return HAL_ERROR;
  }

  config->crc32 = AppConfigStore_CalculateCrc(config);
  target_slot = AppConfigStore_GetNextSlot(store->active_slot);

  status = Eeprom_Write(&store->eeprom,
                        AppConfigStore_GetSlotAddress(target_slot),
                        (const uint8_t *)config,
                        (uint16_t)sizeof(AppConfigImage));
  if (status != HAL_OK)
  {
    return status;
  }

  status = Eeprom_Read(&store->eeprom,
                       AppConfigStore_GetSlotAddress(target_slot),
                       (uint8_t *)&verify_config,
                       (uint16_t)sizeof(AppConfigImage));
  if (status != HAL_OK)
  {
    return status;
  }

  if (AppConfigStore_IsCrcValid(&verify_config) == 0U)
  {
    return HAL_ERROR;
  }

  store->active_slot = target_slot;
  store->active_sequence = config->sequence;
  store->load_source = (target_slot == APP_CONFIG_STORE_SLOT_A) ?
                       APP_CONFIG_STORE_LOAD_SLOT_A :
                       APP_CONFIG_STORE_LOAD_SLOT_B;

  return HAL_OK;
}

HAL_StatusTypeDef AppConfigStore_SaveDefaults(AppConfigStore *store, AppConfigImage *config)
{
  if ((store == NULL) || (config == NULL))
  {
    return HAL_ERROR;
  }

  AppConfig_LoadDefaults(config);
  return AppConfigStore_Save(store, config);
}
