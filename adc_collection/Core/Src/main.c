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
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_POWER_ON_HOLD_TIME_MS 2000U
#define APP_POWER_ON_CHECK_PERIOD_MS 10U
#define APP_POWER_OFF_HOLD_TIME_MS 3000U

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
static uint8_t ad7124_loop_print_enable = 1U;
static uint8_t storage_boot_test_enable = 1U;
static uint32_t ad7124_print_tick = 0U;
static uint32_t led_blink_tick = 0U;
static uint8_t modbus_enabled = 1U;
static uint8_t eeprom_ok = 0U;
static uint8_t rtc_ok = 0U;
static uint8_t sd_ok = 0U;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
/* USER CODE BEGIN PFP */

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

static uint8_t AppPower_ReadOutputData(GPIO_TypeDef *gpio_port, uint16_t gpio_pin)
{
  return ((gpio_port->ODR & gpio_pin) != 0U) ? 1U : 0U;
}

static void AppPower_PrintDiag(const char *tag)
{
  printf("[PWR] %s: ON_OFF=%u KEY_PWR=%u PWR_ON_IDR=%u PWR_ON_ODR=%u\r\n",
         tag,
         (unsigned)((HAL_GPIO_ReadPin(ON_OFF_GPIO_Port, ON_OFF_Pin) == GPIO_PIN_SET) ? 1U : 0U),
         (unsigned)((HAL_GPIO_ReadPin(KEY_PWR_GPIO_Port, KEY_PWR_Pin) == GPIO_PIN_SET) ? 1U : 0U),
         (unsigned)((HAL_GPIO_ReadPin(PWR_ON_GPIO_Port, PWR_ON_Pin) == GPIO_PIN_SET) ? 1U : 0U),
         (unsigned)AppPower_ReadOutputData(PWR_ON_GPIO_Port, PWR_ON_Pin));
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
  {
    uint8_t power_latched;

  /* Latch main power rail: PWR_ON is active-high, must be set ASAP so the
     +2.8V_ADC rail (and other downstream rails) stay on after the boot key
     is released. AD7124 needs this to be powered. */
  /* AppPower_PrintDiag("before latch"); */
  power_latched = AppPower_CheckOnOffLowAndLatch();
  (void)power_latched;
  /* AppPower_PrintDiag("after latch"); */
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
  sd_ok = 0U;  /* SD not tested in normal boot */

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

    /* Heartbeat: blink green LED ~2 Hz */
    if ((HAL_GetTick() - led_blink_tick) >= 250U)
    {
      led_blink_tick = HAL_GetTick();
      HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
    }

    if ((ad7124_loop_print_enable != 0U) && (ad7124_ready != 0U) && ((HAL_GetTick() - ad7124_print_tick) >= 1000U))
    {
      uint8_t round_idx;
      uint8_t enabled_count = 0U;
      uint8_t samples_read = 0U;

      /* Count enabled channels */
      for (round_idx = 0U; round_idx < 16U; round_idx++)
      {
        if (app_config.channels[round_idx].enable != 0U)
        {
          enabled_count++;
        }
      }

      ad7124_print_tick = HAL_GetTick();
      printf("\r\n[AD7124] --- round @ %lu ms (enabled channels: %u) ---\r\n",
             (unsigned long)ad7124_print_tick, (unsigned)enabled_count);

      /* Read samples until we get all enabled channels or timeout.
         AD7124 in continuous mode cycles through enabled channels,
         so we need to read until we've seen all of them. */
      while (samples_read < enabled_count && samples_read < 32U) /* Safety limit */
      {
        uint32_t raw_data = 0U;
        int32_t signed_data = 0;
        int32_t input_uv = 0;
        uint8_t sample_status = 0U;
        uint8_t sample_channel = 0U;
        HAL_StatusTypeDef sample_st;

        /* Service Modbus before each channel read so a request that arrived
           during the previous sample isn't stalled until the whole 16-channel
           round finishes. Round latency: ~0..(per-channel timeout) instead of
           the old worst-case 8s. */
        if (modbus_enabled != 0U)
        {
          ModbusRtu_Poll();
        }

        /* Per-channel timeout: AD7124 当前 FILTER 配置为 Sinc4 + FS=64
           (约 300 SPS)，单通道 settling 约 13ms，100ms 留 ~7× 余量。
           若后续把 ODR 调慢，需要相应增大此超时。 */
        sample_st = AD7124_ReadSample(&had7124, &raw_data, &signed_data, &sample_status, 100U);
        if (sample_st == HAL_OK)
        {
          uint8_t ch_gain;

          sample_channel = AD7124_StatusToChannel(sample_status);

          /* Convert using this channel's actual gain (per-channel config).
             Fall back to default gain if the reported channel is out of range
             or the channel has an invalid (zero) gain. */
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
          printf("  CH%02u STATUS=0x%02X RAW=0x%06lX CODE=%ld GAIN=%u VIN=%ld uV\r\n",
                 (unsigned)sample_channel,
                 (unsigned)sample_status,
                 (unsigned long)raw_data,
                 (long)signed_data,
                 (unsigned)ch_gain,
                 (long)input_uv);

          /* Update Modbus real-time data */
          if (modbus_enabled != 0U)
          {
            AppModbus_UpdateChannelData(sample_channel, raw_data, input_uv);
          }

          samples_read++;
        }
        else
        {
          printf("  CH?? sample error, HAL=%d (read %u/%u samples)\r\n",
                 (int)sample_st, (unsigned)samples_read, (unsigned)enabled_count);
          break;
        }
        HAL_IWDG_Refresh(&hiwdg);
      }

      /* Update sample timestamp and system status */
      if (modbus_enabled != 0U)
      {
        AppModbus_UpdateSampleTimestamp();
        AppModbus_UpdateSystemStatus(ad7124_ready, eeprom_ok, rtc_ok, sd_ok, app_config.run_enable);
      }
    }

    /* Modbus RTU polling */
    if (modbus_enabled != 0U)
    {
      ModbusRtu_Poll();
    }
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE
                              |RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
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
