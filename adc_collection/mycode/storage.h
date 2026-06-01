#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include "ff.h"

#define STORAGE_TEST_FILE_NAME "SD_TEST.CSV"

FRESULT Storage_Init(uint32_t power_delay_ms);
void Storage_PowerOn(uint32_t power_delay_ms);
uint8_t Storage_IsPowerEnabled(void);
FRESULT Storage_Mount(void);
FRESULT Storage_AppendLine(const char *file_name, const char *line);
FRESULT Storage_WriteTestCsv(void);
const char *Storage_FresultText(FRESULT result);

/* Query filesystem capacity. total_kb/free_kb are filled with the volume size
   and free space in kibibytes (1024 bytes). Either out pointer may be NULL. */
FRESULT Storage_GetCapacityKB(uint32_t *total_kb, uint32_t *free_kb);

#endif
