#include "storage.h"

#include "fatfs.h"
#include "main.h"

#define STORAGE_DEFAULT_POWER_DELAY_MS 1000U

static uint8_t storage_mounted = 0U;

static uint32_t Storage_StringLength(const char *text)
{
  uint32_t length = 0U;

  if (text == NULL)
  {
    return 0U;
  }

  while (text[length] != '\0')
  {
    length++;
  }

  return length;
}

static void Storage_BuildPath(char *path, uint32_t path_size, const char *file_name)
{
  uint32_t index = 0U;
  uint32_t name_index = 0U;

  if ((path == NULL) || (path_size == 0U))
  {
    return;
  }

  while ((SDPath[index] != '\0') && (index < (path_size - 1U)))
  {
    path[index] = SDPath[index];
    index++;
  }

  if ((file_name != NULL) && (file_name[0] != '/') && (index < (path_size - 1U)))
  {
    path[index] = '/';
    index++;
  }

  while ((file_name != NULL) && (file_name[name_index] != '\0') && (index < (path_size - 1U)))
  {
    path[index] = file_name[name_index];
    index++;
    name_index++;
  }

  path[index] = '\0';
}

static FRESULT Storage_WriteString(FIL *file, const char *text)
{
  UINT written = 0U;
  uint32_t length;
  FRESULT result;

  if ((file == NULL) || (text == NULL))
  {
    return FR_INVALID_PARAMETER;
  }

  length = Storage_StringLength(text);
  result = f_write(file, text, (UINT)length, &written);
  if (result != FR_OK)
  {
    return result;
  }

  if (written != (UINT)length)
  {
    return FR_DISK_ERR;
  }

  return FR_OK;
}

FRESULT Storage_Init(uint32_t power_delay_ms)
{
  Storage_PowerOn(power_delay_ms);
  storage_mounted = 0U;

  return Storage_Mount();
}

void Storage_PowerOn(uint32_t power_delay_ms)
{
  if (HAL_GPIO_ReadPin(SD_PWR_EN_GPIO_Port, SD_PWR_EN_Pin) != GPIO_PIN_SET)
  {
    storage_mounted = 0U;
  }
  HAL_GPIO_WritePin(SD_PWR_EN_GPIO_Port, SD_PWR_EN_Pin, GPIO_PIN_SET);
  HAL_Delay(power_delay_ms);
}

void Storage_PowerOff(void)
{
  /* Unmount before cutting power so FatFs releases its cached FATFS object;
     next access must call Storage_Mount/Storage_Init again. */
  (void)f_mount(NULL, (TCHAR const *)SDPath, 0U);
  storage_mounted = 0U;
  HAL_GPIO_WritePin(SD_PWR_EN_GPIO_Port, SD_PWR_EN_Pin, GPIO_PIN_RESET);
}

