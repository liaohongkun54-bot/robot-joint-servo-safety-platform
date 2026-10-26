/**
  ******************************************************************************
  * @file    c620_feedback.c
  * @brief   C620/M3508 read-only CAN feedback parser and link monitor.
  ******************************************************************************
  */

#include "c620_feedback.h"

volatile C620_FeedbackStateTypeDef g_c620_feedback;
volatile uint8_t g_c620_feedback_drop_frames_for_test = 0U;
volatile uint32_t g_c620_feedback_dropped_frame_count = 0U;

/**
  * @brief 初始化反馈快照和链路监控状态。
  *
  * 显式清零每个字段，便于审查变量含义，也避免依赖 C 运行库的 memset。
  */
void C620_Feedback_Init(void)
{
  g_c620_feedback.encoder_raw = 0U;
  g_c620_feedback.last_encoder_raw = 0U;
  g_c620_feedback.speed_rpm = 0;
  g_c620_feedback.current_raw = 0;
  g_c620_feedback.temperature_c = 0U;
  g_c620_feedback.reserved_byte = 0U;
  g_c620_feedback.has_valid_feedback = 0U;
  g_c620_feedback.link_state = C620_LINK_NEVER_RECEIVED;
  g_c620_feedback.valid_frame_count = 0U;
  g_c620_feedback.invalid_frame_count = 0U;
  g_c620_feedback.ignored_frame_count = 0U;
  g_c620_feedback.last_rx_tick_ms = 0U;
  g_c620_feedback.timeout_count = 0U;
  g_c620_feedback_drop_frames_for_test = 0U;
  g_c620_feedback_dropped_frame_count = 0U;
}

/**
  * @brief 接收并解析一帧 CAN 数据。
  *
  * 非目标节点反馈帧只计入 ignored_frame_count，不改变 C620 在线状态；目标 ID
  * 的帧若不是标准数据帧、DLC 不是 8 或数据指针无效，则计为协议无效帧。
  * 只有完全有效的目标反馈才会刷新 last_rx_tick_ms 和在线状态。
  */
void C620_Feedback_ProcessCanFrame(uint32_t std_id,
                                   uint32_t ide,
                                   uint32_t rtr,
                                   uint32_t dlc,
                                   const uint8_t *data,
                                   uint32_t rx_tick_ms)
{
  uint16_t encoder = 0U;
  int16_t speed = 0;
  int16_t current = 0;

  if (std_id != C620_FEEDBACK_STD_ID)
  {
    g_c620_feedback.ignored_frame_count++;
    return;
  }

  if ((ide != CAN_ID_STD) ||
      (rtr != CAN_RTR_DATA) ||
      (dlc != C620_FEEDBACK_DATA_LENGTH) ||
      (data == (const uint8_t *)0))
  {
    g_c620_feedback.invalid_frame_count++;
    return;
  }

  /*
   * P3-A 安全故障注入：只阻断上层在线时间刷新，不停止 BSP 原始接收，
   * 不修改 CAN 外设，也不产生任何发送动作。关闭开关后下一帧自动恢复 ONLINE。
   */
  if (g_c620_feedback_drop_frames_for_test != 0U)
  {
    g_c620_feedback_dropped_frame_count++;
    return;
  }

  encoder = (uint16_t)(((uint16_t)data[0] << 8U) | (uint16_t)data[1]);
  speed = (int16_t)(((uint16_t)data[2] << 8U) | (uint16_t)data[3]);
  current = (int16_t)(((uint16_t)data[4] << 8U) | (uint16_t)data[5]);

  if (g_c620_feedback.has_valid_feedback == 0U)
  {
    /* 第一帧没有历史样本，令 last 与 current 相等，避免产生伪造的大跳变。 */
    g_c620_feedback.last_encoder_raw = encoder;
  }
  else
  {
    g_c620_feedback.last_encoder_raw = g_c620_feedback.encoder_raw;
  }

  g_c620_feedback.encoder_raw = encoder;
  g_c620_feedback.speed_rpm = speed;
  g_c620_feedback.current_raw = current;
  g_c620_feedback.temperature_c = data[6];
  g_c620_feedback.reserved_byte = data[7];
  g_c620_feedback.has_valid_feedback = 1U;
  g_c620_feedback.valid_frame_count++;
  g_c620_feedback.last_rx_tick_ms = rx_tick_ms;
  g_c620_feedback.link_state = C620_LINK_ONLINE;
}

/**
  * @brief 根据最后有效反馈时间更新在线/超时状态。
  *
  * 无符号减法能够自然处理 HAL_GetTick() 的 32 位回绕。timeout_count 只在
  * ONLINE 到 TIMEOUT 的状态转换时增加，不会在掉线期间被主循环重复累加。
  */
void C620_Feedback_UpdateLinkState(uint32_t now_tick_ms)
{
  if (g_c620_feedback.has_valid_feedback == 0U)
  {
    g_c620_feedback.link_state = C620_LINK_NEVER_RECEIVED;
    return;
  }

  if ((now_tick_ms - g_c620_feedback.last_rx_tick_ms) > C620_FEEDBACK_TIMEOUT_MS)
  {
    if (g_c620_feedback.link_state == C620_LINK_ONLINE)
    {
      g_c620_feedback.timeout_count++;
    }
    g_c620_feedback.link_state = C620_LINK_TIMEOUT;
  }
}
