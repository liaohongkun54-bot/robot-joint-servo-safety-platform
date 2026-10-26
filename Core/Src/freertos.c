/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "semphr.h"
#include "app_main_task_service.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
static StaticTask_t g_serial_task_control_block;
static uint32_t g_serial_task_stack[512U];
static StaticSemaphore_t g_uart_mutex_control_block;
static osMutexId_t g_uart_mutex;
static osThreadId_t g_serial_task_handle;
volatile uint32_t g_app_control_cycle_count;
volatile uint32_t g_app_serial_service_count;
volatile uint32_t g_app_uart_lock_miss_count;
volatile uint32_t g_app_serial_loop_count;
volatile uint32_t g_app_control_max_gap_ms;
volatile uint32_t g_app_serial_max_gap_ms;
volatile uint32_t g_app_main_stack_min_free_bytes;
volatile uint32_t g_app_serial_stack_min_free_bytes;

/* USER CODE END Variables */
/* Definitions for AppMainTask */
osThreadId_t AppMainTaskHandle;
uint32_t AppMainTaskBuffer[ 512 ];
osStaticThreadDef_t AppMainTaskControlBlock;
const osThreadAttr_t AppMainTask_attributes = {
  .name = "AppMainTask",
  .cb_mem = &AppMainTaskControlBlock,
  .cb_size = sizeof(AppMainTaskControlBlock),
  .stack_mem = &AppMainTaskBuffer[0],
  .stack_size = sizeof(AppMainTaskBuffer),
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void StartSerialTask(void *argument);

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  const osMutexAttr_t uart_mutex_attributes = {
    .name = "UartAdapterMutex",
    .attr_bits = osMutexPrioInherit,
    .cb_mem = &g_uart_mutex_control_block,
    .cb_size = sizeof(g_uart_mutex_control_block),
  };
  g_uart_mutex = osMutexNew(&uart_mutex_attributes);
  if (g_uart_mutex == NULL)
  {
    Error_Handler();
  }
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of AppMainTask */
  AppMainTaskHandle = osThreadNew(StartDefaultTask, NULL, &AppMainTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  {
    const osThreadAttr_t serial_task_attributes = {
      .name = "SerialTask",
      .cb_mem = &g_serial_task_control_block,
      .cb_size = sizeof(g_serial_task_control_block),
      .stack_mem = g_serial_task_stack,
      .stack_size = sizeof(g_serial_task_stack),
      .priority = osPriorityBelowNormal,
    };
    g_serial_task_handle = osThreadNew(
        StartSerialTask, NULL, &serial_task_attributes);
    if ((AppMainTaskHandle == NULL) || (g_serial_task_handle == NULL))
    {
      Error_Handler();
    }
  }
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the AppMainTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  (void)argument;
  /* USER CODE BEGIN StartDefaultTask */
  uint32_t previous_tick_ms = 0U;
  uint8_t has_previous_tick = 0U;
  /* Infinite loop */
  for(;;)
  {
    uint32_t now_ms = HAL_GetTick();
    if (has_previous_tick != 0U)
    {
      uint32_t gap_ms = now_ms - previous_tick_ms;
      if (gap_ms > g_app_control_max_gap_ms)
      {
        g_app_control_max_gap_ms = gap_ms;
      }
    }
    previous_tick_ms = now_ms;
    has_previous_tick = 1U;
    AppMainTaskService_RunControlOnce(now_ms);
    g_app_control_cycle_count++;
    if (osMutexAcquire(g_uart_mutex, 0U) == osOK)
    {
      AppMainTaskService_RunIngressOnce(now_ms);
      (void)osMutexRelease(g_uart_mutex);
    }
    else
    {
      g_app_uart_lock_miss_count++;
    }
    if ((g_app_control_cycle_count & 0xFFU) == 0U)
    {
      g_app_main_stack_min_free_bytes =
          osThreadGetStackSpace(AppMainTaskHandle);
    }
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
static void StartSerialTask(void *argument)
{
  uint32_t previous_tick_ms = 0U;
  uint8_t has_previous_tick = 0U;
  (void)argument;
  for (;;)
  {
    uint32_t now_ms = HAL_GetTick();
    if (has_previous_tick != 0U)
    {
      uint32_t gap_ms = now_ms - previous_tick_ms;
      if (gap_ms > g_app_serial_max_gap_ms)
      {
        g_app_serial_max_gap_ms = gap_ms;
      }
    }
    previous_tick_ms = now_ms;
    has_previous_tick = 1U;
    g_app_serial_loop_count++;
    if (osMutexAcquire(g_uart_mutex, 0U) == osOK)
    {
      AppMainTaskService_RunUartOnce(now_ms);
      g_app_serial_service_count++;
      (void)osMutexRelease(g_uart_mutex);
    }
    if ((g_app_serial_loop_count & 0xFFU) == 0U)
    {
      g_app_serial_stack_min_free_bytes =
          osThreadGetStackSpace(g_serial_task_handle);
    }
    osDelay(1);
  }
}

/* USER CODE END Application */

