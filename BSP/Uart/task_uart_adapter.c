/**
  ******************************************************************************
  * @file    task_uart_adapter.c
  * @brief   USART adapter for structured-task RX and bounded result TX.
  ******************************************************************************
  */

#include "task_uart_adapter.h"
#include "task_uart_platform.h"
#include "device_info_platform.h"

static TaskUart_AdapterResultTypeDef TaskUart_AdapterInitLocked(UART_HandleTypeDef *huart, uint32_t frame_timeout_ms);
static TaskUart_AdapterResultTypeDef TaskUart_AdapterStartLocked(void);
static TaskUart_AdapterResultTypeDef TaskUart_AdapterOnRxCompleteLocked(UART_HandleTypeDef *huart, uint32_t now_ms);
static TaskUart_AdapterResultTypeDef TaskUart_AdapterOnErrorLocked(UART_HandleTypeDef *huart);
static TaskUart_AdapterResultTypeDef TaskUart_AdapterServiceLocked(uint32_t now_ms);
static uint8_t TaskUart_AdapterTakeTaskLocked(TestTask_DescriptorTypeDef *task);
static TaskUart_TxResultTypeDef TaskUart_AdapterSendResultLocked(const TestTask_ResultDescriptorTypeDef *result);
static TaskUart_TxResultTypeDef TaskUart_AdapterQueueStopLocked(const TestTask_StopDescriptorTypeDef *stop);
static TaskUart_TxResultTypeDef TaskUart_AdapterOnTxCompleteLocked(UART_HandleTypeDef *huart);
static TaskUart_TxResultTypeDef TaskUart_AdapterStartStatus(void);
static TaskUart_TxResultTypeDef TaskUart_AdapterStartTelemetry(void);
static TaskUart_TxResultTypeDef TaskUart_AdapterStartInfo(void);
static TaskUart_TxResultTypeDef TaskUart_AdapterStartOperator(void);

TaskUart_AdapterTypeDef g_task_uart_adapter;

static TaskUart_TxResultTypeDef TaskUart_AdapterStartStop(void)
{
  HAL_StatusTypeDef hal_status;
  TestTask_ResultWireErrorTypeDef wire_error;
  uint16_t frame_length = 0U;

  if ((g_task_uart_adapter.tx_busy != 0U) ||
      (g_task_uart_adapter.huart->gState == HAL_UART_STATE_BUSY_TX))
  {
    g_task_uart_adapter.snapshot.last_tx_result = TASK_UART_TX_BUSY;
    return TASK_UART_TX_BUSY;
  }
  wire_error = TestTask_StopWireEncode(
      &g_task_uart_adapter.pending_stop,
      g_task_uart_adapter.tx_frame,
      sizeof(g_task_uart_adapter.tx_frame),
      &frame_length);
  g_task_uart_adapter.snapshot.last_tx_wire_error = wire_error;
  if (wire_error != TEST_TASK_RESULT_WIRE_OK)
  {
    g_task_uart_adapter.snapshot.stop_tx_error_count++;
    g_task_uart_adapter.snapshot.last_tx_result = TASK_UART_TX_ENCODE_ERROR;
    return TASK_UART_TX_ENCODE_ERROR;
  }

  g_task_uart_adapter.tx_frame_length = frame_length;
  g_task_uart_adapter.tx_busy = 1U;
  g_task_uart_adapter.tx_stop_active = 1U;
  g_task_uart_adapter.tx_status_active = 0U;
  g_task_uart_adapter.tx_telemetry_active = 0U;
  hal_status = HAL_UART_Transmit_IT(
      g_task_uart_adapter.huart,
      g_task_uart_adapter.tx_frame,
      frame_length);
  g_task_uart_adapter.snapshot.last_tx_hal_status = hal_status;
  if (hal_status != HAL_OK)
  {
    g_task_uart_adapter.tx_busy = 0U;
    g_task_uart_adapter.tx_stop_active = 0U;
    if (hal_status == HAL_BUSY)
    {
      g_task_uart_adapter.snapshot.last_tx_result = TASK_UART_TX_BUSY;
      return TASK_UART_TX_BUSY;
    }
    g_task_uart_adapter.snapshot.tx_error_count++;
    g_task_uart_adapter.snapshot.stop_tx_error_count++;
    g_task_uart_adapter.snapshot.last_tx_result = TASK_UART_TX_HAL_ERROR;
    return TASK_UART_TX_HAL_ERROR;
  }

  g_task_uart_adapter.snapshot.tx_start_count++;
  g_task_uart_adapter.snapshot.stop_tx_start_count++;
  g_task_uart_adapter.snapshot.last_tx_request_id =
      g_task_uart_adapter.pending_stop.request_id;
  g_task_uart_adapter.snapshot.last_tx_frame_length = frame_length;
  g_task_uart_adapter.snapshot.last_tx_result = TASK_UART_TX_STARTED;
  return TASK_UART_TX_STARTED;
}

static void TaskUart_AdapterQueueRejectedFrameResult(
    uint32_t now_ms,
    TestTask_WireErrorTypeDef wire_error)
{
  if ((g_task_uart_adapter.transport_result_pending != 0U) ||
      (g_task_uart_adapter.tx_busy != 0U))
  {
    g_task_uart_adapter.snapshot.transport_result_drop_count++;
    return;
  }

  if (wire_error == TEST_TASK_WIRE_ERROR_TASK_SCHEMA)
  {
    g_task_uart_adapter.transport_result.request_id =
        g_task_uart_adapter.receiver.snapshot.last_request_id;
    g_task_uart_adapter.transport_result.error_code =
        (uint16_t)g_task_uart_adapter.receiver.snapshot.last_schema_error;
    g_task_uart_adapter.transport_result.stage =
        TEST_TASK_RESULT_STAGE_SCHEMA;
    g_task_uart_adapter.snapshot.schema_result_count++;
  }
  else
  {
    g_task_uart_adapter.transport_result.request_id = 0UL;
    g_task_uart_adapter.transport_result.error_code = (uint16_t)wire_error;
    g_task_uart_adapter.transport_result.stage =
        TEST_TASK_RESULT_STAGE_TRANSPORT;
    g_task_uart_adapter.snapshot.transport_result_count++;
  }
  g_task_uart_adapter.transport_result.timestamp_ms = now_ms;
  g_task_uart_adapter.transport_result.record_count = 0U;
  g_task_uart_adapter.transport_result.status =
      TEST_TASK_RESULT_STATUS_REJECTED;
  g_task_uart_adapter.transport_result.safety_state = 0U;
  g_task_uart_adapter.transport_result.flags = 0U;
  g_task_uart_adapter.transport_result_pending = 1U;
}

