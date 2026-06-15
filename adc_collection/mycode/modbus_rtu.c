#include "modbus_rtu.h"
#include <string.h>

/* Ring buffer for UART RX */
typedef struct
{
  uint8_t buffer[MODBUS_RX_BUFFER_SIZE];
  volatile uint16_t head;
  volatile uint16_t tail;
  volatile uint32_t last_rx_tick;
} ModbusRxRingBuffer;

/* Modbus RTU handle */
typedef struct
{
  UART_HandleTypeDef *huart;
  uint8_t slave_addr;
  ModbusRxRingBuffer rx_buf;
  uint8_t frame_buf[MODBUS_FRAME_MAX_SIZE];
  uint16_t frame_len;
  ModbusCallbacks callbacks;
} ModbusRtuHandle;

static ModbusRtuHandle g_modbus;

/* Ring buffer operations */
static uint16_t RingBuf_Available(const ModbusRxRingBuffer *rb)
{
  uint16_t h = rb->head;
  uint16_t t = rb->tail;
  if (h >= t)
    return h - t;
  else
    return MODBUS_RX_BUFFER_SIZE - t + h;
}

static uint8_t RingBuf_Read(ModbusRxRingBuffer *rb)
{
  uint8_t byte = rb->buffer[rb->tail];
  rb->tail = (rb->tail + 1U) % MODBUS_RX_BUFFER_SIZE;
  return byte;
}

static void RingBuf_Write(ModbusRxRingBuffer *rb, uint8_t byte)
{
  rb->buffer[rb->head] = byte;
  rb->head = (rb->head + 1U) % MODBUS_RX_BUFFER_SIZE;
  rb->last_rx_tick = HAL_GetTick();
}

/* CRC16 calculation (Modbus standard, polynomial 0xA001) */
uint16_t ModbusRtu_Crc16(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  for (uint16_t i = 0U; i < length; i++)
  {
    crc ^= data[i];
    for (uint8_t j = 0U; j < 8U; j++)
    {
      if ((crc & 0x0001U) != 0U)
        crc = (crc >> 1) ^ 0xA001U;
      else
        crc >>= 1;
    }
  }
  return crc;
}

/* Send response frame */
static void SendFrame(const uint8_t *frame, uint16_t length)
{
  uint16_t crc = ModbusRtu_Crc16(frame, length);
  HAL_UART_Transmit(g_modbus.huart, (uint8_t *)frame, length, 100U);
  HAL_UART_Transmit(g_modbus.huart, (uint8_t *)&crc, 2U, 100U);
}

/* Send exception response */
static void SendException(uint8_t function_code, uint8_t exception_code)
{
  uint8_t frame[3];
  frame[0] = g_modbus.slave_addr;
  frame[1] = function_code | 0x80U;
  frame[2] = exception_code;
  SendFrame(frame, 3U);
}

/* Process FC03: Read Holding Registers */
static void ProcessFC03(const uint8_t *frame, uint16_t frame_len)
{
  if (frame_len != 8U)
  {
    SendException(MODBUS_FC_READ_HOLDING_REGISTERS, MODBUS_EX_ILLEGAL_DATA_VALUE);
    return;
  }

  uint16_t start_addr = ((uint16_t)frame[2] << 8) | frame[3];
  uint16_t count = ((uint16_t)frame[4] << 8) | frame[5];

  if (count == 0U || count > 125U)
  {
    SendException(MODBUS_FC_READ_HOLDING_REGISTERS, MODBUS_EX_ILLEGAL_DATA_VALUE);
    return;
  }

  uint16_t regs[125];
  uint8_t result = g_modbus.callbacks.read_holding_regs(start_addr, count, regs);
  if (result != 0U)
  {
    SendException(MODBUS_FC_READ_HOLDING_REGISTERS, result);
    return;
  }

  uint8_t response[256];
  response[0] = g_modbus.slave_addr;
  response[1] = MODBUS_FC_READ_HOLDING_REGISTERS;
  response[2] = (uint8_t)(count * 2U);

  for (uint16_t i = 0U; i < count; i++)
  {
    response[3U + i * 2U] = (uint8_t)(regs[i] >> 8);
    response[4U + i * 2U] = (uint8_t)(regs[i] & 0xFFU);
  }

  SendFrame(response, 3U + count * 2U);
}

/* Process FC04: Read Input Registers */
static void ProcessFC04(const uint8_t *frame, uint16_t frame_len)
{
  if (frame_len != 8U)
  {
    SendException(MODBUS_FC_READ_INPUT_REGISTERS, MODBUS_EX_ILLEGAL_DATA_VALUE);
    return;
  }

  uint16_t start_addr = ((uint16_t)frame[2] << 8) | frame[3];
  uint16_t count = ((uint16_t)frame[4] << 8) | frame[5];

  if (count == 0U || count > 125U)
  {
    SendException(MODBUS_FC_READ_INPUT_REGISTERS, MODBUS_EX_ILLEGAL_DATA_VALUE);
    return;
  }

  uint16_t regs[125];
  uint8_t result = g_modbus.callbacks.read_input_regs(start_addr, count, regs);
  if (result != 0U)
  {
    SendException(MODBUS_FC_READ_INPUT_REGISTERS, result);
    return;
  }

  uint8_t response[256];
  response[0] = g_modbus.slave_addr;
  response[1] = MODBUS_FC_READ_INPUT_REGISTERS;
  response[2] = (uint8_t)(count * 2U);

  for (uint16_t i = 0U; i < count; i++)
  {
    response[3U + i * 2U] = (uint8_t)(regs[i] >> 8);
    response[4U + i * 2U] = (uint8_t)(regs[i] & 0xFFU);
  }

  SendFrame(response, 3U + count * 2U);
}

