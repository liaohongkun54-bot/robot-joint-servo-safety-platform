/**
  ******************************************************************************
  * @file    test_task_receiver.c
  * @brief   Byte-stream receiver for fixed structured-task frames.
  ******************************************************************************
  */

#include "test_task_receiver.h"

#define TEST_TASK_WIRE_MAGIC_BYTE_0 ((uint8_t)(TEST_TASK_WIRE_MAGIC & 0xFFU))
#define TEST_TASK_WIRE_MAGIC_BYTE_1 ((uint8_t)(TEST_TASK_WIRE_MAGIC >> 8U))

static void TestTask_ReceiverResetFrame(TestTask_ReceiverTypeDef *receiver)
{
  receiver->frame_index = 0U;
  receiver->frame_start_tick_ms = 0UL;
  receiver->receiving = 0U;
}

static TestTask_ReceiverResultTypeDef TestTask_ReceiverCheckTimeout(
    TestTask_ReceiverTypeDef *receiver,
    uint32_t now_ms)
{
  if ((receiver->receiving != 0U) &&
      ((uint32_t)(now_ms - receiver->frame_start_tick_ms) >=
       receiver->frame_timeout_ms))
  {
    uint8_t was_status_query =
        ((receiver->frame_index >= 4U) &&
         (receiver->frame[3] == CONTROL_STATUS_QUERY_TYPE)) ? 1U : 0U;
    uint8_t was_telemetry_query =
        ((receiver->frame_index >= 4U) &&
         (receiver->frame[3] == TELEMETRY_QUERY_TYPE)) ? 1U : 0U;
    uint8_t was_info_query =
        ((receiver->frame_index >= 4U) &&
         (receiver->frame[3] == DEVICE_INFO_QUERY_TYPE)) ? 1U : 0U;
    uint8_t was_operator = ((receiver->frame_index >= 4U) &&
         (receiver->frame[3] == OPERATOR_WIRE_TYPE)) ? 1U : 0U;
    TestTask_ReceiverResetFrame(receiver);
    receiver->snapshot.timeout_count++;
    receiver->snapshot.rejected_frame_count++;
    receiver->snapshot.last_wire_error = TEST_TASK_WIRE_ERROR_LENGTH;
    if (was_operator != 0U)
    {
      receiver->snapshot.operator_reject_count++;
      return TEST_TASK_RECEIVER_OPERATOR_REJECTED;
    }
    if (was_status_query != 0U)
    {
      receiver->snapshot.status_query_reject_count++;
      return TEST_TASK_RECEIVER_STATUS_QUERY_REJECTED;
    }
    if (was_telemetry_query != 0U)
    {
      receiver->snapshot.telemetry_query_reject_count++;
      return TEST_TASK_RECEIVER_TELEMETRY_QUERY_REJECTED;
    }
    if (was_info_query != 0U)
    {
      receiver->snapshot.info_query_reject_count++;
      return TEST_TASK_RECEIVER_INFO_QUERY_REJECTED;
    }
    return TEST_TASK_RECEIVER_FRAME_TIMEOUT;
  }
  return TEST_TASK_RECEIVER_NO_EVENT;
}

void TestTask_ReceiverInit(
    TestTask_ReceiverTypeDef *receiver,
    uint32_t timeout_ms)
{
  uint16_t index;

  if (receiver == (TestTask_ReceiverTypeDef *)0)
  {
    return;
  }

  for (index = 0U; index < sizeof(receiver->frame); index++)
  {
    receiver->frame[index] = 0U;
  }
  receiver->frame_index = 0U;
  receiver->frame_start_tick_ms = 0UL;
  receiver->frame_timeout_ms =
      (timeout_ms == 0UL) ? TEST_TASK_RECEIVER_DEFAULT_TIMEOUT_MS : timeout_ms;
  receiver->receiving = 0U;
  receiver->task_pending = 0U;
  receiver->status_query_pending = 0U;
  receiver->telemetry_query_pending = 0U;
  receiver->info_query_pending = 0U;
  receiver->operator_pending = 0U;
  receiver->operator_stop_pending = 0U;
  receiver->snapshot.operator_count = 0UL;
  receiver->snapshot.operator_reject_count = 0UL;
  receiver->snapshot.operator_drop_count = 0UL;

  receiver->snapshot.received_byte_count = 0UL;
  receiver->snapshot.accepted_frame_count = 0UL;
  receiver->snapshot.rejected_frame_count = 0UL;
  receiver->snapshot.timeout_count = 0UL;
  receiver->snapshot.pending_drop_count = 0UL;
  receiver->snapshot.last_frame_tick_ms = 0UL;
  receiver->snapshot.last_request_id = 0UL;
  receiver->snapshot.status_query_count = 0UL;
  receiver->snapshot.status_query_reject_count = 0UL;
  receiver->snapshot.status_query_drop_count = 0UL;
  receiver->snapshot.telemetry_query_count = 0UL;
  receiver->snapshot.telemetry_query_reject_count = 0UL;
  receiver->snapshot.telemetry_query_drop_count = 0UL;
  receiver->snapshot.info_query_count = 0UL;
  receiver->snapshot.info_query_reject_count = 0UL;
  receiver->snapshot.info_query_drop_count = 0UL;
  receiver->snapshot.last_wire_error = TEST_TASK_WIRE_OK;
  receiver->snapshot.last_schema_error = TEST_TASK_VALID;
}

