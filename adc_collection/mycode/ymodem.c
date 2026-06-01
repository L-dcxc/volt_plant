#include "ymodem.h"

#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "iwdg.h"

#define YM_SOH         0x01U
#define YM_STX         0x02U
#define YM_EOT         0x04U
#define YM_ACK         0x06U
#define YM_NAK         0x15U
#define YM_CAN         0x18U
#define YM_C           0x43U  /* 'C' */
#define YM_SUB         0x1AU  /* CP/M EOF, pad byte */

#define YM_BLOCK_DATA  1024U
#define YM_BLOCK_OVHD  5U     /* SOH/STX + seq + ~seq + crcH + crcL */
#define YM_BLOCK_TOTAL (YM_BLOCK_DATA + YM_BLOCK_OVHD)

#define YM_INITIAL_C_TIMEOUT_MS  10000U
#define YM_BLOCK_ACK_TIMEOUT_MS  3000U
#define YM_TX_TIMEOUT_MS         2000U
#define YM_RETRY_LIMIT           6U

static uint8_t s_block[YM_BLOCK_TOTAL];
static uint8_t s_payload[YM_BLOCK_DATA];  /* not on the stack — Stack_Size is 1 KiB */
static FIL     s_file;                    /* FIL is ~550 bytes; keep off the stack too */

static uint16_t YModem_Crc16(const uint8_t *data, uint32_t length)
{
  uint16_t crc = 0U;

  for (uint32_t i = 0U; i < length; i++)
  {
    crc ^= ((uint16_t)data[i]) << 8;
    for (uint8_t b = 0U; b < 8U; b++)
    {
      if (crc & 0x8000U)
      {
        crc = (uint16_t)((crc << 1) ^ 0x1021U);
      }
      else
      {
        crc = (uint16_t)(crc << 1);
      }
    }
  }
  return crc;
}

static HAL_StatusTypeDef YModem_TxByte(UART_HandleTypeDef *huart, uint8_t b)
{
  return HAL_UART_Transmit(huart, &b, 1U, YM_TX_TIMEOUT_MS);
}

static HAL_StatusTypeDef YModem_TxBuf(UART_HandleTypeDef *huart,
                                      const uint8_t *buf, uint32_t len)
{
  return HAL_UART_Transmit(huart, (uint8_t *)buf, len, YM_TX_TIMEOUT_MS);
}

static HAL_StatusTypeDef YModem_RxByte(UART_HandleTypeDef *huart, uint8_t *out,
                                       uint32_t timeout_ms)
{
  return HAL_UART_Receive(huart, out, 1U, timeout_ms);
}

static void YModem_BuildPacket(uint8_t seq, const uint8_t *payload)
{
  uint16_t crc;

  s_block[0] = YM_STX;
  s_block[1] = seq;
  s_block[2] = (uint8_t)(0xFFU - seq);
  memcpy(&s_block[3], payload, YM_BLOCK_DATA);
  crc = YModem_Crc16(&s_block[3], YM_BLOCK_DATA);
  s_block[3U + YM_BLOCK_DATA] = (uint8_t)(crc >> 8);
  s_block[4U + YM_BLOCK_DATA] = (uint8_t)(crc & 0xFFU);
}

/* Wait for receiver to send 'C' (initial poll). Cancels on CAN-CAN.
 * Returns YMODEM_OK / YMODEM_NO_C_TIMEOUT / YMODEM_RECEIVER_CANCELED. */
static YModemResult YModem_WaitForC(UART_HandleTypeDef *huart, uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  uint8_t got_can = 0U;

  while ((HAL_GetTick() - start) < timeout_ms)
  {
    uint8_t b;
    HAL_StatusTypeDef st = YModem_RxByte(huart, &b, 500U);
    HAL_IWDG_Refresh(&hiwdg);
    if (st != HAL_OK)
    {
      continue;
    }
    if (b == YM_C)
    {
      return YMODEM_OK;
    }
    if (b == YM_CAN)
    {
      if (got_can != 0U)
      {
        return YMODEM_RECEIVER_CANCELED;
      }
      got_can = 1U;
    }
    else
    {
      got_can = 0U;
    }
  }
  return YMODEM_NO_C_TIMEOUT;
}

/* Send a fully-built s_block then wait for ACK / NAK / CAN-CAN.
 * Returns YMODEM_OK on ACK, YMODEM_RETRY_EXCEEDED on too many NAK,
 * YMODEM_RECEIVER_CANCELED on cancel. */
static YModemResult YModem_TxPacketWithAck(UART_HandleTypeDef *huart)
{
  uint8_t retries = 0U;
  uint8_t got_can = 0U;

  while (retries < YM_RETRY_LIMIT)
  {
    HAL_StatusTypeDef st = YModem_TxBuf(huart, s_block, YM_BLOCK_TOTAL);
    if (st != HAL_OK)
    {
      return YMODEM_INTERNAL_ERROR;
    }

    uint32_t deadline = HAL_GetTick() + YM_BLOCK_ACK_TIMEOUT_MS;
    while (HAL_GetTick() < deadline)
    {
      uint8_t b;
      st = YModem_RxByte(huart, &b, 500U);
      HAL_IWDG_Refresh(&hiwdg);
      if (st != HAL_OK)
      {
        continue;
      }
      if (b == YM_ACK)
      {
        return YMODEM_OK;
      }
      if (b == YM_NAK)
      {
        got_can = 0U;
        break; /* break inner -> retry tx */
      }
      if (b == YM_CAN)
      {
        if (got_can != 0U)
        {
          return YMODEM_RECEIVER_CANCELED;
        }
        got_can = 1U;
      }
      else
      {
        got_can = 0U;
      }
    }
    retries++;
  }
  return YMODEM_RETRY_EXCEEDED;
}

