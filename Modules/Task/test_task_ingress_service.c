/**
  ******************************************************************************
  * @file    test_task_ingress_service.c
  * @brief   Main-loop service from UART pending task to safe runtime admission.
  ******************************************************************************
  */

#include "test_task_ingress_service.h"

#include "c620_feedback.h"

volatile TestTask_IngressSnapshotTypeDef g_test_task_ingress_snapshot;
TestRecord_BufferTypeDef g_test_task_ingress_records;
TestTask_DescriptorTypeDef g_test_task_ingress_last_task;
TestTask_ResultDescriptorTypeDef g_test_task_ingress_last_result;

static TestTask_DescriptorTypeDef g_test_task_ingress_active_task;
static uint32_t g_test_task_ingress_active_stop_count;
static uint32_t g_test_task_ingress_active_zero_frame_count;
static uint8_t g_test_task_ingress_active_task_valid;

static void TestTask_IngressCopySafety(
    Safety_SnapshotTypeDef *destination,
    const volatile Safety_SnapshotTypeDef *source)
{
  destination->state = source->state;
  destination->output_permitted = source->output_permitted;
  destination->zero_output_required = source->zero_output_required;
  destination->fault_latched = source->fault_latched;
  destination->estop_active = source->estop_active;
  destination->transition_count = source->transition_count;
  destination->fault_count = source->fault_count;
  destination->recovery_valid_frames = source->recovery_valid_frames;
  destination->last_transition_ms = source->last_transition_ms;
  destination->last_fault_code = source->last_fault_code;
}

static void TestTask_IngressBuildResult(
    TestTask_RuntimeResultTypeDef runtime_result,
    const Safety_SnapshotTypeDef *safety,
    uint32_t now_ms,
    uint16_t record_count_before,
    uint32_t sent_frame_count_before,
    uint8_t record_error)
{
  uint8_t flags = 0U;

  g_test_task_ingress_last_result.request_id =
      g_test_task_ingress_last_task.request_id;
  g_test_task_ingress_last_result.timestamp_ms = now_ms;
  g_test_task_ingress_last_result.record_count =
      g_test_task_ingress_snapshot.record_count;
  g_test_task_ingress_last_result.safety_state = (uint8_t)safety->state;

  if (g_test_task_ingress_snapshot.record_count > record_count_before)
  {
    flags |= TEST_TASK_RESULT_FLAG_RECORD_APPENDED;
  }
  if (g_c620_motion_gate_snapshot.sent_frame_count == sent_frame_count_before)
  {
    flags |= TEST_TASK_RESULT_FLAG_OUTPUT_UNCHANGED;
  }
  g_test_task_ingress_last_result.flags = flags;

  if (record_error != 0U)
  {
    g_test_task_ingress_last_result.error_code =
        TEST_TASK_RUNTIME_RECORD_BUFFER_ERROR;
    g_test_task_ingress_last_result.status =
        TEST_TASK_RESULT_STATUS_INTERNAL_ERROR;
    g_test_task_ingress_last_result.stage = TEST_TASK_RESULT_STAGE_RECORD;
    return;
  }

  switch (runtime_result)
  {
    case TEST_TASK_RUNTIME_ACCEPTED:
      g_test_task_ingress_last_result.error_code = 0U;
      g_test_task_ingress_last_result.status =
          TEST_TASK_RESULT_STATUS_ACCEPTED;
      g_test_task_ingress_last_result.stage =
          TEST_TASK_RESULT_STAGE_COMPLETE;
      break;
    case TEST_TASK_RUNTIME_ERROR_SCHEMA:
      g_test_task_ingress_last_result.error_code =
          TEST_TASK_RUNTIME_RECORD_SCHEMA_ERROR;
      g_test_task_ingress_last_result.status =
          TEST_TASK_RESULT_STATUS_REJECTED;
      g_test_task_ingress_last_result.stage = TEST_TASK_RESULT_STAGE_SCHEMA;
      break;
    case TEST_TASK_RUNTIME_ERROR_SAFETY:
      g_test_task_ingress_last_result.error_code =
          TEST_TASK_RUNTIME_RECORD_SAFETY_ERROR;
      g_test_task_ingress_last_result.status =
          TEST_TASK_RESULT_STATUS_REJECTED;
      g_test_task_ingress_last_result.stage = TEST_TASK_RESULT_STAGE_SAFETY;
      break;
    case TEST_TASK_RUNTIME_ERROR_MOTION_GATE:
      g_test_task_ingress_last_result.error_code =
          TEST_TASK_RUNTIME_RECORD_MOTION_ERROR;
      g_test_task_ingress_last_result.status =
          TEST_TASK_RESULT_STATUS_REJECTED;
      g_test_task_ingress_last_result.stage = TEST_TASK_RESULT_STAGE_MOTION;
      break;
    default:
      g_test_task_ingress_last_result.error_code =
          TEST_TASK_RUNTIME_RECORD_BUFFER_ERROR;
      g_test_task_ingress_last_result.status =
          TEST_TASK_RESULT_STATUS_INTERNAL_ERROR;
      g_test_task_ingress_last_result.stage = TEST_TASK_RESULT_STAGE_RECORD;
      break;
  }
}

