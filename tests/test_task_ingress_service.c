#include <assert.h>
#include <stdio.h>

#include "test_task_ingress_service.h"

static uint8_t *g_fake_rx_target;
static uint8_t g_fake_tx_frame[TEST_TASK_STOP_FRAME_LENGTH];
static uint16_t g_fake_tx_length;
static uint32_t g_transport_calls;
static uint32_t g_transport_fail_on_call;

static uint8_t TestTransport(
    const uint8_t data[C620_CONTROL_DATA_LENGTH], void *context)
{
  (void)data;
  (void)context;
  g_transport_calls++;
  return (g_transport_calls == g_transport_fail_on_call) ?
         0U : C620_CONTROL_OUTPUT_TRANSPORT_OK;
}

static void QueueTask(const TestTask_DescriptorTypeDef *task,
                      UART_HandleTypeDef *uart)
{
  uint8_t frame[TEST_TASK_WIRE_FRAME_LENGTH];
  uint16_t length = 0U;
  uint16_t index;

  assert(TestTask_WireEncode(task, frame, sizeof(frame), &length) ==
         TEST_TASK_WIRE_OK);
  for (index = 0U; index < length; index++)
  {
    assert(g_fake_rx_target != (uint8_t *)0);
    *g_fake_rx_target = frame[index];
    (void)TaskUart_AdapterOnRxComplete(uart, (uint32_t)index);
  }
  assert(g_task_uart_adapter.receiver.task_pending != 0U);
}

static void PrepareMotionCase(UART_HandleTypeDef *uart)
{
  Safety_InputTypeDef input = {0};

  Safety_StateMachine_Init();
  TestTask_IngressInit();
  assert(TaskUart_AdapterInit(uart, 100U) == TASK_UART_ADAPTER_OK);
  assert(TaskUart_AdapterStart() == TASK_UART_ADAPTER_RX_ARMED);
  C620_MotionGate_BindTransport(TestTransport, (void *)0);
  g_transport_calls = 0U;
  g_transport_fail_on_call = 0U;
  g_c620_feedback.link_state = C620_LINK_ONLINE;
  input.init_ok = 1U;
  input.link_state = C620_LINK_ONLINE;
  input.valid_frame_count = 1U;
  Safety_StateMachine_Update(&input);
  input.valid_frame_count = 2U;
  Safety_StateMachine_Update(&input);
  g_safety_motion_arm_request = 1U;
  input.valid_frame_count = 3U;
  Safety_StateMachine_Update(&input);
  assert(Safety_StateMachine_IsNonzeroOutputAllowed() == 1U);
}

static void TestMotionStopRecord(void)
{
  TestTask_DescriptorTypeDef task = {
      TEST_TASK_SCHEMA_VERSION, 3001UL, TEST_TASK_TARGET_C620_ID,
      TEST_TASK_ACTION_CURRENT_TEST, 1000, 1000, 100UL, 100UL,
      TEST_TASK_TIMEOUT_STOP, TEST_TASK_PERMISSION_OPERATOR};
  UART_HandleTypeDef uart = {0};
  TestRecord_RecordTypeDef filler = {0};
  const TestRecord_RecordTypeDef *record;
  TestTask_StopDescriptorTypeDef stop_decoded;
  uint16_t index;

  PrepareMotionCase(&uart);
  filler.event = TEST_RECORD_EVENT_FAULT;
  for (index = 0U; index < 62U; index++)
  {
    assert(TestRecord_Append(&g_test_task_ingress_records, &filler) ==
           TEST_RECORD_APPEND_OK);
  }
  QueueTask(&task, &uart);
  assert(TestTask_IngressService(&g_safety_snapshot, 1000U) ==
         TEST_TASK_INGRESS_TASK_ACCEPTED);
  assert(g_transport_calls == 1U);
  assert(g_test_task_ingress_records.count == 63U);
  assert(g_test_task_ingress_records.reserved_count == 1U);
  assert(g_test_task_ingress_records.records[62].event ==
         TEST_RECORD_EVENT_TASK_ACCEPTED);

  g_c620_feedback.has_valid_feedback = 1U;
  g_c620_feedback.valid_frame_count = 4U;
  g_c620_feedback.encoder_raw = 6U;
  g_c620_feedback.speed_rpm = 45;
  C620_MotionGate_Update(&g_safety_snapshot, 1100U);
  assert(g_c620_motion_gate_snapshot.last_stop_reason ==
         C620_MOTION_STOP_DEADLINE);
  assert(TestTask_IngressRecordMotionStop(&g_safety_snapshot) == 1U);
  assert(TestTask_IngressRecordMotionStop(&g_safety_snapshot) == 0U);
  assert(g_transport_calls == 2U);
  assert(g_test_task_ingress_records.count == 64U);
  assert(g_test_task_ingress_records.reserved_count == 0U);
  record = TestRecord_Get(&g_test_task_ingress_records, 63U);
  assert(record != (const TestRecord_RecordTypeDef *)0);
  assert(record->event == TEST_RECORD_EVENT_MOTION_STOP);
  assert(record->request_id == 3001UL);
  assert(record->timestamp_ms == 1100U);
  assert(record->stop_reason == C620_MOTION_STOP_DEADLINE);
  assert(record->observation_sample_count == 1U);
  assert(record->observation_peak_abs_speed_rpm == 45U);
  assert(g_test_task_ingress_snapshot.record_count == 64U);
  assert(g_test_task_ingress_snapshot.stop_response_queued_count == 1UL);
  assert(g_task_uart_adapter.stop_pending == 1U);
  assert(TaskUart_AdapterOnTxComplete(&uart) == TASK_UART_TX_COMPLETE);
  assert(TaskUart_AdapterService(1101U) == TASK_UART_ADAPTER_OK);
  assert(g_fake_tx_length == TEST_TASK_STOP_FRAME_LENGTH);
  assert(TestTask_StopWireDecode(g_fake_tx_frame, g_fake_tx_length,
                                 &stop_decoded) == TEST_TASK_RESULT_WIRE_OK);
  assert(stop_decoded.request_id == 3001UL);
  assert(stop_decoded.timestamp_ms == 1100U);
  assert(stop_decoded.feedback_speed_rpm == 45);
  assert(stop_decoded.observation_sample_count == 1UL);
  assert(TaskUart_AdapterOnTxComplete(&uart) == TASK_UART_TX_COMPLETE);
  assert(g_task_uart_adapter.snapshot.stop_tx_complete_count == 1UL);
}