static TaskUart_AdapterResultTypeDef TaskUart_AdapterArmReceive(void)
{
  HAL_StatusTypeDef status;

  status = HAL_UART_Receive_IT(g_task_uart_adapter.huart,
                               &g_task_uart_adapter.rx_byte,
                               1U);
  g_task_uart_adapter.snapshot.last_hal_status = status;
  g_task_uart_adapter.snapshot.last_hal_error =
      HAL_UART_GetError(g_task_uart_adapter.huart);
  if (status != HAL_OK)
  {
    g_task_uart_adapter.rx_armed = 0U;
    g_task_uart_adapter.snapshot.arm_error_count++;
    g_task_uart_adapter.snapshot.last_result = TASK_UART_ADAPTER_HAL_ERROR;
    return TASK_UART_ADAPTER_HAL_ERROR;
  }

  g_task_uart_adapter.rx_armed = 1U;
  g_task_uart_adapter.snapshot.arm_count++;
  g_task_uart_adapter.snapshot.last_result = TASK_UART_ADAPTER_RX_ARMED;
  return TASK_UART_ADAPTER_RX_ARMED;
}

static TaskUart_AdapterResultTypeDef TaskUart_AdapterMapReceiverResult(
    TestTask_ReceiverResultTypeDef receiver_result)
{
  switch (receiver_result)
  {
    case TEST_TASK_RECEIVER_TASK_READY:
      return TASK_UART_ADAPTER_TASK_READY;
    case TEST_TASK_RECEIVER_FRAME_REJECTED:
      return TASK_UART_ADAPTER_FRAME_REJECTED;
    case TEST_TASK_RECEIVER_FRAME_TIMEOUT:
      return TASK_UART_ADAPTER_FRAME_TIMEOUT;
    case TEST_TASK_RECEIVER_TASK_PENDING:
      return TASK_UART_ADAPTER_TASK_PENDING;
    case TEST_TASK_RECEIVER_STATUS_QUERY_READY:
      return TASK_UART_ADAPTER_STATUS_QUERY_READY;
    case TEST_TASK_RECEIVER_STATUS_QUERY_REJECTED:
      return TASK_UART_ADAPTER_STATUS_QUERY_REJECTED;
    case TEST_TASK_RECEIVER_TELEMETRY_QUERY_READY:
      return TASK_UART_ADAPTER_TELEMETRY_QUERY_READY;
    case TEST_TASK_RECEIVER_TELEMETRY_QUERY_REJECTED:
      return TASK_UART_ADAPTER_TELEMETRY_QUERY_REJECTED;
    case TEST_TASK_RECEIVER_INFO_QUERY_READY:
      return TASK_UART_ADAPTER_INFO_QUERY_READY;
    case TEST_TASK_RECEIVER_INFO_QUERY_REJECTED:
      return TASK_UART_ADAPTER_INFO_QUERY_REJECTED;
    case TEST_TASK_RECEIVER_OPERATOR_READY:
      return TASK_UART_ADAPTER_OPERATOR_READY;
    case TEST_TASK_RECEIVER_OPERATOR_REJECTED:
      return TASK_UART_ADAPTER_OPERATOR_REJECTED;
    default:
      return TASK_UART_ADAPTER_OK;
  }
}