void TestTask_IngressInit(void)
{
    TestRecord_BufferInit(&g_test_task_ingress_records);
  TestTask_RuntimeInit();
  C620_MotionGate_Init();
  g_test_task_ingress_active_stop_count = 0UL;
  g_test_task_ingress_active_zero_frame_count = 0UL;
  g_test_task_ingress_active_task_valid = 0U;

  g_test_task_ingress_snapshot.last_result = TEST_TASK_INGRESS_IDLE;
  g_test_task_ingress_snapshot.last_runtime_result =
      TEST_TASK_RUNTIME_ACCEPTED;
  g_test_task_ingress_snapshot.last_schema_error = TEST_TASK_VALID;
  g_test_task_ingress_snapshot.service_count = 0UL;
  g_test_task_ingress_snapshot.no_task_count = 0UL;
  g_test_task_ingress_snapshot.processed_count = 0UL;
  g_test_task_ingress_snapshot.accepted_count = 0UL;
  g_test_task_ingress_snapshot.rejected_count = 0UL;
  g_test_task_ingress_snapshot.record_error_count = 0UL;
  g_test_task_ingress_snapshot.result_response_count = 0UL;
  g_test_task_ingress_snapshot.result_response_error_count = 0UL;
  g_test_task_ingress_snapshot.stop_response_queued_count = 0UL;
  g_test_task_ingress_snapshot.stop_response_error_count = 0UL;
  g_test_task_ingress_snapshot.status_query_count = 0UL;
  g_test_task_ingress_snapshot.status_response_error_count = 0UL;
  g_test_task_ingress_snapshot.last_request_id = 0UL;
  g_test_task_ingress_snapshot.last_service_ms = 0UL;
  g_test_task_ingress_snapshot.record_count = 0U;
  g_test_task_ingress_snapshot.service_tick_valid = 0U;
  g_test_task_ingress_snapshot.last_result_tx_status = TASK_UART_TX_IDLE;
  g_test_task_ingress_snapshot.last_stop_tx_status = TASK_UART_TX_IDLE;
}

