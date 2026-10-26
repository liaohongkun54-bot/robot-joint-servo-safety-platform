#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "test_task_receiver.h"

static TestTask_ReceiverResultTypeDef Feed(TestTask_ReceiverTypeDef *r,
    const uint8_t *frame, uint16_t n, uint32_t tick)
{
  uint16_t i;
  TestTask_ReceiverResultTypeDef result=TEST_TASK_RECEIVER_NO_EVENT;
  for(i=0U;i<n;i++) { result=TestTask_ReceiverPushByte(r,frame[i],tick); }
  return result;
}
int main(void)
{
  TestTask_ReceiverTypeDef r;
  OperatorWire_Request request={0}, taken;
  uint8_t frame[OPERATOR_WIRE_LENGTH];
  uint8_t query[30];
  uint16_t n;
  TestTask_ReceiverInit(&r,100U);
  request.request_id=1U; request.operation=OPERATOR_OPEN;
  assert(OperatorWire_RequestEncode(&request,frame,sizeof(frame),&n)==OPERATOR_WIRE_OK);
  assert(Feed(&r,frame,30U,0U)==TEST_TASK_RECEIVER_NO_EVENT);
  assert(!r.operator_pending);
  assert(Feed(&r,frame+30,26U,1U)==TEST_TASK_RECEIVER_OPERATOR_READY);
  assert(r.operator_pending && r.frame_index==0U);
  request.request_id=2U;
  assert(OperatorWire_RequestEncode(&request,frame,sizeof(frame),&n)==OPERATOR_WIRE_OK);
  assert(Feed(&r,frame,n,2U)==TEST_TASK_RECEIVER_OPERATOR_REJECTED);
  assert(r.snapshot.operator_drop_count==1U);
  request.operation=OPERATOR_STOP; request.boot_low=1U;
  request.session_id=7U; request.sequence=9U;
  assert(OperatorWire_RequestEncode(&request,frame,sizeof(frame),&n)==OPERATOR_WIRE_OK);
  assert(Feed(&r,frame,n,3U)==TEST_TASK_RECEIVER_OPERATOR_READY);
  assert(TestTask_ReceiverTakeOperator(&r,&taken)==1U && taken.operation==OPERATOR_STOP);
  assert(TestTask_ReceiverTakeOperator(&r,&taken)==1U && taken.operation==OPERATOR_OPEN);
  assert(TestTask_ReceiverTakeOperator(&r,&taken)==0U);
  frame[54]^=1U;
  assert(Feed(&r,frame,n,4U)==TEST_TASK_RECEIVER_OPERATOR_REJECTED);
  assert(!r.operator_pending && !r.operator_stop_pending);
  frame[54]^=1U;
  assert(Feed(&r,frame,35U,5U)==TEST_TASK_RECEIVER_NO_EVENT);
  assert(TestTask_ReceiverService(&r,105U)==TEST_TASK_RECEIVER_OPERATOR_REJECTED);
  assert(Feed(&r,frame,n,106U)==TEST_TASK_RECEIVER_OPERATOR_READY);
  assert(TestTask_ReceiverTakeOperator(&r,&taken)==1U);
  /* A 30-byte old query immediately after 56-byte traffic retains its boundary. */
  memset(query,0,sizeof(query)); query[0]=0x4bU;query[1]=0x54U;query[2]=1U;
  query[3]=4U;query[4]=22U; /* deliberately invalid CRC */
  assert(Feed(&r,query,sizeof(query),107U)==TEST_TASK_RECEIVER_STATUS_QUERY_REJECTED);
  assert(Feed(&r,frame,n,108U)==TEST_TASK_RECEIVER_OPERATOR_READY);
  assert(r.snapshot.operator_count==4U);
  puts("operator_receiver: bounded mailboxes, priority, corruption and recovery passed");
  return 0;
}
