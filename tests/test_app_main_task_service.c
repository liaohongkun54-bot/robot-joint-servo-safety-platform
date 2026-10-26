/**
  ******************************************************************************
  * @file    test_app_main_task_service.c
  * @brief   Host test for the FreeRTOS application service seam.
  ******************************************************************************
  */

#include <stdint.h>
#include <stdio.h>

#if defined(P3C_ACTIVE_FAULT_TEST_ENABLE) && (P3C_ACTIVE_FAULT_TEST_ENABLE != 0U)
extern volatile uint8_t g_p3c_fault_injected;
extern volatile uint32_t g_p3c_fault_injection_tick_ms;
#endif

#include "app_main_task_service.h"
#include "bsp_can.h"
#include "c620_feedback.h"
#include "safety_state_machine.h"
#include "task_uart_adapter.h"
#include "test_task_ingress_service.h"

static uint32_t g_operator_service_calls;
void OperatorRuntime_Service(uint32_t now_ms)
{
  (void)now_ms;
  g_operator_service_calls++;
}

volatile C620_FeedbackStateTypeDef g_c620_feedback;
volatile uint32_t g_bsp_can_hal_error_code;
volatile uint8_t g_safety_test_estop_active;
volatile uint8_t g_safety_operator_clear_request;
volatile uint8_t g_safety_motion_arm_request;
volatile Safety_SnapshotTypeDef g_safety_snapshot;
volatile C620_MotionGateSnapshotTypeDef g_c620_motion_gate_snapshot;

static uint8_t g_call_order[7U];
static uint8_t g_call_count;
static const volatile Safety_SnapshotTypeDef *g_stop_safety_seen;
static uint32_t g_last_now_ms;
static Safety_InputTypeDef g_safety_input;
static BSP_CAN_RxFrameHandlerTypeDef g_received_handler;
static uint8_t g_telemetry_query_ready;
static uint8_t g_telemetry_reply_seen;
static Telemetry_ReplyTypeDef g_telemetry_reply;

void BSP_CAN1_PollExternalFrames(BSP_CAN_RxFrameHandlerTypeDef frame_handler)
{
  g_call_order[g_call_count++] = 1U;
  g_received_handler = frame_handler;
}

uint8_t BSP_CAN1_SendC620ControlPayload(
    const uint8_t data[BSP_CAN1_C620_CONTROL_DATA_LENGTH],
    void *context)
{
  (void)data;
  (void)context;
  return BSP_CAN1_FIXED_TRANSPORT_OK;
}

void C620_Feedback_ProcessCanFrame(uint32_t std_id,
                                   uint32_t ide,
                                   uint32_t rtr,
                                   uint32_t dlc,
                                   const uint8_t *data,
                                   uint32_t rx_tick_ms)
{
  (void)std_id;
  (void)ide;
  (void)rtr;
  (void)dlc;
  (void)data;
  (void)rx_tick_ms;
}

void C620_Feedback_UpdateLinkState(uint32_t now_tick_ms)
{
  g_call_order[g_call_count++] = 2U;
  g_last_now_ms = now_tick_ms;
}

void Safety_StateMachine_Update(const Safety_InputTypeDef *input)
{
  g_call_order[g_call_count++] = 3U;
  g_safety_input = *input;
  g_safety_snapshot.state = input->link_state == C620_LINK_ONLINE ?
                            SAFETY_STATE_ONLINE : SAFETY_STATE_WAIT_FEEDBACK;
}

void C620_MotionGate_BindTransport(
    C620_ControlOutput_TransportFn transport,
    void *context)
{
  (void)transport;
  (void)context;
}

void C620_MotionGate_Update(
    const volatile Safety_SnapshotTypeDef *safety,
    uint32_t now_ms)
{
  g_call_order[g_call_count++] = 4U;
  if (safety == &g_safety_snapshot)
  {
    g_last_now_ms = now_ms;
  }
}

