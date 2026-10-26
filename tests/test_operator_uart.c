#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "task_uart_adapter.h"

static uint8_t *rx;
static uint8_t sent[OPERATOR_WIRE_LENGTH];
static uint16_t sent_length;
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h,uint8_t *data,uint16_t n)
{ (void)h; assert(n==1U); rx=data;return HAL_OK; }
uint32_t HAL_UART_GetError(const UART_HandleTypeDef *h) { return h->ErrorCode; }
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *h,const uint8_t *data,uint16_t n)
{
  assert(n<=sizeof(sent));
  if(h->gState==HAL_UART_STATE_BUSY_TX) { return HAL_BUSY; }
  memcpy(sent,data,n);sent_length=n;h->gState=HAL_UART_STATE_BUSY_TX;return HAL_OK;
}
static void Complete(UART_HandleTypeDef *h)
{ h->gState=HAL_UART_STATE_READY;assert(TaskUart_AdapterOnTxComplete(h)==TASK_UART_TX_COMPLETE); }
int main(void)
{
  UART_HandleTypeDef uart={0};
  OperatorWire_Request request={0},taken;
  OperatorWire_Reply reply={0},decoded;
  TestTask_ResultDescriptorTypeDef result={0};
  uint8_t frame[OPERATOR_WIRE_LENGTH],saved[OPERATOR_WIRE_LENGTH];
  uint16_t length,i;
  assert(TaskUart_AdapterInit(&uart,100U)==TASK_UART_ADAPTER_OK);
  (void)TaskUart_AdapterStart();
  request.request_id=1U;request.nonce=33U;request.operation=OPERATOR_OPEN;
  assert(OperatorWire_RequestEncode(&request,frame,sizeof(frame),&length)==OPERATOR_WIRE_OK);
  for(i=0U;i<length;i++) {
    TaskUart_AdapterResultTypeDef received;
    *rx=frame[i]; received=TaskUart_AdapterOnRxComplete(&uart,1U);
    if(i==length-1U) { assert(received==TASK_UART_ADAPTER_OPERATOR_READY); }
  }
  assert(TaskUart_AdapterTakeOperator(&taken)==1U && taken.nonce==33U);
  reply.request_id=1U;reply.nonce=33U;reply.boot_low=4U;reply.session_id=1U;
  reply.operation=OPERATOR_OPEN;reply.flags=1U;reply.session_remaining_ms=1000U;
  assert(TaskUart_AdapterQueueOperator(&reply)==TASK_UART_TX_QUEUED);
  assert(TaskUart_AdapterQueueOperator(&reply)==TASK_UART_TX_BUSY);
  (void)TaskUart_AdapterService(2U);
  assert(g_task_uart_adapter.tx_operator_active && sent_length==56U);
  assert(OperatorWire_ReplyDecode(sent,sent_length,&decoded)==OPERATOR_WIRE_OK);
  assert(decoded.nonce==33U);
  memcpy(saved,g_task_uart_adapter.tx_frame,sizeof(saved));
  (void)TaskUart_AdapterOnError(&uart);
  assert(g_task_uart_adapter.tx_busy && memcmp(saved,g_task_uart_adapter.tx_frame,sizeof(saved))==0);
  result.request_id=42U;result.status=TEST_TASK_RESULT_STATUS_REJECTED;
  result.stage=TEST_TASK_RESULT_STAGE_SAFETY;result.error_code=0xE2U;
  assert(TaskUart_AdapterSendResult(&result)==TASK_UART_TX_QUEUED);
  assert(g_task_uart_adapter.snapshot.result_queued_behind_operator_count==1U);
  Complete(&uart);
  (void)TaskUart_AdapterService(3U);
  assert(sent_length==24U && !g_task_uart_adapter.tx_operator_active);
  Complete(&uart);
  assert(g_task_uart_adapter.snapshot.operator_tx_complete_count==1U);
  puts("operator_uart: RX handoff, bounded reply, TX ownership and task-result handoff passed");
  return 0;
}
