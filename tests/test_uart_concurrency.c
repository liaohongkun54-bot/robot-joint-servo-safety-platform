#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "task_uart_adapter.h"
#include "task_uart_platform.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL line=%d %s\n", __LINE__, #x); return 1; } } while (0)
static UART_HandleTypeDef uart;
static uint32_t depth;
static uint32_t clock_ms;
static uint8_t *rx;
static const uint8_t *active_tx;
static uint8_t tx_copy[42];
static uint16_t tx_length;
static uint8_t complete_on_transmit;
static uint8_t complete_deferred;
static uint8_t rx_before_enter;
static uint8_t last_byte_on_exit;
static uint8_t queued_byte;
static uint32_t queued_tick;

static void Complete(void)
{
  uart.gState = HAL_UART_STATE_READY;
  (void)TaskUart_AdapterOnTxComplete(&uart);
}

uint32_t TaskUart_PlatformEnter(void)
{
  /* Injection BEFORE mask takes effect: an ISR advanced the receive clock. */
  if (rx_before_enter != 0U && depth == 0U) {
    rx_before_enter = 0U;
    clock_ms = queued_tick;
    uart.RxState = HAL_UART_STATE_READY;
    *rx = queued_byte;
    (void)TaskUart_AdapterOnRxComplete(&uart, clock_ms);
  }
  return depth++;
}

void TaskUart_PlatformExit(uint32_t saved)
{
  assert(depth == saved + 1U);
  depth = saved;
  if (depth == 0U && complete_deferred != 0U) {
    complete_deferred = 0U;
    Complete();
  }
  if (depth == 0U && last_byte_on_exit != 0U) {
    last_byte_on_exit = 0U;
    uart.RxState = HAL_UART_STATE_READY;
    *rx = queued_byte;
    (void)TaskUart_AdapterOnRxComplete(&uart, clock_ms);
  }
}

uint32_t TaskUart_PlatformNowMs(uint32_t legacy)
{
  (void)legacy;
  assert(depth != 0U);
  return clock_ms;
}

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h, uint8_t *data, uint16_t size)
{
  assert(depth != 0U);
  if (h->RxState == HAL_UART_STATE_BUSY_RX) { return HAL_BUSY; }
  if (size != 1U) { return HAL_ERROR; }
  rx = data;
  h->RxState = HAL_UART_STATE_BUSY_RX;
  return HAL_OK;
}
uint32_t HAL_UART_GetError(const UART_HandleTypeDef *h) { return h->ErrorCode; }
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *h, const uint8_t *data, uint16_t size)
{
  assert(depth != 0U);
  if (h->gState == HAL_UART_STATE_BUSY_TX) { return HAL_BUSY; }
  assert(size <= sizeof(tx_copy));
  active_tx = data;
  tx_length = size;
  memcpy(tx_copy, data, size);
  h->gState = HAL_UART_STATE_BUSY_TX;
  if (complete_on_transmit != 0U) { complete_deferred = 1U; }
  return HAL_OK;
}

static void Reset(void)
{
  assert(depth == 0U);
  memset(&uart, 0, sizeof(uart));
  uart.gState = uart.RxState = HAL_UART_STATE_READY;
  complete_on_transmit = complete_deferred = rx_before_enter = last_byte_on_exit = 0U;
  tx_length = 0U;
  assert(TaskUart_AdapterInit(&uart, 100U) == TASK_UART_ADAPTER_OK);
  assert(TaskUart_AdapterStart() == TASK_UART_ADAPTER_RX_ARMED);
}
static TaskUart_AdapterResultTypeDef Feed(uint8_t b, uint32_t tick)
{
  clock_ms = tick;
  uart.RxState = HAL_UART_STATE_READY;
  *rx = b;
  return TaskUart_AdapterOnRxComplete(&uart, tick);
}

