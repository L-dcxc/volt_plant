#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <stdint.h>
#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FILE_BROWSER_MAX_ENTRIES   64U
#define FILE_BROWSER_NAME_SIZE     32U   /* incl. NUL; LFN off so 8.3 fits */
#define FILE_BROWSER_NO_SELECTION  0xFFFFU

typedef struct
{
  char     name[FILE_BROWSER_NAME_SIZE];
  uint32_t size;
} FileBrowserEntry;

/* Snapshot the top-level files of the SD card data directory into an internal
   array. Subsequent SELECT / read-name / read-size operations work against
   this snapshot, isolating Modbus state from concurrent filesystem changes. */
FRESULT FileBrowser_OpenDir(void);

uint16_t FileBrowser_GetCount(void);
uint8_t  FileBrowser_Select(uint16_t index);
uint8_t  FileBrowser_IsSelected(void);
uint16_t FileBrowser_GetSelectedIndex(void);
const FileBrowserEntry *FileBrowser_GetSelected(void);
FRESULT  FileBrowser_DeleteSelected(void);
void     FileBrowser_BuildSelectedPath(char *out, uint32_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* FILE_BROWSER_H */
