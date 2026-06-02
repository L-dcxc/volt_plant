#include "app_modbus.h"
#include "modbus_rtu.h"
#include "file_browser.h"
#include "recorder.h"
#include "battery.h"
#include <string.h>

/* Global state */
static AppConfigImage *g_config = NULL;
static AppConfigStore *g_config_store = NULL;
static AppModbusChannelData g_channel_data[APP_CONFIG_CHANNEL_COUNT];
static uint16_t g_ch_valid_mask = 0U;
static uint16_t g_sample_count = 0U;
static AppRtcDateTime g_last_sample_time;
static uint16_t g_system_status = 0U;
static uint16_t g_last_error_code = 0U;
static uint8_t g_control_force[APP_CONFIG_CONTROL_COUNT] = {0U, 0U, 0U, 0U};
static AppModbusReconfigureCb g_reconfigure_cb = NULL;

/* File transfer state (see modbus_register_map.md §0x0060-0x0074). */
static uint16_t g_file_xfer_state = 0U;
static uint16_t g_file_index = 0U;
static uint16_t g_file_err = 0U;
static uint8_t  g_file_xfer_pending = 0U;

/* Initialize */
void AppModbus_Init(AppConfigImage *config, AppConfigStore *store)
{
  g_config = config;
  g_config_store = store;
  memset(g_channel_data, 0, sizeof(g_channel_data));
  g_ch_valid_mask = 0U;
  g_sample_count = 0U;
  memset(&g_last_sample_time, 0, sizeof(g_last_sample_time));
  g_system_status = 0U;
  g_last_error_code = 0U;
}

/* Register hardware reconfigure callback */
void AppModbus_SetReconfigureCallback(AppModbusReconfigureCb callback)
{
  g_reconfigure_cb = callback;
}

/* Update channel data */
void AppModbus_UpdateChannelData(uint8_t channel, uint32_t raw, int32_t uv)
{
  if (channel >= APP_CONFIG_CHANNEL_COUNT)
    return;

  g_channel_data[channel].raw_code = raw;
  g_channel_data[channel].voltage_uv = uv;
  g_channel_data[channel].valid = 1U;
  g_ch_valid_mask |= (1U << channel);
}

/* Update system status */
void AppModbus_UpdateSystemStatus(uint8_t ad7124_ready, uint8_t eeprom_ok,
                                   uint8_t rtc_ok, uint8_t sd_ok, uint8_t running)
{
  g_system_status = 0U;
  if (ad7124_ready != 0U) g_system_status |= (1U << 0);
  if (eeprom_ok != 0U)    g_system_status |= (1U << 1);
  if (rtc_ok != 0U)       g_system_status |= (1U << 2);
  if (sd_ok != 0U)        g_system_status |= (1U << 3);
  if (running != 0U)      g_system_status |= (1U << 4);
}

/* Update sample timestamp */
void AppModbus_UpdateSampleTimestamp(void)
{
  AppRtc_GetDateTime(&g_last_sample_time);
  g_sample_count++;
}

/* Helper: unpack 2 registers to 32-bit */
static uint32_t Unpack32(const uint16_t *regs)
{
  return ((uint32_t)regs[0] << 16) | regs[1];
}

