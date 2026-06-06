/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "fatfs.h"
#include "i2c.h"
#include "iwdg.h"
#include "rtc.h"
#include "sdmmc.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "app_rtc.h"
#include "ad7124.h"
#include "storage.h"
#include "app_config_store.h"
#include "modbus_rtu.h"
#include "app_modbus.h"
#include "recorder.h"
#include "file_browser.h"
#include "ymodem.h"
#include "battery.h"
#include "control_outputs.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_POWER_ON_HOLD_TIME_MS 2000U
#define APP_POWER_ON_CHECK_PERIOD_MS 10U
#define APP_POWER_OFF_HOLD_TIME_MS 3000U
#define APP_IDLE_SLEEP_MIN_MS 20U
#define APP_SERIAL_ACTIVE_HOLD_MS 60000U
#define APP_KEY0_HOLD_TIME_MS 2000U
#define APP_KEY0_ACTIVE_HOLD_MS 60000U
#define APP_SLEEP_LED_FLASH_MS 120U
#define APP_STOP2_MAX_SLEEP_SECONDS 5U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static AD7124_HandleTypeDef had7124;
static AppConfigStore app_config_store;
static AppConfigImage app_config;
static uint8_t ad7124_ready = 0U;
static uint8_t ad7124_loop_print_enable = 0U;
static uint8_t storage_boot_test_enable = 1U;
static uint32_t ad7124_print_tick = 0U;
static uint32_t led_blink_tick = 0U;
static uint8_t modbus_enabled = 1U;
static uint8_t eeprom_ok = 0U;
static uint8_t rtc_ok = 0U;
static uint8_t sd_ok = 0U;
static uint32_t sample_tick = 0U;
static uint8_t prev_run_enable = 0U;
static uint32_t battery_tick = 0U;
static uint32_t key0_active_until_tick = 0U;
static uint8_t idle_sleep_state = 0U;
static uint8_t led_notice_active = 0U;
static uint8_t led_notice_phase = 0U;
static uint32_t led_notice_tick = 0U;
static volatile uint8_t stop2_rtc_wakeup = 0U;
#define BATTERY_UPDATE_INTERVAL_MS  30000U

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
/* USER CODE BEGIN PFP */
static void AppUart1_EnableStopWakeup(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void PrintSdDebug(const char *tag)
{
  GPIO_PinState cd_state;
  uint32_t hal_error;
  HAL_SD_StateTypeDef hal_state;

  cd_state = HAL_GPIO_ReadPin(SD_CD_GPIO_Port, SD_CD_Pin);
  hal_state = HAL_SD_GetState(&hsd1);
  hal_error = HAL_SD_GetError(&hsd1);

  printf("[SD] %s diag: SD_CD=%u HAL_STATE=%lu HAL_ERR=0x%08lX BLOCK_NBR=%lu BLOCK_SIZE=%lu CARD_TYPE=%lu\r\n",
         tag,
         (unsigned)((cd_state == GPIO_PIN_SET) ? 1U : 0U),
         (unsigned long)hal_state,
         (unsigned long)hal_error,
         (unsigned long)hsd1.SdCard.LogBlockNbr,
         (unsigned long)hsd1.SdCard.LogBlockSize,
         (unsigned long)hsd1.SdCard.CardType);
}

static void ResetSdmmcForRetry(void)
{
  (void)f_mount(NULL, (TCHAR const *)SDPath, 0U);
  (void)HAL_SD_DeInit(&hsd1);
  MX_SDMMC1_SD_Init();
}

static uint8_t AppPower_CheckOnOffLowAndLatch(void)
{
  uint32_t low_start_tick = 0UL;

  HAL_GPIO_WritePin(PWR_ON_GPIO_Port, PWR_ON_Pin, GPIO_PIN_RESET);

  if (HAL_GPIO_ReadPin(ON_OFF_GPIO_Port, ON_OFF_Pin) != GPIO_PIN_RESET)
  {
    return 0U;
  }

  low_start_tick = HAL_GetTick();
  while ((HAL_GetTick() - low_start_tick) < APP_POWER_ON_HOLD_TIME_MS)
  {
    if (HAL_GPIO_ReadPin(ON_OFF_GPIO_Port, ON_OFF_Pin) != GPIO_PIN_RESET)
    {
      return 0U;
    }

    HAL_IWDG_Refresh(&hiwdg);
    HAL_Delay(APP_POWER_ON_CHECK_PERIOD_MS);
  }

  HAL_GPIO_WritePin(PWR_ON_GPIO_Port, PWR_ON_Pin, GPIO_PIN_SET);
  return 1U;
}

/* Modbus reconfigure callback: re-apply current config to AD7124 hardware.
   Returns 0 on success, 1 on failure (becomes a Modbus exception). */
static uint8_t AppModbus_ReconfigureAd7124(void)
{
  if (ad7124_ready == 0U)
  {
    return 1U;
  }
  return (AD7124_ApplyConfig(&had7124, &app_config) == HAL_OK) ? 0U : 1U;
}

/* Non-blocking long-press shutdown detector. Call once per main-loop
   iteration. ON_OFF must stay LOW continuously for APP_POWER_OFF_HOLD_TIME_MS;
   releasing it (HIGH) at any point cancels the shutdown. When the hold is
   satisfied, PWR_ON is driven LOW to cut the latched power rail. */
static void AppPower_PollShutdown(void)
{
  static uint8_t press_active = 0U;
  static uint32_t press_start_tick = 0U;

  if (HAL_GPIO_ReadPin(ON_OFF_GPIO_Port, ON_OFF_Pin) == GPIO_PIN_RESET)
  {
    /* ON_OFF is held low */
    if (press_active == 0U)
    {
      press_active = 1U;
      press_start_tick = HAL_GetTick();
    }
    else if ((HAL_GetTick() - press_start_tick) >= APP_POWER_OFF_HOLD_TIME_MS)
    {
      /* Held long enough: cut power. This removes the board's own supply,
         so execution stops here once the rail collapses. */
      (void)Recorder_Flush();
      HAL_GPIO_WritePin(PWR_ON_GPIO_Port, PWR_ON_Pin, GPIO_PIN_RESET);
      while (1)
      {
        /* Wait for power to drop. Keep feeding the watchdog so we don't
           reset-and-relatch if the user is still holding the key. */
        HAL_IWDG_Refresh(&hiwdg);
      }
    }
  }
  else
  {
    /* Released before hold time elapsed: cancel */
    press_active = 0U;
  }
}

static void AppUart1_EnableStopWakeup(void)
{
  UART_WakeUpTypeDef wakeup_config = {0};

  wakeup_config.WakeUpEvent = UART_WAKEUP_ON_STARTBIT;
  if (HAL_UARTEx_StopModeWakeUpSourceConfig(&huart1, wakeup_config) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_EnableClockStopMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_EnableStopMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  __HAL_UART_CLEAR_FLAG(&huart1, UART_CLEAR_WUF);
  __HAL_UART_ENABLE_IT(&huart1, UART_IT_WUF);
}

static uint8_t AppSamplingDueSoon(uint32_t now_tick)
{
  uint32_t interval_ms;
  uint32_t elapsed;

  if ((app_config.run_enable == 0U) || (ad7124_ready == 0U))
  {
    return 0U;
  }

  interval_ms = app_config.sample_interval_sec * 1000UL;
  if (interval_ms == 0UL)
  {
    interval_ms = 1000UL;
  }

  elapsed = now_tick - sample_tick;
  if (elapsed >= interval_ms)
  {
    return 1U;
  }

  return ((interval_ms - elapsed) <= APP_IDLE_SLEEP_MIN_MS) ? 1U : 0U;
}

static uint32_t AppNextStop2SleepSeconds(uint32_t now_tick)
{
  uint32_t sleep_seconds = APP_STOP2_MAX_SLEEP_SECONDS;

  if ((app_config.run_enable != 0U) && (ad7124_ready != 0U))
  {
    uint32_t interval_ms = app_config.sample_interval_sec * 1000UL;
    uint32_t elapsed;
    uint32_t remaining_ms;
    uint32_t sample_limited_seconds;

    if (interval_ms == 0UL)
    {
      interval_ms = 1000UL;
    }

    elapsed = now_tick - sample_tick;
    if (elapsed >= interval_ms)
    {
      return 0UL;
    }

    remaining_ms = interval_ms - elapsed;
    if (remaining_ms <= APP_IDLE_SLEEP_MIN_MS)
    {
      return 0UL;
    }

    /* RTC wakeup uses a 1 Hz clock here, so round down. If less than one
       second remains, stay awake and let the normal scheduler run the scan. */
    sample_limited_seconds = remaining_ms / 1000UL;
    if (sample_limited_seconds == 0UL)
    {
      return 0UL;
    }
    if (sample_limited_seconds < sleep_seconds)
    {
      sleep_seconds = sample_limited_seconds;
    }
  }

  return sleep_seconds;
}

static uint8_t AppSerialRecentlyActive(uint32_t now_tick)
{
  if (modbus_enabled == 0U)
  {
    return 0U;
  }

  return ((now_tick - ModbusRtu_LastRxTick()) <= APP_SERIAL_ACTIVE_HOLD_MS) ? 1U : 0U;
}

static uint8_t AppKey0ActiveWindow(uint32_t now_tick)
{
  if (key0_active_until_tick == 0U)
  {
    return 0U;
  }

  if ((int32_t)(key0_active_until_tick - now_tick) > 0)
  {
    return 1U;
  }

  key0_active_until_tick = 0U;
  return 0U;
}

static void AppSetAllStatusLeds(GPIO_PinState state)
{
  HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, state);
  HAL_GPIO_WritePin(LED_YELLOW_GPIO_Port, LED_YELLOW_Pin, state);
  HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, state);
}

