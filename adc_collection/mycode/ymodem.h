#ifndef YMODEM_H
#define YMODEM_H

#include <stdint.h>
#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  YMODEM_OK = 0,
  YMODEM_NO_C_TIMEOUT,
  YMODEM_RECEIVER_CANCELED,
  YMODEM_FILE_OPEN_ERROR,
  YMODEM_FILE_READ_ERROR,
  YMODEM_RETRY_EXCEEDED,
  YMODEM_INTERNAL_ERROR
} YModemResult;

/* Send one file to a YMODEM-1K receiver over the given UART.
 *
 * path           : "0:/NAME.CSV" style FatFs path opened with f_open(FA_READ)
 * name_for_block0: short basename written into the YMODEM header packet
 *
 * Blocking. Refreshes IWDG between blocks. Returns YMODEM_OK or a specific
 * error so the caller can map to FILE_ERR. */
YModemResult YModem_SendFile(UART_HandleTypeDef *huart,
                              const char *path,
                              const char *name_for_block0);

#ifdef __cplusplus
}
#endif

#endif /* YMODEM_H */
