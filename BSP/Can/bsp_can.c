/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    bsp_can.c
  * @brief   Current project CAN board support package.
  *
  * 这一层负责把 CAN1 的“工程自定义动作”收拢起来：
  * 1. 先配置一个允许全部标准帧通过的过滤器；
  * 2. 启动 CAN 外设；
  * 3. 在内部回环模式下发送一帧并轮询接收；
  * 4. 把结果留成 Watch 里可直接观察的变量。
  *
  * 这样 `main.c` 就只保留启动入口和调用动作，不再堆 CAN 细节。
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
#include "bsp_can.h"
#include "can.h"

/* Private defines -----------------------------------------------------------*/
/**
  * @brief  CAN1 内部回环过滤器使用的滤波器组编号。
  *
  * 当前工程只用 CAN1，因此采用一个固定的低编号滤波器组即可。
  */
#define BSP_CAN1_FILTER_BANK             (0U)

/**
  * @brief  CAN1 内部回环过滤器分配到 FIFO0。
  *
  * 这样回环消息进入 FIFO0 后，轮询代码只需要盯住一个接收队列。
  */
#define BSP_CAN1_FILTER_FIFO             (CAN_FILTER_FIFO0)

/* Private variables ---------------------------------------------------------*/
volatile BSP_CAN_LoopbackStateTypeDef g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_IDLE;
volatile BSP_CAN_ErrorTypeDef g_bsp_can_last_error = BSP_CAN_ERROR_NONE;
volatile uint32_t g_bsp_can_test_step = 0U;
volatile uint32_t g_bsp_can_hal_state = 0U;
volatile uint32_t g_bsp_can_hal_error_code = 0U;
volatile uint32_t g_bsp_can_tx_mailbox = 0U;
volatile uint32_t g_bsp_can_tx_free_level = 0U;
volatile uint32_t g_bsp_can_rx_fifo0_level = 0U;
volatile uint32_t g_bsp_can_rx_std_id = 0U;
volatile uint32_t g_bsp_can_rx_ide = 0U;
volatile uint32_t g_bsp_can_rx_rtr = 0U;
volatile uint32_t g_bsp_can_rx_dlc = 0U;
uint8_t g_bsp_can_tx_data[BSP_CAN1_LOOPBACK_DATA_LENGTH] = {0U};
uint8_t g_bsp_can_rx_data[BSP_CAN1_LOOPBACK_DATA_LENGTH] = {0U};

/* 外部总线验证状态：这些变量用于 Watch 观察，不承载电机控制逻辑。 */
volatile uint32_t g_bsp_can_external_tx_status = HAL_ERROR;
volatile uint8_t g_bsp_can_external_tx_attempted = 0U;
volatile uint32_t g_bsp_can_external_tx_mailbox = 0U;
volatile uint32_t g_bsp_can_external_tx_error_code = HAL_CAN_ERROR_NONE;
volatile uint32_t g_bsp_can_external_rx_count = 0U;
volatile uint32_t g_bsp_can_external_rx_std_id = 0U;
volatile uint32_t g_bsp_can_external_rx_ide = 0U;
volatile uint32_t g_bsp_can_external_rx_rtr = 0U;
volatile uint32_t g_bsp_can_external_rx_dlc = 0U;
volatile uint32_t g_bsp_can_external_rx_error_code = HAL_CAN_ERROR_NONE;
uint8_t g_bsp_can_external_rx_data[BSP_CAN1_EXTERNAL_TEST_DATA_LENGTH] = {0U};

volatile uint32_t g_bsp_can_c620_tx_attempt_count = 0U;
volatile uint32_t g_bsp_can_c620_tx_success_count = 0U;
volatile uint32_t g_bsp_can_c620_tx_error_count = 0U;
volatile uint32_t g_bsp_can_c620_tx_mailbox = 0U;
volatile uint32_t g_bsp_can_c620_tx_hal_status = HAL_ERROR;
volatile uint32_t g_bsp_can_c620_tx_hal_error = HAL_CAN_ERROR_NONE;
uint8_t g_bsp_can_c620_tx_data[BSP_CAN1_C620_CONTROL_DATA_LENGTH] = {0U};

/* Private function prototypes -----------------------------------------------*/
static HAL_StatusTypeDef BSP_CAN1_ConfigAcceptAllFilter(void);
static void BSP_CAN1_LoadLoopbackPayload(void);
static void BSP_CAN1_SnapshotRuntimeState(void);

