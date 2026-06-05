#ifndef CONTROL_OUTPUTS_H
#define CONTROL_OUTPUTS_H

#include <stdint.h>
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONTROL_OUTPUT_FORCE_AUTO 0U
#define CONTROL_OUTPUT_FORCE_ON   1U
#define CONTROL_OUTPUT_FORCE_OFF  2U

void ControlOutputs_Init(const AppConfigImage *config);
void ControlOutputs_Poll(void);
void ControlOutputs_SetForce(uint8_t index, uint8_t force_mode);
uint8_t ControlOutputs_GetForce(uint8_t index);
uint8_t ControlOutputs_GetOutputState(uint8_t index);
uint16_t ControlOutputs_GetStatus(uint8_t index);
uint16_t ControlOutputs_GetRemainSeconds(uint8_t index);

#ifdef __cplusplus
}
#endif

#endif /* CONTROL_OUTPUTS_H */
