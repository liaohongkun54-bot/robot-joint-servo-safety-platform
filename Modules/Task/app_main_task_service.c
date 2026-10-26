/**
  ******************************************************************************
  * @file    app_main_task_service.c
  * @brief   One behavior-equivalent service iteration for the main application.
  ******************************************************************************
  */

#include "app_main_task_service.h"

#ifndef P3C_ACTIVE_FAULT_TEST_ENABLE
#define P3C_ACTIVE_FAULT_TEST_ENABLE (0U)
#endif

#if (P3C_ACTIVE_FAULT_TEST_ENABLE != 0U)
volatile uint8_t g_p3c_fault_injected = 0U;
volatile uint32_t g_p3c_fault_injection_tick_ms = 0UL;
#endif

#include "bsp_can.h"
#include "c620_feedback.h"
#include "c620_motion_gate.h"
#include "safety_state_machine.h"
#include "task_uart_adapter.h"
#include "test_task_ingress_service.h"
#include "operator_runtime.h"

void AppMainTaskService_RunControlOnce(uint32_t now_ms)
{
  Safety_InputTypeDef safety_input;
  Telemetry_QueryTypeDef telemetry_query;
  Telemetry_ReplyTypeDef telemetry_reply;

  #if (P3C_ACTIVE_FAULT_TEST_ENABLE != 0U)
  uint32_t elapsed_ms;
#endif

  /* Preserve the pre-FreeRTOS polling order and keep CAN receive read-only. */
  BSP_CAN1_PollExternalFrames(C620_Feedback_ProcessCanFrame);
  C620_Feedback_UpdateLinkState(now_ms);

#if (P3C_ACTIVE_FAULT_TEST_ENABLE != 0U)
  elapsed_ms =
      (uint32_t)(now_ms - g_c620_motion_gate_snapshot.start_tick_ms);

  if ((g_p3c_fault_injected == 0U) &&
      (g_safety_test_estop_active == 0U) &&
      (g_c620_motion_gate_snapshot.active != 0U) &&
      (g_c620_motion_gate_snapshot.last_request_id == 7506UL) &&
      (g_c620_motion_gate_snapshot.active_current_raw == 1000) &&
      (g_c620_motion_gate_snapshot.active_duration_ms == 1000UL) &&
      (elapsed_ms >= 200UL) &&
      (elapsed_ms < 1000UL))
  {
    g_p3c_fault_injection_tick_ms = now_ms;
    g_p3c_fault_injected = 1U;
    g_safety_test_estop_active = 1U;
  }
#endif

  safety_input.now_ms = now_ms;
  safety_input.link_state = g_c620_feedback.link_state;
  safety_input.valid_frame_count = g_c620_feedback.valid_frame_count;
  safety_input.can_error_code = g_bsp_can_hal_error_code;
  safety_input.estop_active = g_safety_test_estop_active;
  safety_input.init_ok = 1U;
  safety_input.operator_clear = g_safety_operator_clear_request;
  Safety_StateMachine_Update(&safety_input);
  /* Remote recovery is a one-cycle request, not a latent future clear. */
  g_safety_operator_clear_request = 0U;

  C620_MotionGate_BindTransport(BSP_CAN1_SendC620ControlPayload, (void *)0);
  OperatorRuntime_Service(now_ms);
  C620_MotionGate_Update(&g_safety_snapshot, now_ms);

  /* Sample only on a validated read-only query, in the CAN/feedback owner. */
  if (TaskUart_AdapterTakeTelemetryQuery(&telemetry_query) != 0U)
  {
    telemetry_reply.request_id = telemetry_query.request_id;
    telemetry_reply.nonce = telemetry_query.nonce;
    telemetry_reply.sample_tick_ms = now_ms;
    telemetry_reply.last_rx_tick_ms = g_c620_feedback.last_rx_tick_ms;
    telemetry_reply.valid_frame_count = g_c620_feedback.valid_frame_count;
    telemetry_reply.encoder_raw = g_c620_feedback.encoder_raw;
    telemetry_reply.speed_rpm = g_c620_feedback.speed_rpm;
    telemetry_reply.current_raw = g_c620_feedback.current_raw;
    telemetry_reply.temperature_c = g_c620_feedback.temperature_c;
    telemetry_reply.link_state = (uint8_t)g_c620_feedback.link_state;
    telemetry_reply.has_valid_feedback = g_c620_feedback.has_valid_feedback;
    telemetry_reply.source_id = TELEMETRY_SOURCE_C620_CAN1;
    telemetry_reply.feedback_std_id = C620_FEEDBACK_STD_ID;
    (void)TaskUart_AdapterQueueTelemetry(&telemetry_reply);
  }
}

void AppMainTaskService_RunIngressOnce(uint32_t now_ms)
{
  (void)TestTask_IngressRecordMotionStop(&g_safety_snapshot);
  (void)TestTask_IngressService(&g_safety_snapshot, now_ms);
}

void AppMainTaskService_RunUartOnce(uint32_t now_ms)
{
  (void)TaskUart_AdapterService(now_ms);
}

void AppMainTaskService_RunOnce(uint32_t now_ms)
{
  AppMainTaskService_RunControlOnce(now_ms);
  (void)TestTask_IngressRecordMotionStop(&g_safety_snapshot);
  AppMainTaskService_RunUartOnce(now_ms);
  (void)TestTask_IngressService(&g_safety_snapshot, now_ms);
}