/**
  * @brief  为 CAN1 配置一个“全通过”过滤器，并启动外设。
  * @retval HAL_OK 表示过滤器和启动流程都成功，其他值表示失败。
  */
HAL_StatusTypeDef BSP_CAN1_Init(void)
{
  uint8_t index;

  g_bsp_can_external_tx_status = (uint32_t)HAL_ERROR;
  g_bsp_can_external_tx_attempted = 0U;
  g_bsp_can_external_tx_mailbox = 0U;
  g_bsp_can_external_tx_error_code = HAL_CAN_ERROR_NONE;
  g_bsp_can_external_rx_count = 0U;
  g_bsp_can_external_rx_std_id = 0U;
  g_bsp_can_external_rx_ide = 0U;
  g_bsp_can_external_rx_rtr = 0U;
  g_bsp_can_external_rx_dlc = 0U;
  g_bsp_can_external_rx_error_code = HAL_CAN_ERROR_NONE;
  g_bsp_can_c620_tx_attempt_count = 0U;
  g_bsp_can_c620_tx_success_count = 0U;
  g_bsp_can_c620_tx_error_count = 0U;
  g_bsp_can_c620_tx_mailbox = 0U;
  g_bsp_can_c620_tx_hal_status = HAL_ERROR;
  g_bsp_can_c620_tx_hal_error = HAL_CAN_ERROR_NONE;
  for (index = 0U; index < BSP_CAN1_C620_CONTROL_DATA_LENGTH; ++index)
  {
    g_bsp_can_c620_tx_data[index] = 0U;
  }

  g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_IDLE;
  g_bsp_can_last_error = BSP_CAN_ERROR_NONE;
  g_bsp_can_test_step = 1U;
  BSP_CAN1_SnapshotRuntimeState();

  if (BSP_CAN1_ConfigAcceptAllFilter() != HAL_OK)
  {
    g_bsp_can_last_error = BSP_CAN_ERROR_FILTER_CONFIG;
    g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_FAILED;
    BSP_CAN1_SnapshotRuntimeState();
    return HAL_ERROR;
  }

  g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_FILTER_READY;
  g_bsp_can_test_step = 2U;
  BSP_CAN1_SnapshotRuntimeState();

  if (HAL_CAN_Start(&hcan1) != HAL_OK)
  {
    g_bsp_can_last_error = BSP_CAN_ERROR_START;
    g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_FAILED;
    BSP_CAN1_SnapshotRuntimeState();
    return HAL_ERROR;
  }

  g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_CAN_STARTED;
  g_bsp_can_test_step = 3U;
  BSP_CAN1_SnapshotRuntimeState();

  return HAL_OK;
}

/**
  * @brief  在 CAN1 内部回环模式下发一帧并等回读。
  *
  * 这个函数是当前阶段最关键的板上证据入口：
  * - 发送是否进入邮箱；
  * - 接收 FIFO 是否收到回环帧；
  * - 接收帧头和数据是否与发送一致。
  *
  * @retval HAL_OK 表示回环通过，其他值表示失败。
  */
