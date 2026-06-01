#ifndef APP_MODBUS_H
#define APP_MODBUS_H

#include <stdint.h>
#include "app_config.h"
#include "app_config_store.h"
#include "app_rtc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Real-time channel measurement data */
typedef struct
{
  int32_t voltage_uv;   /* Microvolts (signed) */
  uint32_t raw_code;    /* 24-bit ADC code */
  uint8_t valid;        /* Sample valid flag */
} AppModbusChannelData;

/* Callback invoked after channel/ADC config is written, to re-apply config
   to the AD7124 hardware immediately. Returns 0 on success, non-zero on
   failure (which becomes a Modbus exception). */
typedef uint8_t (*AppModbusReconfigureCb)(void);

/* Initialize Modbus application layer */
void AppModbus_Init(AppConfigImage *config, AppConfigStore *store);

/* Register the hardware reconfigure callback (optional; NULL disables
   immediate apply, config then only takes effect after save + reset) */
void AppModbus_SetReconfigureCallback(AppModbusReconfigureCb callback);

/* Update real-time channel data (called after AD7124 sampling) */
void AppModbus_UpdateChannelData(uint8_t channel, uint32_t raw, int32_t uv);

/* Update system status flags */
void AppModbus_UpdateSystemStatus(uint8_t ad7124_ready, uint8_t eeprom_ok,
                                   uint8_t rtc_ok, uint8_t sd_ok, uint8_t running);

/* Update last sample timestamp */
void AppModbus_UpdateSampleTimestamp(void);

/* Modbus register access callbacks (called by modbus_rtu.c) */
uint8_t AppModbus_ReadHoldingRegisters(uint16_t start_addr, uint16_t count, uint16_t *out_regs);
uint8_t AppModbus_ReadInputRegisters(uint16_t start_addr, uint16_t count, uint16_t *out_regs);
uint8_t AppModbus_WriteSingleRegister(uint16_t addr, uint16_t value);
uint8_t AppModbus_WriteMultipleRegisters(uint16_t start_addr, uint16_t count, const uint16_t *values);

/* File transfer hand-off to main loop. START sets a pending flag that
   main.c polls after ModbusRtu_Poll, ensuring the FC06 ACK is fully sent
   before USART1 is repurposed for YMODEM. */
uint8_t AppModbus_FileXferStartPending(void);
void    AppModbus_FileXferClearPending(void);
void    AppModbus_FileXferSetState(uint16_t state, uint16_t err);

#ifdef __cplusplus
}
#endif

#endif /* APP_MODBUS_H */