uint8_t TestTask_IngressRecordMotionStop(
    const volatile Safety_SnapshotTypeDef *safety)
{
  g_call_order[g_call_count++] = 5U;
  g_stop_safety_seen = safety;
  return 0U;
}

TaskUart_AdapterResultTypeDef TaskUart_AdapterService(uint32_t now_ms)
{
  g_call_order[g_call_count++] = 6U;
  g_last_now_ms = now_ms;
  return TASK_UART_ADAPTER_OK;
}

uint8_t TaskUart_AdapterTakeTelemetryQuery(Telemetry_QueryTypeDef *query)
{
  if (g_telemetry_query_ready == 0U)
  {
    return 0U;
  }
  query->request_id = 0x12345678UL;
  query->nonce = 0x9ABCDEF0UL;
  g_telemetry_query_ready = 0U;
  return 1U;
}

TaskUart_TxResultTypeDef TaskUart_AdapterQueueTelemetry(
    const Telemetry_ReplyTypeDef *telemetry)
{
  g_telemetry_reply = *telemetry;
  g_telemetry_reply_seen = 1U;
  return TASK_UART_TX_QUEUED;
}

TestTask_IngressResultTypeDef TestTask_IngressService(
    const volatile Safety_SnapshotTypeDef *safety,
    uint32_t now_ms)
{
  g_call_order[g_call_count++] = 7U;
  if (safety != &g_safety_snapshot)
  {
    return TEST_TASK_INGRESS_ERROR_NULL_SAFETY;
  }
  g_last_now_ms = now_ms;
  return TEST_TASK_INGRESS_NO_TASK;
}

