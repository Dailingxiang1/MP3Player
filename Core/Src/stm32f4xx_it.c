/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
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
#include "stm32f4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
volatile uint32_t g_fault_id;
volatile uint32_t g_fault_cfsr;
volatile uint32_t g_fault_hfsr;
volatile uint32_t g_fault_dfsr;
volatile uint32_t g_fault_afsr;
volatile uint32_t g_fault_bfar;
volatile uint32_t g_fault_mmfar;
volatile uint32_t g_fault_app_phase;
volatile uint32_t g_fault_last_cmd;
volatile uint32_t g_fault_sp;
volatile uint32_t g_fault_r0;
volatile uint32_t g_fault_r1;
volatile uint32_t g_fault_r2;
volatile uint32_t g_fault_r3;
volatile uint32_t g_fault_r12;
volatile uint32_t g_fault_lr;
volatile uint32_t g_fault_pc;
volatile uint32_t g_fault_psr;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void Fault_Delay(uint32_t count)
{
    volatile uint32_t delay;

    for (delay = 0; delay < count; delay++)
    {
      __NOP();
    }
}

void Fault_HandlerC(uint32_t *fault_stack, uint32_t fault_id)
{
  g_fault_id = fault_id;
  g_fault_cfsr = SCB->CFSR;
  g_fault_hfsr = SCB->HFSR;
  g_fault_dfsr = SCB->DFSR;
  g_fault_afsr = SCB->AFSR;
  g_fault_bfar = SCB->BFAR;
  g_fault_mmfar = SCB->MMFAR;
  //g_fault_app_phase = g_app_phase;
  //g_fault_last_cmd = g_app_last_cmd;

  if (fault_stack != 0) {
    g_fault_sp = (uint32_t)fault_stack;
    g_fault_r0 = fault_stack[0];
    g_fault_r1 = fault_stack[1];
    g_fault_r2 = fault_stack[2];
    g_fault_r3 = fault_stack[3];
    g_fault_r12 = fault_stack[4];
    g_fault_lr = fault_stack[5];
    g_fault_pc = fault_stack[6];
    g_fault_psr = fault_stack[7];
  }

  __disable_irq();
  while (1)
  {
    uint32_t i;
    for (i = 0; i < fault_id; i++)
    {
      HAL_GPIO_WritePin(led_out_GPIO_Port, led_out_Pin, GPIO_PIN_SET);
      Fault_Delay(200000U);
      HAL_GPIO_WritePin(led_out_GPIO_Port, led_out_Pin, GPIO_PIN_RESET);
      Fault_Delay(200000U);
    }
    Fault_Delay(1200000U);
  }
}

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_HandleTypeDef hdma_spi2_tx;
extern I2S_HandleTypeDef hi2s2;
extern DMA_HandleTypeDef hdma_sdio_rx;
extern DMA_HandleTypeDef hdma_sdio_tx;
extern SD_HandleTypeDef hsd;
extern TIM_HandleTypeDef htim14;