YModemResult YModem_SendFile(UART_HandleTypeDef *huart, const char *path,
                              const char *name_for_block0)
{
  FIL *file = &s_file;
  FRESULT fr;
  uint8_t *payload = s_payload;
  uint32_t bytes_remaining;
  uint8_t seq;
  YModemResult yr;

  if ((huart == NULL) || (path == NULL) || (name_for_block0 == NULL))
  {
    return YMODEM_INTERNAL_ERROR;
  }

  fr = f_open(file, path, FA_READ);
  if (fr != FR_OK)
  {
    return YMODEM_FILE_OPEN_ERROR;
  }

  bytes_remaining = (uint32_t)f_size(file);

  /* 1) wait for the initial 'C' from receiver */
  yr = YModem_WaitForC(huart, YM_INITIAL_C_TIMEOUT_MS);
  if (yr != YMODEM_OK)
  {
    (void)f_close(file);
    return yr;
  }

  /* 2) block 0: filename + decimal size + zero padding */
  {
    uint32_t off = 0U;
    uint32_t name_len = strlen(name_for_block0);
    char size_buf[12];
    int n;

    memset(payload, 0, YM_BLOCK_DATA);
    if (name_len >= YM_BLOCK_DATA)
    {
      (void)f_close(file);
      return YMODEM_INTERNAL_ERROR;
    }
    memcpy(payload, name_for_block0, name_len);
    off = name_len + 1U; /* NUL after name */

    n = snprintf(size_buf, sizeof(size_buf), "%lu", (unsigned long)bytes_remaining);
    if ((n <= 0) || ((uint32_t)n >= YM_BLOCK_DATA - off - 1U))
    {
      (void)f_close(file);
      return YMODEM_INTERNAL_ERROR;
    }
    memcpy(&payload[off], size_buf, (uint32_t)n);
    /* trailing NULs already there from memset */
  }

  YModem_BuildPacket(0U, payload);
  yr = YModem_TxPacketWithAck(huart);
  if (yr != YMODEM_OK)
  {
    (void)f_close(file);
    return yr;
  }

  /* 3) data blocks: receiver re-issues 'C' after header ACK */
  yr = YModem_WaitForC(huart, YM_INITIAL_C_TIMEOUT_MS);
  if (yr != YMODEM_OK)
  {
    (void)f_close(file);
    return yr;
  }

  seq = 1U;
  while (bytes_remaining > 0U)
  {
    UINT br = 0U;
    uint32_t chunk = (bytes_remaining > YM_BLOCK_DATA) ? YM_BLOCK_DATA : bytes_remaining;

    fr = f_read(file, payload, chunk, &br);
    if ((fr != FR_OK) || (br != chunk))
    {
      (void)f_close(file);
      return YMODEM_FILE_READ_ERROR;
    }

    if (chunk < YM_BLOCK_DATA)
    {
      memset(&payload[chunk], YM_SUB, YM_BLOCK_DATA - chunk);
    }

    YModem_BuildPacket(seq, payload);
    yr = YModem_TxPacketWithAck(huart);
    HAL_IWDG_Refresh(&hiwdg);
    if (yr != YMODEM_OK)
    {
      (void)f_close(file);
      return yr;
    }

    bytes_remaining -= chunk;
    seq++;
  }
  (void)f_close(file);

  /* 4) EOT handshake: send EOT, expect NAK, send EOT again, expect ACK */
  {
    uint8_t b;
    HAL_StatusTypeDef st;
    uint32_t deadline;

    if (YModem_TxByte(huart, YM_EOT) != HAL_OK)
    {
      return YMODEM_INTERNAL_ERROR;
    }
    deadline = HAL_GetTick() + YM_BLOCK_ACK_TIMEOUT_MS;
    while (HAL_GetTick() < deadline)
    {
      st = YModem_RxByte(huart, &b, 500U);
      HAL_IWDG_Refresh(&hiwdg);
      if (st == HAL_OK && b == YM_NAK)
      {
        break;
      }
    }

    if (YModem_TxByte(huart, YM_EOT) != HAL_OK)
    {
      return YMODEM_INTERNAL_ERROR;
    }
    deadline = HAL_GetTick() + YM_BLOCK_ACK_TIMEOUT_MS;
    while (HAL_GetTick() < deadline)
    {
      st = YModem_RxByte(huart, &b, 500U);
      HAL_IWDG_Refresh(&hiwdg);
      if (st == HAL_OK && b == YM_ACK)
      {
        break;
      }
    }
  }

  /* 5) closing null block 0: wait for 'C', send all-zero header, expect ACK */
  yr = YModem_WaitForC(huart, YM_BLOCK_ACK_TIMEOUT_MS);
  if (yr != YMODEM_OK)
  {
    /* Some clients skip this — treat as success since file content was ACKed */
    return YMODEM_OK;
  }

  memset(payload, 0, sizeof(payload));
  YModem_BuildPacket(0U, payload);
  yr = YModem_TxPacketWithAck(huart);
  if (yr != YMODEM_OK)
  {
    return YMODEM_OK; /* tolerant: file is already delivered */
  }
  return YMODEM_OK;
}
