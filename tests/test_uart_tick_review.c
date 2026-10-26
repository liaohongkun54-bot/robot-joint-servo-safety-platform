#include <stdio.h>
#include <string.h>
#include "task_uart_adapter.h"

#ifndef UART_REVIEW_GUARD
#define UART_REVIEW_GUARD 0
#endif

#define CHECK(expr) do { if (!(expr)) { \
  printf("FAIL line=%d: %s\n", __LINE__, #expr); return 1; } } while (0)

static UART_HandleTypeDef uart;
static uint8_t *rx_target;
static uint8_t tx_copy[TEST_TASK_STOP_FRAME_LENGTH];
static uint16_t tx_length;
static uint8_t complete_in_hal;
static HAL_StatusTypeDef transmit_status = HAL_OK;
static const uint8_t *active_tx_buffer;

HAL_StatusTypeDef HAL_UART_Receive_IT(
    UART_HandleTypeDef *huart, uint8_t *data, uint16_t size)
{
  (void)huart;
  if (size != 1U) { return HAL_ERROR; }
  rx_target = data;
  return HAL_OK;
}

uint32_t HAL_UART_GetError(const UART_HandleTypeDef *huart)
{
  return huart->ErrorCode;
}

HAL_StatusTypeDef HAL_UART_Transmit_IT(
    UART_HandleTypeDef *huart, const uint8_t *data, uint16_t size)
{
  if (transmit_status != HAL_OK) { return transmit_status; }
  if (size > sizeof(tx_copy)) { return HAL_ERROR; }
  active_tx_buffer = data;
  memcpy(tx_copy, data, size);
  tx_length = size;
  /* Deterministic callback at the HAL-call boundary, not a timing model. */
  if (complete_in_hal != 0U) { (void)TaskUart_AdapterOnTxComplete(huart); }
  return HAL_OK;
}

static int Reset(void)
{
  complete_in_hal = 0U;
  transmit_status = HAL_OK;
  tx_length = 0U;
  uart.ErrorCode = HAL_UART_ERROR_NONE;
  CHECK(TaskUart_AdapterInit(&uart, 100U) == TASK_UART_ADAPTER_OK);
  CHECK(TaskUart_AdapterStart() == TASK_UART_ADAPTER_RX_ARMED);
  return 0;
}

static TaskUart_AdapterResultTypeDef Feed(uint8_t byte, uint32_t tick)
{
  *rx_target = byte;
  return TaskUart_AdapterOnRxComplete(&uart, tick);
}

static int StaleTick(uint32_t sampled, uint32_t arrival, const char *name)
{
  TaskUart_AdapterResultTypeDef result;
  TestTask_ResultDescriptorTypeDef decoded;
  CHECK(Reset() == 0);
  CHECK(Feed(0x4BU, arrival) == TASK_UART_ADAPTER_OK);
  CHECK(g_task_uart_adapter.receiver.receiving == 1U);
  CHECK(g_task_uart_adapter.receiver.frame_start_tick_ms == arrival);
  result = TaskUart_AdapterService(sampled);
#if UART_REVIEW_GUARD
  (void)decoded;
  CHECK(result == TASK_UART_ADAPTER_OK);
  CHECK(g_task_uart_adapter.receiver.receiving == 1U);
  CHECK(g_task_uart_adapter.receiver.snapshot.timeout_count == 0U);
  CHECK(tx_length == 0U);
#else
  CHECK(result == TASK_UART_ADAPTER_FRAME_TIMEOUT);
  CHECK(g_task_uart_adapter.receiver.receiving == 0U);
  CHECK(g_task_uart_adapter.receiver.snapshot.timeout_count == 1U);
  CHECK(tx_length == TEST_TASK_RESULT_FRAME_LENGTH);
  CHECK(TestTask_ResultWireDecode(tx_copy, tx_length, &decoded) ==
        TEST_TASK_RESULT_WIRE_OK);
  CHECK(decoded.request_id == 0U);
  CHECK(decoded.stage == TEST_TASK_RESULT_STAGE_TRANSPORT);
  CHECK(decoded.error_code == TEST_TASK_WIRE_ERROR_LENGTH);
#endif
  printf("%s: %s delta=%lu\n", name,
         UART_REVIEW_GUARD ? "guarded" : "false_timeout_reproduced",
         (unsigned long)(uint32_t)(sampled - arrival));
  return 0;
}

int main(void)
{
  const TestTask_DescriptorTypeDef source = {
    TEST_TASK_SCHEMA_VERSION, 1001U, TEST_TASK_TARGET_C620_ID,
    TEST_TASK_ACTION_CURRENT_TEST, 0, 100, 100U, 100U,
    TEST_TASK_TIMEOUT_STOP, TEST_TASK_PERMISSION_OPERATOR
  };
  const TestTask_ResultDescriptorTypeDef result_source = {
    1001U, 50U, 0x00E2U, 1U, TEST_TASK_RESULT_STATUS_REJECTED,
    TEST_TASK_RESULT_STAGE_SAFETY, 0U, TEST_TASK_RESULT_FLAG_OUTPUT_UNCHANGED
  };
  const TestTask_StopDescriptorTypeDef stop = {
    1001U, 150U, 0, 0, 0, 0U, 25U, 1U, 2U, 1U, 0U, 0U, 0U, 0U
  };
  TestTask_DescriptorTypeDef taken;
  uint8_t frame[TEST_TASK_WIRE_FRAME_LENGTH];
  uint16_t length = 0U;
  uint16_t i;
  TestTask_ReceiverTypeDef receiver;

  CHECK(StaleTick(100U, 101U, "stale_tick") == 0);
  CHECK(StaleTick(UINT32_MAX, 0U, "stale_tick_at_wrap") == 0);

  CHECK(Reset() == 0);
  CHECK(Feed(0x4BU, UINT32_MAX - 49U) == TASK_UART_ADAPTER_OK);
  CHECK(TaskUart_AdapterService(49U) == TASK_UART_ADAPTER_OK);
  CHECK(TaskUart_AdapterService(50U) == TASK_UART_ADAPTER_FRAME_TIMEOUT);
  CHECK(g_task_uart_adapter.receiver.snapshot.timeout_count == 1U);
  CHECK(TaskUart_AdapterOnTxComplete(&uart) == TASK_UART_TX_COMPLETE);
  CHECK(TaskUart_AdapterService(51U) == TASK_UART_ADAPTER_OK);
  CHECK(g_task_uart_adapter.receiver.snapshot.timeout_count == 1U);
  puts("real_wrap_timeout_99_100_and_once=pass");

  CHECK(TestTask_WireEncode(&source, frame, sizeof(frame), &length) ==
        TEST_TASK_WIRE_OK);
  CHECK(length == 30U);
  CHECK(Reset() == 0);
  for (i = 0U; i < length; i++) {
    TaskUart_AdapterResultTypeDef r = Feed(frame[i], 200U + i);
    CHECK(r == ((i + 1U == length) ? TASK_UART_ADAPTER_TASK_READY :
                                   TASK_UART_ADAPTER_OK));
  }
  CHECK(TaskUart_AdapterService(199U) == TASK_UART_ADAPTER_OK);
  CHECK(TaskUart_AdapterTakeTask(&taken) == 1U);
  CHECK(taken.request_id == 1001U);
  CHECK(taken.amplitude_raw == 0);
  CHECK(TaskUart_AdapterTakeTask(&taken) == 0U);
  puts("completed_frame_stale_service_and_take_once=pass");

  /* Pending A is preserved when a partial B is aborted by a UART error. */
  CHECK(Reset() == 0);
  for (i = 0U; i < length; i++) { (void)Feed(frame[i], 300U + i); }
  CHECK(Feed(frame[0], 340U) == TASK_UART_ADAPTER_OK);
  uart.ErrorCode = 0x08U;
  CHECK(TaskUart_AdapterOnError(&uart) == TASK_UART_ADAPTER_UART_ERROR);
  CHECK(g_task_uart_adapter.receiver.receiving == 0U);
  CHECK(TaskUart_AdapterService(341U) == TASK_UART_ADAPTER_OK);
  CHECK(g_task_uart_adapter.rx_armed == 1U);
  CHECK(TaskUart_AdapterTakeTask(&taken) == 1U);
  CHECK(taken.request_id == 1001U);
  puts("abort_preserves_pending_and_rearms=pass");

  TestTask_ReceiverInit(&receiver, 100U);
  for (i = 0U; i < length; i++) {
    (void)TestTask_ReceiverPushByte(&receiver, frame[i], 400U + i);
  }
  for (i = 0U; i < length; i++) {
    (void)TestTask_ReceiverPushByte(&receiver, frame[i], 450U + i);
  }
  CHECK(receiver.snapshot.pending_drop_count == 1U);
  CHECK(TestTask_ReceiverTakeTask(&receiver, &taken) == 1U);
  CHECK(TestTask_ReceiverTakeTask(&receiver, &taken) == 0U);
  puts("pending_second_frame_is_bounded=pass");

  CHECK(Reset() == 0);
  CHECK(TaskUart_AdapterSendResult(&result_source) == TASK_UART_TX_STARTED);
  CHECK(TaskUart_AdapterQueueStop(&stop) == TASK_UART_TX_QUEUED);
  CHECK(TaskUart_AdapterService(500U) == TASK_UART_ADAPTER_OK);
  CHECK(tx_length == 24U);
  CHECK(TaskUart_AdapterOnTxComplete(&uart) == TASK_UART_TX_COMPLETE);
  CHECK(TaskUart_AdapterService(501U) == TASK_UART_ADAPTER_OK);
  CHECK(tx_length == 42U);
  CHECK(TaskUart_AdapterOnTxComplete(&uart) == TASK_UART_TX_COMPLETE);
  CHECK(g_task_uart_adapter.snapshot.tx_complete_count == 2U);
  CHECK(g_task_uart_adapter.snapshot.stop_tx_complete_count == 1U);
  CHECK(g_task_uart_adapter.tx_busy == 0U);
  puts("result_stop_tx_callback_handoff=pass");

  CHECK(Reset() == 0);
  complete_in_hal = 1U;
  CHECK(TaskUart_AdapterSendResult(&result_source) == TASK_UART_TX_STARTED);
  CHECK(g_task_uart_adapter.snapshot.tx_complete_count == 1U);
  CHECK(g_task_uart_adapter.tx_busy == 0U);
  /* Caller overwrites COMPLETE with STARTED after the injected callback. */
  CHECK(g_task_uart_adapter.snapshot.last_tx_result == TASK_UART_TX_STARTED);
  puts("tx_callback_before_hal_return: diagnostic_state_overwrite_reproduced");

  /* HAL RX error may leave TX running. The active TX buffer must stay owned. */
  CHECK(Reset() == 0);
  CHECK(TaskUart_AdapterSendResult(&result_source) == TASK_UART_TX_STARTED);
  CHECK(memcmp(active_tx_buffer, tx_copy, tx_length) == 0);
  uart.ErrorCode = 0x08U;
  CHECK(TaskUart_AdapterOnError(&uart) == TASK_UART_ADAPTER_UART_ERROR);
  CHECK(g_task_uart_adapter.tx_busy == 0U);
  transmit_status = HAL_BUSY; /* Model HAL still transmitting the first frame. */
  {
    TestTask_ResultDescriptorTypeDef next = result_source;
    next.request_id = 1002U;
    CHECK(TaskUart_AdapterSendResult(&next) == TASK_UART_TX_HAL_ERROR);
    CHECK(memcmp(active_tx_buffer, tx_copy, tx_length) != 0);
  }
  puts("rx_error_while_tx_active: active_tx_buffer_overwrite_reproduced");
  printf("uart_tick_review=pass mode=%s cases=9\n",
         UART_REVIEW_GUARD ? "temporary_arithmetic_guard" : "current_source");
  return 0;
}
