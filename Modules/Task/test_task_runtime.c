/**
  ******************************************************************************
  * @file    test_task_runtime.c
  * @brief   Runtime task admission and audit recording.
  ******************************************************************************
  */

#include "test_task_runtime.h"

volatile TestTask_RuntimeSnapshotTypeDef g_test_task_runtime_snapshot;
static TestTask_AuthorizeFn g_authorize;
static TestTask_AdmissionDoneFn g_admission_done;

void TestTask_RuntimeBindAuthorization(TestTask_AuthorizeFn authorize,
    TestTask_AdmissionDoneFn done)
{ g_authorize = authorize; g_admission_done = done; }

static uint8_t TestTask_RuntimeRecord(
    const TestTask_DescriptorTypeDef *task,
    const Safety_SnapshotTypeDef *safety,
    TestRecord_BufferTypeDef *records,
    uint32_t now_ms,
    TestRecord_EventTypeDef event,
    uint8_t error_code,
    uint8_t use_reserved)
{
  TestRecord_RecordTypeDef record = {0};

  if (records == (TestRecord_BufferTypeDef *)0)
  {
    g_test_task_runtime_snapshot.record_error_count++;
    return 0U;
  }

  record.timestamp_ms = now_ms;
  record.request_id = (task != (const TestTask_DescriptorTypeDef *)0) ?
                      task->request_id : 0UL;
  record.event = event;
  record.target_node_id = (task != (const TestTask_DescriptorTypeDef *)0) ?
                          task->target_node_id : 0U;
  record.safety_state = (safety != (const Safety_SnapshotTypeDef *)0) ?
                        (uint8_t)safety->state : 0U;
  record.link_state = (uint8_t)g_c620_feedback.link_state;
  record.reserved = 0U;
  record.command_amplitude_raw = (task != (const TestTask_DescriptorTypeDef *)0) ?
                                 task->amplitude_raw : 0;
  record.feedback_speed_rpm = g_c620_feedback.speed_rpm;
  record.feedback_current_raw = g_c620_feedback.current_raw;
  record.feedback_encoder_raw = g_c620_feedback.encoder_raw;
  record.feedback_temperature_c = g_c620_feedback.temperature_c;
  record.error_code = error_code;

 if (((use_reserved != 0U) ?
     TestRecord_AppendReserved(records, &record) :
     TestRecord_Append(records, &record)) != TEST_RECORD_APPEND_OK)
  {
    g_test_task_runtime_snapshot.record_error_count++;
    return 0U;
  }
  return 1U;
}

void TestTask_RuntimeInit(void)
{
  g_authorize = 0;
  g_admission_done = 0;
  g_test_task_runtime_snapshot.last_result =
      TEST_TASK_RUNTIME_ACCEPTED;
  g_test_task_runtime_snapshot.last_schema_error = TEST_TASK_VALID;
  g_test_task_runtime_snapshot.last_motion_error = MOTION_GATE_ERROR_NONE;
  g_test_task_runtime_snapshot.accepted_count = 0UL;
  g_test_task_runtime_snapshot.rejected_count = 0UL;
  g_test_task_runtime_snapshot.record_error_count = 0UL;
  g_test_task_runtime_snapshot.last_request_id = 0UL;
}

