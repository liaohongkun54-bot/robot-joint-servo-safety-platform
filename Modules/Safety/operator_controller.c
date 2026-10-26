#include "operator_controller.h"
#include <string.h>

void OperatorController_Init(OperatorController *c, uint32_t low, uint32_t high,
    OperatorController_StopFn stop, OperatorController_ClearFn clear, void *context)
{
  if (c == 0) { return; }
  memset(c, 0, sizeof(*c));
  c->boot_low=low; c->boot_high=high; c->next_session_id=1U;
  c->stop=stop; c->clear=clear; c->context=context;
  c->ready=((low!=0U || high!=0U) && stop!=0 && clear!=0) ? 1U : 0U;
}

static void StopPending(OperatorController *c, uint32_t now,
    const OperatorSession_Conditions *conditions)
{
  uint8_t reason;
  if (c->session.pending_stop == 0U) { return; }
  reason=(conditions==0 || conditions->safe_to_enable==0U) ? OPERATOR_STOP_LOCAL_SAFETY :
      ((c->session.connected==0U) ? OPERATOR_STOP_SESSION_LOST : OPERATOR_STOP_REQUESTED);
  if (c->stop==0 || c->stop(c->context,now,reason)==0U)
  { c->stop_failure_count++; OperatorSession_Stopped(&c->session,0U); }
  else { OperatorSession_Stopped(&c->session,1U); }
}

void OperatorController_Tick(OperatorController *c, uint32_t now,
    const OperatorSession_Conditions *conditions)
{
  if (c == 0) { return; }
  OperatorSession_Tick(&c->session,now,conditions);
  if (conditions!=0 && conditions->motion_active!=0U &&
      OperatorController_Permitted(c)==0U)
  { c->session.pending_stop=1U; }
  StopPending(c,now,conditions);
}

static OperatorSession_Result Handle(OperatorController *c,
    const OperatorWire_Request *r, uint32_t now, const OperatorSession_Conditions *conditions)
{
  OperatorSession_Result result;
  if (r->operation==OPERATOR_OPEN)
  {
    if (r->boot_low!=0U || r->boot_high!=0U || r->session_id!=0U || r->sequence!=0U ||
        c->next_session_id==0U) { return OPERATOR_ERROR_SESSION; }
    result=OperatorSession_Open(&c->session,c->next_session_id,now,conditions);
    if (result==OPERATOR_OK) { c->next_session_id++; }
    return result;
  }
  if (r->boot_low!=c->boot_low || r->boot_high!=c->boot_high)
  { return OPERATOR_ERROR_SESSION; }
  switch(r->operation)
  {
    case OPERATOR_HEARTBEAT:
      return OperatorSession_Heartbeat(&c->session,r->session_id,r->sequence,now,conditions);
    case OPERATOR_GRANT:
      return OperatorSession_Grant(&c->session,r->session_id,r->sequence,now,conditions,&r->task);
    case OPERATOR_STOP:
      return OperatorSession_Stop(&c->session,r->session_id,r->sequence,now,conditions);
    case OPERATOR_CLEAR:
      result=OperatorSession_Clear(&c->session,r->session_id,r->sequence,now,conditions);
      if (result==OPERATOR_OK && c->clear(c->context)==0U) { result=OPERATOR_ERROR_STATE; }
      return result;
    case OPERATOR_CLOSE:
      result=OperatorSession_Stop(&c->session,r->session_id,r->sequence,now,conditions);
      if (result==OPERATOR_OK) { OperatorSession_Close(&c->session); }
      return result;
    default: return OPERATOR_ERROR_ARGUMENT;
  }
}

uint8_t OperatorController_Handle(OperatorController *c,
    const OperatorWire_Request *r, uint32_t now,
    const OperatorSession_Conditions *conditions, OperatorWire_Reply *reply)
{
  OperatorSession_Result result;
  if (c==0 || r==0 || conditions==0 || reply==0 || c->ready==0U || r->request_id==0U ||
      r->operation<OPERATOR_OPEN || r->operation>OPERATOR_CLOSE) { return 0U; }
  OperatorController_Tick(c,now,conditions);
  result=Handle(c,r,now,conditions);
  StopPending(c,now,conditions);
  c->processed_count++;
  if (result!=OPERATOR_OK) { c->rejected_count++; }
  memset(reply,0,sizeof(*reply));
  reply->request_id=r->request_id; reply->nonce=r->nonce;
  reply->boot_low=c->boot_low; reply->boot_high=c->boot_high;
  reply->session_id=c->session.session_id;
  reply->sequence=r->sequence; reply->timestamp_ms=now;
  reply->operation=r->operation; reply->result=(uint8_t)result;
  reply->flags=(uint16_t)((c->session.connected ? 1U : 0U) |
      (c->session.granted ? 2U : 0U) | (c->session.executing ? 4U : 0U) |
      (c->session.pending_stop ? 8U : 0U));
  if (c->session.granted || c->session.executing || c->session.pending_stop)
  { reply->task_request_id=c->session.task.request_id; }
  if (c->session.granted)
  { reply->grant_remaining_ms=OPERATOR_GRANT_TIMEOUT_MS-(uint32_t)(now-c->session.grant_tick_ms); }
  if (c->session.connected)
  { reply->session_remaining_ms=OPERATOR_SESSION_TIMEOUT_MS-(uint32_t)(now-c->session.last_contact_ms); }
  /* Caller supplies local fault_code from the same control-owner snapshot. */
  return 1U;
}

OperatorSession_Result OperatorController_Consume(OperatorController *c,
    const TestTask_DescriptorTypeDef *task, uint32_t now,
    const OperatorSession_Conditions *conditions)
{
  if (c==0 || c->ready==0U) { return OPERATOR_ERROR_SESSION; }
  OperatorController_Tick(c,now,conditions);
  return OperatorSession_Consume(&c->session,now,conditions,task);
}

uint8_t OperatorController_Permitted(const OperatorController *c)
{
  return (c!=0 && c->ready!=0U && c->session.connected!=0U &&
      c->session.pending_stop==0U && (c->session.granted!=0U || c->session.executing!=0U)) ? 1U : 0U;
}