/* Process FC06: Write Single Register */
static void ProcessFC06(const uint8_t *frame, uint16_t frame_len)
{
  if (frame_len != 8U)
  {
    SendException(MODBUS_FC_WRITE_SINGLE_REGISTER, MODBUS_EX_ILLEGAL_DATA_VALUE);
    return;
  }

  uint16_t addr = ((uint16_t)frame[2] << 8) | frame[3];
  uint16_t value = ((uint16_t)frame[4] << 8) | frame[5];

  uint8_t result = g_modbus.callbacks.write_single_reg(addr, value);
  if (result != 0U)
  {
    SendException(MODBUS_FC_WRITE_SINGLE_REGISTER, result);
    return;
  }

  /* Echo request as response */
  SendFrame(frame, 6U);
}

/* Process FC16: Write Multiple Registers */
static void ProcessFC16(const uint8_t *frame, uint16_t frame_len)
{
  if (frame_len < 9U)
  {
    SendException(MODBUS_FC_WRITE_MULTIPLE_REGISTERS, MODBUS_EX_ILLEGAL_DATA_VALUE);
    return;
  }

  uint16_t start_addr = ((uint16_t)frame[2] << 8) | frame[3];
  uint16_t count = ((uint16_t)frame[4] << 8) | frame[5];
  uint8_t byte_count = frame[6];

  if (count == 0U || count > 123U || byte_count != count * 2U || frame_len != (9U + byte_count))
  {
    SendException(MODBUS_FC_WRITE_MULTIPLE_REGISTERS, MODBUS_EX_ILLEGAL_DATA_VALUE);
    return;
  }

  uint16_t values[123];
  for (uint16_t i = 0U; i < count; i++)
  {
    values[i] = ((uint16_t)frame[7U + i * 2U] << 8) | frame[8U + i * 2U];
  }

  uint8_t result = g_modbus.callbacks.write_multiple_regs(start_addr, count, values);
  if (result != 0U)
  {
    SendException(MODBUS_FC_WRITE_MULTIPLE_REGISTERS, result);
    return;
  }

  /* Response: echo address and count */
  uint8_t response[6];
  response[0] = g_modbus.slave_addr;
  response[1] = MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
  response[2] = frame[2];
  response[3] = frame[3];
  response[4] = frame[4];
  response[5] = frame[5];
  SendFrame(response, 6U);
}

/* Process received frame */
static void ProcessFrame(const uint8_t *frame, uint16_t frame_len)
{
  /* Minimum frame: addr(1) + fc(1) + crc(2) = 4 bytes */
  if (frame_len < 4U)
    return;

  /* Check CRC (Modbus RTU: CRC low byte first, then high byte) */
  uint16_t crc_received = ((uint16_t)frame[frame_len - 2U]) | ((uint16_t)frame[frame_len - 1U] << 8);
  uint16_t crc_calculated = ModbusRtu_Crc16(frame, frame_len - 2U);
  if (crc_received != crc_calculated)
    return;

  uint8_t addr = frame[0];
  uint8_t fc = frame[1];

  /* Broadcast address (0): execute write commands but don't respond */
  if (addr == 0U)
  {
    if (fc == MODBUS_FC_WRITE_SINGLE_REGISTER)
    {
      if (frame_len == 8U)
      {
        uint16_t reg_addr = ((uint16_t)frame[2] << 8) | frame[3];
        uint16_t value = ((uint16_t)frame[4] << 8) | frame[5];
        g_modbus.callbacks.write_single_reg(reg_addr, value);
      }
    }
    else if (fc == MODBUS_FC_WRITE_MULTIPLE_REGISTERS)
    {
      if (frame_len >= 9U)
      {
        uint16_t start_addr = ((uint16_t)frame[2] << 8) | frame[3];
        uint16_t count = ((uint16_t)frame[4] << 8) | frame[5];
        uint8_t byte_count = frame[6];
        if (count > 0U && count <= 123U && byte_count == count * 2U && frame_len == (9U + byte_count))
        {
          uint16_t values[123];
          for (uint16_t i = 0U; i < count; i++)
          {
            values[i] = ((uint16_t)frame[7U + i * 2U] << 8) | frame[8U + i * 2U];
          }
          g_modbus.callbacks.write_multiple_regs(start_addr, count, values);
        }
      }
    }
    return; /* No response for broadcast */
  }

  /* Check if addressed to this slave */
  if (addr != g_modbus.slave_addr)
    return;

  /* Dispatch by function code */
  switch (fc)
  {
    case MODBUS_FC_READ_HOLDING_REGISTERS:
      ProcessFC03(frame, frame_len);
      break;
    case MODBUS_FC_READ_INPUT_REGISTERS:
      ProcessFC04(frame, frame_len);
      break;
    case MODBUS_FC_WRITE_SINGLE_REGISTER:
      ProcessFC06(frame, frame_len);
      break;
    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
      ProcessFC16(frame, frame_len);
      break;
    default:
      SendException(fc, MODBUS_EX_ILLEGAL_FUNCTION);
      break;
  }
}

