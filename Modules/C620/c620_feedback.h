/**
  ******************************************************************************
  * @file    c620_feedback.h
  * @brief   C620/M3508 read-only CAN feedback parser and link monitor.
  ******************************************************************************
  * @attention
  *
  * 本模块只解析电调周期反馈并判断在线状态，不提供任何电流、速度、位置
  * 或使能命令发送接口。这样可以保证 P3-A 阶段即使上层误调用，也无法通过
  * 本模块驱动电机动作。
  *
  ******************************************************************************
  */

#ifndef __C620_FEEDBACK_H__
#define __C620_FEEDBACK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/** 当前唯一 C620 节点 ID；必须与电调实际设置一致。 */
#define C620_NODE_ID                     (1U)

/** C620 周期反馈标准帧 ID = 0x200 + 节点 ID。 */
#define C620_FEEDBACK_STD_ID             (0x200U + C620_NODE_ID)

/** C620 周期反馈的固定载荷长度，单位：字节。 */
#define C620_FEEDBACK_DATA_LENGTH        (8U)

/**
  * @brief 反馈掉线判定时间，单位：毫秒。
  *
  * 实测反馈周期约为 1 ms。100 ms 门限允许偶发丢帧，同时仍能在较短时间内
  * 识别电调断电、总线断开或接收链路异常。后续必须通过故障注入实验再确认。
  */
#define C620_FEEDBACK_TIMEOUT_MS         (100U)

/** C620 只读链路状态。 */
typedef enum
{
  C620_LINK_NEVER_RECEIVED = 0U,
  C620_LINK_ONLINE = 1U,
  C620_LINK_TIMEOUT = 2U
} C620_LinkStateTypeDef;

/**
  * @brief C620 最新反馈和可追溯链路状态。
  *
  * 所有物理量均保留协议原始单位：
  * - encoder_raw：0~8191 的转子编码器原始值；
  * - speed_rpm：协议给出的有符号转速，单位 rpm；
  * - current_raw：协议给出的有符号反馈电流原始值，实际安培换算待协议核验；
  * - temperature_c：电调温度，单位摄氏度。
  */
typedef struct
{
  uint16_t encoder_raw;
  uint16_t last_encoder_raw;
  int16_t speed_rpm;
  int16_t current_raw;
  uint8_t temperature_c;
  uint8_t reserved_byte;
  uint8_t has_valid_feedback;
  C620_LinkStateTypeDef link_state;
  uint32_t valid_frame_count;
  uint32_t invalid_frame_count;
  uint32_t ignored_frame_count;
  uint32_t last_rx_tick_ms;
  uint32_t timeout_count;
} C620_FeedbackStateTypeDef;

/** Watch 中直接展开即可查看的 C620 最新反馈状态。 */
extern volatile C620_FeedbackStateTypeDef g_c620_feedback;

/**
  * @brief P3-A 故障注入开关，仅用于 Watch 测试。
  *
  * 设为 1 时，解析器会故意丢弃格式正确的目标节点反馈，不刷新在线时间；
  * BSP 原始接收和 CAN 总线保持正常，也不会发送任何控制帧。复位后恢复为 0。
  */
extern volatile uint8_t g_c620_feedback_drop_frames_for_test;

/** 故障注入期间被解析器主动丢弃的有效目标节点反馈帧数。 */
extern volatile uint32_t g_c620_feedback_dropped_frame_count;

void C620_Feedback_Init(void);
void C620_Feedback_ProcessCanFrame(uint32_t std_id,
                                   uint32_t ide,
                                   uint32_t rtr,
                                   uint32_t dlc,
                                   const uint8_t *data,
                                   uint32_t rx_tick_ms);
void C620_Feedback_UpdateLinkState(uint32_t now_tick_ms);

#ifdef __cplusplus
}
#endif

#endif /* __C620_FEEDBACK_H__ */
