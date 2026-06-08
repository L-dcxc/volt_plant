#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stdint.h>
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Modbus function codes */
#define MODBUS_FC_READ_HOLDING_REGISTERS    0x03U
#define MODBUS_FC_READ_INPUT_REGISTERS      0x04U
#define MODBUS_FC_WRITE_SINGLE_REGISTER     0x06U
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS  0x10U

/* Modbus exception codes */
#define MODBUS_EX_ILLEGAL_FUNCTION          0x01U
#define MODBUS_EX_ILLEGAL_DATA_ADDRESS      0x02U
#define MODBUS_EX_ILLEGAL_DATA_VALUE        0x03U
#define MODBUS_EX_SLAVE_DEVICE_FAILURE      0x04U

/* Frame timing */
#define MODBUS_FRAME_GAP_MS                 2U
#define MODBUS_RX_BUFFER_SIZE               256U
#define MODBUS_FRAME_MAX_SIZE               256U

/* Application callbacks (implemented in app_modbus.c) */
typedef uint8_t (*ModbusReadHoldingRegsCb)(uint16_t start_addr, uint16_t count, uint16_t *out_regs);
typedef uint8_t (*ModbusReadInputRegsCb)(uint16_t start_addr, uint16_t count, uint16_t *out_regs);
typedef uint8_t (*ModbusWriteSingleRegCb)(uint16_t addr, uint16_t value);
typedef uint8_t (*ModbusWriteMultipleRegsCb)(uint16_t start_addr, uint16_t count, const uint16_t *values);

typedef struct
{
  ModbusReadHoldingRegsCb read_holding_regs;
  ModbusReadInputRegsCb read_input_regs;
  ModbusWriteSingleRegCb write_single_reg;
  ModbusWriteMultipleRegsCb write_multiple_regs;
} ModbusCallbacks;

/* Initialize Modbus RTU (enables USART RX interrupt) */
void ModbusRtu_Init(UART_HandleTypeDef *huart, uint8_t slave_addr, const ModbusCallbacks *callbacks);

/* Poll for frame reception and processing (call in main loop) */
void ModbusRtu_Poll(void);
uint32_t ModbusRtu_LastRxTick(void);
void ModbusRtu_MarkActivity(void);
void ModbusRtu_ResetRx(void);

/* RX callback (call from USART IRQ handler) */
void ModbusRtu_RxCallback(uint8_t byte);

/* CRC16 calculation (Modbus standard) */
uint16_t ModbusRtu_Crc16(const uint8_t *data, uint16_t length);

/* Temporarily yield USART1 to another protocol (e.g. YMODEM). PauseRx masks
   the RXNE interrupt and drops any half-decoded frame; ResumeRx flushes the
   overrun flag and re-enables RXNE. */
void ModbusRtu_PauseRx(void);
void ModbusRtu_ResumeRx(void);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_RTU_H */