HAL_StatusTypeDef BSP_CAN1_PerformInternalLoopbackTest(void)
{
  CAN_TxHeaderTypeDef tx_header = {0};
  CAN_RxHeaderTypeDef rx_header = {0};
  uint32_t start_tick = 0U;
  uint8_t index = 0U;

  /*
   * 该函数只能在 CubeMX/HAL 已按内部回环模式初始化时运行。当前 P3-A
   * 固件使用 CAN_MODE_NORMAL；硬门禁可防止以后误调用时向真实总线发帧。
   */
  if (hcan1.Init.Mode != CAN_MODE_LOOPBACK)
  {
    g_bsp_can_last_error = BSP_CAN_ERROR_INVALID_MODE;
    g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_FAILED;
    BSP_CAN1_SnapshotRuntimeState();
    return HAL_ERROR;
  }

  if (g_bsp_can_loopback_state != BSP_CAN_LOOPBACK_STATE_CAN_STARTED)
  {
    g_bsp_can_last_error = BSP_CAN_ERROR_START;
    g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_FAILED;
    BSP_CAN1_SnapshotRuntimeState();
    return HAL_ERROR;
  }

  BSP_CAN1_LoadLoopbackPayload();

  tx_header.StdId = BSP_CAN1_LOOPBACK_STD_ID;
  tx_header.ExtId = 0U;
  tx_header.IDE = CAN_ID_STD;
  tx_header.RTR = CAN_RTR_DATA;
  tx_header.DLC = BSP_CAN1_LOOPBACK_DATA_LENGTH;
  tx_header.TransmitGlobalTime = DISABLE;

  g_bsp_can_test_step = 4U;
  g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_TX_QUEUED;
  BSP_CAN1_SnapshotRuntimeState();
  if (HAL_CAN_AddTxMessage(&hcan1,
                           &tx_header,
                           g_bsp_can_tx_data,
                           (uint32_t *)&g_bsp_can_tx_mailbox) != HAL_OK)
  {
    g_bsp_can_last_error = BSP_CAN_ERROR_TX;
    g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_FAILED;
    BSP_CAN1_SnapshotRuntimeState();
    return HAL_ERROR;
  }

  g_bsp_can_test_step = 5U;
  g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_RX_WAITING;
  BSP_CAN1_SnapshotRuntimeState();
  start_tick = HAL_GetTick();
  while (HAL_CAN_GetRxFifoFillLevel(&hcan1, BSP_CAN1_FILTER_FIFO) == 0U)
  {
    BSP_CAN1_SnapshotRuntimeState();
    if ((HAL_GetTick() - start_tick) >= BSP_CAN1_LOOPBACK_TIMEOUT_MS)
    {
      g_bsp_can_last_error = BSP_CAN_ERROR_RX_TIMEOUT;
      g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_FAILED;
      BSP_CAN1_SnapshotRuntimeState();
      return HAL_ERROR;
    }
  }

  BSP_CAN1_SnapshotRuntimeState();
  if (HAL_CAN_GetRxMessage(&hcan1,
                           BSP_CAN1_FILTER_FIFO,
                           &rx_header,
                           g_bsp_can_rx_data) != HAL_OK)
  {
    g_bsp_can_last_error = BSP_CAN_ERROR_RX_READ;
    g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_FAILED;
    BSP_CAN1_SnapshotRuntimeState();
    return HAL_ERROR;
  }

  g_bsp_can_rx_std_id = rx_header.StdId;
  g_bsp_can_rx_ide = rx_header.IDE;
  g_bsp_can_rx_rtr = rx_header.RTR;
  g_bsp_can_rx_dlc = rx_header.DLC;

  if ((rx_header.StdId != tx_header.StdId) ||
      (rx_header.IDE != tx_header.IDE) ||
      (rx_header.RTR != tx_header.RTR) ||
      (rx_header.DLC != tx_header.DLC))
  {
    g_bsp_can_last_error = BSP_CAN_ERROR_FRAME_MISMATCH;
    g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_FAILED;
    BSP_CAN1_SnapshotRuntimeState();
    return HAL_ERROR;
  }

  for (index = 0U; index < BSP_CAN1_LOOPBACK_DATA_LENGTH; ++index)
  {
    if (g_bsp_can_rx_data[index] != g_bsp_can_tx_data[index])
    {
      g_bsp_can_last_error = BSP_CAN_ERROR_FRAME_MISMATCH;
      g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_FAILED;
      BSP_CAN1_SnapshotRuntimeState();
      return HAL_ERROR;
    }
  }

  g_bsp_can_last_error = BSP_CAN_ERROR_NONE;
  g_bsp_can_test_step = 6U;
  g_bsp_can_loopback_state = BSP_CAN_LOOPBACK_STATE_PASSED;
  BSP_CAN1_SnapshotRuntimeState();
  return HAL_OK;
}

/**
  * @brief 发送一帧不会触发 C620 运动的外部 CAN 测试帧。
  *
  * ID 0x321 仅用于总线连通性验证，不使用 C620 的控制 ID。
  * 函数只发送一次，避免在首次上电阶段持续占用总线或误触发控制逻辑。
  * @retval HAL_OK    已放入 CAN 发送邮箱
  * @retval HAL_BUSY  当前没有可用发送邮箱
  * @retval HAL_ERROR 发送配置或总线状态异常
  */