int main(void)
{
  const TestTask_DescriptorTypeDef task = {
    TEST_TASK_SCHEMA_VERSION, 1001U, TEST_TASK_TARGET_C620_ID,
    TEST_TASK_ACTION_CURRENT_TEST, 0, 100, 100U, 100U,
    TEST_TASK_TIMEOUT_STOP, TEST_TASK_PERMISSION_OPERATOR
  };
  const TestTask_ResultDescriptorTypeDef result = {
    1001U, 50U, 0x00E2U, 1U, TEST_TASK_RESULT_STATUS_REJECTED,
    TEST_TASK_RESULT_STAGE_SAFETY, 0U, TEST_TASK_RESULT_FLAG_OUTPUT_UNCHANGED
  };
  const TestTask_StopDescriptorTypeDef stop = {
    1001U, 150U, 0, 0, 0, 0U, 25U, 1U, 2U, 1U, 0U, 0U, 0U, 0U
  };
  uint8_t frame[30];
  uint16_t length, i;
  TestTask_DescriptorTypeDef taken;
  TestTask_StopDescriptorTypeDef decoded_stop;
  TestTask_ResultDescriptorTypeDef decoded_result;

  Reset();
  clock_ms = 100U; queued_tick = 101U; queued_byte = 0x4BU; rx_before_enter = 1U;
  CHECK(TaskUart_AdapterService(100U) == TASK_UART_ADAPTER_OK);
  CHECK(g_task_uart_adapter.receiver.receiving == 1U);
  CHECK(g_task_uart_adapter.receiver.frame_start_tick_ms == 101U);
  CHECK(g_task_uart_adapter.receiver.snapshot.timeout_count == 0U && tx_length == 0U);
  puts("old_tick_ISR_before_mask=pass");

  Reset();
  clock_ms = UINT32_MAX; queued_tick = 0U; rx_before_enter = 1U;
  CHECK(TaskUart_AdapterService(UINT32_MAX) == TASK_UART_ADAPTER_OK);
  CHECK(g_task_uart_adapter.receiver.receiving == 1U);
  clock_ms = 99U;
  CHECK(TaskUart_AdapterService(UINT32_MAX) == TASK_UART_ADAPTER_OK);
  clock_ms = 100U;
  CHECK(TaskUart_AdapterService(UINT32_MAX) == TASK_UART_ADAPTER_FRAME_TIMEOUT);
  CHECK(g_task_uart_adapter.receiver.snapshot.timeout_count == 1U);
  CHECK(TestTask_ResultWireDecode(tx_copy, tx_length, &decoded_result) == TEST_TASK_RESULT_WIRE_OK);
  CHECK(decoded_result.stage == TEST_TASK_RESULT_STAGE_TRANSPORT);
  CHECK(decoded_result.error_code == TEST_TASK_WIRE_ERROR_LENGTH);
  Complete(); clock_ms = 101U;
  CHECK(TaskUart_AdapterService(0U) == TASK_UART_ADAPTER_OK);
  CHECK(g_task_uart_adapter.receiver.snapshot.timeout_count == 1U);
  puts("wrap_99_100_and_timeout_once=pass");

  CHECK(TestTask_WireEncode(&task, frame, sizeof(frame), &length) == TEST_TASK_WIRE_OK);
  Reset();
  for (i=0U; i<length-1U; i++) { (void)Feed(frame[i], 200U+i); }
  queued_byte=frame[length-1U]; last_byte_on_exit=1U;
  CHECK(TaskUart_AdapterTakeTask(&taken) == 0U); /* Completed only AFTER atomic take. */
  CHECK(TaskUart_AdapterTakeTask(&taken) == 1U && taken.request_id == 1001U);
  CHECK(TaskUart_AdapterTakeTask(&taken) == 0U);
  puts("pending_last_byte_after_take=pass");

  Reset();
  for (i=0U; i<length; i++) { (void)Feed(frame[i], 300U+i); }
  (void)Feed(0x4BU, 340U);
  uart.RxState = HAL_UART_STATE_READY; uart.ErrorCode=0x08U;
  CHECK(TaskUart_AdapterOnError(&uart) == TASK_UART_ADAPTER_UART_ERROR);
  CHECK(g_task_uart_adapter.receiver.receiving == 0U);
  CHECK(TaskUart_AdapterTakeTask(&taken) == 1U && taken.request_id == 1001U);
  clock_ms=341U;
  CHECK(TaskUart_AdapterService(0U) == TASK_UART_ADAPTER_OK);
  CHECK(g_task_uart_adapter.rx_armed == 1U);
  puts("ORE_abort_preserves_pending_and_rearms=pass");

  Reset();
  CHECK(TaskUart_AdapterOnError(&uart) == TASK_UART_ADAPTER_UART_ERROR);
  CHECK(g_task_uart_adapter.rx_armed == 1U); /* Non-blocking RX error, HAL still busy. */
  CHECK(TaskUart_AdapterService(0U) == TASK_UART_ADAPTER_OK);
  CHECK(g_task_uart_adapter.snapshot.arm_error_count == 0U);
  puts("nonblocking_RX_error_no_double_arm=pass");

  Reset(); complete_on_transmit=1U;
  CHECK(TaskUart_AdapterSendResult(&result) == TASK_UART_TX_STARTED);
  CHECK(g_task_uart_adapter.tx_busy == 0U);
  CHECK(g_task_uart_adapter.snapshot.tx_complete_count == 1U);
  CHECK(g_task_uart_adapter.snapshot.last_tx_result == TASK_UART_TX_COMPLETE);
  CHECK(TaskUart_AdapterOnTxComplete(&uart) == TASK_UART_TX_IDLE);
  CHECK(g_task_uart_adapter.snapshot.tx_complete_count == 1U);
  puts("TX_deferred_until_unlock_and_duplicate_ignored=pass");

  Reset();
  CHECK(TaskUart_AdapterSendResult(&result) == TASK_UART_TX_STARTED);
  uart.ErrorCode=0x08U; uart.RxState=HAL_UART_STATE_READY;
  CHECK(TaskUart_AdapterOnError(&uart) == TASK_UART_ADAPTER_UART_ERROR);
  CHECK(g_task_uart_adapter.tx_busy == 1U);
  CHECK(TaskUart_AdapterSendResult(&result) == TASK_UART_TX_BUSY);
  CHECK(memcmp(active_tx, tx_copy, tx_length) == 0);
  CHECK(TaskUart_AdapterQueueStop(&stop) == TASK_UART_TX_QUEUED);
  clock_ms=500U; CHECK(TaskUart_AdapterService(0U) == TASK_UART_ADAPTER_OK);
  CHECK(tx_length == 24U && memcmp(active_tx, tx_copy, tx_length) == 0);
  Complete(); clock_ms=501U;
  CHECK(TaskUart_AdapterService(0U) == TASK_UART_ADAPTER_OK);
  CHECK(tx_length == 42U);
  CHECK(TestTask_StopWireDecode(tx_copy, tx_length, &decoded_stop) == TEST_TASK_RESULT_WIRE_OK);
  CHECK(decoded_stop.request_id == 1001U);
  Complete();
  CHECK(g_task_uart_adapter.snapshot.tx_complete_count == 2U);
  CHECK(g_task_uart_adapter.snapshot.stop_tx_complete_count == 1U);
  CHECK(g_task_uart_adapter.snapshot.tx_error_count == 0U);
  puts("RX_error_preserves_TX_buffer_then_stop_handoff=pass");

  Reset(); memset(g_task_uart_adapter.tx_frame, 0xA5, sizeof(g_task_uart_adapter.tx_frame));
  uart.gState=HAL_UART_STATE_BUSY_TX;
  CHECK(TaskUart_AdapterSendResult(&result) == TASK_UART_TX_BUSY);
  for(i=0U; i<42U; i++) { CHECK(g_task_uart_adapter.tx_frame[i] == 0xA5U); }
  CHECK(TaskUart_AdapterQueueStop(&stop) == TASK_UART_TX_QUEUED);
  CHECK(TaskUart_AdapterService(0U) == TASK_UART_ADAPTER_OK);
  CHECK(g_task_uart_adapter.stop_pending == 1U);
  for(i=0U; i<42U; i++) { CHECK(g_task_uart_adapter.tx_frame[i] == 0xA5U); }
  CHECK(depth == 0U);
  puts("unexpected_HAL_busy_does_not_encode_or_drop_stop=pass");
  puts("uart_concurrency_interleaving_tests=pass cases=8");
  return 0;
}