TestTask_IngressResultTypeDef TestTask_IngressService(
    const volatile Safety_SnapshotTypeDef *safety,
    uint32_t now_ms)
{
  Safety_SnapshotTypeDef safety_copy;
  TestTask_RuntimeResultTypeDef runtime_result;
  uint32_t record_errors_before;
  uint32_t sent_frame_count_before;
  uint16_t record_count_before;
  uint8_t record_error;
  TaskUart_TxResultTypeDef tx_result;
  ControlStatus_QueryTypeDef status_query;
  ControlStatus_ReplyTypeDef status_reply;

  if (safety == (const volatile Safety_SnapshotTypeDef *)0)
  {
    g_test_task_ingress_snapshot.last_result =
        TEST_TASK_INGRESS_ERROR_NULL_SAFETY;
    return TEST_TASK_INGRESS_ERROR_NULL_SAFETY;
  }

  if ((g_test_task_ingress_snapshot.service_tick_valid != 0U) &&
      (g_test_task_ingress_snapshot.last_service_ms == now_ms))
  {
    return TEST_TASK_INGRESS_NO_TASK;
  }
  g_test_task_ingress_snapshot.service_tick_valid = 1U;
  g_test_task_ingress_snapshot.last_service_ms = now_ms;
  g_test_task_ingress_snapshot.service_count++;

  if (TaskUart_AdapterTakeStatusQuery(&status_query) != 0U)
  {
    TestTask_IngressCopySafety(&safety_copy, safety);
    status_reply.request_id = status_query.request_id;
    status_reply.nonce = status_query.nonce;
    status_reply.timestamp_ms = now_ms;
    status_reply.feature_mask = CONTROL_STATUS_FEATURE_QUERY;
    status_reply.safety_state = (uint8_t)safety_copy.state;
    status_reply.output_permitted = safety_copy.output_permitted;
    status_reply.zero_output_required = safety_copy.zero_output_required;
    status_reply.fault_latched = safety_copy.fault_latched;
    status_reply.motion_active = g_c620_motion_gate_snapshot.active;
    status_reply.record_count = TestRecord_Count(&g_test_task_ingress_records);
    status_reply.safety_fault_code = safety_copy.last_fault_code;
    g_test_task_ingress_snapshot.status_query_count++;
    if (TaskUart_AdapterQueueStatus(&status_reply) != TASK_UART_TX_QUEUED)
    {
      g_test_task_ingress_snapshot.status_response_error_count++;
    }
  }

  if (TaskUart_AdapterTakeTask(&g_test_task_ingress_last_task) == 0U)
  {
    g_test_task_ingress_snapshot.no_task_count++;
    return TEST_TASK_INGRESS_NO_TASK;
  }

  g_test_task_ingress_snapshot.processed_count++;
  g_test_task_ingress_snapshot.last_request_id =
      g_test_task_ingress_last_task.request_id;
  TestTask_IngressCopySafety(&safety_copy, safety);

  record_errors_before = g_test_task_runtime_snapshot.record_error_count;
  record_count_before = TestRecord_Count(&g_test_task_ingress_records);
  sent_frame_count_before = g_c620_motion_gate_snapshot.sent_frame_count;
  runtime_result = TestTask_RuntimeSubmit(
      &g_test_task_ingress_last_task,
      &safety_copy,
      &g_test_task_ingress_records,
      now_ms);
  g_test_task_ingress_snapshot.last_runtime_result = runtime_result;
  g_test_task_ingress_snapshot.last_schema_error =
      g_test_task_runtime_snapshot.last_schema_error;
  g_test_task_ingress_snapshot.record_count =
      TestRecord_Count(&g_test_task_ingress_records);

  record_error =
      (g_test_task_runtime_snapshot.record_error_count != record_errors_before) ?
      1U : 0U;
  if (record_error != 0U)
  {
    g_test_task_ingress_snapshot.record_error_count++;
    g_test_task_ingress_snapshot.last_result =
        TEST_TASK_INGRESS_ERROR_RECORD;
  }
    else if (runtime_result == TEST_TASK_RUNTIME_ACCEPTED)
  {
    if ((g_test_task_ingress_last_task.amplitude_raw != 0) &&
        (g_c620_motion_gate_snapshot.active != 0U))
    {
      g_test_task_ingress_active_task = g_test_task_ingress_last_task;
      g_test_task_ingress_active_stop_count =
          g_c620_motion_gate_snapshot.stop_count;
      g_test_task_ingress_active_zero_frame_count =
          g_c620_motion_gate_snapshot.zero_frame_count;
      g_test_task_ingress_active_task_valid = 1U;
    }
    g_test_task_ingress_snapshot.accepted_count++;
    g_test_task_ingress_snapshot.last_result =
        TEST_TASK_INGRESS_TASK_ACCEPTED;
  }
  else
  {
    g_test_task_ingress_snapshot.rejected_count++;
    g_test_task_ingress_snapshot.last_result =
        TEST_TASK_INGRESS_TASK_REJECTED;
  }

  TestTask_IngressBuildResult(
      runtime_result,
      &safety_copy,
      now_ms,
      record_count_before,
      sent_frame_count_before,
      record_error);
  tx_result = TaskUart_AdapterSendResult(&g_test_task_ingress_last_result);
  g_test_task_ingress_snapshot.last_result_tx_status = tx_result;
  if ((tx_result != TASK_UART_TX_STARTED) &&
      (tx_result != TASK_UART_TX_QUEUED))
  {
    g_test_task_ingress_snapshot.result_response_error_count++;
    g_test_task_ingress_snapshot.last_result =
        TEST_TASK_INGRESS_ERROR_RESULT_TX;
    return TEST_TASK_INGRESS_ERROR_RESULT_TX;
  }
  g_test_task_ingress_snapshot.result_response_count++;

  return g_test_task_ingress_snapshot.last_result;
}