TestTask_ReceiverResultTypeDef TestTask_ReceiverPushByte(
    TestTask_ReceiverTypeDef *receiver,
    uint8_t byte,
    uint32_t now_ms)
{
  TestTask_ReceiverResultTypeDef timeout_result;
  TestTask_DescriptorTypeDef decoded_task;
  TestTask_ValidationErrorTypeDef schema_error = TEST_TASK_VALID;
  TestTask_WireErrorTypeDef wire_error;
  ControlStatus_QueryTypeDef status_query;
  Telemetry_QueryTypeDef telemetry_query;
  DeviceInfo_QueryTypeDef info_query;

  if (receiver == (TestTask_ReceiverTypeDef *)0)
  {
    return TEST_TASK_RECEIVER_ERROR_NULL_ARGUMENT;
  }

  timeout_result = TestTask_ReceiverCheckTimeout(receiver, now_ms);
  receiver->snapshot.received_byte_count++;

  if (receiver->receiving == 0U)
  {
    if (byte == TEST_TASK_WIRE_MAGIC_BYTE_0)
    {
      receiver->frame[0] = byte;
      receiver->frame_index = 1U;
      receiver->frame_start_tick_ms = now_ms;
      receiver->receiving = 1U;
    }
    return timeout_result;
  }

  if (receiver->frame_index == 1U)
  {
    if (byte == TEST_TASK_WIRE_MAGIC_BYTE_1)
    {
      receiver->frame[1] = byte;
      receiver->frame_index = 2U;
    }
    else if (byte == TEST_TASK_WIRE_MAGIC_BYTE_0)
    {
      receiver->frame[0] = byte;
      receiver->frame_start_tick_ms = now_ms;
    }
    else
    {
      TestTask_ReceiverResetFrame(receiver);
    }
    return timeout_result;
  }

  receiver->frame[receiver->frame_index] = byte;
  receiver->frame_index++;
  if (receiver->frame_index < ((receiver->frame[3] == OPERATOR_WIRE_TYPE) ?
      OPERATOR_WIRE_LENGTH : TEST_TASK_WIRE_FRAME_LENGTH))
  {
    return timeout_result;
  }

  if (receiver->frame[3] == OPERATOR_WIRE_TYPE)
  {
    OperatorWire_Request request;
    OperatorWire_Result result = OperatorWire_RequestDecode(
        receiver->frame, OPERATOR_WIRE_LENGTH, &request);
    TestTask_ReceiverResetFrame(receiver);
    receiver->snapshot.last_frame_tick_ms = now_ms;
    if (result != OPERATOR_WIRE_OK)
    {
      receiver->snapshot.operator_reject_count++;
      receiver->snapshot.rejected_frame_count++;
      return TEST_TASK_RECEIVER_OPERATOR_REJECTED;
    }
    if ((request.operation == OPERATOR_STOP) || (request.operation == OPERATOR_CLOSE))
    {
      if (receiver->operator_stop_pending != 0U)
      {
        receiver->snapshot.operator_drop_count++;
        return TEST_TASK_RECEIVER_OPERATOR_REJECTED;
      }
      receiver->pending_operator_stop = request;
      receiver->operator_stop_pending = 1U;
    }
    else
    {
      if (receiver->operator_pending != 0U)
      {
        receiver->snapshot.operator_drop_count++;
        return TEST_TASK_RECEIVER_OPERATOR_REJECTED;
      }
      receiver->pending_operator = request;
      receiver->operator_pending = 1U;
    }
    receiver->snapshot.operator_count++;
    return TEST_TASK_RECEIVER_OPERATOR_READY;
  }

  if (receiver->frame[3] == CONTROL_STATUS_QUERY_TYPE)
  {
    ControlStatus_WireResultTypeDef status_error = ControlStatus_QueryDecode(
        receiver->frame, TEST_TASK_WIRE_FRAME_LENGTH, &status_query);
    TestTask_ReceiverResetFrame(receiver);
    receiver->snapshot.last_frame_tick_ms = now_ms;
    if (status_error != CONTROL_STATUS_WIRE_OK)
    {
      receiver->snapshot.status_query_reject_count++;
      receiver->snapshot.rejected_frame_count++;
      return TEST_TASK_RECEIVER_STATUS_QUERY_REJECTED;
    }
    if (receiver->status_query_pending != 0U)
    {
      receiver->snapshot.status_query_drop_count++;
      return TEST_TASK_RECEIVER_STATUS_QUERY_REJECTED;
    }
    receiver->pending_status_query = status_query;
    receiver->status_query_pending = 1U;
    receiver->snapshot.status_query_count++;
    return TEST_TASK_RECEIVER_STATUS_QUERY_READY;
  }

  if (receiver->frame[3] == TELEMETRY_QUERY_TYPE)
  {
    Telemetry_WireResultTypeDef telemetry_error = Telemetry_QueryDecode(
        receiver->frame, TEST_TASK_WIRE_FRAME_LENGTH, &telemetry_query);
    TestTask_ReceiverResetFrame(receiver);
    receiver->snapshot.last_frame_tick_ms = now_ms;
    if (telemetry_error != TELEMETRY_WIRE_OK)
    {
      receiver->snapshot.telemetry_query_reject_count++;
      receiver->snapshot.rejected_frame_count++;
      return TEST_TASK_RECEIVER_TELEMETRY_QUERY_REJECTED;
    }
    if (receiver->telemetry_query_pending != 0U)
    {
      receiver->snapshot.telemetry_query_drop_count++;
      return TEST_TASK_RECEIVER_TELEMETRY_QUERY_REJECTED;
    }
    receiver->pending_telemetry_query = telemetry_query;
    receiver->telemetry_query_pending = 1U;
    receiver->snapshot.telemetry_query_count++;
    return TEST_TASK_RECEIVER_TELEMETRY_QUERY_READY;
  }

  if (receiver->frame[3] == DEVICE_INFO_QUERY_TYPE)
  {
    DeviceInfo_WireResultTypeDef info_error = DeviceInfo_QueryDecode(
        receiver->frame, TEST_TASK_WIRE_FRAME_LENGTH, &info_query);
    TestTask_ReceiverResetFrame(receiver);
    receiver->snapshot.last_frame_tick_ms = now_ms;
    if (info_error != DEVICE_INFO_WIRE_OK)
    {
      receiver->snapshot.info_query_reject_count++;
      receiver->snapshot.rejected_frame_count++;
      return TEST_TASK_RECEIVER_INFO_QUERY_REJECTED;
    }
    if (receiver->info_query_pending != 0U)
    {
      receiver->snapshot.info_query_drop_count++;
      return TEST_TASK_RECEIVER_INFO_QUERY_REJECTED;
    }
    receiver->pending_info_query = info_query;
    receiver->info_query_pending = 1U;
    receiver->snapshot.info_query_count++;
    return TEST_TASK_RECEIVER_INFO_QUERY_READY;
  }

  wire_error = TestTask_WireDecode(
      receiver->frame,
      TEST_TASK_WIRE_FRAME_LENGTH,
      &decoded_task,
      &schema_error);
  TestTask_ReceiverResetFrame(receiver);
  receiver->snapshot.last_frame_tick_ms = now_ms;
  receiver->snapshot.last_wire_error = wire_error;
  receiver->snapshot.last_schema_error = schema_error;

  if (wire_error == TEST_TASK_WIRE_ERROR_TASK_SCHEMA)
  {
    /* Header and CRC passed, so the decoded request ID is transport-valid. */
    receiver->snapshot.last_request_id = decoded_task.request_id;
  }

  if (wire_error != TEST_TASK_WIRE_OK)
  {
    receiver->snapshot.rejected_frame_count++;
    return TEST_TASK_RECEIVER_FRAME_REJECTED;
  }

  if (receiver->task_pending != 0U)
  {
    receiver->snapshot.pending_drop_count++;
    return TEST_TASK_RECEIVER_TASK_PENDING;
  }

  receiver->pending_task = decoded_task;
  receiver->task_pending = 1U;
  receiver->snapshot.accepted_frame_count++;
  receiver->snapshot.last_request_id = decoded_task.request_id;
  return TEST_TASK_RECEIVER_TASK_READY;
}