static TaskUart_AdapterResultTypeDef TaskUart_AdapterInitLocked(
    UART_HandleTypeDef *huart,
    uint32_t frame_timeout_ms)
{
  uint16_t index;

  if (huart == (UART_HandleTypeDef *)0)
  {
    return TASK_UART_ADAPTER_ERROR_NULL_ARGUMENT;
  }

  g_task_uart_adapter.huart = huart;
  g_task_uart_adapter.rx_byte = 0U;
  for (index = 0U; index < sizeof(g_task_uart_adapter.tx_frame); index++)
  {
    g_task_uart_adapter.tx_frame[index] = 0U;
  }
  g_task_uart_adapter.tx_frame_length = 0U;
  g_task_uart_adapter.initialized = 1U;
  g_task_uart_adapter.operator_pending = 0U;
  g_task_uart_adapter.tx_operator_active = 0U;
  g_task_uart_adapter.snapshot.operator_queued_count = 0UL;
  g_task_uart_adapter.snapshot.operator_drop_count = 0UL;
  g_task_uart_adapter.snapshot.operator_tx_complete_count = 0UL;
  g_task_uart_adapter.snapshot.result_queued_behind_operator_count = 0UL;
  g_task_uart_adapter.rx_armed = 0U;
  g_task_uart_adapter.tx_busy = 0U;
  g_task_uart_adapter.transport_result_pending = 0U;
  g_task_uart_adapter.result_pending = 0U;
  g_task_uart_adapter.stop_pending = 0U;
  g_task_uart_adapter.tx_stop_active = 0U;
  g_task_uart_adapter.status_pending = 0U;
  g_task_uart_adapter.tx_status_active = 0U;
  g_task_uart_adapter.telemetry_pending = 0U;
  g_task_uart_adapter.tx_telemetry_active = 0U;
  g_task_uart_adapter.info_pending = 0U;
  g_task_uart_adapter.tx_info_active = 0U;
  g_task_uart_adapter.snapshot.info_queued_count = 0UL;
  g_task_uart_adapter.snapshot.info_tx_complete_count = 0UL;
  g_task_uart_adapter.snapshot.info_drop_count = 0UL;
  g_task_uart_adapter.snapshot.result_queued_behind_info_count = 0UL;
  g_task_uart_adapter.snapshot.last_info_wire_error = DEVICE_INFO_WIRE_OK;
  TestTask_ReceiverInit(&g_task_uart_adapter.receiver, frame_timeout_ms);

  g_task_uart_adapter.snapshot.rx_complete_count = 0UL;
  g_task_uart_adapter.snapshot.arm_count = 0UL;
  g_task_uart_adapter.snapshot.arm_error_count = 0UL;
  g_task_uart_adapter.snapshot.uart_error_count = 0UL;
  g_task_uart_adapter.snapshot.ignored_uart_count = 0UL;
  g_task_uart_adapter.snapshot.tx_start_count = 0UL;
  g_task_uart_adapter.snapshot.tx_complete_count = 0UL;
  g_task_uart_adapter.snapshot.tx_error_count = 0UL;
  g_task_uart_adapter.snapshot.tx_drop_count = 0UL;
  g_task_uart_adapter.snapshot.transport_result_count = 0UL;
  g_task_uart_adapter.snapshot.schema_result_count = 0UL;
  g_task_uart_adapter.snapshot.transport_result_drop_count = 0UL;
  g_task_uart_adapter.snapshot.stop_queued_count = 0UL;
  g_task_uart_adapter.snapshot.stop_tx_start_count = 0UL;
  g_task_uart_adapter.snapshot.stop_tx_complete_count = 0UL;
  g_task_uart_adapter.snapshot.stop_tx_error_count = 0UL;
  g_task_uart_adapter.snapshot.stop_queue_full_count = 0UL;
  g_task_uart_adapter.snapshot.status_queued_count = 0UL;
  g_task_uart_adapter.snapshot.status_tx_complete_count = 0UL;
  g_task_uart_adapter.snapshot.status_drop_count = 0UL;
  g_task_uart_adapter.snapshot.telemetry_queued_count = 0UL;
  g_task_uart_adapter.snapshot.telemetry_tx_complete_count = 0UL;
  g_task_uart_adapter.snapshot.telemetry_drop_count = 0UL;
  g_task_uart_adapter.snapshot.result_queued_behind_telemetry_count = 0UL;
  g_task_uart_adapter.snapshot.last_hal_error = HAL_UART_ERROR_NONE;
  g_task_uart_adapter.snapshot.last_request_id = 0UL;
  g_task_uart_adapter.snapshot.last_tx_request_id = 0UL;
  g_task_uart_adapter.snapshot.last_tx_frame_length = 0U;
  g_task_uart_adapter.snapshot.last_hal_status = HAL_OK;
  g_task_uart_adapter.snapshot.last_tx_hal_status = HAL_OK;
  g_task_uart_adapter.snapshot.last_result = TASK_UART_ADAPTER_OK;
  g_task_uart_adapter.snapshot.last_tx_result = TASK_UART_TX_IDLE;
  g_task_uart_adapter.snapshot.last_receiver_result =
      TEST_TASK_RECEIVER_NO_EVENT;
  g_task_uart_adapter.snapshot.last_tx_wire_error =
      TEST_TASK_RESULT_WIRE_OK;
  g_task_uart_adapter.snapshot.last_status_wire_error =
      CONTROL_STATUS_WIRE_OK;
  g_task_uart_adapter.snapshot.last_telemetry_wire_error =
      TELEMETRY_WIRE_OK;
  return TASK_UART_ADAPTER_OK;
}

static TaskUart_AdapterResultTypeDef TaskUart_AdapterStartLocked(void)
{
  if ((g_task_uart_adapter.initialized == 0U) ||
      (g_task_uart_adapter.huart == (UART_HandleTypeDef *)0))
  {
    g_task_uart_adapter.snapshot.last_result =
        TASK_UART_ADAPTER_ERROR_NOT_INITIALIZED;
    return TASK_UART_ADAPTER_ERROR_NOT_INITIALIZED;
  }
  if (g_task_uart_adapter.rx_armed != 0U)
  {
    return TASK_UART_ADAPTER_RX_ARMED;
  }
  return TaskUart_AdapterArmReceive();
}

static TaskUart_AdapterResultTypeDef TaskUart_AdapterOnRxCompleteLocked(
    UART_HandleTypeDef *huart,
    uint32_t now_ms)
{
  TestTask_ReceiverResultTypeDef receiver_result;
  TaskUart_AdapterResultTypeDef result;

  if (huart == (UART_HandleTypeDef *)0)
  {
    return TASK_UART_ADAPTER_ERROR_NULL_ARGUMENT;
  }
  if ((g_task_uart_adapter.initialized == 0U) ||
      (g_task_uart_adapter.huart == (UART_HandleTypeDef *)0))
  {
    return TASK_UART_ADAPTER_ERROR_NOT_INITIALIZED;
  }
  if (huart != g_task_uart_adapter.huart)
  {
    g_task_uart_adapter.snapshot.ignored_uart_count++;
    g_task_uart_adapter.snapshot.last_result = TASK_UART_ADAPTER_UART_IGNORED;
    return TASK_UART_ADAPTER_UART_IGNORED;
  }

  g_task_uart_adapter.rx_armed = 0U;
  g_task_uart_adapter.snapshot.rx_complete_count++;
  receiver_result = TestTask_ReceiverPushByte(
      &g_task_uart_adapter.receiver,
      g_task_uart_adapter.rx_byte,
      now_ms);
  g_task_uart_adapter.snapshot.last_receiver_result = receiver_result;
  result = TaskUart_AdapterMapReceiverResult(receiver_result);

  if (receiver_result == TEST_TASK_RECEIVER_FRAME_REJECTED)
  {
    TaskUart_AdapterQueueRejectedFrameResult(
        now_ms,
        g_task_uart_adapter.receiver.snapshot.last_wire_error);
  }

  if (TaskUart_AdapterArmReceive() == TASK_UART_ADAPTER_HAL_ERROR)
  {
    return TASK_UART_ADAPTER_HAL_ERROR;
  }

  g_task_uart_adapter.snapshot.last_result = result;
  if (receiver_result == TEST_TASK_RECEIVER_TASK_READY)
  {
    g_task_uart_adapter.snapshot.last_request_id =
        g_task_uart_adapter.receiver.snapshot.last_request_id;
  }
  return result;
}