/* Read Holding Registers (FC03) */
uint8_t AppModbus_ReadHoldingRegisters(uint16_t start_addr, uint16_t count, uint16_t *out_regs)
{
  if (g_config == NULL)
    return MODBUS_EX_SLAVE_DEVICE_FAILURE;

  for (uint16_t i = 0U; i < count; i++)
  {
    uint16_t addr = start_addr + i;

    /* 0x0000–0x0003: Device Info (read-only) */
    if (addr == 0x0000U) out_regs[i] = (uint16_t)(g_config->device_id >> 16);
    else if (addr == 0x0001U) out_regs[i] = (uint16_t)(g_config->device_id & 0xFFFFU);
    else if (addr == 0x0002U) out_regs[i] = APP_CONFIG_VERSION;
    else if (addr == 0x0003U) out_regs[i] = g_config->version;

    /* 0x0010–0x0019: System Config */
    else if (addr == 0x0010U) out_regs[i] = g_config->modbus_addr;
    else if (addr == 0x0011U) out_regs[i] = (uint16_t)(g_config->uart_baudrate >> 16);
    else if (addr == 0x0012U) out_regs[i] = (uint16_t)(g_config->uart_baudrate & 0xFFFFU);
    else if (addr == 0x0013U) out_regs[i] = g_config->run_enable;
    else if (addr == 0x0014U) out_regs[i] = g_config->average_enable;
    else if (addr == 0x0015U) out_regs[i] = g_config->file_format;
    else if (addr == 0x0016U) out_regs[i] = (uint16_t)(g_config->sample_interval_sec >> 16);
    else if (addr == 0x0017U) out_regs[i] = (uint16_t)(g_config->sample_interval_sec & 0xFFFFU);
    else if (addr == 0x0018U) out_regs[i] = (uint16_t)(g_config->record_interval_sec >> 16);
    else if (addr == 0x0019U) out_regs[i] = (uint16_t)(g_config->record_interval_sec & 0xFFFFU);

    /* 0x0020–0x0022: ADC Config */
    else if (addr == 0x0020U) out_regs[i] = g_config->adc_vref_mv;
    else if (addr == 0x0021U) out_regs[i] = g_config->adc_default_gain;
    else if (addr == 0x0022U) out_regs[i] = g_config->adc_default_filter;

    /* 0x0030–0x0033: RTC Time */
    else if (addr >= 0x0030U && addr <= 0x0033U)
    {
      AppRtcDateTime dt;
      if (AppRtc_GetDateTime(&dt) == 0U)
        return MODBUS_EX_SLAVE_DEVICE_FAILURE;

      if (addr == 0x0030U) out_regs[i] = dt.year;
      else if (addr == 0x0031U) out_regs[i] = ((uint16_t)dt.month << 8) | dt.day;
      else if (addr == 0x0032U) out_regs[i] = ((uint16_t)dt.hour << 8) | dt.minute;
      else if (addr == 0x0033U) out_regs[i] = dt.second;
    }

    /* 0x0040: Command register (read returns 0) */
    else if (addr == 0x0040U) out_regs[i] = 0U;

    /* 0x0050–0x0053: Control Force */
    else if (addr >= 0x0050U && addr <= 0x0053U)
    {
      uint8_t ctrl_idx = (uint8_t)(addr - 0x0050U);
      out_regs[i] = g_control_force[ctrl_idx];
    }

    /* 0x0100–0x01FF: Channel Config (16 channels × 16 regs) */
    else if (addr >= 0x0100U && addr <= 0x01FFU)
    {
      uint16_t ch_offset = addr - 0x0100U;
      uint8_t ch_idx = (uint8_t)(ch_offset / 0x10U);
      uint8_t reg_offset = (uint8_t)(ch_offset % 0x10U);

      if (ch_idx >= APP_CONFIG_CHANNEL_COUNT)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;

      const AppChannelConfig *ch = &g_config->channels[ch_idx];

      if (reg_offset == 0x0U) out_regs[i] = ((uint16_t)ch->enable << 8) | ch->mode;
      else if (reg_offset == 0x1U) out_regs[i] = ((uint16_t)ch->positive_input << 8) | ch->negative_input;
      else if (reg_offset == 0x2U) out_regs[i] = ((uint16_t)ch->sensor_type << 8) | ch->gain;
      else if (reg_offset == 0x3U) out_regs[i] = ((uint16_t)ch->filter_mode << 8);
      else if (reg_offset == 0x4U) out_regs[i] = (uint16_t)(ch->range_uv >> 16);
      else if (reg_offset == 0x5U) out_regs[i] = (uint16_t)(ch->range_uv & 0xFFFFU);
      else if (reg_offset == 0x6U) out_regs[i] = (uint16_t)(ch->calib_offset_uv >> 16);
      else if (reg_offset == 0x7U) out_regs[i] = (uint16_t)(ch->calib_offset_uv & 0xFFFFU);
      else if (reg_offset == 0x8U) out_regs[i] = (uint16_t)(ch->calib_scale_ppm >> 16);
      else if (reg_offset == 0x9U) out_regs[i] = (uint16_t)(ch->calib_scale_ppm & 0xFFFFU);
      else if (reg_offset == 0xAU) out_regs[i] = (uint16_t)(ch->warmup_ms >> 16);
      else if (reg_offset == 0xBU) out_regs[i] = (uint16_t)(ch->warmup_ms & 0xFFFFU);
      else out_regs[i] = 0U; /* Reserved */
    }

    /* 0x0200–0x022F: Control Output Config (4 controls × 12 regs) */
    else if (addr >= 0x0200U && addr <= 0x022FU)
    {
      uint16_t ctrl_offset = addr - 0x0200U;
      uint8_t ctrl_idx = (uint8_t)(ctrl_offset / 0x0CU);
      uint8_t reg_offset = (uint8_t)(ctrl_offset % 0x0CU);

      if (ctrl_idx >= APP_CONFIG_CONTROL_COUNT)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;

      const AppControlOutputConfig *ctrl = &g_config->controls[ctrl_idx];

      if (reg_offset == 0x0U) out_regs[i] = ((uint16_t)ctrl->enable << 8) | ctrl->output_id;
      else if (reg_offset == 0x1U) out_regs[i] = 0U; /* Reserved */
      else if (reg_offset == 0x2U) out_regs[i] = (uint16_t)(ctrl->interval_sec >> 16);
      else if (reg_offset == 0x3U) out_regs[i] = (uint16_t)(ctrl->interval_sec & 0xFFFFU);
      else if (reg_offset == 0x4U) out_regs[i] = (uint16_t)(ctrl->on_duration_sec >> 16);
      else if (reg_offset == 0x5U) out_regs[i] = (uint16_t)(ctrl->on_duration_sec & 0xFFFFU);
      else if (reg_offset == 0x6U) out_regs[i] = (uint16_t)(ctrl->phase_offset_sec >> 16);
      else if (reg_offset == 0x7U) out_regs[i] = (uint16_t)(ctrl->phase_offset_sec & 0xFFFFU);
      else out_regs[i] = 0U; /* Reserved */
    }

    /* 0x0060–0x0061: File Transfer Control */
    else if (addr == 0x0060U) out_regs[i] = 0U;            /* FILE_CMD: read returns 0 */
    else if (addr == 0x0061U) out_regs[i] = g_file_index;  /* FILE_INDEX */

    else
      return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
  }

  return 0U; /* Success */
}

