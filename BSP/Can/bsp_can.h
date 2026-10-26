/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    bsp_can.h
  * @brief   CAN board support package for the current C-board minimal project.
  *
  * 这一层只放工程自己的 CAN 业务入口，不放 CubeMX 自动生成的底层
  * 初始化代码。这样做的目的，是把“外设初始化”和“工程自定义行为”
  * 明确分开，后续重新生成代码时也更容易保住业务结构。
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

#ifndef __BSP_CAN_H__
#define __BSP_CAN_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private defines -----------------------------------------------------------*/
/**
  * @brief  内部回环测试使用的标准帧 ID。
  *
  * 选择一个固定的标准帧 ID，便于在 Watch 里直接核对发送和接收是否一致。
  */
#define BSP_CAN1_LOOPBACK_STD_ID         (0x321U)

/**
  * @brief  内部回环测试的载荷长度，固定为 8 字节，便于做完整数据对比。
  */
#define BSP_CAN1_LOOPBACK_DATA_LENGTH    (8U)

/**
  * @brief  内部回环测试等待接收的超时时间，单位毫秒。
  *
  * 这个值只用于当前的小步验证，不用于生产级总线超时策略。
  */
#define BSP_CAN1_LOOPBACK_TIMEOUT_MS     (100U)

/**
  * @brief 外部总线验证使用的标准帧 ID。
  *
  * 该帧不属于 C620 的使能、位置、速度或力矩控制协议，
  * 只用于确认 MCU、C 板 CAN 收发器和 USB-CAN 总线能够正常收发。
  */
#define BSP_CAN1_EXTERNAL_TEST_STD_ID    (0x321U)

/**
  * @brief 外部总线验证帧的数据长度。
  */
#define BSP_CAN1_EXTERNAL_TEST_DATA_LENGTH (8U)

/* Fixed C620 ID 1 current-control group. No caller-selectable ID or DLC. */
#define BSP_CAN1_C620_CONTROL_STD_ID       (0x200U)
#define BSP_CAN1_C620_CONTROL_DATA_LENGTH  (8U)
#define BSP_CAN1_FIXED_TRANSPORT_OK        (1U)

/* Public types --------------------------------------------------------------*/
typedef enum
{
  BSP_CAN_LOOPBACK_STATE_IDLE = 0U,
  BSP_CAN_LOOPBACK_STATE_FILTER_READY = 1U,
  BSP_CAN_LOOPBACK_STATE_CAN_STARTED = 2U,
  BSP_CAN_LOOPBACK_STATE_TX_QUEUED = 3U,
  BSP_CAN_LOOPBACK_STATE_RX_WAITING = 4U,
  BSP_CAN_LOOPBACK_STATE_PASSED = 5U,
  BSP_CAN_LOOPBACK_STATE_FAILED = 6U
} BSP_CAN_LoopbackStateTypeDef;

typedef enum
{
  BSP_CAN_ERROR_NONE = 0U,
  BSP_CAN_ERROR_FILTER_CONFIG = 1U,
  BSP_CAN_ERROR_START = 2U,
  BSP_CAN_ERROR_TX = 3U,
  BSP_CAN_ERROR_RX_TIMEOUT = 4U,
  BSP_CAN_ERROR_RX_READ = 5U,
  BSP_CAN_ERROR_FRAME_MISMATCH = 6U,
  BSP_CAN_ERROR_INVALID_MODE = 7U
} BSP_CAN_ErrorTypeDef;

/**
  * @brief 外部 CAN 原始帧的同步观察回调。
  *
  * BSP 在成功读取每一帧后立即调用该函数。data 指向 BSP 栈上的 8 字节
  * 临时缓冲区，回调只能在调用期间读取，不得保存该指针供后续使用。
  */
typedef void (*BSP_CAN_RxFrameHandlerTypeDef)(uint32_t std_id,
                                              uint32_t ide,
                                              uint32_t rtr,
                                              uint32_t dlc,
                                              const uint8_t *data,
                                              uint32_t rx_tick_ms);

/* Public variables ----------------------------------------------------------*/
extern volatile BSP_CAN_LoopbackStateTypeDef g_bsp_can_loopback_state;
extern volatile BSP_CAN_ErrorTypeDef g_bsp_can_last_error;
extern volatile uint32_t g_bsp_can_test_step;
extern volatile uint32_t g_bsp_can_hal_state;
extern volatile uint32_t g_bsp_can_hal_error_code;
extern volatile uint32_t g_bsp_can_tx_mailbox;
extern volatile uint32_t g_bsp_can_tx_free_level;
extern volatile uint32_t g_bsp_can_rx_fifo0_level;
extern volatile uint32_t g_bsp_can_rx_std_id;
extern volatile uint32_t g_bsp_can_rx_ide;
extern volatile uint32_t g_bsp_can_rx_rtr;
extern volatile uint32_t g_bsp_can_rx_dlc;
extern uint8_t g_bsp_can_tx_data[BSP_CAN1_LOOPBACK_DATA_LENGTH];
extern uint8_t g_bsp_can_rx_data[BSP_CAN1_LOOPBACK_DATA_LENGTH];

/* 外部总线验证的可观察运行状态。 */
extern volatile uint32_t g_bsp_can_external_tx_status;
extern volatile uint8_t g_bsp_can_external_tx_attempted;
extern volatile uint32_t g_bsp_can_external_tx_mailbox;
extern volatile uint32_t g_bsp_can_external_tx_error_code;
extern volatile uint32_t g_bsp_can_external_rx_count;
extern volatile uint32_t g_bsp_can_external_rx_std_id;
extern volatile uint32_t g_bsp_can_external_rx_ide;
extern volatile uint32_t g_bsp_can_external_rx_rtr;
extern volatile uint32_t g_bsp_can_external_rx_dlc;
extern volatile uint32_t g_bsp_can_external_rx_error_code;
extern uint8_t g_bsp_can_external_rx_data[BSP_CAN1_EXTERNAL_TEST_DATA_LENGTH];

/* Watch-visible snapshot for the constrained C620 transmit boundary. */
extern volatile uint32_t g_bsp_can_c620_tx_attempt_count;
extern volatile uint32_t g_bsp_can_c620_tx_success_count;
extern volatile uint32_t g_bsp_can_c620_tx_error_count;
extern volatile uint32_t g_bsp_can_c620_tx_mailbox;
extern volatile uint32_t g_bsp_can_c620_tx_hal_status;
extern volatile uint32_t g_bsp_can_c620_tx_hal_error;
extern uint8_t g_bsp_can_c620_tx_data[BSP_CAN1_C620_CONTROL_DATA_LENGTH];

/* Public function prototypes ------------------------------------------------*/
HAL_StatusTypeDef BSP_CAN1_Init(void);
HAL_StatusTypeDef BSP_CAN1_PerformInternalLoopbackTest(void);
HAL_StatusTypeDef BSP_CAN1_SendSafeExternalTestFrame(void);
void BSP_CAN1_PollExternalFrames(BSP_CAN_RxFrameHandlerTypeDef frame_handler);

/** Fixed 0x200/DLC8 payload transport for C620_ControlOutput. */
uint8_t BSP_CAN1_SendC620ControlPayload(
    const uint8_t data[BSP_CAN1_C620_CONTROL_DATA_LENGTH],
    void *context);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_CAN_H__ */
