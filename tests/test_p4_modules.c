/**
  ******************************************************************************
  * @file    test_p4_modules.c
  * @brief   Host-side unit tests for the P4 task/runtime/record modules.
  *
  * This test never opens hardware and never sends CAN.  It verifies only the
  * software admission and append-only recording contracts.
  ******************************************************************************
  */

#include <assert.h>
#include <stdio.h>

#include "c620_feedback.h"
#include "c620_motion_gate.h"
#include "safety_state_machine.h"
#include "test_record_buffer.h"
#include "test_task_runtime.h"
#include "test_task_schema.h"

static TestTask_DescriptorTypeDef MakeValidTask(void)
{
  TestTask_DescriptorTypeDef task;

  task.schema_version = TEST_TASK_SCHEMA_VERSION;
  task.request_id = 1001UL;
  task.target_node_id = TEST_TASK_TARGET_C620_ID;
  task.action = TEST_TASK_ACTION_CURRENT_TEST;
  task.amplitude_raw = 0;
  task.amplitude_limit_raw = 100;
  task.duration_ms = 100UL;
  task.feedback_timeout_ms = 100UL;
  task.timeout_policy = TEST_TASK_TIMEOUT_STOP;
  task.permission = TEST_TASK_PERMISSION_OPERATOR;
  return task;
}

static Safety_SnapshotTypeDef MakeSafeSnapshot(void)
{
  Safety_SnapshotTypeDef safety;

  safety.state = SAFETY_STATE_SAFE;
  safety.output_permitted = 0U;
  safety.zero_output_required = 1U;
  safety.fault_latched = 1U;
  safety.estop_active = 0U;
  safety.transition_count = 0UL;
  safety.fault_count = 1UL;
  safety.recovery_valid_frames = 0UL;
  safety.last_transition_ms = 0UL;
  safety.last_fault_code = SAFETY_FAULT_FEEDBACK_TIMEOUT;
  return safety;
}

static void TestC620NodeId(void)
{
  const uint8_t feedback[8] = {0x10U, 0x34U, 0x00U, 0x00U,
                               0x00U, 0x00U, 0x1CU, 0x00U};

  assert(C620_NODE_ID == 1U);
  assert(C620_FEEDBACK_STD_ID == 0x201U);

  C620_Feedback_Init();
  C620_Feedback_ProcessCanFrame(0x206U,
                                CAN_ID_STD,
                                CAN_RTR_DATA,
                                8U,
                                feedback,
                                10U);
  assert(g_c620_feedback.valid_frame_count == 0U);
  assert(g_c620_feedback.ignored_frame_count == 1U);

  C620_Feedback_ProcessCanFrame(C620_FEEDBACK_STD_ID,
                                CAN_ID_STD,
                                CAN_RTR_DATA,
                                8U,
                                feedback,
                                20U);
  assert(g_c620_feedback.valid_frame_count == 1U);
  assert(g_c620_feedback.link_state == C620_LINK_ONLINE);
  assert(g_c620_feedback.encoder_raw == 0x1034U);
}