int main(void)
{
  const uint32_t now_ms = 123456UL;

  g_c620_feedback.link_state = C620_LINK_ONLINE;
  g_c620_feedback.valid_frame_count = 77UL;
  g_bsp_can_hal_error_code = 0UL;
  g_safety_test_estop_active = 0U;
  g_safety_operator_clear_request = 1U;

  AppMainTaskService_RunOnce(now_ms);

  if ((g_call_count != 7U) ||
      (g_call_order[0] != 1U) ||
      (g_call_order[1] != 2U) ||
      (g_call_order[2] != 3U) ||
      (g_call_order[3] != 4U) ||
      (g_call_order[4] != 5U) ||
      (g_call_order[5] != 6U) ||
      (g_call_order[6] != 7U) ||
      (g_stop_safety_seen != &g_safety_snapshot) ||
      (g_received_handler != C620_Feedback_ProcessCanFrame) ||
      (g_last_now_ms != now_ms) ||
      (g_safety_input.now_ms != now_ms) ||
      (g_safety_input.link_state != C620_LINK_ONLINE) ||
      (g_safety_input.valid_frame_count != 77UL) ||
      (g_safety_input.can_error_code != 0UL) ||
      (g_safety_input.estop_active != 0U) ||
      (g_safety_input.init_ok != 1U) ||
      (g_safety_input.operator_clear != 1U) ||
      (g_c620_motion_gate_snapshot.sent_frame_count != 0UL))
  {
    return 1;
  }

  /* The production tasks keep safety/motion ahead of serialized ingress. */
  g_call_count = 0U;
  AppMainTaskService_RunControlOnce(now_ms + 1UL);
  AppMainTaskService_RunIngressOnce(now_ms + 1UL);
  AppMainTaskService_RunUartOnce(now_ms + 1UL);
  if ((g_call_count != 7U) ||
      (g_call_order[0] != 1U) ||
      (g_call_order[1] != 2U) ||
      (g_call_order[2] != 3U) ||
      (g_call_order[3] != 4U) ||
      (g_call_order[4] != 5U) ||
      (g_call_order[5] != 7U) ||
      (g_call_order[6] != 6U) ||
      (g_stop_safety_seen != &g_safety_snapshot))
  {
    return 8;
  }
  g_telemetry_query_ready = 1U;
  g_telemetry_reply_seen = 0U;
  g_c620_feedback.last_rx_tick_ms = now_ms;
  g_c620_feedback.encoder_raw = 2048U;
  g_c620_feedback.speed_rpm = -1964;
  g_c620_feedback.current_raw = 1114;
  g_c620_feedback.temperature_c = 30U;
  g_c620_feedback.has_valid_feedback = 1U;
  g_call_count = 0U;
  AppMainTaskService_RunControlOnce(now_ms + 2UL);
  if ((g_telemetry_reply_seen == 0U) ||
      (g_telemetry_reply.request_id != 0x12345678UL) ||
      (g_telemetry_reply.nonce != 0x9ABCDEF0UL) ||
      (g_telemetry_reply.sample_tick_ms != now_ms + 2UL) ||
      (g_telemetry_reply.last_rx_tick_ms != now_ms) ||
      (g_telemetry_reply.speed_rpm != -1964) ||
      (g_telemetry_reply.current_raw != 1114) ||
      (g_telemetry_reply.encoder_raw != 2048U) ||
      (g_telemetry_reply.temperature_c != 30U) ||
      (g_telemetry_reply.feedback_std_id != C620_FEEDBACK_STD_ID) ||
      (g_c620_motion_gate_snapshot.sent_frame_count != 0UL))
  {
    return 9;
  }
#if defined(P3C_ACTIVE_FAULT_TEST_ENABLE) && (P3C_ACTIVE_FAULT_TEST_ENABLE != 0U)
  g_c620_motion_gate_snapshot.start_tick_ms = 1000UL;
  g_c620_motion_gate_snapshot.active_duration_ms = 1000UL;
  g_c620_motion_gate_snapshot.active_current_raw = 1000;
  g_c620_motion_gate_snapshot.active = 1U;

  /* 其他请求号：不注入。 */
  g_c620_motion_gate_snapshot.last_request_id = 7507UL;
  g_call_count = 0U;
  AppMainTaskService_RunOnce(1200UL);
  if ((g_p3c_fault_injected != 0U) ||
      (g_safety_test_estop_active != 0U))
  {
    return 2;
  }

  /* 目标请求尚未活动：不注入。 */
  g_c620_motion_gate_snapshot.last_request_id = 7506UL;
  g_c620_motion_gate_snapshot.active = 0U;
  g_call_count = 0U;
  AppMainTaskService_RunOnce(1200UL);
  if (g_p3c_fault_injected != 0U)
  {
    return 3;
  }

  /* 199 ms：尚未达到触发点。 */
  g_c620_motion_gate_snapshot.active = 1U;
  g_call_count = 0U;
  AppMainTaskService_RunOnce(1199UL);
  if (g_p3c_fault_injected != 0U)
  {
    return 4;
  }

  /* 200 ms：本轮 Safety 输入必须看到 ESTOP。 */
  g_call_count = 0U;
  AppMainTaskService_RunOnce(1200UL);
  if ((g_p3c_fault_injected != 1U) ||
      (g_p3c_fault_injection_tick_ms != 1200UL) ||
      (g_safety_test_estop_active != 1U) ||
      (g_safety_input.estop_active != 1U) ||
      (g_call_order[2] != 3U) ||
      (g_call_order[3] != 4U))
  {
    return 5;
  }

  /* 清除输入后也不能再次自动注入。 */
  g_safety_test_estop_active = 0U;
  g_call_count = 0U;
  AppMainTaskService_RunOnce(1201UL);
  if ((g_safety_test_estop_active != 0U) ||
      (g_p3c_fault_injection_tick_ms != 1200UL))
  {
    return 6;
  }

  /* 已到 1000 ms：不能伪装成提前故障停止。 */
  g_p3c_fault_injected = 0U;
  g_p3c_fault_injection_tick_ms = 0UL;
  g_call_count = 0U;
  AppMainTaskService_RunOnce(2000UL);
  if ((g_p3c_fault_injected != 0U) ||
      (g_safety_test_estop_active != 0U))
  {
    return 7;
  }

  (void)printf("p3c_active_fault_injection_tests=pass\n");
#endif
  (void)printf("app_main_task_service_tests=pass\n");
  return 0;
}