static void AppLedNoticeStart(void)
{
  led_notice_active = 1U;
  led_notice_phase = 0U;
  led_notice_tick = HAL_GetTick();
  AppSetAllStatusLeds(GPIO_PIN_SET);
}

static uint8_t AppLedNoticePoll(void)
{
  if (led_notice_active == 0U)
  {
    return 0U;
  }

  if ((HAL_GetTick() - led_notice_tick) < APP_SLEEP_LED_FLASH_MS)
  {
    return 1U;
  }

  led_notice_tick = HAL_GetTick();
  if (led_notice_phase == 0U)
  {
    led_notice_phase = 1U;
    AppSetAllStatusLeds(GPIO_PIN_RESET);
    return 1U;
  }

  led_notice_active = 0U;
  led_notice_phase = 0U;
  return 0U;
}

static void AppAdvanceHalTick(uint32_t elapsed_ms)
{
  while (elapsed_ms > 0UL)
  {
    HAL_IncTick();
    elapsed_ms--;
  }
}

void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *hrtc)
{
  (void)hrtc;
  stop2_rtc_wakeup = 1U;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == KEY0_Pin)
  {
    key0_active_until_tick = HAL_GetTick() + APP_KEY0_ACTIVE_HOLD_MS;
    led_blink_tick = 0U;
    if (idle_sleep_state != 0U)
    {
      idle_sleep_state = 0U;
      AppLedNoticeStart();
    }
  }
}