static void TestSchema(void)
{
  TestTask_DescriptorTypeDef task = MakeValidTask();

  assert(TestTask_Validate(&task) == TEST_TASK_VALID);

  task.schema_version = 2U;
  assert(TestTask_Validate(&task) == TEST_TASK_ERROR_SCHEMA_VERSION);

  task = MakeValidTask();
  task.request_id = 0UL;
  assert(TestTask_Validate(&task) == TEST_TASK_ERROR_REQUEST_ID);

  task = MakeValidTask();
  task.target_node_id = TEST_TASK_TARGET_C620_ID + 1U;
  assert(TestTask_Validate(&task) == TEST_TASK_ERROR_TARGET_NODE);

  task = MakeValidTask();
  task.action = TEST_TASK_ACTION_NONE;
  assert(TestTask_Validate(&task) == TEST_TASK_ERROR_ACTION);

  task = MakeValidTask();
  task.amplitude_raw = (int16_t)(TEST_TASK_MAX_AMPLITUDE_RAW + 1L);
  assert(TestTask_Validate(&task) == TEST_TASK_ERROR_AMPLITUDE_RANGE);

  task = MakeValidTask();
  task.amplitude_limit_raw = 0;
  assert(TestTask_Validate(&task) == TEST_TASK_ERROR_AMPLITUDE_LIMIT);

  task = MakeValidTask();
  task.amplitude_raw = 101;
  assert(TestTask_Validate(&task) == TEST_TASK_ERROR_AMPLITUDE_LIMIT);

  task = MakeValidTask();
  task.duration_ms = 0UL;
  assert(TestTask_Validate(&task) == TEST_TASK_ERROR_DURATION);

  task = MakeValidTask();
  task.duration_ms = TEST_TASK_MAX_DURATION_MS + 1UL;
  assert(TestTask_Validate(&task) == TEST_TASK_ERROR_DURATION);

  task = MakeValidTask();
  task.feedback_timeout_ms = TEST_TASK_MIN_FEEDBACK_TIMEOUT_MS - 1UL;
  assert(TestTask_Validate(&task) == TEST_TASK_ERROR_FEEDBACK_TIMEOUT);

  task = MakeValidTask();
  task.feedback_timeout_ms = TEST_TASK_MAX_FEEDBACK_TIMEOUT_MS + 1UL;
  assert(TestTask_Validate(&task) == TEST_TASK_ERROR_FEEDBACK_TIMEOUT);

  task = MakeValidTask();
  task.timeout_policy = TEST_TASK_TIMEOUT_REJECT;
  assert(TestTask_Validate(&task) == TEST_TASK_ERROR_TIMEOUT_POLICY);

  task = MakeValidTask();
  task.permission = TEST_TASK_PERMISSION_NONE;
  assert(TestTask_Validate(&task) == TEST_TASK_ERROR_PERMISSION);

  assert(TestTask_Validate((const TestTask_DescriptorTypeDef *)0) ==
         TEST_TASK_ERROR_NULL_ARGUMENT);
}

static void TestRecordBuffer(void)
{
  TestRecord_BufferTypeDef buffer;
  TestRecord_RecordTypeDef record = {0};
  uint16_t index;

  TestRecord_BufferInit(&buffer);
  record.request_id = 7UL;
  record.event = TEST_RECORD_EVENT_TASK_REJECTED;
  for (index = 0U; index < TEST_RECORD_BUFFER_CAPACITY; index++)
  {
    assert(TestRecord_Append(&buffer, &record) == TEST_RECORD_APPEND_OK);
    record.request_id++;
  }
  assert(TestRecord_Count(&buffer) == TEST_RECORD_BUFFER_CAPACITY);
  assert(TestRecord_Append(&buffer, &record) == TEST_RECORD_APPEND_ERROR_FULL);
  assert(buffer.dropped_count == 1UL);
  assert(TestRecord_Get(&buffer, 0U)->request_id == 7UL);
  assert(TestRecord_Get(&buffer, TEST_RECORD_BUFFER_CAPACITY) ==
         (const TestRecord_RecordTypeDef *)0);
}

static void TestRecordReservation(void)
{
  TestRecord_BufferTypeDef buffer;
  TestRecord_RecordTypeDef record = {0};
  uint16_t index;

  TestRecord_BufferInit(&buffer);

  /* 先放入 62 条普通记录，数组还剩两个位置。 */
  for (index = 0U; index < 62U; index++)
  {
    assert(TestRecord_Append(&buffer, &record) == TEST_RECORD_APPEND_OK);
  }

  /* 一次预留两个位置：接入记录一个，停止记录一个。 */
  assert(TestRecord_Reserve(&buffer, 2U) == 1U);
  assert(buffer.count == 62U);
  assert(buffer.reserved_count == 2U);

  /* 普通记录不能抢占这两个已预留的位置。 */
  assert(TestRecord_Append(&buffer, &record) ==
         TEST_RECORD_APPEND_ERROR_FULL);

  record.event = TEST_RECORD_EVENT_TASK_ACCEPTED;
  assert(TestRecord_AppendReserved(&buffer, &record) ==
         TEST_RECORD_APPEND_OK);
  assert(buffer.count == 63U);
  assert(buffer.reserved_count == 1U);

  /* 接入后，最后一个位置仍只属于未来的停止记录。 */
  assert(TestRecord_Append(&buffer, &record) ==
         TEST_RECORD_APPEND_ERROR_FULL);

  record.event = TEST_RECORD_EVENT_MOTION_STOP;
  assert(TestRecord_AppendReserved(&buffer, &record) ==
         TEST_RECORD_APPEND_OK);
  assert(buffer.count == 64U);
  assert(buffer.reserved_count == 0U);
  assert(TestRecord_Get(&buffer, 62U)->event ==
         TEST_RECORD_EVENT_TASK_ACCEPTED);
  assert(TestRecord_Get(&buffer, 63U)->event ==
         TEST_RECORD_EVENT_MOTION_STOP);

  /* 只有一个位置时，不允许承诺写入两条记录。 */
  TestRecord_BufferInit(&buffer);
  for (index = 0U; index < 63U; index++)
  {
    assert(TestRecord_Append(&buffer, &record) == TEST_RECORD_APPEND_OK);
  }
  assert(TestRecord_Reserve(&buffer, 2U) == 0U);
  assert(buffer.count == 63U);
  assert(buffer.reserved_count == 0U);
}

