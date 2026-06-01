#include "file_browser.h"

#include <string.h>
#include "fatfs.h"

static FileBrowserEntry s_entries[FILE_BROWSER_MAX_ENTRIES];
static uint16_t s_count = 0U;
static uint16_t s_selected = FILE_BROWSER_NO_SELECTION;

static uint32_t FileBrowser_StringLength(const char *text)
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

static void FileBrowser_BuildPath(char *out, uint32_t out_size, const char *name)
{
  uint32_t idx = 0U;
  uint32_t i = 0U;

  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  while ((SDPath[idx] != '\0') && (idx < (out_size - 1U)))
  {
    out[idx] = SDPath[idx];
    idx++;
  }

  if ((name != NULL) && (name[0] != '/') && (idx < (out_size - 1U)))
  {
    out[idx] = '/';
    idx++;
  }

  while ((name != NULL) && (name[i] != '\0') && (idx < (out_size - 1U)))
  {
    out[idx] = name[i];
    idx++;
    i++;
  }
  out[idx] = '\0';
}

FRESULT FileBrowser_OpenDir(void)
{
  DIR dir;
  FILINFO fno;
  FRESULT fr;

  s_count = 0U;
  s_selected = FILE_BROWSER_NO_SELECTION;

  fr = f_opendir(&dir, SDPath);
  if (fr != FR_OK)
  {
    return fr;
  }

  for (;;)
  {
    fr = f_readdir(&dir, &fno);
    if (fr != FR_OK)
    {
      (void)f_closedir(&dir);
      return fr;
    }
    if (fno.fname[0] == '\0')
    {
      break; /* end of directory */
    }
    if (fno.fattrib & (AM_DIR | AM_HID | AM_SYS))
    {
      continue; /* skip subdirs and hidden/system entries */
    }

    uint32_t name_len = FileBrowser_StringLength(fno.fname);
    if (name_len == 0U || name_len >= FILE_BROWSER_NAME_SIZE)
    {
      continue; /* defensive: 8.3 names always fit, but guard anyway */
    }

    FileBrowserEntry *e = &s_entries[s_count];
    memcpy(e->name, fno.fname, name_len);
    e->name[name_len] = '\0';
    e->size = (uint32_t)fno.fsize;

    s_count++;
    if (s_count >= FILE_BROWSER_MAX_ENTRIES)
    {
      break; /* snapshot capacity reached */
    }
  }

  (void)f_closedir(&dir);
  return FR_OK;
}

uint16_t FileBrowser_GetCount(void)
{
  return s_count;
}

uint8_t FileBrowser_Select(uint16_t index)
{
  if (index >= s_count)
  {
    return 0U;
  }
  s_selected = index;
  return 1U;
}

uint8_t FileBrowser_IsSelected(void)
{
  return (s_selected != FILE_BROWSER_NO_SELECTION) ? 1U : 0U;
}

uint16_t FileBrowser_GetSelectedIndex(void)
{
  return s_selected;
}

const FileBrowserEntry *FileBrowser_GetSelected(void)
{
  if (s_selected >= s_count)
  {
    return NULL;
  }
  return &s_entries[s_selected];
}

FRESULT FileBrowser_DeleteSelected(void)
{
  char path[FILE_BROWSER_NAME_SIZE + 8U];
  const FileBrowserEntry *e = FileBrowser_GetSelected();
  FRESULT fr;

  if (e == NULL)
  {
    return FR_INVALID_PARAMETER;
  }

  FileBrowser_BuildPath(path, sizeof(path), e->name);
  fr = f_unlink(path);
  if (fr == FR_OK)
  {
    s_selected = FILE_BROWSER_NO_SELECTION;
  }
  return fr;
}

void FileBrowser_BuildSelectedPath(char *out, uint32_t out_size)
{
  const FileBrowserEntry *e = FileBrowser_GetSelected();

  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }
  if (e == NULL)
  {
    out[0] = '\0';
    return;
  }
  FileBrowser_BuildPath(out, out_size, e->name);
}