uint8_t Storage_IsPowerEnabled(void)
{
  return (HAL_GPIO_ReadPin(SD_PWR_EN_GPIO_Port, SD_PWR_EN_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}

void Storage_InvalidateMount(void)
{
  storage_mounted = 0U;
}

FRESULT Storage_Mount(void)
{
  FRESULT result;

  if (Storage_IsPowerEnabled() == 0U)
  {
    Storage_PowerOn(STORAGE_DEFAULT_POWER_DELAY_MS);
  }

  result = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1U);
  storage_mounted = (result == FR_OK) ? 1U : 0U;
  return result;
}

FRESULT Storage_EnsureReady(uint32_t power_delay_ms)
{
  if (Storage_IsPowerEnabled() == 0U)
  {
    Storage_PowerOn(power_delay_ms);
  }

  if (storage_mounted != 0U)
  {
    return FR_OK;
  }

  return Storage_Mount();
}

FRESULT Storage_AppendLine(const char *file_name, const char *line)
{
  char path[32];
  FIL file;
  FRESULT result;

  if ((file_name == NULL) || (line == NULL))
  {
    return FR_INVALID_PARAMETER;
  }

  result = Storage_EnsureReady(STORAGE_DEFAULT_POWER_DELAY_MS);
  if (result != FR_OK)
  {
    return result;
  }

  Storage_BuildPath(path, (uint32_t)sizeof(path), file_name);

  result = f_open(&file, path, FA_WRITE | FA_OPEN_APPEND);
  if (result != FR_OK)
  {
    storage_mounted = 0U;
    return result;
  }

  result = Storage_WriteString(&file, line);
  if (result == FR_OK)
  {
    result = Storage_WriteString(&file, "\r\n");
  }

  if (result == FR_OK)
  {
    result = f_sync(&file);
  }

  if (f_close(&file) != FR_OK)
  {
    if (result == FR_OK)
    {
      result = FR_DISK_ERR;
    }
  }

  if (result != FR_OK)
  {
    storage_mounted = 0U;
  }

  return result;
}

FRESULT Storage_WriteTestCsv(void)
{
  char path[32];
  FIL file;
  FRESULT result;

  result = Storage_EnsureReady(STORAGE_DEFAULT_POWER_DELAY_MS);
  if (result != FR_OK)
  {
    return result;
  }

  Storage_BuildPath(path, (uint32_t)sizeof(path), STORAGE_TEST_FILE_NAME);

  result = f_open(&file, path, FA_WRITE | FA_OPEN_APPEND);
  if (result != FR_OK)
  {
    storage_mounted = 0U;
    return result;
  }

  if (f_size(&file) == 0U)
  {
    result = Storage_WriteString(&file, "index,message\r\n");
  }

  if (result == FR_OK)
  {
    result = Storage_WriteString(&file, "1,sd card test ok\r\n");
  }

  if (result == FR_OK)
  {
    result = f_sync(&file);
  }

  if (f_close(&file) != FR_OK)
  {
    if (result == FR_OK)
    {
      result = FR_DISK_ERR;
    }
  }

  if (result != FR_OK)
  {
    storage_mounted = 0U;
  }

  return result;
}

FRESULT Storage_GetCapacityKB(uint32_t *total_kb, uint32_t *free_kb)
{
  FATFS *fs = NULL;
  DWORD free_clusters = 0U;
  FRESULT result;

  result = Storage_EnsureReady(STORAGE_DEFAULT_POWER_DELAY_MS);
  if (result != FR_OK)
  {
    return result;
  }

  result = f_getfree((TCHAR const *)SDPath, &free_clusters, &fs);
  if (result != FR_OK)
  {
    storage_mounted = 0U;
    return result;
  }
  if (fs == NULL)
  {
    return FR_INT_ERR;
  }

  /* Sector size is fixed at 512 B (_MIN_SS == _MAX_SS). 1 KiB = 2 sectors.
     Compute in 64-bit to avoid overflow on large cards. */
  if (total_kb != NULL)
  {
    DWORD total_clusters = fs->n_fatent - 2U;
    *total_kb = (uint32_t)(((uint64_t)total_clusters * fs->csize) / 2U);
  }
  if (free_kb != NULL)
  {
    *free_kb = (uint32_t)(((uint64_t)free_clusters * fs->csize) / 2U);
  }

  return FR_OK;
}

const char *Storage_FresultText(FRESULT result)
{
  switch (result)
  {
    case FR_OK:
      return "FR_OK";
    case FR_DISK_ERR:
      return "FR_DISK_ERR";
    case FR_INT_ERR:
      return "FR_INT_ERR";
    case FR_NOT_READY:
      return "FR_NOT_READY";
    case FR_NO_FILE:
      return "FR_NO_FILE";
    case FR_NO_PATH:
      return "FR_NO_PATH";
    case FR_INVALID_NAME:
      return "FR_INVALID_NAME";
    case FR_DENIED:
      return "FR_DENIED";
    case FR_EXIST:
      return "FR_EXIST";
    case FR_INVALID_OBJECT:
      return "FR_INVALID_OBJECT";
    case FR_WRITE_PROTECTED:
      return "FR_WRITE_PROTECTED";
    case FR_INVALID_DRIVE:
      return "FR_INVALID_DRIVE";
    case FR_NOT_ENABLED:
      return "FR_NOT_ENABLED";
    case FR_NO_FILESYSTEM:
      return "FR_NO_FILESYSTEM";
    case FR_MKFS_ABORTED:
      return "FR_MKFS_ABORTED";
    case FR_TIMEOUT:
      return "FR_TIMEOUT";
    case FR_LOCKED:
      return "FR_LOCKED";
    case FR_NOT_ENOUGH_CORE:
      return "FR_NOT_ENOUGH_CORE";
    case FR_TOO_MANY_OPEN_FILES:
      return "FR_TOO_MANY_OPEN_FILES";
    case FR_INVALID_PARAMETER:
      return "FR_INVALID_PARAMETER";
    default:
      return "FR_UNKNOWN";
  }
}