static void TestRuntimeSafetyRejection(void)
{
  TestTask_DescriptorTypeDef task = MakeValidTask();
  Safety_SnapshotTypeDef safety = MakeSafeSnapshot();
  TestRecord_BufferTypeDef records;

  TestTask_RuntimeInit();
  C620_MotionGate_Init();
  TestRecord_BufferInit(&records);

  assert(TestTask_RuntimeSubmit(&task, &safety, &records, 1234UL) ==
         TEST_TASK_RUNTIME_ERROR_SAFETY);
  assert(g_test_task_runtime_snapshot.rejected_count == 1UL);
  assert(records.count == 1U);
  assert(records.records[0].event == TEST_RECORD_EVENT_TASK_REJECTED);
  assert(records.records[0].error_code ==
         TEST_TASK_RUNTIME_RECORD_SAFETY_ERROR);
  assert(g_c620_motion_gate_snapshot.sent_frame_count == 0UL);
}

static void TestRuntimeSchemaRejection(void)
{
  TestTask_DescriptorTypeDef task = MakeValidTask();
  Safety_SnapshotTypeDef safety = MakeSafeSnapshot();
  TestRecord_BufferTypeDef records;

  task.request_id = 1002UL;
  task.permission = TEST_TASK_PERMISSION_NONE;
  TestRecord_BufferInit(&records);
  assert(TestTask_RuntimeSubmit(&task, &safety, &records, 2000UL) ==
         TEST_TASK_RUNTIME_ERROR_SCHEMA);
  assert(g_test_task_runtime_snapshot.last_schema_error ==
         TEST_TASK_ERROR_PERMISSION);
  assert(records.records[0].error_code ==
         TEST_TASK_RUNTIME_RECORD_SCHEMA_ERROR);
  assert(g_c620_motion_gate_snapshot.sent_frame_count == 0UL);
}

static uint32_t g_runtime_fake_send_count;

static uint8_t RuntimeFakeTransport(
    const uint8_t data[C620_CONTROL_DATA_LENGTH],
    void *context)
{
  (void)data;
  (void)context;
  g_runtime_fake_send_count++;
  return C620_CONTROL_OUTPUT_TRANSPORT_OK;
}

static Safety_SnapshotTypeDef MakeArmedOnlineSnapshot(void)
{
  Safety_InputTypeDef input = {0};
  Safety_SnapshotTypeDef safety = {0};

  Safety_StateMachine_Init();

  input.init_ok = 1U;
  input.link_state = C620_LINK_ONLINE;
  input.valid_frame_count = 1U;
  input.now_ms = 10U;
  Safety_StateMachine_Update(&input);

  input.valid_frame_count = 2U;
  input.now_ms = 11U;
  Safety_StateMachine_Update(&input);

  g_safety_motion_arm_request = 1U;
  input.valid_frame_count = 3U;
  input.now_ms = 12U;
  Safety_StateMachine_Update(&input);

  assert(Safety_StateMachine_IsNonzeroOutputAllowed() == 1U);

  safety.state = g_safety_snapshot.state;
  safety.output_permitted = g_safety_snapshot.output_permitted;
  safety.zero_output_required = g_safety_snapshot.zero_output_required;
  safety.fault_latched = g_safety_snapshot.fault_latched;
  safety.estop_active = g_safety_snapshot.estop_active;
  return safety;
}