static TaskUart_AdapterResultTypeDef TaskUart_AdapterOnErrorLocked(
    UART_HandleTypeDef *huart)
{
  if (huart == (UART_HandleTypeDef *)0)
  {
    return TASK_UART_ADAPTER_ERROR_NULL_ARGUMENT;
  }
  if ((g_task_uart_adapter.initialized == 0U) ||
      (g_task_uart_adapter.huart == (UART_HandleTypeDef *)0))
  {
    return TASK_UART_ADAPTER_ERROR_NOT_INITIALIZED;
  }
  if (huart != g_task_uart_adapter.huart)
  {
    g_task_uart_adapter.snapshot.ignored_uart_count++;
    g_task_uart_adapter.snapshot.last_result = TASK_UART_ADAPTER_UART_IGNORED;
    return TASK_UART_ADAPTER_UART_IGNORED;
  }

  g_task_uart_adapter.rx_armed =
      (huart->RxState == HAL_UART_STATE_BUSY_RX) ? 1U : 0U;
  g_task_uart_adapter.snapshot.uart_error_count++;
  g_task_uart_adapter.snapshot.last_hal_error = HAL_UART_GetError(huart);
  /* RX failure cannot release the buffer still owned by HAL TX. */
  (void)TestTask_ReceiverAbortFrame(&g_task_uart_adapter.receiver);
  g_task_uart_adapter.snapshot.last_result = TASK_UART_ADAPTER_UART_ERROR;
  return TASK_UART_ADAPTER_UART_ERROR;
}

static TaskUart_AdapterResultTypeDef TaskUart_AdapterServiceLocked(uint32_t now_ms)
{
  TestTask_ReceiverResultTypeDef receiver_result;
  TaskUart_AdapterResultTypeDef result;
  DeviceInfo_QueryTypeDef info_query;

  if ((g_task_uart_adapter.initialized == 0U) ||
      (g_task_uart_adapter.huart == (UART_HandleTypeDef *)0))
  {
    return TASK_UART_ADAPTER_ERROR_NOT_INITIALIZED;
  }

  receiver_result = TestTask_ReceiverService(
      &g_task_uart_adapter.receiver,
      now_ms);
  g_task_uart_adapter.snapshot.last_receiver_result = receiver_result;
  result = TaskUart_AdapterMapReceiverResult(receiver_result);

  if (receiver_result == TEST_TASK_RECEIVER_FRAME_TIMEOUT)
  {
    TaskUart_AdapterQueueRejectedFrameResult(
        now_ms,
        TEST_TASK_WIRE_ERROR_LENGTH);
  }

  if ((g_task_uart_adapter.rx_armed == 0U) &&
      (TaskUart_AdapterArmReceive() == TASK_UART_ADAPTER_HAL_ERROR))
  {
    return TASK_UART_ADAPTER_HAL_ERROR;
  }

  if (TestTask_ReceiverTakeInfoQuery(&g_task_uart_adapter.receiver, &info_query) != 0U)
  {
    if (g_task_uart_adapter.info_pending != 0U)
    {
      g_task_uart_adapter.snapshot.info_drop_count++;
    }
    else
    {
      DeviceInfo_PlatformRead(&info_query, now_ms, &g_task_uart_adapter.pending_info);
      g_task_uart_adapter.info_pending = 1U;
      g_task_uart_adapter.snapshot.info_queued_count++;
    }
  }

  if ((g_task_uart_adapter.result_pending != 0U) &&
      (g_task_uart_adapter.tx_busy == 0U))
  {
    TaskUart_TxResultTypeDef task_result = TaskUart_AdapterSendResultLocked(
        &g_task_uart_adapter.pending_result);
    if (task_result != TASK_UART_TX_BUSY)
    {
      g_task_uart_adapter.result_pending = 0U;
    }
  }

  if ((g_task_uart_adapter.transport_result_pending != 0U) &&
      (g_task_uart_adapter.tx_busy == 0U))
  {
    g_task_uart_adapter.transport_result_pending = 0U;
    if (TaskUart_AdapterSendResultLocked(
            &g_task_uart_adapter.transport_result) != TASK_UART_TX_STARTED)
    {
      g_task_uart_adapter.snapshot.transport_result_drop_count++;
    }
  }

  if ((g_task_uart_adapter.stop_pending != 0U) &&
      (g_task_uart_adapter.tx_busy == 0U))
  {
    TaskUart_TxResultTypeDef stop_result = TaskUart_AdapterStartStop();
    if (stop_result != TASK_UART_TX_BUSY)
    {
      g_task_uart_adapter.stop_pending = 0U;
    }
  }

  if ((g_task_uart_adapter.operator_pending != 0U) &&
      (g_task_uart_adapter.tx_busy == 0U) &&
      (g_task_uart_adapter.stop_pending == 0U))
  {
    TaskUart_TxResultTypeDef operator_result = TaskUart_AdapterStartOperator();
    if (operator_result != TASK_UART_TX_BUSY)
    {
      g_task_uart_adapter.operator_pending = 0U;
    }
  }

  if ((g_task_uart_adapter.status_pending != 0U) &&
      (g_task_uart_adapter.tx_busy == 0U) &&
      (g_task_uart_adapter.stop_pending == 0U))
  {
    TaskUart_TxResultTypeDef status_result = TaskUart_AdapterStartStatus();
    if (status_result != TASK_UART_TX_BUSY)
    {
      g_task_uart_adapter.status_pending = 0U;
    }
  }

  if ((g_task_uart_adapter.info_pending != 0U) &&
      (g_task_uart_adapter.tx_busy == 0U) &&
      (g_task_uart_adapter.stop_pending == 0U) &&
      (g_task_uart_adapter.status_pending == 0U) &&
      (g_task_uart_adapter.transport_result_pending == 0U) &&
      (g_task_uart_adapter.result_pending == 0U) &&
      (g_task_uart_adapter.receiver.task_pending == 0U) &&
      (g_task_uart_adapter.receiver.receiving == 0U))
  {
    TaskUart_TxResultTypeDef info_result =
        TaskUart_AdapterStartInfo();
    if (info_result != TASK_UART_TX_BUSY)
    {
      g_task_uart_adapter.info_pending = 0U;
    }
  }

  if ((g_task_uart_adapter.telemetry_pending != 0U) &&
      (g_task_uart_adapter.tx_busy == 0U) &&
      (g_task_uart_adapter.stop_pending == 0U) &&
      (g_task_uart_adapter.status_pending == 0U) &&
      (g_task_uart_adapter.transport_result_pending == 0U) &&
      (g_task_uart_adapter.result_pending == 0U) &&
      (g_task_uart_adapter.receiver.task_pending == 0U) &&
      (g_task_uart_adapter.receiver.receiving == 0U))
  {
    TaskUart_TxResultTypeDef telemetry_result =
        TaskUart_AdapterStartTelemetry();
    if (telemetry_result != TASK_UART_TX_BUSY)
    {
      g_task_uart_adapter.telemetry_pending = 0U;
    }
  }

  g_task_uart_adapter.snapshot.last_result = result;
  return result;
}