static void AppEnterStop2Slice(uint32_t sleep_seconds)
{
  HAL_StatusTypeDef rtc_status;

  if (sleep_seconds == 0UL)
  {
    return;
  }

  AppSetAllStatusLeds(GPIO_PIN_RESET);

  (void)HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
  stop2_rtc_wakeup = 0U;
  rtc_status = HAL_RTCEx_SetWakeUpTimer_IT(&hrtc,
                                           sleep_seconds,
                                           RTC_WAKEUPCLOCK_CK_SPRE_16BITS);
  if (rtc_status != HAL_OK)
  {
    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    return;
  }

  HAL_SuspendTick();
  HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);

  SystemClock_Config();
  HAL_ResumeTick();
  if (stop2_rtc_wakeup != 0U)
  {
    AppAdvanceHalTick(sleep_seconds * 1000UL);
  }
  HAL_IWDG_Refresh(&hiwdg);
  (void)HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
}

static void AppKey0_PollModeToggle(void)
{
  static uint8_t press_active = 0U;
  static uint8_t toggled_this_press = 0U;
  static uint32_t press_start_tick = 0U;

  if (HAL_GPIO_ReadPin(KEY0_GPIO_Port, KEY0_Pin) == GPIO_PIN_RESET)
  {
    if (press_active == 0U)
    {
      press_active = 1U;
      toggled_this_press = 0U;
      press_start_tick = HAL_GetTick();
    }
    else if ((toggled_this_press == 0U) &&
             ((HAL_GetTick() - press_start_tick) >= APP_KEY0_HOLD_TIME_MS))
    {
      key0_active_until_tick = HAL_GetTick() + APP_KEY0_ACTIVE_HOLD_MS;
      toggled_this_press = 1U;
      led_blink_tick = 0U;
      AppLedNoticeStart();
    }
  }
  else
  {
    press_active = 0U;
    toggled_this_press = 0U;
  }
}