static void TestRuntimeCapacityBeforeOutput(void)
{
  TestTask_DescriptorTypeDef task = MakeValidTask();
  Safety_SnapshotTypeDef safety;
  TestRecord_BufferTypeDef records;
  TestRecord_RecordTypeDef filler = {0};
  uint16_t index;

  TestTask_RuntimeInit();
  C620_MotionGate_Init();
  C620_MotionGate_BindTransport(RuntimeFakeTransport, (void *)0);
  safety = MakeArmedOnlineSnapshot();
  g_runtime_fake_send_count = 0U;

  task.amplitude_raw = 1000;
  task.amplitude_limit_raw = 1000;
  task.duration_ms = 1000UL;
  assert(TestTask_Validate(&task) == TEST_TASK_VALID);

  /* 只剩一个槽：必须在假发送函数被调用之前拒绝。 */
  TestRecord_BufferInit(&records);
  for (index = 0U; index < 63U; index++)
  {
    assert(TestRecord_Append(&records, &filler) ==
           TEST_RECORD_APPEND_OK);
  }

  assert(TestTask_RuntimeSubmit(&task, &safety, &records, 100U) ==
         TEST_TASK_RUNTIME_ERROR_RECORD_FULL);
  assert(g_runtime_fake_send_count == 0U);
  assert(g_c620_motion_gate_snapshot.active == 0U);
  assert(records.count == 64U);
  assert(records.records[63].event == TEST_RECORD_EVENT_TASK_REJECTED);
  assert(records.reserved_count == 0U);

  /* 已满：同样不能发帧，也不能凭空写入拒绝记录。 */
  TestRecord_BufferInit(&records);
  for (index = 0U; index < 64U; index++)
  {
    assert(TestRecord_Append(&records, &filler) ==
           TEST_RECORD_APPEND_OK);
  }

  task.request_id++;
  assert(TestTask_RuntimeSubmit(&task, &safety, &records, 101U) ==
         TEST_TASK_RUNTIME_ERROR_RECORD_FULL);
  assert(g_runtime_fake_send_count == 0U);
  assert(records.count == 64U);

  /* 还剩两个槽：可以接入，但必须为停止记录留下一个。 */
  TestRecord_BufferInit(&records);
  for (index = 0U; index < 62U; index++)
  {
    assert(TestRecord_Append(&records, &filler) ==
           TEST_RECORD_APPEND_OK);
  }

  task.request_id++;
  assert(TestTask_RuntimeSubmit(&task, &safety, &records, 102U) ==
         TEST_TASK_RUNTIME_ACCEPTED);
  assert(g_runtime_fake_send_count == 1U);
  assert(g_c620_motion_gate_snapshot.active == 1U);
  assert(records.count == 63U);
  assert(records.records[62].event == TEST_RECORD_EVENT_TASK_ACCEPTED);
  assert(records.reserved_count == 1U);
}

static void TestZeroTaskUsesOneRecordSlot(void)
{
  TestTask_DescriptorTypeDef task = MakeValidTask();
  Safety_SnapshotTypeDef safety;
  TestRecord_BufferTypeDef records;
  TestRecord_RecordTypeDef filler = {0};
  uint16_t index;

  TestTask_RuntimeInit();
  C620_MotionGate_Init();
  C620_MotionGate_BindTransport(RuntimeFakeTransport, (void *)0);
  safety = MakeArmedOnlineSnapshot();
  g_runtime_fake_send_count = 0U;
  task.amplitude_raw = 0;
  task.amplitude_limit_raw = 1000;

  TestRecord_BufferInit(&records);
  for (index = 0U; index < 63U; index++)
  {
    assert(TestRecord_Append(&records, &filler) == TEST_RECORD_APPEND_OK);
  }
  assert(TestTask_RuntimeSubmit(&task, &safety, &records, 200U) ==
         TEST_TASK_RUNTIME_ACCEPTED);
  assert(g_runtime_fake_send_count == 1U);
  assert(records.count == 64U);
  assert(records.reserved_count == 0U);
  assert(records.records[63].event == TEST_RECORD_EVENT_TASK_ACCEPTED);
  assert(g_c620_motion_gate_snapshot.active == 0U);
}

int main(void)
{
  TestC620NodeId();
  TestSchema();
  TestRecordBuffer();
  TestRecordReservation();
  TestRuntimeSafetyRejection();
  TestRuntimeSchemaRejection();
  TestRuntimeCapacityBeforeOutput();
  TestZeroTaskUsesOneRecordSlot();
  (void)printf("p4_host_tests=pass\n");
  return 0;
}