TestTask_RuntimeResultTypeDef TestTask_RuntimeSubmit(
    const TestTask_DescriptorTypeDef *task,
    const Safety_SnapshotTypeDef *safety,
    TestRecord_BufferTypeDef *records,
    uint32_t now_ms)
{
  C620_MotionRequestTypeDef motion_request;
  TestTask_ValidationErrorTypeDef schema_error;
  HAL_StatusTypeDef motion_status;
  uint16_t required_slots;

  g_test_task_runtime_snapshot.last_request_id =
      (task != (const TestTask_DescriptorTypeDef *)0) ?
      task->request_id : 0UL;

  if ((task == (const TestTask_DescriptorTypeDef *)0) ||
      (safety == (const Safety_SnapshotTypeDef *)0) ||
      (records == (TestRecord_BufferTypeDef *)0))
  {
    g_test_task_runtime_snapshot.last_result =
        TEST_TASK_RUNTIME_ERROR_NULL_ARGUMENT;
    g_test_task_runtime_snapshot.rejected_count++;
    (void)TestTask_RuntimeRecord(
        task, safety, records, now_ms,
        TEST_RECORD_EVENT_TASK_REJECTED,
        TEST_TASK_RUNTIME_RECORD_BUFFER_ERROR, 0U);
    return TEST_TASK_RUNTIME_ERROR_NULL_ARGUMENT;
  }

  schema_error = TestTask_Validate(task);
  g_test_task_runtime_snapshot.last_schema_error = schema_error;
  if (schema_error != TEST_TASK_VALID)
  {
    g_test_task_runtime_snapshot.last_result =
        TEST_TASK_RUNTIME_ERROR_SCHEMA;
    g_test_task_runtime_snapshot.rejected_count++;
    (void)TestTask_RuntimeRecord(
        task, safety, records, now_ms,
        TEST_RECORD_EVENT_TASK_REJECTED,
        TEST_TASK_RUNTIME_RECORD_SCHEMA_ERROR, 0U);
    return TEST_TASK_RUNTIME_ERROR_SCHEMA;
  }

  if ((safety->state != SAFETY_STATE_ONLINE) ||
      (safety->fault_latched != 0U) ||
      (safety->estop_active != 0U) ||
      (safety->output_permitted == 0U) ||
      (safety->zero_output_required != 0U) ||
      (Safety_StateMachine_IsNonzeroOutputAllowed() == 0U))
  {
    g_test_task_runtime_snapshot.last_result =
        TEST_TASK_RUNTIME_ERROR_SAFETY;
    g_test_task_runtime_snapshot.rejected_count++;
    (void)TestTask_RuntimeRecord(
        task, safety, records, now_ms,
        TEST_RECORD_EVENT_TASK_REJECTED,
        TEST_TASK_RUNTIME_RECORD_SAFETY_ERROR, 0U);
    return TEST_TASK_RUNTIME_ERROR_SAFETY;
  }

  if ((task->amplitude_raw != 0) && (g_authorize != 0) &&
      (g_authorize(task, now_ms) == 0U))
  {
    g_test_task_runtime_snapshot.last_result = TEST_TASK_RUNTIME_ERROR_SAFETY;
    g_test_task_runtime_snapshot.rejected_count++;
    (void)TestTask_RuntimeRecord(task, safety, records, now_ms,
        TEST_RECORD_EVENT_TASK_REJECTED, TEST_TASK_RUNTIME_RECORD_SAFETY_ERROR, 0U);
    return TEST_TASK_RUNTIME_ERROR_SAFETY;
  }

  required_slots = (task->amplitude_raw == 0) ? 1U : 2U;
  if (TestRecord_Reserve(records, required_slots) == 0U)
  {
    if ((task->amplitude_raw != 0) && (g_admission_done != 0)) { g_admission_done(0U, now_ms); }
    g_test_task_runtime_snapshot.record_error_count++;
    g_test_task_runtime_snapshot.rejected_count++;
    g_test_task_runtime_snapshot.last_result =
        TEST_TASK_RUNTIME_ERROR_RECORD_FULL;

    if (((uint32_t)records->count + records->reserved_count) <
        TEST_RECORD_BUFFER_CAPACITY)
    {
      (void)TestTask_RuntimeRecord(
          task, safety, records, now_ms,
          TEST_RECORD_EVENT_TASK_REJECTED,
          TEST_TASK_RUNTIME_RECORD_BUFFER_ERROR, 0U);
    }
    return TEST_TASK_RUNTIME_ERROR_RECORD_FULL;
  }

  motion_request.current_raw = task->amplitude_raw;
  motion_request.duration_ms = task->duration_ms;
  motion_request.start_tick_ms = now_ms;
  motion_request.request_id = task->request_id;
  motion_request.requested = 1U;

  motion_status = C620_MotionGate_Service(&motion_request, safety);
  g_test_task_runtime_snapshot.last_motion_error =
      g_c620_motion_gate_snapshot.last_error;

  if (motion_status != HAL_OK)
  {
    if ((task->amplitude_raw != 0) && (g_admission_done != 0)) { g_admission_done(0U, now_ms); }
    TestRecord_Release(records, required_slots);
    g_test_task_runtime_snapshot.last_result =
        TEST_TASK_RUNTIME_ERROR_MOTION_GATE;
    g_test_task_runtime_snapshot.rejected_count++;
    (void)TestTask_RuntimeRecord(
        task, safety, records, now_ms,
        TEST_RECORD_EVENT_TASK_REJECTED,
        TEST_TASK_RUNTIME_RECORD_MOTION_ERROR, 0U);
    return TEST_TASK_RUNTIME_ERROR_MOTION_GATE;
  }

  if (TestTask_RuntimeRecord(
          task, safety, records, now_ms,
          TEST_RECORD_EVENT_TASK_ACCEPTED, 0U, 1U) == 0U)
  {
    (void)C620_MotionGate_ForceZero(now_ms);
    if ((task->amplitude_raw != 0) && (g_admission_done != 0)) { g_admission_done(0U, now_ms); }
    TestRecord_Release(records, required_slots);
    g_test_task_runtime_snapshot.last_result =
        TEST_TASK_RUNTIME_ERROR_RECORD_FULL;
    g_test_task_runtime_snapshot.rejected_count++;
    return TEST_TASK_RUNTIME_ERROR_RECORD_FULL;
  }

  g_test_task_runtime_snapshot.last_result = TEST_TASK_RUNTIME_ACCEPTED;
  if ((task->amplitude_raw != 0) && (g_admission_done != 0)) { g_admission_done(1U, now_ms); }
  g_test_task_runtime_snapshot.accepted_count++;
  return TEST_TASK_RUNTIME_ACCEPTED;
}
