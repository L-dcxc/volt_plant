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
#include "sdmmc.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "ad7124.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static AD7124_HandleTypeDef had7124;
static uint8_t ad7124_ready = 0U;
static uint32_t ad7124_print_tick = 0U;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  /* USER CODE BEGIN 2 */
  /* Latch main power rail: PWR_ON is active-high, must be set ASAP so the
     +2.8V_ADC rail (and other downstream rails) stay on after the boot key
     is released. AD7124 needs this to be powered. */
  HAL_GPIO_WritePin(PWR_ON_GPIO_Port, PWR_ON_Pin, GPIO_PIN_SET);
  AD7124_BoardPowerOn(500U);

  printf("OPEN is OK!!!\r\n");
  printf("PWR_ON latched HIGH\r\n");
  printf("ADC_PWR_EN set HIGH\r\n");
  {
    uint8_t ad7124_id = 0U;
    uint8_t ad7124_status = 0U;
    uint16_t ad7124_control = 0U;
    uint32_t ad7124_error = 0U;
    HAL_StatusTypeDef ad_status;
    HAL_StatusTypeDef cfg_status;

    printf("\r\n\r\n=== AD7124-8 Communication Test ===\r\n");
    printf("SPI1 ready, resetting AD7124...\r\n");

    ad_status = AD7124_Init(&had7124, &hspi1);
    if (ad_status != HAL_OK)
    {
      printf("[FAIL] SPI transfer error, HAL status = %d\r\n", (int)ad_status);
    }
    else
    {
      ad_status = AD7124_ReadID(&had7124, &ad7124_id);
      if (ad_status != HAL_OK)
      {
        printf("[FAIL] ID read error, HAL status = %d\r\n", (int)ad_status);
      }
      else
      {
        printf("ID register read back: 0x%02X\r\n", (unsigned)ad7124_id);
        if (AD7124_IsDeviceID(ad7124_id) != 0U)
        {
          printf("[ OK ] AD7124-8 detected, silicon revision 0x%X\r\n",
                 (unsigned)(ad7124_id & 0x0FU));
          if (AD7124_ReadStatus(&had7124, &ad7124_status) == HAL_OK)
          {
            printf("STATUS register: 0x%02X\r\n", (unsigned)ad7124_status);
          }
          if (AD7124_ReadAdcControl(&had7124, &ad7124_control) == HAL_OK)
          {
            printf("ADC_CONTROL register: 0x%04X\r\n", (unsigned)ad7124_control);
          }
          if (AD7124_ReadError(&had7124, &ad7124_error) == HAL_OK)
          {
            printf("ERROR register: 0x%06lX\r\n", (unsigned long)ad7124_error);
          }
          cfg_status = AD7124_ConfigAin15SingleEnded(&had7124);
          if (cfg_status == HAL_OK)
          {
            ad7124_ready = 1U;
            printf("AD7124 channel15 acquisition started: AIN15-AVSS\r\n");
          }
          else
          {
            printf("[FAIL] AD7124 acquisition config error, HAL status = %d\r\n", (int)cfg_status);
          }
        }
        else
        {
          printf("[FAIL] Unexpected ID, upper nibble should be 0x1\r\n");
          printf("       Check: SPI wiring, CS to GND, +2.8V_ADC, SPI mode 3\r\n");
        }
      }
    }
    printf("=== Test done, entering main loop ===\r\n");
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
    HAL_IWDG_Refresh(&hiwdg);
    HAL_Delay(250);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if ((ad7124_ready != 0U) && ((HAL_GetTick() - ad7124_print_tick) >= 1000U))
    {
      uint32_t raw_data = 0U;
      int32_t signed_data = 0;
      int32_t input_uv = 0;
      uint8_t sample_status = 0U;
      uint8_t sample_channel = 0U;
      HAL_StatusTypeDef sample_st;

      ad7124_print_tick = HAL_GetTick();
      sample_st = AD7124_ReadSample(&had7124, &raw_data, &signed_data, &sample_status, 200U);
      if (sample_st == HAL_OK)
      {
        sample_channel = AD7124_StatusToChannel(sample_status);
        input_uv = AD7124_BipolarCodeToMicrovolts(raw_data, AD7124_DEFAULT_VREF_MV, AD7124_DEFAULT_GAIN);
        printf("[AD7124] CH=%u STATUS=0x%02X RAW=0x%06lX CODE=%ld VIN=%lduV\r\n",
               (unsigned)sample_channel,
               (unsigned)sample_status,
               (unsigned long)raw_data,
               (long)signed_data,
               (long)input_uv);
      }
      else
      {
        printf("[AD7124] sample read error, HAL status = %d\r\n", (int)sample_st);
      }
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
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
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1U, 100U);
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