/* Read Input Registers (FC04) */
uint8_t AppModbus_ReadInputRegisters(uint16_t start_addr, uint16_t count, uint16_t *out_regs)
{
  for (uint16_t i = 0U; i < count; i++)
  {
    uint16_t addr = start_addr + i;

    /* 0x0000–0x003F: Channel Measurement (16 channels × 4 regs) */
    if (addr <= 0x003FU)
    {
      uint8_t ch_idx = (uint8_t)(addr / 4U);
      uint8_t reg_offset = (uint8_t)(addr % 4U);

      if (ch_idx >= APP_CONFIG_CHANNEL_COUNT)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;

      const AppModbusChannelData *ch = &g_channel_data[ch_idx];

      if (reg_offset == 0U) out_regs[i] = (uint16_t)(ch->voltage_uv >> 16);
      else if (reg_offset == 1U) out_regs[i] = (uint16_t)(ch->voltage_uv & 0xFFFFU);
      else if (reg_offset == 2U) out_regs[i] = (uint16_t)(ch->raw_code >> 16);
      else if (reg_offset == 3U) out_regs[i] = (uint16_t)(ch->raw_code & 0xFFFFU);
    }

    /* 0x0040–0x0047: Control Output Status (4 controls × 2 regs) */
    else if (addr >= 0x0040U && addr <= 0x0047U)
    {
      uint8_t ctrl_offset = (uint8_t)(addr - 0x0040U);
      uint8_t ctrl_idx = ctrl_offset / 2U;
      uint8_t reg_offset = ctrl_offset % 2U;

      if (ctrl_idx >= APP_CONFIG_CONTROL_COUNT)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;

      if (reg_offset == 0U)
      {
        /* STATUS: bit0=output, bit1=enable, bit2=force */
        uint16_t status = 0U;
        if (g_config->controls[ctrl_idx].enable != 0U) status |= (1U << 1);
        if (g_control_force[ctrl_idx] != 0U) status |= (1U << 2);
        out_regs[i] = status;
      }
      else
      {
        out_regs[i] = 0U; /* REMAIN_S (not implemented) */
      }
    }

    /* 0x0048–0x004F: Data Freshness */
    else if (addr == 0x0048U) out_regs[i] = g_ch_valid_mask;
    else if (addr == 0x0049U) out_regs[i] = g_sample_count;
    else if (addr == 0x004AU) out_regs[i] = g_last_sample_time.year;
    else if (addr == 0x004BU) out_regs[i] = ((uint16_t)g_last_sample_time.month << 8) | g_last_sample_time.day;
    else if (addr == 0x004CU) out_regs[i] = ((uint16_t)g_last_sample_time.hour << 8) | g_last_sample_time.minute;
    else if (addr == 0x004DU) out_regs[i] = g_last_sample_time.second;
    else if (addr >= 0x004EU && addr <= 0x004FU) out_regs[i] = 0U; /* Reserved */

    /* 0x0050–0x0053: System Status. RUNNING bit reflects the live config so
       the host sees state changes immediately after writing HR_RUN_ENABLE,
       without having to wait for the next sample cycle to refresh it. */
    else if (addr == 0x0050U)
    {
      uint16_t s = g_system_status & ~(uint16_t)(1U << 4);
      if (g_config != NULL && g_config->run_enable != 0U) s |= (uint16_t)(1U << 4);
      out_regs[i] = s;
    }
    else if (addr == 0x0051U) out_regs[i] = (uint16_t)(HAL_GetTick() >> 16);
    else if (addr == 0x0052U) out_regs[i] = (uint16_t)(HAL_GetTick() & 0xFFFFU);
    else if (addr == 0x0053U) out_regs[i] = g_last_error_code;

    /* 0x0054–0x0057: SD capacity in MiB (total then free, each 32-bit). */
    else if (addr >= 0x0054U && addr <= 0x0057U)
    {
      uint32_t total_kb = 0U;
      uint32_t free_kb = 0U;
      Recorder_GetCapacityKB(&total_kb, &free_kb);
      uint32_t total_mb = total_kb / 1024U;
      uint32_t free_mb = free_kb / 1024U;
      if (addr == 0x0054U)      out_regs[i] = (uint16_t)(total_mb >> 16);
      else if (addr == 0x0055U) out_regs[i] = (uint16_t)(total_mb & 0xFFFFU);
      else if (addr == 0x0056U) out_regs[i] = (uint16_t)(free_mb >> 16);
      else                      out_regs[i] = (uint16_t)(free_mb & 0xFFFFU);
    }
    /* 0x0058–0x0059: Battery voltage + measured VDDA, both in millivolts. */
    else if (addr == 0x0058U) out_regs[i] = Battery_GetMilliVolts();
    else if (addr == 0x0059U) out_regs[i] = Battery_GetVddaMilliVolts();
    else if (addr >= 0x005AU && addr <= 0x005FU) out_regs[i] = 0U; /* Reserved */

    /* 0x0060–0x0074: File Transfer Status */
    else if (addr == 0x0060U) out_regs[i] = g_file_xfer_state;
    else if (addr == 0x0061U) out_regs[i] = FileBrowser_GetCount();
    else if (addr == 0x0062U || addr == 0x0063U)
    {
      const FileBrowserEntry *e = FileBrowser_GetSelected();
      uint32_t size = (e != NULL) ? e->size : 0U;
      out_regs[i] = (addr == 0x0062U) ? (uint16_t)(size >> 16) : (uint16_t)(size & 0xFFFFU);
    }
    else if (addr >= 0x0064U && addr <= 0x0073U)
    {
      const FileBrowserEntry *e = FileBrowser_GetSelected();
      uint16_t name_idx = addr - 0x0064U;
      uint8_t hi = 0U;
      uint8_t lo = 0U;
      if (e != NULL)
      {
        uint16_t name_len = (uint16_t)strlen(e->name);
        uint16_t pos_hi = (uint16_t)(name_idx * 2U);
        uint16_t pos_lo = (uint16_t)(name_idx * 2U + 1U);
        if (pos_hi < name_len) hi = (uint8_t)e->name[pos_hi];
        if (pos_lo < name_len) lo = (uint8_t)e->name[pos_lo];
      }
      out_regs[i] = ((uint16_t)hi << 8) | lo;
    }
    else if (addr == 0x0074U) out_regs[i] = g_file_err;

    else
      return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
  }

  return 0U; /* Success */
}