static void AppMaybeSleepIdle(void)
{
  uint32_t sleep_seconds;

  if (AppKey0ActiveWindow(HAL_GetTick()) != 0U)
  {
    if (idle_sleep_state != 0U)
    {
      idle_sleep_state = 0U;
      AppLedNoticeStart();
    }
    return;
  }

  if (AppSerialRecentlyActive(HAL_GetTick()) != 0U)
  {
    if (idle_sleep_state != 0U)
    {
      idle_sleep_state = 0U;
      AppLedNoticeStart();
    }
    return;
  }

  if (AppModbus_FileXferStartPending() != 0U)
  {
    return;
  }

  if (AppSamplingDueSoon(HAL_GetTick()) != 0U)
  {
    return;
  }

  if (AppLedNoticePoll() != 0U)
  {
    return;
  }

  if (idle_sleep_state == 0U)
  {
    idle_sleep_state = 1U;
    AppLedNoticeStart();
    return;
  }

  sleep_seconds = AppNextStop2SleepSeconds(HAL_GetTick());
  if (sleep_seconds == 0UL)
  {
    return;
  }

  AppEnterStop2Slice(sleep_seconds);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_IWDG_Init();
  MX_SDMMC1_SD_Init();
  MX_ADC1_Init();
  MX_I2C2_Init();
  MX_SPI1_Init();
  MX_TIM6_Init();
  MX_TIM7_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  MX_FATFS_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */
  AppUart1_EnableStopWakeup();
  {
    uint8_t power_latched;

  /* Latch main power rail: PWR_ON is active-high, must be set ASAP so the
     +2.8V_ADC rail (and other downstream rails) stay on after the boot key
     is released. AD7124 needs this to be powered. */
  power_latched = AppPower_CheckOnOffLowAndLatch();
  (void)power_latched;
  AD7124_BoardPowerOn(500U);

  /* printf("OPEN is OK!!!\r\n"); */
  /* if (power_latched != 0U)
  {
    printf("PWR_ON latched HIGH after ON_OFF LOW hold\r\n");
  }
  else
  {
    printf("PWR_ON not latched, continue without battery power lock\r\n");
  } */
  /* printf("ADC_PWR_EN set HIGH\r\n"); */
  }
  {
    HAL_StatusTypeDef config_status;
    uint32_t enabled_channel_mask;

    /* printf("\r\n=== EEPROM Config Test ===\r\n"); */
    AppConfigStore_Init(&app_config_store, &hi2c2);
    config_status = Eeprom_IsReady(&app_config_store.eeprom);
    if (config_status != HAL_OK)
    {
      /* printf("[EEPROM] not ready, HAL status = %d\r\n", (int)config_status); */
      AppConfig_LoadDefaults(&app_config);
      /* printf("[CONFIG] using RAM defaults only\r\n"); */
    }
    else
    {
      /* printf("[EEPROM] device ready\r\n"); */
      config_status = AppConfigStore_Load(&app_config_store, &app_config);
      if (config_status != HAL_OK)
      {
        /* printf("[CONFIG] load error, HAL status = %d\r\n", (int)config_status); */
        AppConfig_LoadDefaults(&app_config);
      }
      else if (app_config_store.load_source == APP_CONFIG_STORE_LOAD_DEFAULT)
      {
        /* printf("[CONFIG] no valid EEPROM config, writing defaults...\r\n"); */
        HAL_IWDG_Refresh(&hiwdg);
        config_status = AppConfigStore_SaveDefaults(&app_config_store, &app_config);
        (void)config_status;
        /* if (config_status == HAL_OK)
        {
          printf("[CONFIG] defaults saved to EEPROM\r\n");
        }
        else
        {
          printf("[CONFIG] save defaults fail, HAL status = %d\r\n", (int)config_status);
        } */
      }
      else
      {
        /* printf("[CONFIG] EEPROM config loaded\r\n"); */
      }
    }

    enabled_channel_mask = AppConfig_GetEnabledChannelMask(&app_config);
    printf("[CONFIG] Enabled channel mask: 0x%04lX\r\n", (unsigned long)enabled_channel_mask);
    for (uint8_t ch = 0U; ch < 16U; ch++)
    {
      if (app_config.channels[ch].enable != 0U)
      {
        printf("[CONFIG] CH%02u: enable=%u mode=%u pos=%u neg=%u gain=%u\r\n",
               (unsigned)ch,
               (unsigned)app_config.channels[ch].enable,
               (unsigned)app_config.channels[ch].mode,
               (unsigned)app_config.channels[ch].positive_input,
               (unsigned)app_config.channels[ch].negative_input,
               (unsigned)app_config.channels[ch].gain);
      }
    }
    /* printf("[CONFIG] source=%u slot=%u seq=%lu crc=0x%08lX\r\n", ... ); */
    /* printf("[CONFIG] device_id=%lu modbus=%u baud=%lu run=%u\r\n", ... ); */
    /* printf("[CONFIG] sample=%lus record=%lus avg=%u channel_mask=0x%04lX\r\n", ... ); */
    /* printf("=== EEPROM Config Test done ===\r\n"); */
    HAL_IWDG_Refresh(&hiwdg);
  }
  AppRtc_Init();
  {
    char rtc_timestamp[APP_RTC_TIMESTAMP_SIZE];

    if (AppRtc_FormatTimestamp(rtc_timestamp, sizeof(rtc_timestamp)) != 0U)
    {
      /* printf("[RTC] timestamp: %s\r\n", rtc_timestamp); */
      (void)rtc_timestamp;
    }
  }
  if (storage_boot_test_enable != 0U)
  {
    FRESULT sd_result;
    uint8_t sd_try;

    printf("\r\n=== SD Card Test ===\r\n");
    HAL_IWDG_Refresh(&hiwdg);
    printf("[SD] SD_PWR_EN before power on: %u\r\n", (unsigned)Storage_IsPowerEnabled());
    Storage_PowerOn(1000U);
    printf("[SD] SD_PWR_EN after power on: %u\r\n", (unsigned)Storage_IsPowerEnabled());
    PrintSdDebug("before mount");
    sd_result = FR_NOT_READY;
    for (sd_try = 0U; sd_try < 3U; sd_try++)
    {
      HAL_IWDG_Refresh(&hiwdg);
      ResetSdmmcForRetry();
      PrintSdDebug("before mount try");
      sd_result = Storage_Mount();
      if (sd_result == FR_OK)
      {
        break;
      }
      printf("[SD] mount try %u fail: %s (%d)\r\n",
             (unsigned)(sd_try + 1U),
             Storage_FresultText(sd_result),
             (int)sd_result);
      PrintSdDebug("mount fail");
      HAL_Delay(500U);
    }
    if (sd_result == FR_OK)
    {
      printf("[SD] mount ok\r\n");
      sd_result = Storage_WriteTestCsv();
      if (sd_result == FR_OK)
      {
        printf("[SD] write %s ok\r\n", STORAGE_TEST_FILE_NAME);
      }
      else
      {
        printf("[SD] write %s fail: %s (%d)\r\n",
               STORAGE_TEST_FILE_NAME,
               Storage_FresultText(sd_result),
               (int)sd_result);
      }
    }
    else
    {
      printf("[SD] mount fail: %s (%d)\r\n",
             Storage_FresultText(sd_result),
             (int)sd_result);
    }
    HAL_IWDG_Refresh(&hiwdg);
  }
  {
    uint8_t ad7124_id = 0U;
    uint8_t ad7124_status = 0U;
    uint16_t ad7124_control = 0U;
    uint32_t ad7124_error = 0U;
    HAL_StatusTypeDef ad_status;
    HAL_StatusTypeDef cfg_status;

    (void)ad7124_status;
    (void)ad7124_control;
    (void)ad7124_error;
    /* printf("\r\n\r\n=== AD7124-8 Communication Test ===\r\n"); */
    ad_status = AD7124_Init(&had7124, &hspi1);
    if (ad_status == HAL_OK)
    {
      ad_status = AD7124_ReadID(&had7124, &ad7124_id);
      if ((ad_status == HAL_OK) && (AD7124_IsDeviceID(ad7124_id) != 0U))
      {
        cfg_status = AD7124_ApplyConfig(&had7124, &app_config);
        if (cfg_status == HAL_OK)
        {
          ad7124_ready = 1U;
          printf("[AD7124] config applied from EEPROM (per-channel mode/gain/filter)\r\n");
        }
        else
        {
          printf("[AD7124] config error, HAL=%d\r\n", (int)cfg_status);
        }
      }
      else
      {
        printf("[AD7124] ID error, HAL=%d ID=0x%02X\r\n", (int)ad_status, (unsigned)ad7124_id);
      }
    }
    else
    {
      printf("[AD7124] SPI init error, HAL=%d\r\n", (int)ad_status);
    }
  }

  /* Update subsystem status flags */
  eeprom_ok = (Eeprom_IsReady(&app_config_store.eeprom) == HAL_OK) ? 1U : 0U;
  rtc_ok = 1U; /* RTC initialized successfully if we got here */

  /* Ensure SD is powered + mounted for the recorder. If the boot self-test
     already ran, power is on; otherwise turn it on now. */
  if (Storage_IsPowerEnabled() == 0U)
  {
    Storage_PowerOn(500U);
  }
  sd_ok = (Storage_Mount() == FR_OK) ? 1U : 0U;

  /* Bring up the periodic recorder (accumulators + day-rotated SD writer) */
  Recorder_Init(&app_config);
  ControlOutputs_Init(&app_config);
  Battery_Init();
  (void)Battery_Update();   /* seed an initial reading for the host */
  battery_tick = HAL_GetTick();
  sample_tick = HAL_GetTick();
  prev_run_enable = app_config.run_enable;

  /* Initialize Modbus RTU */
  if (ad7124_ready != 0U && modbus_enabled != 0U)
  {
    ModbusCallbacks modbus_cb;
    modbus_cb.read_holding_regs = AppModbus_ReadHoldingRegisters;
    modbus_cb.read_input_regs = AppModbus_ReadInputRegisters;
    modbus_cb.write_single_reg = AppModbus_WriteSingleRegister;
    modbus_cb.write_multiple_regs = AppModbus_WriteMultipleRegisters;

    AppModbus_Init(&app_config, &app_config_store);
    AppModbus_SetReconfigureCallback(AppModbus_ReconfigureAd7124);
    ModbusRtu_Init(&huart1, app_config.modbus_addr, &modbus_cb);
    printf("[MODBUS] initialized on USART1, addr=%u, baud=%lu\r\n",
           (unsigned)app_config.modbus_addr,
           (unsigned long)app_config.uart_baudrate);
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    HAL_IWDG_Refresh(&hiwdg);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* Long-press shutdown: hold ON_OFF low for 3s to cut power */
    AppPower_PollShutdown();
    AppKey0_PollModeToggle();
    ControlOutputs_Poll();

    uint8_t notice_running = AppLedNoticePoll();

    /* Heartbeat: only blink while the host is recently active. In idle sleep
       the LED stays off, avoiding short duty-cycle ghost flashes. */
    if (notice_running != 0U)
    {
      /* Let the three-LED notice finish without heartbeat overriding it. */
    }
    else if ((AppKey0ActiveWindow(HAL_GetTick()) != 0U) ||
             (AppSerialRecentlyActive(HAL_GetTick()) != 0U))
    {
      if ((HAL_GetTick() - led_blink_tick) >= 250U)
      {
        led_blink_tick = HAL_GetTick();
        HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
      }
    }
    else
    {
      HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
    }

    /* Edge-detect run_enable 0->1: reset cadence so a freshly-started run
       waits a full sample_interval before its first scan, and recorder state
       starts clean. */
    if ((app_config.run_enable != 0U) && (prev_run_enable == 0U))
    {
      sd_ok = Recorder_Flush();
      sample_tick = HAL_GetTick();
      Recorder_ResetTiming();
    }
    else if ((app_config.run_enable == 0U) && (prev_run_enable != 0U))
    {
      sd_ok = Recorder_Flush();
    }
    prev_run_enable = app_config.run_enable;

    /* Periodic AD7124 scan + recorder feed. AD7124 stays powered when paused;
       only the schedule + Modbus realtime updates + SD writes are gated. */
    if ((app_config.run_enable != 0U) && (ad7124_ready != 0U))
    {
      uint32_t interval_ms = app_config.sample_interval_sec * 1000UL;
      if (interval_ms == 0UL)
      {
        interval_ms = 1000UL;
      }

      if ((HAL_GetTick() - sample_tick) >= interval_ms)
      {
        uint8_t round_idx;
        uint8_t enabled_count = 0U;
        uint8_t samples_read = 0U;

        sample_tick = HAL_GetTick();

        for (round_idx = 0U; round_idx < APP_CONFIG_CHANNEL_COUNT; round_idx++)
        {
          if (app_config.channels[round_idx].enable != 0U)
          {
            enabled_count++;
          }
        }

        while (samples_read < enabled_count && samples_read < 32U)
        {
          uint32_t raw_data = 0U;
          int32_t signed_data = 0;
          int32_t input_uv = 0;
          uint8_t sample_status = 0U;
          uint8_t sample_channel = 0U;
          HAL_StatusTypeDef sample_st;

          if (modbus_enabled != 0U)
          {
            ModbusRtu_Poll();
          }

          /* Per-channel timeout: AD7124 FILTER 当前固定 Sinc4 + FS=64
             (~300 SPS)，单通道 settling ~13ms，100ms 留 ~7× 余量。 */
          sample_st = AD7124_ReadSample(&had7124, &raw_data, &signed_data, &sample_status, 100U);
          if (sample_st != HAL_OK)
          {
            break;
          }

          sample_channel = AD7124_StatusToChannel(sample_status);

          uint8_t ch_gain;
          if (sample_channel < APP_CONFIG_CHANNEL_COUNT &&
              app_config.channels[sample_channel].gain != 0U)
          {
            ch_gain = app_config.channels[sample_channel].gain;
          }
          else
          {
            ch_gain = AD7124_DEFAULT_GAIN;
          }

          input_uv = AD7124_BipolarCodeToMicrovolts(raw_data,
                                                    (int32_t)app_config.adc_vref_mv,
                                                    ch_gain);
          (void)signed_data; /* still computed by ReadSample, no longer printed */

          if (modbus_enabled != 0U)
          {
            AppModbus_UpdateChannelData(sample_channel, raw_data, input_uv);
          }
          Recorder_OnChannelSample(sample_channel, input_uv);

          samples_read++;
          HAL_IWDG_Refresh(&hiwdg);
        }

        Recorder_OnScanComplete();
        sd_ok = Recorder_GetSdOk();

        if (modbus_enabled != 0U)
        {
          AppModbus_UpdateSampleTimestamp();
          AppModbus_UpdateSystemStatus(ad7124_ready, eeprom_ok, rtc_ok, sd_ok, app_config.run_enable);
        }
      }
    }
    else if (modbus_enabled != 0U)
    {
      /* Paused: still keep RUNNING bit in Modbus status truthful so the UI
         reflects the stop. */
      AppModbus_UpdateSystemStatus(ad7124_ready, eeprom_ok, rtc_ok, sd_ok, app_config.run_enable);
    }

    /* Keep the legacy debug printf loop available behind ad7124_loop_print_enable
       for bench debugging, but disabled by default to keep USART2 quiet during
       normal operation. */
    (void)ad7124_loop_print_enable;
    (void)ad7124_print_tick;

    /* Modbus RTU polling */
    if (modbus_enabled != 0U)
    {
      ModbusRtu_Poll();
    }

    /* Battery voltage: low-rate background read. Uses ADC1, which is
       otherwise idle, so it does not interfere with AD7124 sampling. */
    if ((HAL_GetTick() - battery_tick) >= BATTERY_UPDATE_INTERVAL_MS)
    {
      (void)Battery_Update();
      battery_tick = HAL_GetTick();
    }

    /* File transfer kick: app_modbus set the pending flag when it answered a
       FC06 START. The FC06 ACK has been sent by ModbusRtu_Poll above, so we
       can now safely repurpose USART1 for YMODEM. */
    if (AppModbus_FileXferStartPending() != 0U)
    {
      const FileBrowserEntry *entry = FileBrowser_GetSelected();
      AppModbus_FileXferClearPending();

      if (entry == NULL)
      {
        AppModbus_FileXferSetState(5U, 0x0004U);
      }
      else
      {
        char file_path[FILE_BROWSER_NAME_SIZE + 8U];
        YModemResult yr;

        sd_ok = Recorder_Flush();
        FileBrowser_BuildSelectedPath(file_path, sizeof(file_path));

        /* ModbusRtu_PauseRx was already called inside WriteSingleRegister
           when the FC06 START arrived, so RXNE has been masked since before
           the ACK was sent. We only need to resume after YMODEM finishes. */
        yr = YModem_SendFile(&huart1, file_path, entry->name);
        ModbusRtu_ResumeRx();

        if (yr == YMODEM_OK)
        {
          AppModbus_FileXferSetState(4U, 0U);
        }
        else
        {
          uint16_t err = (yr == YMODEM_FILE_OPEN_ERROR || yr == YMODEM_FILE_READ_ERROR)
                             ? 0x0004U : 0x00FFU;
          AppModbus_FileXferSetState(5U, err);
        }
      }
    }

    AppMaybeSleepIdle();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI
                              |RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 20;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_SDMMC1|RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCCLKSOURCE_PLLSAI1;
  PeriphClkInit.Sdmmc1ClockSelection = RCC_SDMMC1CLKSOURCE_PLLSAI1;
  PeriphClkInit.PLLSAI1.PLLSAI1Source = RCC_PLLSOURCE_HSE;
  PeriphClkInit.PLLSAI1.PLLSAI1M = 1;
  PeriphClkInit.PLLSAI1.PLLSAI1N = 8;
  PeriphClkInit.PLLSAI1.PLLSAI1P = RCC_PLLP_DIV7;
  PeriphClkInit.PLLSAI1.PLLSAI1Q = RCC_PLLQ_DIV2;
  PeriphClkInit.PLLSAI1.PLLSAI1R = RCC_PLLR_DIV2;
  PeriphClkInit.PLLSAI1.PLLSAI1ClockOut = RCC_PLLSAI1_48M2CLK|RCC_PLLSAI1_ADC1CLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* ---------------------------------------------------------------------------
 * printf retarget to USART1 (PA9 TX / PA10 RX, 115200 8N1).
 * If "Use MicroLIB" is OFF in Keil, the __use_no_semihosting stubs below keep
 * the linker happy.
 * --------------------------------------------------------------------------- */
#if defined(__ARMCC_VERSION) && !defined(__MICROLIB)
#pragma import(__use_no_semihosting)
struct __FILE { int handle; };
FILE __stdout;
void _sys_exit(int x)   { (void)x; while (1) { } }
void _ttywrch(int ch)   { (void)ch; }
#endif

int fputc(int ch, FILE *f)
{
  (void)f;
  HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1U, 100U);
  return ch;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
