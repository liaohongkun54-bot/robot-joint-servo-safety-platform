#include "operator_session.h"
#include <string.h>

static void Revoke(OperatorSession *s)
{
  s->granted = 0U;
  if (s->executing != 0U) { s->pending_stop = 1U; }
}

static uint8_t Supported(const TestTask_DescriptorTypeDef *t)
{
  if (TestTask_Validate(t) != TEST_TASK_VALID) { return 0U; }
  /* Match the existing gate, not the wider schema/protocol numeric range. */
  return ((t->amplitude_raw != 0) &&
      (t->amplitude_raw >= -1000) && (t->amplitude_raw <= 1000) &&
      (t->amplitude_limit_raw <= 1000) &&
      ((t->duration_ms <= 100UL) ||
       ((t->duration_ms == 1000UL) && (t->amplitude_raw == 1000)))) ? 1U : 0U;
}

static uint8_t SameTask(const TestTask_DescriptorTypeDef *a,
    const TestTask_DescriptorTypeDef *b)
{
  /* Do not compare structure padding. Bind every field, including request ID. */
  return ((a->schema_version == b->schema_version) &&
      (a->request_id == b->request_id) && (a->target_node_id == b->target_node_id) &&
      (a->action == b->action) && (a->amplitude_raw == b->amplitude_raw) &&
      (a->amplitude_limit_raw == b->amplitude_limit_raw) &&
      (a->duration_ms == b->duration_ms) &&
      (a->feedback_timeout_ms == b->feedback_timeout_ms) &&
      (a->timeout_policy == b->timeout_policy) && (a->permission == b->permission)) ? 1U : 0U;
}

void OperatorSession_Init(OperatorSession *s)
{
  if (s != 0) { memset(s, 0, sizeof(*s)); }
}

OperatorSession_Result OperatorSession_Open(OperatorSession *s,
    uint32_t id, uint32_t now, const OperatorSession_Conditions *c)
{
  if ((s == 0) || (c == 0) || (id == 0U)) { return OPERATOR_ERROR_ARGUMENT; }
  if ((s->connected != 0U) || (s->executing != 0U) ||
      (s->pending_stop != 0U) || (c->motion_active != 0U)) { return OPERATOR_ERROR_STATE; }
  OperatorSession_Init(s);
  s->session_id = id;
  s->last_contact_ms = now;
  s->connected = 1U;
  return OPERATOR_OK;
}

void OperatorSession_Tick(OperatorSession *s, uint32_t now,
    const OperatorSession_Conditions *c)
{
  if (s == 0) { return; }
  if (c == 0) { OperatorSession_Close(s); return; }
  if ((s->connected != 0U) &&
      ((uint32_t)(now - s->last_contact_ms) >= OPERATOR_SESSION_TIMEOUT_MS)) {
    OperatorSession_Close(s);
  }
  if (c->safe_to_enable == 0U) { Revoke(s); }
  if ((s->granted != 0U) &&
      ((uint32_t)(now - s->grant_tick_ms) >= OPERATOR_GRANT_TIMEOUT_MS)) {
    s->granted = 0U;
  }
}

static OperatorSession_Result Request(OperatorSession *s, uint32_t id,
    uint32_t sequence, uint32_t now, const OperatorSession_Conditions *c)
{
  if ((s == 0) || (c == 0)) { return OPERATOR_ERROR_ARGUMENT; }
  OperatorSession_Tick(s, now, c);
  if ((s->connected == 0U) || (s->session_id != id)) { return OPERATOR_ERROR_SESSION; }
  /* Sequence exhaustion requires a new session; wrapping is never accepted. */
  if ((sequence == 0U) || (sequence <= s->last_sequence)) { return OPERATOR_ERROR_SEQUENCE; }
  s->last_sequence = sequence;
  s->last_contact_ms = now;
  return OPERATOR_OK;
}

OperatorSession_Result OperatorSession_Heartbeat(OperatorSession *s,
    uint32_t id, uint32_t sequence, uint32_t now, const OperatorSession_Conditions *c)
{
  return Request(s, id, sequence, now, c);
}

OperatorSession_Result OperatorSession_Grant(OperatorSession *s,
    uint32_t id, uint32_t sequence, uint32_t now,
    const OperatorSession_Conditions *c, const TestTask_DescriptorTypeDef *t)
{
  OperatorSession_Result r = Request(s, id, sequence, now, c);
  if (r != OPERATOR_OK) { return r; }
  /* A grant attempt cannot leave an older grant usable after rejection. */
  s->granted = 0U;
  if ((c->safe_to_enable == 0U) || (c->motion_active != 0U) ||
      (s->executing != 0U) || (s->pending_stop != 0U)) { return OPERATOR_ERROR_STATE; }
  if (Supported(t) == 0U) { return OPERATOR_ERROR_TASK; }
  if (c->free_record_slots < 2U) { return OPERATOR_ERROR_CAPACITY; }
  s->task = *t;
  s->grant_tick_ms = now;
  s->granted = 1U;
  return OPERATOR_OK;
}

OperatorSession_Result OperatorSession_Consume(OperatorSession *s,
    uint32_t now, const OperatorSession_Conditions *c, const TestTask_DescriptorTypeDef *t)
{
  uint8_t granted;
  if ((s == 0) || (c == 0)) { return OPERATOR_ERROR_ARGUMENT; }
  OperatorSession_Tick(s, now, c);
  granted = s->granted;
  s->granted = 0U; /* Failed admission also consumes the single-use grant. */
  if ((s->connected == 0U) || (granted == 0U)) { return OPERATOR_ERROR_GRANT; }
  if ((c->safe_to_enable == 0U) || (c->motion_active != 0U) ||
      (s->executing != 0U) || (s->pending_stop != 0U)) { return OPERATOR_ERROR_STATE; }
  if ((Supported(t) == 0U) || (SameTask(&s->task, t) == 0U)) { return OPERATOR_ERROR_TASK; }
  if (c->free_record_slots < 2U) { return OPERATOR_ERROR_CAPACITY; }
  s->executing = 1U;
  return OPERATOR_OK;
}

OperatorSession_Result OperatorSession_Stop(OperatorSession *s,
    uint32_t id, uint32_t sequence, uint32_t now, const OperatorSession_Conditions *c)
{
  OperatorSession_Result r = Request(s, id, sequence, now, c);
  if (r != OPERATOR_OK) { return r; }
  Revoke(s);
  /* Also stop legacy motion; an idle zero submission is harmless and explicit. */
  s->pending_stop = 1U;
  return OPERATOR_OK;
}

OperatorSession_Result OperatorSession_Clear(OperatorSession *s,
    uint32_t id, uint32_t sequence, uint32_t now, const OperatorSession_Conditions *c)
{
  OperatorSession_Result r = Request(s, id, sequence, now, c);
  if (r != OPERATOR_OK) { return r; }
  Revoke(s);
  if ((c->can_clear_fault == 0U) || (c->motion_active != 0U) ||
      (s->executing != 0U) || (s->pending_stop != 0U)) { return OPERATOR_ERROR_STATE; }
  return OPERATOR_OK;
}

void OperatorSession_Close(OperatorSession *s)
{
  if (s != 0) { Revoke(s); s->connected = 0U; }
}

void OperatorSession_Stopped(OperatorSession *s, uint8_t zero_succeeded)
{
  if (s == 0) { return; }
  s->granted = 0U;
  if (zero_succeeded != 0U) { s->executing = 0U; s->pending_stop = 0U; }
  else { s->pending_stop = 1U; }
}