/* Write Single Register (FC06) */
uint8_t AppModbus_WriteSingleRegister(uint16_t addr, uint16_t value)
{
  if (g_config == NULL || g_config_store == NULL)
    return MODBUS_EX_SLAVE_DEVICE_FAILURE;

  /* 0x0000–0x0003: Device Info (read-only) */
  if (addr <= 0x0003U)
    return MODBUS_EX_ILLEGAL_DATA_ADDRESS;

  /* 0x0010–0x0019: System Config */
  if (addr == 0x0010U)
  {
    if (value < 1U || value > 247U)
      return MODBUS_EX_ILLEGAL_DATA_VALUE;
    g_config->modbus_addr = (uint8_t)value;
  }
  else if (addr == 0x0013U) g_config->run_enable = (uint8_t)value;
  else if (addr == 0x0014U) g_config->average_enable = (uint8_t)value;
  else if (addr == 0x0015U)
  {
    if (value > 2U)
      return MODBUS_EX_ILLEGAL_DATA_VALUE;
    g_config->file_format = (uint8_t)value;
  }

  /* 0x0020–0x0022: ADC Config */
  else if (addr == 0x0020U)
  {
    if (value == 0U)
      return MODBUS_EX_ILLEGAL_DATA_VALUE;
    g_config->adc_vref_mv = value;
    if (g_reconfigure_cb != NULL && g_reconfigure_cb() != 0U)
      return MODBUS_EX_SLAVE_DEVICE_FAILURE;
  }
  else if (addr == 0x0021U)
  {
    if (value == 0U)
      return MODBUS_EX_ILLEGAL_DATA_VALUE;
    g_config->adc_default_gain = (uint8_t)value;
    if (g_reconfigure_cb != NULL && g_reconfigure_cb() != 0U)
      return MODBUS_EX_SLAVE_DEVICE_FAILURE;
  }
  else if (addr == 0x0022U)
  {
    g_config->adc_default_filter = (uint8_t)value;
    if (g_reconfigure_cb != NULL && g_reconfigure_cb() != 0U)
      return MODBUS_EX_SLAVE_DEVICE_FAILURE;
  }

  /* 0x0030–0x0033: RTC Time */
  else if (addr >= 0x0030U && addr <= 0x0033U)
  {
    /* RTC write requires all 4 registers, defer to FC16 */
    return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
  }

  /* 0x0040: Command Register */
  else if (addr == 0x0040U)
  {
    if (value == 0x0001U) /* Save config */
    {
      if (AppConfigStore_Save(g_config_store, g_config) != HAL_OK)
        return MODBUS_EX_SLAVE_DEVICE_FAILURE;
    }
    else if (value == 0x0002U) /* Restore defaults */
    {
      if (AppConfigStore_SaveDefaults(g_config_store, g_config) != HAL_OK)
        return MODBUS_EX_SLAVE_DEVICE_FAILURE;
    }
    else if (value == 0x0003U) /* Software reset */
    {
      HAL_Delay(100U); /* Allow response to be sent */
      NVIC_SystemReset();
    }
    else
      return MODBUS_EX_ILLEGAL_DATA_VALUE;
  }

  /* 0x0050–0x0053: Control Force */
  else if (addr >= 0x0050U && addr <= 0x0053U)
  {
    uint8_t ctrl_idx = (uint8_t)(addr - 0x0050U);
    if (value > 2U)
      return MODBUS_EX_ILLEGAL_DATA_VALUE;
    if (g_config->controls[ctrl_idx].enable == 0U && value != 0U)
      return MODBUS_EX_ILLEGAL_DATA_VALUE; /* Can't force disabled control */
    g_control_force[ctrl_idx] = (uint8_t)value;
  }

  /* 0x0061: FILE_INDEX (stage the index for the next SELECT/START/DELETE) */
  else if (addr == 0x0061U)
  {
    g_file_index = value;
  }

  /* 0x0060: FILE_CMD (drives the file browser state machine) */
  else if (addr == 0x0060U)
  {
    if (value == 0x0001U) /* OPEN_DIR */
    {
      FRESULT fr = FileBrowser_OpenDir();
      if (fr != FR_OK)
      {
        g_file_xfer_state = 5U;
        g_file_err = 0x0004U;
        return MODBUS_EX_SLAVE_DEVICE_FAILURE;
      }
      g_file_xfer_state = 1U;
      g_file_err = 0U;
    }
    else if (value == 0x0002U) /* SELECT */
    {
      if (g_file_xfer_state < 1U)
        return MODBUS_EX_ILLEGAL_DATA_VALUE;
      if (FileBrowser_Select(g_file_index) == 0U)
        return MODBUS_EX_ILLEGAL_DATA_VALUE;
      g_file_xfer_state = 2U;
      g_file_err = 0U;
    }
    else if (value == 0x0003U) /* START */
    {
      if (g_file_xfer_state != 2U)
        return MODBUS_EX_ILLEGAL_DATA_VALUE;
      g_file_xfer_pending = 1U;
      g_file_xfer_state = 3U;
      g_file_err = 0U;
      /* Yield USART1 RX immediately so the host's 'C' poll byte cannot be
         swallowed by the Modbus IRQ between now and the time main.c reaches
         YModem_SendFile. TX of the FC06 ACK is unaffected (polled by HAL). */
      ModbusRtu_PauseRx();
    }
    else if (value == 0x0004U) /* DELETE */
    {
      if (g_file_xfer_state != 2U)
        return MODBUS_EX_ILLEGAL_DATA_VALUE;
      if (FileBrowser_DeleteSelected() != FR_OK)
      {
        g_file_err = 0x0004U;
        return MODBUS_EX_SLAVE_DEVICE_FAILURE;
      }
      /* Refresh snapshot so subsequent indexes are consistent */
      (void)FileBrowser_OpenDir();
      g_file_xfer_state = 1U;
      g_file_err = 0U;
    }
    else
    {
      return MODBUS_EX_ILLEGAL_DATA_VALUE;
    }
  }

  /* Multi-register fields require FC16 */
  else
    return MODBUS_EX_ILLEGAL_DATA_ADDRESS;

  return 0U; /* Success */
}