/* USER CODE BEGIN EV */
void hard_fault_handler_c(unsigned int * svc_args)
{
    unsigned int stacked_r0;
    unsigned int stacked_r1;
    unsigned int stacked_r2;
    unsigned int stacked_r3;
    unsigned int stacked_r12;
    unsigned int stacked_lr;
    unsigned int stacked_pc;
    unsigned int stacked_psr;

    stacked_r0 = ((unsigned long) svc_args[0]);
    stacked_r1 = ((unsigned long) svc_args[1]);
    stacked_r2 = ((unsigned long) svc_args[2]);
    stacked_r3 = ((unsigned long) svc_args[3]);

    stacked_r12 = ((unsigned long) svc_args[4]);
    stacked_lr = ((unsigned long) svc_args[5]);
    stacked_pc = ((unsigned long) svc_args[6]);
    stacked_psr = ((unsigned long) svc_args[7]);

    printf("\r\n--- Hard Fault Detected ---\r\n");
    printf("R0       = 0x%08x\r\n", stacked_r0);
    printf("R1       = 0x%08x\r\n", stacked_r1);
    printf("R2       = 0x%08x\r\n", stacked_r2);
    printf("R3       = 0x%08x\r\n", stacked_r3);
    printf("R12      = 0x%08x\r\n", stacked_r12);
    printf("LR [R14] = 0x%08x (Return Address)\r\n", stacked_lr);
    printf("PC [R15] = 0x%08x (Error Code Location)\r\n", stacked_pc);
    printf("PSR      = 0x%08x\r\n", stacked_psr);
    
    // 如果是由于非法访问内存，查看这个寄存器（针对 Cortex-M3/M4/M7）
    // printf("BFAR     = 0x%08x\r\n", (*((volatile unsigned long *)(0xE000ED38))));
    // printf("CFSR     = 0x%08x\r\n", (*((volatile unsigned long *)(0xE000ED28))));

    while (1);
}
/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32F4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f4xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles EXTI line0 interrupt.
  */
void EXTI0_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI0_IRQn 0 */

  /* USER CODE END EXTI0_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
  /* USER CODE BEGIN EXTI0_IRQn 1 */

  /* USER CODE END EXTI0_IRQn 1 */
}

/**
  * @brief This function handles EXTI line1 interrupt.
  */
void EXTI1_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI1_IRQn 0 */

  /* USER CODE END EXTI1_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1);
  /* USER CODE BEGIN EXTI1_IRQn 1 */

  /* USER CODE END EXTI1_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream4 global interrupt.
  */
void DMA1_Stream4_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream4_IRQn 0 */

  /* USER CODE END DMA1_Stream4_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_spi2_tx);
  /* USER CODE BEGIN DMA1_Stream4_IRQn 1 */

  /* USER CODE END DMA1_Stream4_IRQn 1 */
}

/**
  * @brief This function handles SPI2 global interrupt.
  */
void SPI2_IRQHandler(void)
{
  /* USER CODE BEGIN SPI2_IRQn 0 */

  /* USER CODE END SPI2_IRQn 0 */
  HAL_I2S_IRQHandler(&hi2s2);
  /* USER CODE BEGIN SPI2_IRQn 1 */

  /* USER CODE END SPI2_IRQn 1 */
}

/**
  * @brief This function handles TIM8 trigger and commutation interrupts and TIM14 global interrupt.
  */
void TIM8_TRG_COM_TIM14_IRQHandler(void)
{
  /* USER CODE BEGIN TIM8_TRG_COM_TIM14_IRQn 0 */

  /* USER CODE END TIM8_TRG_COM_TIM14_IRQn 0 */
  HAL_TIM_IRQHandler(&htim14);
  /* USER CODE BEGIN TIM8_TRG_COM_TIM14_IRQn 1 */

  /* USER CODE END TIM8_TRG_COM_TIM14_IRQn 1 */
}

/**
  * @brief This function handles SDIO global interrupt.
  */
void SDIO_IRQHandler(void)
{
  /* USER CODE BEGIN SDIO_IRQn 0 */

  /* USER CODE END SDIO_IRQn 0 */
  HAL_SD_IRQHandler(&hsd);
  /* USER CODE BEGIN SDIO_IRQn 1 */

  /* USER CODE END SDIO_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream3 global interrupt.
  */
void DMA2_Stream3_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream3_IRQn 0 */

  /* USER CODE END DMA2_Stream3_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_sdio_rx);
  /* USER CODE BEGIN DMA2_Stream3_IRQn 1 */

  /* USER CODE END DMA2_Stream3_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream6 global interrupt.
  */
void DMA2_Stream6_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream6_IRQn 0 */

  /* USER CODE END DMA2_Stream6_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_sdio_tx);
  /* USER CODE BEGIN DMA2_Stream6_IRQn 1 */

  /* USER CODE END DMA2_Stream6_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