static uint8_t TaskUart_AdapterTakeTaskLocked(TestTask_DescriptorTypeDef *task)
{
  if (task == (TestTask_DescriptorTypeDef *)0)
  {
    return 0U;
  }
  return TestTask_ReceiverTakeTask(&g_task_uart_adapter.receiver, task);
}

static TaskUart_TxResultTypeDef TaskUart_AdapterSendResultLocked(
    const TestTask_ResultDescriptorTypeDef *result)
{
  TestTask_ResultWireErrorTypeDef wire_error;
  HAL_StatusTypeDef hal_status;
  uint16_t frame_length = 0U;

  if (result == (const TestTask_ResultDescriptorTypeDef *)0)
  {
    return TASK_UART_TX_ERROR_NULL_ARGUMENT;
  }
  if ((g_task_uart_adapter.initialized == 0U) ||
      (g_task_uart_adapter.huart == (UART_HandleTypeDef *)0))
  {
    return TASK_UART_TX_ERROR_NOT_INITIALIZED;
  }
  if ((g_task_uart_adapter.tx_busy != 0U) ||
      (g_task_uart_adapter.huart->gState == HAL_UART_STATE_BUSY_TX))
  {
    if (((g_task_uart_adapter.tx_telemetry_active != 0U) ||
         (g_task_uart_adapter.tx_info_active != 0U) ||
         (g_task_uart_adapter.tx_operator_active != 0U)) &&
        (g_task_uart_adapter.result_pending == 0U))
    {
      g_task_uart_adapter.pending_result = *result;
      g_task_uart_adapter.result_pending = 1U;
      if (g_task_uart_adapter.tx_info_active != 0U)
      { g_task_uart_adapter.snapshot.result_queued_behind_info_count++; }
      else if (g_task_uart_adapter.tx_operator_active != 0U)
      { g_task_uart_adapter.snapshot.result_queued_behind_operator_count++; }
      else
      { g_task_uart_adapter.snapshot.result_queued_behind_telemetry_count++; }
      return TASK_UART_TX_QUEUED;
    }
    g_task_uart_adapter.snapshot.tx_drop_count++;
    g_task_uart_adapter.snapshot.last_tx_result = TASK_UART_TX_BUSY;
    return TASK_UART_TX_BUSY;
  }

  wire_error = TestTask_ResultWireEncode(
      result,
      g_task_uart_adapter.tx_frame,
      TEST_TASK_RESULT_FRAME_LENGTH,
      &frame_length);
  g_task_uart_adapter.snapshot.last_tx_wire_error = wire_error;
  if (wire_error != TEST_TASK_RESULT_WIRE_OK)
  {
    g_task_uart_adapter.snapshot.tx_error_count++;
    g_task_uart_adapter.snapshot.last_tx_result =
        TASK_UART_TX_ENCODE_ERROR;
    return TASK_UART_TX_ENCODE_ERROR;
  }

  g_task_uart_adapter.tx_frame_length = frame_length;
  g_task_uart_adapter.tx_busy = 1U;
  g_task_uart_adapter.tx_stop_active = 0U;
  g_task_uart_adapter.tx_status_active = 0U;
  g_task_uart_adapter.tx_telemetry_active = 0U;
  hal_status = HAL_UART_Transmit_IT(
      g_task_uart_adapter.huart,
      g_task_uart_adapter.tx_frame,
      frame_length);
  g_task_uart_adapter.snapshot.last_tx_hal_status = hal_status;
  if (hal_status != HAL_OK)
  {
    g_task_uart_adapter.tx_busy = 0U;
    g_task_uart_adapter.snapshot.tx_error_count++;
    g_task_uart_adapter.snapshot.last_tx_result = TASK_UART_TX_HAL_ERROR;
    return TASK_UART_TX_HAL_ERROR;
  }

  g_task_uart_adapter.snapshot.tx_start_count++;
  g_task_uart_adapter.snapshot.last_tx_request_id = result->request_id;
  g_task_uart_adapter.snapshot.last_tx_frame_length = frame_length;
  g_task_uart_adapter.snapshot.last_tx_result = TASK_UART_TX_STARTED;
  return TASK_UART_TX_STARTED;
}

static TaskUart_TxResultTypeDef TaskUart_AdapterQueueStopLocked(
    const TestTask_StopDescriptorTypeDef *stop)
{
  if (stop == (const TestTask_StopDescriptorTypeDef *)0)
  {
    return TASK_UART_TX_ERROR_NULL_ARGUMENT;
  }
  if ((g_task_uart_adapter.initialized == 0U) ||
      (g_task_uart_adapter.huart == (UART_HandleTypeDef *)0))
  {
    return TASK_UART_TX_ERROR_NOT_INITIALIZED;
  }
  if ((stop->request_id == 0UL) ||
      (stop->stop_reason < 1U) || (stop->stop_reason > 5U))
  {
    g_task_uart_adapter.snapshot.stop_tx_error_count++;
    return TASK_UART_TX_ENCODE_ERROR;
  }
  if (g_task_uart_adapter.stop_pending != 0U)
  {
    g_task_uart_adapter.snapshot.stop_queue_full_count++;
    return TASK_UART_TX_BUSY;
  }

  g_task_uart_adapter.pending_stop = *stop;
  g_task_uart_adapter.stop_pending = 1U;
  g_task_uart_adapter.snapshot.stop_queued_count++;
  return TASK_UART_TX_QUEUED;
}