TestTask_ReceiverResultTypeDef TestTask_ReceiverService(
    TestTask_ReceiverTypeDef *receiver,
    uint32_t now_ms)
{
  if (receiver == (TestTask_ReceiverTypeDef *)0)
  {
    return TEST_TASK_RECEIVER_ERROR_NULL_ARGUMENT;
  }
  return TestTask_ReceiverCheckTimeout(receiver, now_ms);
}

uint8_t TestTask_ReceiverAbortFrame(TestTask_ReceiverTypeDef *receiver)
{
  if ((receiver == (TestTask_ReceiverTypeDef *)0) ||
      (receiver->receiving == 0U))
  {
    return 0U;
  }

  TestTask_ReceiverResetFrame(receiver);
  receiver->snapshot.rejected_frame_count++;
  receiver->snapshot.last_wire_error = TEST_TASK_WIRE_ERROR_LENGTH;
  return 1U;
}

uint8_t TestTask_ReceiverTakeTask(
    TestTask_ReceiverTypeDef *receiver,
    TestTask_DescriptorTypeDef *task)
{
  if ((receiver == (TestTask_ReceiverTypeDef *)0) ||
      (task == (TestTask_DescriptorTypeDef *)0) ||
      (receiver->task_pending == 0U))
  {
    return 0U;
  }

  *task = receiver->pending_task;
  receiver->task_pending = 0U;
  return 1U;
}

