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

#endif