uint8_t TestTask_IngressRecordMotionStop(
    const volatile Safety_SnapshotTypeDef *safety)
{
  TestRecord_RecordTypeDef record = {0};
  TestTask_StopDescriptorTypeDef stop;
  TaskUart_TxResultTypeDef tx_result;

  if ((safety == (const volatile Safety_SnapshotTypeDef *)0) ||
      (g_test_task_ingress_active_task_valid == 0U))
  {
    return 0U;
  }

  if ((g_c620_motion_gate_snapshot.active != 0U) ||
      (g_c620_motion_gate_snapshot.stop_count ==
       g_test_task_ingress_active_stop_count) ||
      (g_c620_motion_gate_snapshot.zero_frame_count ==
       g_test_task_ingress_active_zero_frame_count))
  {
    return 0U;
  }

  record.timestamp_ms = g_c620_motion_gate_snapshot.stop_tick_ms;
  record.request_id = g_test_task_ingress_active_task.request_id;
  record.event = TEST_RECORD_EVENT_MOTION_STOP;
  record.target_node_id =
      g_test_task_ingress_active_task.target_node_id;
  record.safety_state = (uint8_t)safety->state;
  record.link_state = (uint8_t)g_c620_feedback.link_state;
  record.command_amplitude_raw =
      g_test_task_ingress_active_task.amplitude_raw;
  record.feedback_speed_rpm = g_c620_feedback.speed_rpm;
  record.feedback_current_raw = g_c620_feedback.current_raw;
  record.feedback_encoder_raw = g_c620_feedback.encoder_raw;
  record.feedback_temperature_c = g_c620_feedback.temperature_c;
  record.stop_reason =
      (uint8_t)g_c620_motion_gate_snapshot.last_stop_reason;
  record.safety_fault_code = safety->last_fault_code;
  record.observation_abs_travel_raw =
      g_c620_motion_gate_snapshot.observation_abs_travel_raw;
  record.observation_peak_abs_speed_rpm =
      g_c620_motion_gate_snapshot.observation_peak_abs_speed_rpm;
  record.observation_sample_count =
      g_c620_motion_gate_snapshot.observation_sample_count;

  if (TestRecord_AppendReserved(
          &g_test_task_ingress_records, &record) !=
      TEST_RECORD_APPEND_OK)
  {
    g_test_task_ingress_snapshot.record_error_count++;
    return 0U;
  }

  g_test_task_ingress_snapshot.record_count =
      TestRecord_Count(&g_test_task_ingress_records);
  g_test_task_ingress_active_task_valid = 0U;

  stop.request_id = record.request_id;
  stop.timestamp_ms = record.timestamp_ms;
  stop.command_amplitude_raw = record.command_amplitude_raw;
  stop.feedback_speed_rpm = record.feedback_speed_rpm;
  stop.feedback_current_raw = record.feedback_current_raw;
  stop.feedback_encoder_raw = record.feedback_encoder_raw;
  stop.feedback_temperature_c = record.feedback_temperature_c;
  stop.stop_reason = record.stop_reason;
  stop.safety_state = record.safety_state;
  stop.link_state = record.link_state;
  stop.safety_fault_code = record.safety_fault_code;
  stop.observation_abs_travel_raw = record.observation_abs_travel_raw;
  stop.observation_peak_abs_speed_rpm =
      record.observation_peak_abs_speed_rpm;
  stop.observation_sample_count = record.observation_sample_count;
  tx_result = TaskUart_AdapterQueueStop(&stop);
  g_test_task_ingress_snapshot.last_stop_tx_status = tx_result;
  if (tx_result == TASK_UART_TX_QUEUED)
  {
    g_test_task_ingress_snapshot.stop_response_queued_count++;
  }
  else
  {
    g_test_task_ingress_snapshot.stop_response_error_count++;
  }
  return 1U;
}