uint8_t TestTask_ReceiverTakeStatusQuery(
    TestTask_ReceiverTypeDef *receiver,
    ControlStatus_QueryTypeDef *query)
{
  if ((receiver == (TestTask_ReceiverTypeDef *)0) ||
      (query == (ControlStatus_QueryTypeDef *)0) ||
      (receiver->status_query_pending == 0U))
  {
    return 0U;
  }
  *query = receiver->pending_status_query;
  receiver->status_query_pending = 0U;
  return 1U;
}

uint8_t TestTask_ReceiverTakeTelemetryQuery(
    TestTask_ReceiverTypeDef *receiver,
    Telemetry_QueryTypeDef *query)
{
  if ((receiver == (TestTask_ReceiverTypeDef *)0) ||
      (query == (Telemetry_QueryTypeDef *)0) ||
      (receiver->telemetry_query_pending == 0U))
  {
    return 0U;
  }
  *query = receiver->pending_telemetry_query;
  receiver->telemetry_query_pending = 0U;
  return 1U;
}

uint8_t TestTask_ReceiverTakeInfoQuery(
    TestTask_ReceiverTypeDef *receiver,
    DeviceInfo_QueryTypeDef *query)
{
  if ((receiver == (TestTask_ReceiverTypeDef *)0) ||
      (query == (DeviceInfo_QueryTypeDef *)0) ||
      (receiver->info_query_pending == 0U))
  {
    return 0U;
  }
  *query = receiver->pending_info_query;
  receiver->info_query_pending = 0U;
  return 1U;
}

uint8_t TestTask_ReceiverTakeOperator(TestTask_ReceiverTypeDef *receiver,
    OperatorWire_Request *request)
{
  if ((receiver == 0) || (request == 0)) { return 0U; }
  if (receiver->operator_stop_pending != 0U)
  {
    *request = receiver->pending_operator_stop;
    receiver->operator_stop_pending = 0U;
    return 1U;
  }
  if (receiver->operator_pending != 0U)
  {
    *request = receiver->pending_operator;
    receiver->operator_pending = 0U;
    return 1U;
  }
  return 0U;
}