static void TestSafetyStopAfterFailedZero(void)
{
  TestTask_DescriptorTypeDef task = {
      TEST_TASK_SCHEMA_VERSION, 3002UL, TEST_TASK_TARGET_C620_ID,
      TEST_TASK_ACTION_CURRENT_TEST, 1000, 1000, 100UL, 100UL,
      TEST_TASK_TIMEOUT_STOP, TEST_TASK_PERMISSION_OPERATOR};
  UART_HandleTypeDef uart = {0};
  const TestRecord_RecordTypeDef *record;

  PrepareMotionCase(&uart);
  QueueTask(&task, &uart);
  assert(TestTask_IngressService(&g_safety_snapshot, 2000U) ==
         TEST_TASK_INGRESS_TASK_ACCEPTED);
  g_transport_fail_on_call = 2U;
  g_safety_snapshot.state = SAFETY_STATE_ESTOP;
  g_safety_snapshot.estop_active = 1U;
  g_safety_snapshot.output_permitted = 0U;
  g_safety_snapshot.zero_output_required = 1U;
  g_safety_snapshot.last_fault_code = SAFETY_FAULT_ESTOP;
  C620_MotionGate_Update(&g_safety_snapshot, 2020U);
  assert(g_c620_motion_gate_snapshot.active == 1U);
  assert(TestTask_IngressRecordMotionStop(&g_safety_snapshot) == 0U);
  assert(g_test_task_ingress_records.count == 1U);
  assert(g_test_task_ingress_records.reserved_count == 1U);

  g_transport_fail_on_call = 0U;
  C620_MotionGate_Update(&g_safety_snapshot, 2021U);
  assert(TestTask_IngressRecordMotionStop(&g_safety_snapshot) == 1U);
  assert(TestTask_IngressRecordMotionStop(&g_safety_snapshot) == 0U);
  assert(g_test_task_ingress_records.count == 2U);
  assert(g_test_task_ingress_records.reserved_count == 0U);
  record = TestRecord_Get(&g_test_task_ingress_records, 1U);
  assert(record->request_id == 3002UL);
  assert(record->timestamp_ms == 2021U);
  assert(record->stop_reason == C620_MOTION_STOP_SAFETY);
  assert(record->safety_fault_code == SAFETY_FAULT_ESTOP);
}