/* Initialize Modbus RTU */
void ModbusRtu_Init(UART_HandleTypeDef *huart, uint8_t slave_addr, const ModbusCallbacks *callbacks)
{
  memset(&g_modbus, 0, sizeof(g_modbus));
  g_modbus.huart = huart;
  g_modbus.slave_addr = slave_addr;
  g_modbus.callbacks = *callbacks;

  /* Enable USART RX interrupt */
  __HAL_UART_ENABLE_IT(huart, UART_IT_RXNE);
  HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/* RX callback (called from USART IRQ) */
void ModbusRtu_RxCallback(uint8_t byte)
{
  RingBuf_Write(&g_modbus.rx_buf, byte);
}

/* Pause/Resume the RXNE interrupt so another protocol (e.g. YMODEM) can use
   USART1 in polled mode. We do NOT touch the NVIC line — the IRQ vector still
   runs, but the RXNE branch in stm32l4xx_it.c checks RXNEIE and exits cleanly
   while RXNE is masked. */
void ModbusRtu_PauseRx(void)
{
  if (g_modbus.huart == NULL)
    return;
  __HAL_UART_DISABLE_IT(g_modbus.huart, UART_IT_RXNE);
  g_modbus.rx_buf.head = 0U;
  g_modbus.rx_buf.tail = 0U;
  g_modbus.frame_len = 0U;
}

void ModbusRtu_ResumeRx(void)
{
  if (g_modbus.huart == NULL)
    return;
  /* Drain any pending RDR byte and clear overrun before reopening RXNE. */
  __HAL_UART_CLEAR_OREFLAG(g_modbus.huart);
  if (__HAL_UART_GET_FLAG(g_modbus.huart, UART_FLAG_RXNE))
  {
    (void)g_modbus.huart->Instance->RDR;
  }
  g_modbus.rx_buf.head = 0U;
  g_modbus.rx_buf.tail = 0U;
  g_modbus.frame_len = 0U;
  g_modbus.rx_buf.last_rx_tick = HAL_GetTick();
  __HAL_UART_ENABLE_IT(g_modbus.huart, UART_IT_RXNE);
}

void ModbusRtu_ResetRx(void)
{
  if (g_modbus.huart == NULL)
    return;

  __HAL_UART_DISABLE_IT(g_modbus.huart, UART_IT_RXNE);
  __HAL_UART_CLEAR_FEFLAG(g_modbus.huart);
  __HAL_UART_CLEAR_NEFLAG(g_modbus.huart);
  __HAL_UART_CLEAR_OREFLAG(g_modbus.huart);
  __HAL_UART_CLEAR_IDLEFLAG(g_modbus.huart);
  if (__HAL_UART_GET_FLAG(g_modbus.huart, UART_FLAG_RXNE))
  {
    (void)g_modbus.huart->Instance->RDR;
  }

  g_modbus.rx_buf.head = 0U;
  g_modbus.rx_buf.tail = 0U;
  g_modbus.frame_len = 0U;

  __HAL_UART_ENABLE_IT(g_modbus.huart, UART_IT_RXNE);
  HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/* Poll for frame reception and processing */
void ModbusRtu_Poll(void)
{
  uint16_t available = RingBuf_Available(&g_modbus.rx_buf);
  if (available == 0U)
    return;

  /* Check frame gap (2 ms silence) */
  uint32_t elapsed = HAL_GetTick() - g_modbus.rx_buf.last_rx_tick;
  if (elapsed < MODBUS_FRAME_GAP_MS)
    return;

  /* Extract frame from ring buffer */
  g_modbus.frame_len = 0U;
  while (RingBuf_Available(&g_modbus.rx_buf) > 0U && g_modbus.frame_len < MODBUS_FRAME_MAX_SIZE)
  {
    g_modbus.frame_buf[g_modbus.frame_len++] = RingBuf_Read(&g_modbus.rx_buf);
  }

  /* Process frame */
  if (g_modbus.frame_len > 0U)
  {
    ProcessFrame(g_modbus.frame_buf, g_modbus.frame_len);
  }
}

uint32_t ModbusRtu_LastRxTick(void)
{
  return g_modbus.rx_buf.last_rx_tick;
}

void ModbusRtu_MarkActivity(void)
{
  g_modbus.rx_buf.last_rx_tick = HAL_GetTick();
}

void ModbusRtu_SetLastRxTick(uint32_t value)
{
  g_modbus.rx_buf.last_rx_tick = value;
}
