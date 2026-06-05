#include "control_outputs.h"

#include "main.h"
#include "stm32l4xx_hal.h"

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
} ControlOutputPin;

static const AppConfigImage *s_config = 0;
static uint8_t s_force[APP_CONFIG_CONTROL_COUNT] = {0U, 0U, 0U, 0U};
static uint8_t s_state[APP_CONFIG_CONTROL_COUNT] = {0U, 0U, 0U, 0U};
static uint8_t s_pin_state[APP_CONFIG_CONTROL_COUNT] = {0U, 0U, 0U, 0U};
static uint16_t s_remain_s[APP_CONFIG_CONTROL_COUNT] = {0U, 0U, 0U, 0U};

static const ControlOutputPin s_pins[APP_CONFIG_CONTROL_COUNT] =
{
  {CTRL_OUT1_GPIO_Port, CTRL_OUT1_Pin},
  {CTRL_OUT2_GPIO_Port, CTRL_OUT2_Pin},
  {CTRL_OUT3_GPIO_Port, CTRL_OUT3_Pin},
  {CTRL_OUT4_GPIO_Port, CTRL_OUT4_Pin}
};

static void ControlOutputs_WritePin(uint8_t output_id, uint8_t on)
{
  if (output_id >= APP_CONFIG_CONTROL_COUNT)
  {
    return;
  }

  HAL_GPIO_WritePin(s_pins[output_id].port,
                    s_pins[output_id].pin,
                    (on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  s_pin_state[output_id] = (on != 0U) ? 1U : 0U;
}

static uint8_t ControlOutputs_AutoState(const AppControlOutputConfig *ctrl,
                                        uint16_t *remain_s)
{
  uint32_t now_s;
  uint32_t phase;
  uint32_t position;

  if (remain_s != 0)
  {
    *remain_s = 0U;
  }

  if ((ctrl == 0) ||
      (ctrl->enable == 0U) ||
      (ctrl->interval_sec == 0UL) ||
      (ctrl->on_duration_sec == 0UL) ||
      (ctrl->on_duration_sec > ctrl->interval_sec))
  {
    return 0U;
  }

  now_s = HAL_GetTick() / 1000UL;
  phase = ctrl->phase_offset_sec % ctrl->interval_sec;
  position = (now_s + ctrl->interval_sec - phase) % ctrl->interval_sec;

  if (position < ctrl->on_duration_sec)
  {
    uint32_t remain = ctrl->on_duration_sec - position;
    if (remain_s != 0)
    {
      *remain_s = (uint16_t)((remain > 0xFFFFUL) ? 0xFFFFU : remain);
    }
    return 1U;
  }

  if (remain_s != 0)
  {
    uint32_t remain = ctrl->interval_sec - position;
    *remain_s = (uint16_t)((remain > 0xFFFFUL) ? 0xFFFFU : remain);
  }
  return 0U;
}

void ControlOutputs_Init(const AppConfigImage *config)
{
  uint8_t i;

  s_config = config;
  for (i = 0U; i < APP_CONFIG_CONTROL_COUNT; i++)
  {
    s_force[i] = CONTROL_OUTPUT_FORCE_AUTO;
    s_state[i] = 0U;
    s_remain_s[i] = 0U;
    ControlOutputs_WritePin(i, 0U);
  }
}

void ControlOutputs_Poll(void)
{
  uint8_t i;
  uint8_t desired_pin_state[APP_CONFIG_CONTROL_COUNT] = {0U, 0U, 0U, 0U};

  if (s_config == 0)
  {
    return;
  }

  for (i = 0U; i < APP_CONFIG_CONTROL_COUNT; i++)
  {
    const AppControlOutputConfig *ctrl = &s_config->controls[i];
    uint8_t next_state = 0U;
    uint16_t remain = 0U;

    if (ctrl->enable == 0U)
    {
      s_force[i] = CONTROL_OUTPUT_FORCE_AUTO;
      next_state = 0U;
      remain = 0U;
    }
    else if (s_force[i] == CONTROL_OUTPUT_FORCE_ON)
    {
      next_state = 1U;
      remain = 0U;
    }
    else if (s_force[i] == CONTROL_OUTPUT_FORCE_OFF)
    {
      next_state = 0U;
      remain = 0U;
    }
    else
    {
      next_state = ControlOutputs_AutoState(ctrl, &remain);
    }

    if (ctrl->output_id < APP_CONFIG_CONTROL_COUNT && next_state != 0U)
    {
      desired_pin_state[ctrl->output_id] = 1U;
    }

    s_state[i] = next_state;
    s_remain_s[i] = remain;
  }

  for (i = 0U; i < APP_CONFIG_CONTROL_COUNT; i++)
  {
    if (s_pin_state[i] != desired_pin_state[i])
    {
      ControlOutputs_WritePin(i, desired_pin_state[i]);
    }
  }
}

void ControlOutputs_SetForce(uint8_t index, uint8_t force_mode)
{
  if (index >= APP_CONFIG_CONTROL_COUNT)
  {
    return;
  }
  if (force_mode > CONTROL_OUTPUT_FORCE_OFF)
  {
    return;
  }
  s_force[index] = force_mode;
}

uint8_t ControlOutputs_GetForce(uint8_t index)
{
  if (index >= APP_CONFIG_CONTROL_COUNT)
  {
    return CONTROL_OUTPUT_FORCE_AUTO;
  }
  return s_force[index];
}

uint8_t ControlOutputs_GetOutputState(uint8_t index)
{
  if (index >= APP_CONFIG_CONTROL_COUNT)
  {
    return 0U;
  }
  return s_state[index];
}

uint16_t ControlOutputs_GetStatus(uint8_t index)
{
  uint16_t status = 0U;

  if ((s_config == 0) || (index >= APP_CONFIG_CONTROL_COUNT))
  {
    return 0U;
  }

  if (s_state[index] != 0U)
  {
    status |= (1U << 0);
  }
  if (s_config->controls[index].enable != 0U)
  {
    status |= (1U << 1);
  }
  if (s_force[index] != CONTROL_OUTPUT_FORCE_AUTO)
  {
    status |= (1U << 2);
  }
  return status;
}

uint16_t ControlOutputs_GetRemainSeconds(uint8_t index)
{
  if (index >= APP_CONFIG_CONTROL_COUNT)
  {
    return 0U;
  }
  return s_remain_s[index];
}