static void TestStatusQueryNeverRunsMotion(void)
{
  static const uint8_t query_frame[CONTROL_STATUS_QUERY_LENGTH] = {
      0x4b, 0x54, 0x01, 0x04, 0x16, 0x00, 0x78, 0x56,
      0x34, 0x12, 0xf0, 0xde, 0xbc, 0x9a, 0x01, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0xb6, 0x7e
  };
  UART_HandleTypeDef uart = {0};
  Safety_SnapshotTypeDef safety = {0};
  ControlStatus_ReplyTypeDef decoded;
  uint16_t index;
  Safety_StateMachine_Init();
  TestTask_IngressInit();
  g_transport_calls = 0U;
  safety.state = SAFETY_STATE_WAIT_FEEDBACK;
  safety.zero_output_required = 1U;
  safety.last_fault_code = SAFETY_FAULT_FEEDBACK_TIMEOUT;
  assert(TaskUart_AdapterInit(&uart, 100U) == TASK_UART_ADAPTER_OK);
  assert(TaskUart_AdapterStart() == TASK_UART_ADAPTER_RX_ARMED);
  for (index = 0U; index < sizeof(query_frame); index++)
  {
    *g_fake_rx_target = query_frame[index];
    (void)TaskUart_AdapterOnRxComplete(&uart, index);
  }
  assert(TestTask_IngressService(&safety, 100U) == TEST_TASK_INGRESS_NO_TASK);
  assert(g_test_task_ingress_snapshot.status_query_count == 1UL);
  assert(g_test_task_ingress_snapshot.processed_count == 0UL);
  assert(g_test_task_ingress_records.count == 0U);
  assert(g_transport_calls == 0UL);
  assert(TaskUart_AdapterService(101U) == TASK_UART_ADAPTER_OK);
  assert(ControlStatus_ReplyDecode(g_fake_tx_frame, g_fake_tx_length,
                                   &decoded) == CONTROL_STATUS_WIRE_OK);
  assert(decoded.request_id == 0x12345678UL);
  assert(decoded.nonce == 0x9ABCDEF0UL);
  assert(decoded.safety_state == SAFETY_STATE_WAIT_FEEDBACK);
  assert(decoded.output_permitted == 0U);
  assert(decoded.motion_active == 0U);
  assert(decoded.record_count == 0U);
  assert(decoded.safety_fault_code == SAFETY_FAULT_FEEDBACK_TIMEOUT);
}

HAL_StatusTypeDef HAL_UART_Receive_IT(
    UART_HandleTypeDef *huart,
    uint8_t *data,
    uint16_t size)
{
  (void)huart;
  if (size != 1U)
  {
    return HAL_ERROR;
  }
  g_fake_rx_target = data;
  return HAL_OK;
}

uint32_t HAL_UART_GetError(const UART_HandleTypeDef *huart)
{
  return huart->ErrorCode;
}

HAL_StatusTypeDef HAL_UART_Transmit_IT(
    UART_HandleTypeDef *huart,
    const uint8_t *data,
    uint16_t size)
{
  uint16_t index;

  (void)huart;
  g_fake_tx_length = size;
  for (index = 0U; index < size; index++)
  {
    g_fake_tx_frame[index] = data[index];
  }
  return HAL_OK;
}

