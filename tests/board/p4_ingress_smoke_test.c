/**
  ******************************************************************************
  * @file    p4_ingress_smoke_test.c
  * @brief   Board-side software-injection fixture for the P4 task ingress.
  ******************************************************************************
  */

#include "p4_ingress_smoke_test.h"

volatile P4_IngressSmokeSnapshotTypeDef g_p4_ingress_smoke_snapshot;
TestRecord_BufferTypeDef g_p4_ingress_smoke_records;
TestTask_ReceiverTypeDef g_p4_ingress_smoke_receiver;

static void P4_IngressSmokeFail(P4_IngressSmokeErrorTypeDef error)
{
  g_p4_ingress_smoke_snapshot.state = P4_INGRESS_SMOKE_FAILED;
  g_p4_ingress_smoke_snapshot.error = error;
}

uint8_t P4_IngressSmokeTest_Run(
    const volatile Safety_SnapshotTypeDef *safety,
    uint32_t now_ms)
{
  TestTask_DescriptorTypeDef source_task;
  TestTask_DescriptorTypeDef decoded_task;
  Safety_SnapshotTypeDef safety_copy;
  const TestRecord_RecordTypeDef *record;
  uint8_t frame[TEST_TASK_WIRE_FRAME_LENGTH];
  uint16_t frame_length = 0U;
  uint16_t index;

  g_p4_ingress_smoke_snapshot.state = P4_INGRESS_SMOKE_RUNNING;
  g_p4_ingress_smoke_snapshot.error = P4_INGRESS_SMOKE_ERROR_NONE;
  g_p4_ingress_smoke_snapshot.step = 1UL;
  g_p4_ingress_smoke_snapshot.request_id = 0UL;
  g_p4_ingress_smoke_snapshot.frame_length = 0U;
  g_p4_ingress_smoke_snapshot.record_count = 0U;
  g_p4_ingress_smoke_snapshot.record_error_code = 0U;
  g_p4_ingress_smoke_snapshot.wire_error = TEST_TASK_WIRE_OK;
  g_p4_ingress_smoke_snapshot.receiver_result =
      TEST_TASK_RECEIVER_NO_EVENT;
  g_p4_ingress_smoke_snapshot.runtime_result =
      TEST_TASK_RUNTIME_ACCEPTED;
  g_p4_ingress_smoke_snapshot.sent_frame_count_before =
      g_c620_motion_gate_snapshot.sent_frame_count;
  g_p4_ingress_smoke_snapshot.sent_frame_count_after =
      g_c620_motion_gate_snapshot.sent_frame_count;

  if (safety == (const volatile Safety_SnapshotTypeDef *)0)
  {
    P4_IngressSmokeFail(P4_INGRESS_SMOKE_ERROR_NULL_SAFETY);
    return 0U;
  }

  source_task.schema_version = TEST_TASK_SCHEMA_VERSION;
  source_task.request_id = 1001UL;
  source_task.target_node_id = TEST_TASK_TARGET_C620_ID;
  source_task.action = TEST_TASK_ACTION_CURRENT_TEST;
  source_task.amplitude_raw = 0;
  source_task.amplitude_limit_raw = 100;
  source_task.duration_ms = 100UL;
  source_task.feedback_timeout_ms = 100UL;
  source_task.timeout_policy = TEST_TASK_TIMEOUT_STOP;
  source_task.permission = TEST_TASK_PERMISSION_OPERATOR;

  g_p4_ingress_smoke_snapshot.step = 2UL;
  g_p4_ingress_smoke_snapshot.wire_error = TestTask_WireEncode(
      &source_task,
      frame,
      sizeof(frame),
      &frame_length);
  g_p4_ingress_smoke_snapshot.frame_length = frame_length;
  if (g_p4_ingress_smoke_snapshot.wire_error != TEST_TASK_WIRE_OK)
  {
    P4_IngressSmokeFail(P4_INGRESS_SMOKE_ERROR_ENCODE);
    return 0U;
  }

  TestTask_ReceiverInit(&g_p4_ingress_smoke_receiver, 100UL);
  g_p4_ingress_smoke_snapshot.step = 3UL;
  for (index = 0U; index < frame_length; index++)
  {
    g_p4_ingress_smoke_snapshot.receiver_result =
        TestTask_ReceiverPushByte(
            &g_p4_ingress_smoke_receiver,
            frame[index],
            now_ms + (uint32_t)index);
  }
  if (g_p4_ingress_smoke_snapshot.receiver_result !=
      TEST_TASK_RECEIVER_TASK_READY)
  {
    P4_IngressSmokeFail(P4_INGRESS_SMOKE_ERROR_RECEIVER);
    return 0U;
  }

  g_p4_ingress_smoke_snapshot.step = 4UL;
  if (TestTask_ReceiverTakeTask(
          &g_p4_ingress_smoke_receiver,
          &decoded_task) == 0U)
  {
    P4_IngressSmokeFail(P4_INGRESS_SMOKE_ERROR_TAKE_TASK);
    return 0U;
  }
  g_p4_ingress_smoke_snapshot.request_id = decoded_task.request_id;

  safety_copy.state = safety->state;
  safety_copy.output_permitted = safety->output_permitted;
  safety_copy.zero_output_required = safety->zero_output_required;
  safety_copy.fault_latched = safety->fault_latched;
  safety_copy.estop_active = safety->estop_active;
  safety_copy.transition_count = safety->transition_count;
  safety_copy.fault_count = safety->fault_count;
  safety_copy.recovery_valid_frames = safety->recovery_valid_frames;
  safety_copy.last_transition_ms = safety->last_transition_ms;
  safety_copy.last_fault_code = safety->last_fault_code;

  TestRecord_BufferInit(&g_p4_ingress_smoke_records);
  TestTask_RuntimeInit();
  g_p4_ingress_smoke_snapshot.step = 5UL;
  g_p4_ingress_smoke_snapshot.runtime_result = TestTask_RuntimeSubmit(
      &decoded_task,
      &safety_copy,
      &g_p4_ingress_smoke_records,
      now_ms + (uint32_t)frame_length);
  if (g_p4_ingress_smoke_snapshot.runtime_result !=
      TEST_TASK_RUNTIME_ERROR_SAFETY)
  {
    P4_IngressSmokeFail(P4_INGRESS_SMOKE_ERROR_RUNTIME_RESULT);
    return 0U;
  }

  g_p4_ingress_smoke_snapshot.step = 6UL;
  g_p4_ingress_smoke_snapshot.record_count =
      TestRecord_Count(&g_p4_ingress_smoke_records);
  if (g_p4_ingress_smoke_snapshot.record_count != 1U)
  {
    P4_IngressSmokeFail(P4_INGRESS_SMOKE_ERROR_RECORD_COUNT);
    return 0U;
  }

  record = TestRecord_Get(&g_p4_ingress_smoke_records, 0U);
  if (record != (const TestRecord_RecordTypeDef *)0)
  {
    g_p4_ingress_smoke_snapshot.record_error_code = record->error_code;
  }
  if ((record == (const TestRecord_RecordTypeDef *)0) ||
      (record->request_id != source_task.request_id) ||
      (record->event != TEST_RECORD_EVENT_TASK_REJECTED) ||
      (record->error_code != TEST_TASK_RUNTIME_RECORD_SAFETY_ERROR))
  {
    P4_IngressSmokeFail(P4_INGRESS_SMOKE_ERROR_RECORD_CONTENT);
    return 0U;
  }

  g_p4_ingress_smoke_snapshot.step = 7UL;
  g_p4_ingress_smoke_snapshot.sent_frame_count_after =
      g_c620_motion_gate_snapshot.sent_frame_count;
  if (g_p4_ingress_smoke_snapshot.sent_frame_count_after !=
      g_p4_ingress_smoke_snapshot.sent_frame_count_before)
  {
    P4_IngressSmokeFail(P4_INGRESS_SMOKE_ERROR_OUTPUT_CHANGED);
    return 0U;
  }

  g_p4_ingress_smoke_snapshot.step = 8UL;
  g_p4_ingress_smoke_snapshot.state = P4_INGRESS_SMOKE_PASSED;
  return 1U;
}