/* Write Multiple Registers (FC16) */
uint8_t AppModbus_WriteMultipleRegisters(uint16_t start_addr, uint16_t count, const uint16_t *values)
{
  uint8_t adc_config_dirty = 0U;

  if (g_config == NULL)
    return MODBUS_EX_SLAVE_DEVICE_FAILURE;

  /* Validate entire range first */
  for (uint16_t i = 0U; i < count; i++)
  {
    uint16_t addr = start_addr + i;

    /* Read-only regions */
    if (addr <= 0x0003U)
      return MODBUS_EX_ILLEGAL_DATA_ADDRESS;

    /* Command register is write-only via FC06 */
    if (addr == 0x0040U)
      return MODBUS_EX_ILLEGAL_DATA_ADDRESS;

    /* Channel config (0x0100-0x01FF) is handled inline below and needs a
       hardware reconfigure afterwards. ADC config (0x0020-0x0022) is
       delegated to FC06, which fires its own reconfigure. */
    if (addr >= 0x0100U && addr <= 0x01FFU)
      adc_config_dirty = 1U;
  }

  /* Write all registers */
  for (uint16_t i = 0U; i < count; i++)
  {
    uint16_t addr = start_addr + i;
    uint16_t value = values[i];

    /* System Config (multi-register fields) */
    if (addr == 0x0011U && i + 1U < count)
    {
      g_config->uart_baudrate = Unpack32(&values[i]);
      i++; /* Skip next register */
    }
    else if (addr == 0x0016U && i + 1U < count)
    {
      uint32_t interval = Unpack32(&values[i]);
      if (interval == 0U)
        return MODBUS_EX_ILLEGAL_DATA_VALUE;
      g_config->sample_interval_sec = interval;
      i++;
    }
    else if (addr == 0x0018U && i + 1U < count)
    {
      uint32_t interval = Unpack32(&values[i]);
      if (interval == 0U)
        return MODBUS_EX_ILLEGAL_DATA_VALUE;
      g_config->record_interval_sec = interval;
      i++;
    }

    /* RTC Time (4 registers) */
    else if (addr == 0x0030U && i + 3U < count)
    {
      AppRtcDateTime dt;
      dt.year = values[i];
      dt.month = (uint8_t)(values[i + 1U] >> 8);
      dt.day = (uint8_t)(values[i + 1U] & 0xFFU);
      dt.hour = (uint8_t)(values[i + 2U] >> 8);
      dt.minute = (uint8_t)(values[i + 2U] & 0xFFU);
      dt.second = (uint8_t)values[i + 3U];

      if (AppRtc_SetDateTime(&dt) == 0U)
        return MODBUS_EX_SLAVE_DEVICE_FAILURE;

      i += 3U;
    }

    /* Channel Config */
    else if (addr >= 0x0100U && addr <= 0x01FFU)
    {
      uint16_t ch_offset = addr - 0x0100U;
      uint8_t ch_idx = (uint8_t)(ch_offset / 0x10U);
      uint8_t reg_offset = (uint8_t)(ch_offset % 0x10U);

      if (ch_idx >= APP_CONFIG_CHANNEL_COUNT)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;

      AppChannelConfig *ch = &g_config->channels[ch_idx];

      if (reg_offset == 0x0U)
      {
        ch->enable = (uint8_t)(value >> 8);
        ch->mode = (uint8_t)(value & 0xFFU);
      }
      else if (reg_offset == 0x1U)
      {
        ch->positive_input = (uint8_t)(value >> 8);
        ch->negative_input = (uint8_t)(value & 0xFFU);
      }
      else if (reg_offset == 0x2U)
      {
        ch->sensor_type = (uint8_t)(value >> 8);
        ch->gain = (uint8_t)(value & 0xFFU);
      }
      else if (reg_offset == 0x3U)
      {
        ch->filter_mode = (uint8_t)(value >> 8);
      }
      else if (reg_offset == 0x4U && i + 1U < count)
      {
        ch->range_uv = Unpack32(&values[i]);
        i++;
      }
      else if (reg_offset == 0x6U && i + 1U < count)
      {
        ch->calib_offset_uv = (int32_t)Unpack32(&values[i]);
        i++;
      }
      else if (reg_offset == 0x8U && i + 1U < count)
      {
        ch->calib_scale_ppm = (int32_t)Unpack32(&values[i]);
        i++;
      }
      else if (reg_offset == 0xAU && i + 1U < count)
      {
        ch->warmup_ms = Unpack32(&values[i]);
        i++;
      }
    }

    /* Control Output Config */
    else if (addr >= 0x0200U && addr <= 0x022FU)
    {
      uint16_t ctrl_offset = addr - 0x0200U;
      uint8_t ctrl_idx = (uint8_t)(ctrl_offset / 0x0CU);
      uint8_t reg_offset = (uint8_t)(ctrl_offset % 0x0CU);

      if (ctrl_idx >= APP_CONFIG_CONTROL_COUNT)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;

      AppControlOutputConfig *ctrl = &g_config->controls[ctrl_idx];

      if (reg_offset == 0x0U)
      {
        ctrl->enable = (uint8_t)(value >> 8);
        ctrl->output_id = (uint8_t)(value & 0xFFU);
      }
      else if (reg_offset == 0x2U && i + 1U < count)
      {
        ctrl->interval_sec = Unpack32(&values[i]);
        i++;
      }
      else if (reg_offset == 0x4U && i + 1U < count)
      {
        ctrl->on_duration_sec = Unpack32(&values[i]);
        i++;
      }
      else if (reg_offset == 0x6U && i + 1U < count)
      {
        ctrl->phase_offset_sec = Unpack32(&values[i]);
        i++;
      }
    }

    /* Single-register fields: delegate to FC06 */
    else
    {
      uint8_t result = AppModbus_WriteSingleRegister(addr, value);
      if (result != 0U)
        return result;
    }
  }

  /* Apply config to AD7124 hardware immediately if channel/ADC config changed */
  if (adc_config_dirty != 0U && g_reconfigure_cb != NULL)
  {
    if (g_reconfigure_cb() != 0U)
      return MODBUS_EX_SLAVE_DEVICE_FAILURE;
  }

  return 0U; /* Success */
}

/* File transfer hand-off helpers (used by main.c after Modbus poll). */
uint8_t AppModbus_FileXferStartPending(void)
{
  return g_file_xfer_pending;
}

void AppModbus_FileXferClearPending(void)
{
  g_file_xfer_pending = 0U;
}

void AppModbus_FileXferSetState(uint16_t state, uint16_t err)
{
  g_file_xfer_state = state;
  g_file_err = err;
}