int main(void)
{
  const TestTask_DescriptorTypeDef source = {
      TEST_TASK_SCHEMA_VERSION,
      2001UL,
      TEST_TASK_TARGET_C620_ID,
      TEST_TASK_ACTION_CURRENT_TEST,
      0,
      100,
      100UL,
      100UL,
      TEST_TASK_TIMEOUT_STOP,
      TEST_TASK_PERMISSION_OPERATOR};
  UART_HandleTypeDef uart = {0};
  Safety_SnapshotTypeDef safety = {0};
  uint8_t frame[TEST_TASK_WIRE_FRAME_LENGTH];
  uint16_t frame_length = 0U;
  uint16_t index;
  TestTask_ResultDescriptorTypeDef result;
  TestTask_DescriptorTypeDef full_task;
  TestRecord_RecordTypeDef filler = {0};

  C620_MotionGate_Init();
  TestTask_IngressInit();
  if ((TaskUart_AdapterInit(&uart, 100UL) != TASK_UART_ADAPTER_OK) ||
      (TaskUart_AdapterStart() != TASK_UART_ADAPTER_RX_ARMED))
  {
    return 1;
  }
  if (TestTask_WireEncode(&source, frame, sizeof(frame), &frame_length) !=
      TEST_TASK_WIRE_OK)
  {
    return 2;
  }

  for (index = 0U; index < frame_length; index++)
  {
    *g_fake_rx_target = frame[index];
    (void)TaskUart_AdapterOnRxComplete(&uart, (uint32_t)index);
  }
  if (g_task_uart_adapter.receiver.task_pending == 0U)
  {
    return 3;
  }

  safety.state = SAFETY_STATE_INIT;
  safety.output_permitted = 0U;
  safety.zero_output_required = 1U;
  if (TestTask_IngressService(&safety, 100UL) !=
      TEST_TASK_INGRESS_TASK_REJECTED)
  {
    return 4;
  }
  if ((g_test_task_ingress_snapshot.processed_count != 1UL) ||
      (g_test_task_ingress_snapshot.rejected_count != 1UL) ||
      (g_test_task_ingress_snapshot.last_request_id != 2001UL) ||
      (g_test_task_ingress_snapshot.last_runtime_result !=
       TEST_TASK_RUNTIME_ERROR_SAFETY) ||
      (g_test_task_ingress_snapshot.record_count != 1U) ||
      (g_test_task_ingress_snapshot.result_response_count != 1UL) ||
      (g_test_task_ingress_snapshot.result_response_error_count != 0UL) ||
      (g_test_task_ingress_records.records[0].error_code != 0xE2U) ||
      (g_c620_motion_gate_snapshot.sent_frame_count != 0UL))
  {
    return 5;
  }
  if ((g_fake_tx_length != TEST_TASK_RESULT_FRAME_LENGTH) ||
      (TestTask_ResultWireDecode(
          g_fake_tx_frame,
          g_fake_tx_length,
          &result) != TEST_TASK_RESULT_WIRE_OK) ||
      (result.request_id != 2001UL) ||
      (result.error_code != 0x00E2U) ||
      (result.record_count != 1U) ||
      (result.status != TEST_TASK_RESULT_STATUS_REJECTED) ||
      (result.stage != TEST_TASK_RESULT_STAGE_SAFETY) ||
      (result.flags != (TEST_TASK_RESULT_FLAG_RECORD_APPENDED |
                        TEST_TASK_RESULT_FLAG_OUTPUT_UNCHANGED)))
  {
    return 6;
  }
  if (TaskUart_AdapterOnTxComplete(&uart) != TASK_UART_TX_COMPLETE)
  {
    return 7;
  }

  if (TestTask_IngressService(&safety, 101UL) !=
      TEST_TASK_INGRESS_NO_TASK)
  {
    return 8;
  }
  if ((g_test_task_ingress_snapshot.no_task_count != 1UL) ||
      (g_test_task_ingress_snapshot.service_count != 2UL) ||
      (g_test_task_ingress_snapshot.last_result !=
       TEST_TASK_INGRESS_TASK_REJECTED))
  {
    return 9;
  }

  if (TestTask_IngressService(&safety, 101UL) !=
      TEST_TASK_INGRESS_NO_TASK)
  {
    return 10;
  }
  if ((g_test_task_ingress_snapshot.no_task_count != 1UL) ||
      (g_test_task_ingress_snapshot.service_count != 2UL))
  {
    return 11;
  }

  filler.event = TEST_RECORD_EVENT_FAULT;
  while (TestRecord_Count(&g_test_task_ingress_records) <
         TEST_RECORD_BUFFER_CAPACITY)
  {
    if (TestRecord_Append(&g_test_task_ingress_records, &filler) !=
        TEST_RECORD_APPEND_OK)
    {
      return 12;
    }
  }

  full_task = source;
  full_task.request_id = 2002UL;
  if (TestTask_WireEncode(
          &full_task, frame, sizeof(frame), &frame_length) !=
      TEST_TASK_WIRE_OK)
  {
    return 13;
  }
  for (index = 0U; index < frame_length; index++)
  {
    *g_fake_rx_target = frame[index];
    (void)TaskUart_AdapterOnRxComplete(&uart, 200UL + (uint32_t)index);
  }
  if (TestTask_IngressService(&safety, 300UL) !=
      TEST_TASK_INGRESS_ERROR_RECORD)
  {
    return 14;
  }
  if ((TestTask_ResultWireDecode(
          g_fake_tx_frame,
          g_fake_tx_length,
          &result) != TEST_TASK_RESULT_WIRE_OK) ||
      (result.request_id != 2002UL) ||
      (result.status != TEST_TASK_RESULT_STATUS_INTERNAL_ERROR) ||
      (result.stage != TEST_TASK_RESULT_STAGE_RECORD) ||
      (result.error_code != TEST_TASK_RUNTIME_RECORD_BUFFER_ERROR) ||
      (result.record_count != TEST_RECORD_BUFFER_CAPACITY) ||
      ((result.flags & TEST_TASK_RESULT_FLAG_RECORD_APPENDED) != 0U) ||
      ((result.flags & TEST_TASK_RESULT_FLAG_OUTPUT_UNCHANGED) == 0U))
  {
    return 15;
  }

  TestMotionStopRecord();
  TestSafetyStopAfterFailedZero();
  TestStatusQueryNeverRunsMotion();
  (void)printf("task_ingress_service_tests=pass\n");
  return 0;
}