HAL_StatusTypeDef BSP_CAN1_SendSafeExternalTestFrame(void)
{
  CAN_TxHeaderTypeDef tx_header = {0};
  uint8_t tx_data[BSP_CAN1_EXTERNAL_TEST_DATA_LENGTH] =
  {
    0x11U, 0x22U, 0x33U, 0x44U,
    0x55U, 0x66U, 0x77U, 0x88U
  };
  HAL_StatusTypeDef status = HAL_ERROR;

  tx_header.StdId = BSP_CAN1_EXTERNAL_TEST_STD_ID;
  tx_header.ExtId = 0U;
  tx_header.IDE = CAN_ID_STD;
  tx_header.RTR = CAN_RTR_DATA;
  tx_header.DLC = BSP_CAN1_EXTERNAL_TEST_DATA_LENGTH;
  tx_header.TransmitGlobalTime = DISABLE;

  g_bsp_can_external_tx_attempted = 1U;
  status = HAL_CAN_AddTxMessage(&hcan1,
                                &tx_header,
                                tx_data,
                                (uint32_t *)&g_bsp_can_external_tx_mailbox);
  g_bsp_can_external_tx_status = (uint32_t)status;
  g_bsp_can_external_tx_error_code = HAL_CAN_GetError(&hcan1);
  g_bsp_can_hal_state = (uint32_t)HAL_CAN_GetState(&hcan1);
  g_bsp_can_hal_error_code = g_bsp_can_external_tx_error_code;
  g_bsp_can_tx_free_level = HAL_CAN_GetTxMailboxesFreeLevel(&hcan1);
  return status;
}

uint8_t BSP_CAN1_SendC620ControlPayload(
    const uint8_t data[BSP_CAN1_C620_CONTROL_DATA_LENGTH],
    void *context)
{
  CAN_TxHeaderTypeDef tx_header = {0};
  HAL_StatusTypeDef status;
  uint8_t index;

  (void)context;
  g_bsp_can_c620_tx_attempt_count++;
  if (data == (const uint8_t *)0)
  {
    g_bsp_can_c620_tx_error_count++;
    g_bsp_can_c620_tx_hal_status = HAL_ERROR;
    return 0U;
  }

  for (index = 0U; index < BSP_CAN1_C620_CONTROL_DATA_LENGTH; ++index)
  {
    g_bsp_can_c620_tx_data[index] = data[index];
  }

  tx_header.StdId = BSP_CAN1_C620_CONTROL_STD_ID;
  tx_header.ExtId = 0U;
  tx_header.IDE = CAN_ID_STD;
  tx_header.RTR = CAN_RTR_DATA;
  tx_header.DLC = BSP_CAN1_C620_CONTROL_DATA_LENGTH;
  tx_header.TransmitGlobalTime = DISABLE;

  status = HAL_CAN_AddTxMessage(&hcan1,
                                &tx_header,
                                g_bsp_can_c620_tx_data,
                                (uint32_t *)&g_bsp_can_c620_tx_mailbox);
  g_bsp_can_c620_tx_hal_status = (uint32_t)status;
  g_bsp_can_c620_tx_hal_error = HAL_CAN_GetError(&hcan1);
  g_bsp_can_hal_state = (uint32_t)HAL_CAN_GetState(&hcan1);
  g_bsp_can_hal_error_code = g_bsp_can_c620_tx_hal_error;
  g_bsp_can_tx_free_level = HAL_CAN_GetTxMailboxesFreeLevel(&hcan1);
  if (status != HAL_OK)
  {
    g_bsp_can_c620_tx_error_count++;
    return 0U;
  }

  g_bsp_can_c620_tx_success_count++;
  return BSP_CAN1_FIXED_TRANSPORT_OK;
}

/**
  * @brief 轮询并保存外部 CAN 总线收到的最新一帧。
  *
  * 主循环调用本函数即可观察 USB-CAN 或 C620 发来的帧。
  * 函数不解析、不执行任何电机命令，只把原始帧头和数据保存到 Watch 变量。
  */