static TaskUart_TxResultTypeDef TaskUart_AdapterStartStatus(void)
{
  uint16_t frame_length = 0U;
  HAL_StatusTypeDef hal_status;
  ControlStatus_WireResultTypeDef wire_error;
  if ((g_task_uart_adapter.tx_busy != 0U) ||
      (g_task_uart_adapter.huart->gState == HAL_UART_STATE_BUSY_TX))
  {
    return TASK_UART_TX_BUSY;
  }
  wire_error = ControlStatus_ReplyEncode(
      &g_task_uart_adapter.pending_status, g_task_uart_adapter.tx_frame,
      sizeof(g_task_uart_adapter.tx_frame), &frame_length);
  g_task_uart_adapter.snapshot.last_status_wire_error = wire_error;
  if (wire_error != CONTROL_STATUS_WIRE_OK)
  {
    g_task_uart_adapter.snapshot.status_drop_count++;
    return TASK_UART_TX_ENCODE_ERROR;
  }
  g_task_uart_adapter.tx_frame_length = frame_length;
  g_task_uart_adapter.tx_busy = 1U;
  g_task_uart_adapter.tx_stop_active = 0U;
  g_task_uart_adapter.tx_status_active = 1U;
  g_task_uart_adapter.tx_telemetry_active = 0U;
  hal_status = HAL_UART_Transmit_IT(
      g_task_uart_adapter.huart, g_task_uart_adapter.tx_frame, frame_length);
  g_task_uart_adapter.snapshot.last_tx_hal_status = hal_status;
  if (hal_status != HAL_OK)
  {
    g_task_uart_adapter.tx_busy = 0U;
    g_task_uart_adapter.tx_status_active = 0U;
    if (hal_status == HAL_BUSY)
    {
      return TASK_UART_TX_BUSY;
    }
    g_task_uart_adapter.snapshot.status_drop_count++;
    return TASK_UART_TX_HAL_ERROR;
  }
  g_task_uart_adapter.snapshot.tx_start_count++;
  g_task_uart_adapter.snapshot.last_tx_request_id =
      g_task_uart_adapter.pending_status.request_id;
  g_task_uart_adapter.snapshot.last_tx_frame_length = frame_length;
  g_task_uart_adapter.snapshot.last_tx_result = TASK_UART_TX_STARTED;
  return TASK_UART_TX_STARTED;
}

static TaskUart_TxResultTypeDef TaskUart_AdapterQueueStatusLocked(
    const ControlStatus_ReplyTypeDef *status)
{
  if (status == (const ControlStatus_ReplyTypeDef *)0)
  {
    return TASK_UART_TX_ERROR_NULL_ARGUMENT;
  }
  if ((g_task_uart_adapter.initialized == 0U) ||
      (g_task_uart_adapter.huart == (UART_HandleTypeDef *)0))
  {
    return TASK_UART_TX_ERROR_NOT_INITIALIZED;
  }
  if (g_task_uart_adapter.status_pending != 0U)
  {
    g_task_uart_adapter.snapshot.status_drop_count++;
    return TASK_UART_TX_BUSY;
  }
  g_task_uart_adapter.pending_status = *status;
  g_task_uart_adapter.status_pending = 1U;
  g_task_uart_adapter.snapshot.status_queued_count++;
  return TASK_UART_TX_QUEUED;
}

static TaskUart_TxResultTypeDef TaskUart_AdapterStartTelemetry(void)
{
  uint16_t frame_length = 0U;
  HAL_StatusTypeDef hal_status;
  Telemetry_WireResultTypeDef wire_error;
  if ((g_task_uart_adapter.tx_busy != 0U) ||
      (g_task_uart_adapter.huart->gState == HAL_UART_STATE_BUSY_TX))
  {
    return TASK_UART_TX_BUSY;
  }
  wire_error = Telemetry_ReplyEncode(
      &g_task_uart_adapter.pending_telemetry, g_task_uart_adapter.tx_frame,
      sizeof(g_task_uart_adapter.tx_frame), &frame_length);
  g_task_uart_adapter.snapshot.last_telemetry_wire_error = wire_error;
  if (wire_error != TELEMETRY_WIRE_OK)
  {
    g_task_uart_adapter.snapshot.telemetry_drop_count++;
    return TASK_UART_TX_ENCODE_ERROR;
  }
  g_task_uart_adapter.tx_frame_length = frame_length;
  g_task_uart_adapter.tx_busy = 1U;
  g_task_uart_adapter.tx_stop_active = 0U;
  g_task_uart_adapter.tx_status_active = 0U;
  g_task_uart_adapter.tx_telemetry_active = 1U;
  hal_status = HAL_UART_Transmit_IT(
      g_task_uart_adapter.huart, g_task_uart_adapter.tx_frame, frame_length);
  g_task_uart_adapter.snapshot.last_tx_hal_status = hal_status;
  if (hal_status != HAL_OK)
  {
    g_task_uart_adapter.tx_busy = 0U;
    g_task_uart_adapter.tx_telemetry_active = 0U;
    if (hal_status == HAL_BUSY)
    {
      return TASK_UART_TX_BUSY;
    }
    g_task_uart_adapter.snapshot.telemetry_drop_count++;
    return TASK_UART_TX_HAL_ERROR;
  }
  g_task_uart_adapter.snapshot.tx_start_count++;
  g_task_uart_adapter.snapshot.last_tx_request_id =
      g_task_uart_adapter.pending_telemetry.request_id;
  g_task_uart_adapter.snapshot.last_tx_frame_length = frame_length;
  g_task_uart_adapter.snapshot.last_tx_result = TASK_UART_TX_STARTED;
  return TASK_UART_TX_STARTED;
}

static TaskUart_TxResultTypeDef TaskUart_AdapterStartInfo(void)
{
  uint16_t frame_length = 0U;
  HAL_StatusTypeDef hal_status;
  DeviceInfo_WireResultTypeDef wire_error;
  if ((g_task_uart_adapter.tx_busy != 0U) ||
      (g_task_uart_adapter.huart->gState == HAL_UART_STATE_BUSY_TX))
  {
    return TASK_UART_TX_BUSY;
  }
  wire_error = DeviceInfo_ReplyEncode(
      &g_task_uart_adapter.pending_info, g_task_uart_adapter.tx_frame,
      sizeof(g_task_uart_adapter.tx_frame), &frame_length);
  g_task_uart_adapter.snapshot.last_info_wire_error = wire_error;
  if (wire_error != DEVICE_INFO_WIRE_OK)
  {
    g_task_uart_adapter.snapshot.info_drop_count++;
    return TASK_UART_TX_ENCODE_ERROR;
  }
  g_task_uart_adapter.tx_frame_length = frame_length;
  g_task_uart_adapter.tx_busy = 1U;
  g_task_uart_adapter.tx_stop_active = 0U;
  g_task_uart_adapter.tx_status_active = 0U;
  g_task_uart_adapter.tx_telemetry_active = 0U;
  g_task_uart_adapter.tx_info_active = 1U;
  hal_status = HAL_UART_Transmit_IT(
      g_task_uart_adapter.huart, g_task_uart_adapter.tx_frame, frame_length);
  g_task_uart_adapter.snapshot.last_tx_hal_status = hal_status;
  if (hal_status != HAL_OK)
  {
    g_task_uart_adapter.tx_busy = 0U;
    g_task_uart_adapter.tx_info_active = 0U;
    if (hal_status == HAL_BUSY)
    {
      return TASK_UART_TX_BUSY;
    }
    g_task_uart_adapter.snapshot.info_drop_count++;
    return TASK_UART_TX_HAL_ERROR;
  }
  g_task_uart_adapter.snapshot.tx_start_count++;
  g_task_uart_adapter.snapshot.last_tx_request_id =
      g_task_uart_adapter.pending_info.request_id;
  g_task_uart_adapter.snapshot.last_tx_frame_length = frame_length;
  g_task_uart_adapter.snapshot.last_tx_result = TASK_UART_TX_STARTED;
  return TASK_UART_TX_STARTED;
}

