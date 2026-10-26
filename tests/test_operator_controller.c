#include <assert.h>
#include <stdio.h>
#include "operator_controller.h"

typedef struct { unsigned stops, clears; uint8_t stop_ok, clear_ok, reason; } Fake;
static uint8_t Stop(void *p, uint32_t now, uint8_t reason)
{ Fake *f=p; (void)now; f->stops++; f->reason=reason; return f->stop_ok; }
static uint8_t Clear(void *p)
{ Fake *f=p; f->clears++; return f->clear_ok; }
static TestTask_DescriptorTypeDef Task(void)
{ TestTask_DescriptorTypeDef t={1U,42U,1U,TEST_TASK_ACTION_CURRENT_TEST,1000,1000,1000U,100U,TEST_TASK_TIMEOUT_STOP,TEST_TASK_PERMISSION_OPERATOR}; return t; }

int main(void)
{
  OperatorController c;
  OperatorSession_Conditions cond={1U,0U,0U,64U};
  OperatorWire_Request r={0};
  OperatorWire_Reply reply;
  TestTask_DescriptorTypeDef task=Task();
  Fake fake={0,0,1U,1U,0U};
  uint32_t first_session;
  OperatorController_Init(&c,0U,0U,Stop,Clear,&fake);
  r.request_id=1U;r.nonce=99U;r.operation=OPERATOR_OPEN;
  assert(!OperatorController_Handle(&c,&r,0U,&cond,&reply));
  assert(!OperatorController_Permitted(&c)); /* No boot identity, no permission. */
  OperatorController_Init(&c,123U,456U,Stop,Clear,&fake);
  cond.motion_active=1U;
  OperatorController_Tick(&c,0U,&cond);
  assert(fake.stops==1U && !OperatorController_Permitted(&c));
  cond.motion_active=0U;
  assert(OperatorController_Handle(&c,&r,0U,&cond,&reply));
  assert(reply.result==OPERATOR_OK && reply.flags==1U);
  first_session=reply.session_id;
  r.operation=OPERATOR_GRANT;r.boot_low=123U;r.boot_high=456U;
  r.session_id=first_session;r.sequence=1U;r.task=task;
  assert(OperatorController_Handle(&c,&r,1U,&cond,&reply));
  assert(reply.result==OPERATOR_OK && reply.flags==3U && reply.task_request_id==42U);
  assert(OperatorController_Permitted(&c) && fake.stops==1U);
  assert(OperatorController_Consume(&c,&task,2U,&cond)==OPERATOR_OK);
  assert(OperatorController_Consume(&c,&task,3U,&cond)==OPERATOR_ERROR_GRANT);
  cond.motion_active=1U;
  r.operation=OPERATOR_STOP;r.sequence=3U;fake.stop_ok=0U;
  assert(OperatorController_Handle(&c,&r,4U,&cond,&reply));
  assert(reply.result==OPERATOR_OK && (reply.flags & 8U)!=0U);
  assert(fake.reason==OPERATOR_STOP_REQUESTED && c.stop_failure_count==1U);
  assert(!OperatorController_Permitted(&c));
  fake.stop_ok=1U;
  OperatorController_Tick(&c,5U,&cond);
  assert(!c.session.executing && !c.session.pending_stop);
  cond.motion_active=0U;
  r.operation=OPERATOR_GRANT;r.sequence=2U;
  assert(OperatorController_Handle(&c,&r,6U,&cond,&reply));
  assert(reply.result==OPERATOR_ERROR_SEQUENCE); /* Queued older grant cannot undo STOP. */
  r.operation=OPERATOR_CLEAR;r.sequence=4U;
  assert(OperatorController_Handle(&c,&r,7U,&cond,&reply));
  assert(reply.result==OPERATOR_ERROR_STATE && fake.clears==0U);
  cond.can_clear_fault=1U;r.sequence=5U;
  assert(OperatorController_Handle(&c,&r,8U,&cond,&reply));
  assert(reply.result==OPERATOR_OK && fake.clears==1U && !OperatorController_Permitted(&c));
  r.operation=OPERATOR_GRANT;r.sequence=6U;
  assert(OperatorController_Handle(&c,&r,9U,&cond,&reply));
  assert(OperatorController_Consume(&c,&task,10U,&cond)==OPERATOR_OK);
  cond.motion_active=1U;
  OperatorController_Tick(&c,1009U,&cond);
  assert(!c.session.connected && fake.reason==OPERATOR_STOP_SESSION_LOST);
  assert(!OperatorController_Permitted(&c));
  cond.motion_active=0U;
  r.operation=OPERATOR_OPEN;r.boot_low=0U;r.boot_high=0U;r.session_id=0U;r.sequence=0U;
  assert(OperatorController_Handle(&c,&r,1010U,&cond,&reply));
  assert(reply.session_id!=first_session);
  r.operation=OPERATOR_GRANT;r.boot_low=123U;r.boot_high=456U;r.session_id=first_session;r.sequence=7U;
  assert(OperatorController_Handle(&c,&r,1011U,&cond,&reply));
  assert(reply.result==OPERATOR_ERROR_SESSION);
  /* A different boot rejects an old valid grant even if the numeric session repeats. */
  OperatorController_Init(&c,124U,456U,Stop,Clear,&fake);
  assert(OperatorController_Handle(&c,&r,1012U,&cond,&reply));
  assert(reply.result==OPERATOR_ERROR_SESSION);
  assert(fake.stops==4U);
  puts("operator_controller: offline dispatch, precise grant, stop failure and stale-session tests passed");
  return 0;
}
