#ifndef APP_CONFIG_STORE_H
#define APP_CONFIG_STORE_H

#include <stdint.h>
#include "app_config.h"
#include "eeprom.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CONFIG_STORE_SLOT_SIZE_BYTES  1024U
#define APP_CONFIG_STORE_SLOT_A_ADDR      0x0000U
#define APP_CONFIG_STORE_SLOT_B_ADDR      0x0400U

typedef enum
{
  APP_CONFIG_STORE_SLOT_A = 0U,
  APP_CONFIG_STORE_SLOT_B = 1U,
  APP_CONFIG_STORE_SLOT_NONE = 0xFFU
} AppConfigStoreSlot;

typedef enum
{
  APP_CONFIG_STORE_LOAD_DEFAULT = 0U,
  APP_CONFIG_STORE_LOAD_SLOT_A = 1U,
  APP_CONFIG_STORE_LOAD_SLOT_B = 2U
} AppConfigStoreLoadSource;

typedef struct
{
  EepromHandle eeprom;
  AppConfigStoreSlot active_slot;
  AppConfigStoreLoadSource load_source;
  uint32_t active_sequence;
} AppConfigStore;

void AppConfigStore_Init(AppConfigStore *store, I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef AppConfigStore_Load(AppConfigStore *store, AppConfigImage *config);
HAL_StatusTypeDef AppConfigStore_Save(AppConfigStore *store, AppConfigImage *config);
HAL_StatusTypeDef AppConfigStore_SaveDefaults(AppConfigStore *store, AppConfigImage *config);
uint32_t AppConfigStore_CalculateCrc(const AppConfigImage *config);
uint8_t AppConfigStore_IsCrcValid(const AppConfigImage *config);

#ifdef __cplusplus
}
#endif

#endif