void BSP_CAN1_PollExternalFrames(BSP_CAN_RxFrameHandlerTypeDef frame_handler)
{
  CAN_RxHeaderTypeDef rx_header = {0};
  uint8_t rx_data[BSP_CAN1_EXTERNAL_TEST_DATA_LENGTH] = {0U};
  uint8_t index = 0U;

  while (HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0) > 0U)
  {
    if (HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK)
    {
      g_bsp_can_external_rx_error_code = HAL_CAN_GetError(&hcan1);
      g_bsp_can_hal_error_code = g_bsp_can_external_rx_error_code;
      return;
    }

    g_bsp_can_external_rx_std_id = rx_header.StdId;
    g_bsp_can_external_rx_ide = rx_header.IDE;
    g_bsp_can_external_rx_rtr = rx_header.RTR;
    g_bsp_can_external_rx_dlc = rx_header.DLC;
    g_bsp_can_external_rx_error_code = HAL_CAN_GetError(&hcan1);
    g_bsp_can_external_rx_count++;

    for (index = 0U; index < BSP_CAN1_EXTERNAL_TEST_DATA_LENGTH; ++index)
    {
      g_bsp_can_external_rx_data[index] = rx_data[index];
    }

    /*
     * 在原始帧已经完整保存后同步通知上层解析器。BSP 不识别任何电机协议，
     * 因此以后更换设备时不需要修改 CAN 驱动层。
     */
    if (frame_handler != 0)
    {
      frame_handler(rx_header.StdId,
                    rx_header.IDE,
                    rx_header.RTR,
                    rx_header.DLC,
                    rx_data,
                    HAL_GetTick());
    }
  }

  g_bsp_can_hal_state = (uint32_t)HAL_CAN_GetState(&hcan1);
  g_bsp_can_hal_error_code = HAL_CAN_GetError(&hcan1);
  g_bsp_can_rx_fifo0_level = HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0);
}

/**
  * @brief  配置一个允许全部帧通过的 CAN 过滤器。
  *
  * 当前 P3-A 需要同时观察固定测试帧和 C620 周期反馈，使用全通过过滤器
  * 便于保留原始总线证据；进入正式任务执行阶段前再按协议收紧过滤规则。
  */
static HAL_StatusTypeDef BSP_CAN1_ConfigAcceptAllFilter(void)
{
  CAN_FilterTypeDef filter = {0};

  filter.FilterBank = BSP_CAN1_FILTER_BANK;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterIdHigh = 0U;
  filter.FilterIdLow = 0U;
  filter.FilterMaskIdHigh = 0U;
  filter.FilterMaskIdLow = 0U;
  filter.FilterFIFOAssignment = BSP_CAN1_FILTER_FIFO;
  filter.FilterActivation = ENABLE;
  filter.SlaveStartFilterBank = 14U;

  return HAL_CAN_ConfigFilter(&hcan1, &filter);
}

/**
  * @brief  填充一组固定的测试载荷。
  *
  * 使用固定值的好处是：下载后在 Watch 里一眼能看出“发送前数据”
  * 和“接收后数据”是否完全一致。
  */
static void BSP_CAN1_LoadLoopbackPayload(void)
{
  g_bsp_can_tx_data[0] = 0x11U;
  g_bsp_can_tx_data[1] = 0x22U;
  g_bsp_can_tx_data[2] = 0x33U;
  g_bsp_can_tx_data[3] = 0x44U;
  g_bsp_can_tx_data[4] = 0x55U;
  g_bsp_can_tx_data[5] = 0x66U;
  g_bsp_can_tx_data[6] = 0x77U;
  g_bsp_can_tx_data[7] = 0x88U;

  g_bsp_can_rx_data[0] = 0U;
  g_bsp_can_rx_data[1] = 0U;
  g_bsp_can_rx_data[2] = 0U;
  g_bsp_can_rx_data[3] = 0U;
  g_bsp_can_rx_data[4] = 0U;
  g_bsp_can_rx_data[5] = 0U;
  g_bsp_can_rx_data[6] = 0U;
  g_bsp_can_rx_data[7] = 0U;
}

/**
  * @brief  将 HAL 层运行态同步到 Watch 变量。
  *
  * 这样调试时可以同时看见 CAN 状态机、错误码、发送邮箱空闲数和
  * FIFO0 当前深度，避免只看到一个超时结论却不知道卡在哪一层。
  */
static void BSP_CAN1_SnapshotRuntimeState(void)
{
  g_bsp_can_hal_state = (uint32_t)HAL_CAN_GetState(&hcan1);
  g_bsp_can_hal_error_code = HAL_CAN_GetError(&hcan1);
  g_bsp_can_tx_free_level = HAL_CAN_GetTxMailboxesFreeLevel(&hcan1);
  g_bsp_can_rx_fifo0_level = HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0);
}