static TaskUart_TxResultTypeDef TaskUart_AdapterQueueTelemetryLocked(
    const Telemetry_ReplyTypeDef *telemetry)
{
  if (telemetry == (const Telemetry_ReplyTypeDef *)0)
  {
    return TASK_UART_TX_ERROR_NULL_ARGUMENT;
  }
  if ((g_task_uart_adapter.initialized == 0U) ||
      (g_task_uart_adapter.huart == (UART_HandleTypeDef *)0))
  {
    return TASK_UART_TX_ERROR_NOT_INITIALIZED;
  }
  if (g_task_uart_adapter.telemetry_pending != 0U)
  {
    g_task_uart_adapter.snapshot.telemetry_drop_count++;
    return TASK_UART_TX_BUSY;
  }
  g_task_uart_adapter.pending_telemetry = *telemetry;
  g_task_uart_adapter.telemetry_pending = 1U;
  g_task_uart_adapter.snapshot.telemetry_queued_count++;
  return TASK_UART_TX_QUEUED;
}

static TaskUart_TxResultTypeDef TaskUart_AdapterOnTxCompleteLocked(
    UART_HandleTypeDef *huart)
{
  if (huart == (UART_HandleTypeDef *)0)
  {
    return TASK_UART_TX_ERROR_NULL_ARGUMENT;
  }
  if ((g_task_uart_adapter.initialized == 0U) ||
      (g_task_uart_adapter.huart == (UART_HandleTypeDef *)0))
  {
    return TASK_UART_TX_ERROR_NOT_INITIALIZED;
  }
  if (huart != g_task_uart_adapter.huart)
  {
    g_task_uart_adapter.snapshot.ignored_uart_count++;
    g_task_uart_adapter.snapshot.last_tx_result =
        TASK_UART_TX_UART_IGNORED;
    return TASK_UART_TX_UART_IGNORED;
  }

  if (g_task_uart_adapter.tx_busy == 0U)
  {
    return TASK_UART_TX_IDLE;
  }
  g_task_uart_adapter.tx_busy = 0U;
  g_task_uart_adapter.snapshot.tx_complete_count++;
  if (g_task_uart_adapter.tx_stop_active != 0U)
  {
    g_task_uart_adapter.snapshot.stop_tx_complete_count++;
    g_task_uart_adapter.tx_stop_active = 0U;
  }
  if (g_task_uart_adapter.tx_status_active != 0U)
  {
    g_task_uart_adapter.snapshot.status_tx_complete_count++;
    g_task_uart_adapter.tx_status_active = 0U;
  }
  if (g_task_uart_adapter.tx_telemetry_active != 0U)
  {
    g_task_uart_adapter.snapshot.telemetry_tx_complete_count++;
    g_task_uart_adapter.tx_telemetry_active = 0U;
  }
  if (g_task_uart_adapter.tx_info_active != 0U)
  {
    g_task_uart_adapter.snapshot.info_tx_complete_count++;
    g_task_uart_adapter.tx_info_active = 0U;
  }
  if (g_task_uart_adapter.tx_operator_active != 0U)
  {
    g_task_uart_adapter.snapshot.operator_tx_complete_count++;
    g_task_uart_adapter.tx_operator_active = 0U;
  }
  g_task_uart_adapter.snapshot.last_tx_result = TASK_UART_TX_COMPLETE;
  return TASK_UART_TX_COMPLETE;
}

/* Public entry points serialize ISR/task state; task mutex stays outside. */
static TaskUart_TxResultTypeDef TaskUart_AdapterStartOperator(void)
{
  uint16_t length = 0U;
  HAL_StatusTypeDef hal_status;
  if ((g_task_uart_adapter.tx_busy != 0U) ||
      (g_task_uart_adapter.huart->gState == HAL_UART_STATE_BUSY_TX))
  { return TASK_UART_TX_BUSY; }
  if (OperatorWire_ReplyEncode(&g_task_uart_adapter.pending_operator,
      g_task_uart_adapter.tx_frame, sizeof(g_task_uart_adapter.tx_frame),
      &length) != OPERATOR_WIRE_OK)
  { g_task_uart_adapter.snapshot.operator_drop_count++; return TASK_UART_TX_ENCODE_ERROR; }
  g_task_uart_adapter.tx_frame_length = length;
  g_task_uart_adapter.tx_busy = 1U;
  g_task_uart_adapter.tx_operator_active = 1U;
  hal_status = HAL_UART_Transmit_IT(g_task_uart_adapter.huart,
      g_task_uart_adapter.tx_frame, length);
  g_task_uart_adapter.snapshot.last_tx_hal_status = hal_status;
  if (hal_status != HAL_OK)
  {
    g_task_uart_adapter.tx_busy = 0U;
    g_task_uart_adapter.tx_operator_active = 0U;
    if (hal_status == HAL_BUSY) { return TASK_UART_TX_BUSY; }
    g_task_uart_adapter.snapshot.tx_error_count++;
    g_task_uart_adapter.snapshot.operator_drop_count++;
    return TASK_UART_TX_HAL_ERROR;
  }
  g_task_uart_adapter.snapshot.tx_start_count++;
  g_task_uart_adapter.snapshot.last_tx_request_id = g_task_uart_adapter.pending_operator.request_id;
  g_task_uart_adapter.snapshot.last_tx_frame_length = length;
  return TASK_UART_TX_STARTED;
}

uint8_t TaskUart_AdapterTakeOperator(OperatorWire_Request *request)
{
  uint32_t mask = TaskUart_PlatformEnter();
  uint8_t outcome = TestTask_ReceiverTakeOperator(&g_task_uart_adapter.receiver, request);
  TaskUart_PlatformExit(mask);
  return outcome;
}

TaskUart_TxResultTypeDef TaskUart_AdapterQueueOperator(const OperatorWire_Reply *reply)
{
  uint32_t mask;
  TaskUart_TxResultTypeDef outcome;
  if (reply == 0) { return TASK_UART_TX_ERROR_NULL_ARGUMENT; }
  mask = TaskUart_PlatformEnter();
  if (g_task_uart_adapter.initialized == 0U)
  { outcome = TASK_UART_TX_ERROR_NOT_INITIALIZED; }
  else if (g_task_uart_adapter.operator_pending != 0U)
  { g_task_uart_adapter.snapshot.operator_drop_count++; outcome = TASK_UART_TX_BUSY; }
  else
  {
    g_task_uart_adapter.pending_operator = *reply;
    g_task_uart_adapter.operator_pending = 1U;
    g_task_uart_adapter.snapshot.operator_queued_count++;
    outcome = TASK_UART_TX_QUEUED;
  }
  TaskUart_PlatformExit(mask);
  return outcome;
}

TaskUart_AdapterResultTypeDef TaskUart_AdapterInit(UART_HandleTypeDef *huart, uint32_t frame_timeout_ms)
{
  uint32_t mask = TaskUart_PlatformEnter();
  TaskUart_AdapterResultTypeDef outcome = TaskUart_AdapterInitLocked(huart, frame_timeout_ms);
  TaskUart_PlatformExit(mask);
  return outcome;
}

TaskUart_AdapterResultTypeDef TaskUart_AdapterStart(void)
{
  uint32_t mask = TaskUart_PlatformEnter();
  TaskUart_AdapterResultTypeDef outcome = TaskUart_AdapterStartLocked();
  TaskUart_PlatformExit(mask);
  return outcome;
}

TaskUart_AdapterResultTypeDef TaskUart_AdapterOnRxComplete(UART_HandleTypeDef *huart, uint32_t now_ms)
{
  uint32_t mask = TaskUart_PlatformEnter();
  TaskUart_AdapterResultTypeDef outcome = TaskUart_AdapterOnRxCompleteLocked(huart, TaskUart_PlatformNowMs(now_ms));
  TaskUart_PlatformExit(mask);
  return outcome;
}

TaskUart_AdapterResultTypeDef TaskUart_AdapterOnError(UART_HandleTypeDef *huart)
{
  uint32_t mask = TaskUart_PlatformEnter();
  TaskUart_AdapterResultTypeDef outcome = TaskUart_AdapterOnErrorLocked(huart);
  TaskUart_PlatformExit(mask);
  return outcome;
}

TaskUart_AdapterResultTypeDef TaskUart_AdapterService(uint32_t now_ms)
{
  uint32_t mask = TaskUart_PlatformEnter();
  TaskUart_AdapterResultTypeDef outcome = TaskUart_AdapterServiceLocked(TaskUart_PlatformNowMs(now_ms));
  TaskUart_PlatformExit(mask);
  return outcome;
}

uint8_t TaskUart_AdapterTakeTask(TestTask_DescriptorTypeDef *task)
{
  uint32_t mask = TaskUart_PlatformEnter();
  uint8_t outcome = TaskUart_AdapterTakeTaskLocked(task);
  TaskUart_PlatformExit(mask);
  return outcome;
}

uint8_t TaskUart_AdapterTakeStatusQuery(ControlStatus_QueryTypeDef *query)
{
  uint32_t mask = TaskUart_PlatformEnter();
  uint8_t outcome = TestTask_ReceiverTakeStatusQuery(
      &g_task_uart_adapter.receiver, query);
  TaskUart_PlatformExit(mask);
  return outcome;
}

uint8_t TaskUart_AdapterTakeTelemetryQuery(Telemetry_QueryTypeDef *query)
{
  uint32_t mask = TaskUart_PlatformEnter();
  uint8_t outcome = TestTask_ReceiverTakeTelemetryQuery(
      &g_task_uart_adapter.receiver, query);
  TaskUart_PlatformExit(mask);
  return outcome;
}

TaskUart_TxResultTypeDef TaskUart_AdapterSendResult(const TestTask_ResultDescriptorTypeDef *result)
{
  uint32_t mask = TaskUart_PlatformEnter();
  TaskUart_TxResultTypeDef outcome = TaskUart_AdapterSendResultLocked(result);
  TaskUart_PlatformExit(mask);
  return outcome;
}

TaskUart_TxResultTypeDef TaskUart_AdapterQueueStop(const TestTask_StopDescriptorTypeDef *stop)
{
  uint32_t mask = TaskUart_PlatformEnter();
  TaskUart_TxResultTypeDef outcome = TaskUart_AdapterQueueStopLocked(stop);
  TaskUart_PlatformExit(mask);
  return outcome;
}

TaskUart_TxResultTypeDef TaskUart_AdapterQueueStatus(
    const ControlStatus_ReplyTypeDef *status)
{
  uint32_t mask = TaskUart_PlatformEnter();
  TaskUart_TxResultTypeDef outcome = TaskUart_AdapterQueueStatusLocked(status);
  TaskUart_PlatformExit(mask);
  return outcome;
}

TaskUart_TxResultTypeDef TaskUart_AdapterQueueTelemetry(
    const Telemetry_ReplyTypeDef *telemetry)
{
  uint32_t mask = TaskUart_PlatformEnter();
  TaskUart_TxResultTypeDef outcome =
      TaskUart_AdapterQueueTelemetryLocked(telemetry);
  TaskUart_PlatformExit(mask);
  return outcome;
}

TaskUart_TxResultTypeDef TaskUart_AdapterOnTxComplete(UART_HandleTypeDef *huart)
{
  uint32_t mask = TaskUart_PlatformEnter();
  TaskUart_TxResultTypeDef outcome = TaskUart_AdapterOnTxCompleteLocked(huart);
  TaskUart_PlatformExit(mask);
  return outcome;
}
