#include <assert.h>
#include <stdio.h>
#include "operator_session.h"

static TestTask_DescriptorTypeDef Task(void)
{
  TestTask_DescriptorTypeDef t = {0};
  t.schema_version = 1U; t.request_id = 42U; t.target_node_id = 1U;
  t.action = TEST_TASK_ACTION_CURRENT_TEST;
  t.amplitude_raw = 1000; t.amplitude_limit_raw = 1000;
  t.duration_ms = 1000U; t.feedback_timeout_ms = 100U;
  t.timeout_policy = TEST_TASK_TIMEOUT_STOP;
  t.permission = TEST_TASK_PERMISSION_OPERATOR;
  return t;
}

int main(void)
{
  OperatorSession s;
  OperatorSession_Conditions c = {1U, 0U, 0U, 64U};
  TestTask_DescriptorTypeDef t = Task();
  TestTask_DescriptorTypeDef changed;
  uint32_t tick;
  uint32_t seq;
  unsigned field;

  OperatorSession_Init(&s);
  assert(!s.connected && !s.granted && !s.executing);
  assert(OperatorSession_Consume(&s, 0U, &c, &t) == OPERATOR_ERROR_GRANT);
  assert(OperatorSession_Open(&s, 7U, 0U, &c) == OPERATOR_OK);
  assert(!s.granted); /* Connecting never arms. */
  assert(OperatorSession_Grant(&s, 8U, 1U, 1U, &c, &t) == OPERATOR_ERROR_SESSION);
  assert(OperatorSession_Grant(&s, 7U, 1U, 1U, &c, &t) == OPERATOR_OK);
  assert(OperatorSession_Grant(&s, 7U, 1U, 2U, &c, &t) == OPERATOR_ERROR_SEQUENCE);
  assert(OperatorSession_Consume(&s, 3U, &c, &t) == OPERATOR_OK);
  assert(!s.granted && s.executing);
  assert(OperatorSession_Consume(&s, 4U, &c, &t) == OPERATOR_ERROR_GRANT);
  assert(OperatorSession_Open(&s, 9U, 5U, &c) == OPERATOR_ERROR_STATE);
  OperatorSession_Tick(&s, 1001U, &c);
  assert(!s.connected && s.pending_stop && s.executing);
  OperatorSession_Stopped(&s, 0U);
  assert(s.pending_stop && s.executing); /* Failed zero is not completion. */
  OperatorSession_Stopped(&s, 1U);
  assert(!s.pending_stop && !s.executing);

  assert(OperatorSession_Open(&s, 9U, 2000U, &c) == OPERATOR_OK);
  assert(OperatorSession_Grant(&s, 9U, 1U, 2000U, &c, &t) == OPERATOR_OK);
  seq = 1U;
  for (tick = 2250U; tick <= 12000U; tick += 250U) {
    assert(OperatorSession_Heartbeat(&s, 9U, ++seq, tick, &c) == OPERATOR_OK);
  }
  assert(s.connected && !s.granted); /* Heartbeats never extend grant TTL. */
  assert(OperatorSession_Consume(&s, 12000U, &c, &t) == OPERATOR_ERROR_GRANT);

  /* Alter each bound field independently, including otherwise valid values. */
  for (field = 0U; field < 10U; ++field) {
    assert(OperatorSession_Grant(&s, 9U, ++seq, 12001U, &c, &t) == OPERATOR_OK);
    changed = t;
    switch (field) {
      case 0U: changed.schema_version++; break;
      case 1U: changed.request_id++; break;
      case 2U: changed.target_node_id++; break;
      case 3U: changed.action = TEST_TASK_ACTION_NONE; break;
      case 4U: changed.amplitude_raw = -1000; break;
      case 5U: changed.amplitude_limit_raw = 1001; break;
      case 6U: changed.duration_ms = 100U; break;
      case 7U: changed.feedback_timeout_ms = 200U; break;
      case 8U: changed.timeout_policy = TEST_TASK_TIMEOUT_REJECT; break;
      default: changed.permission = TEST_TASK_PERMISSION_NONE; break;
    }
    assert(OperatorSession_Consume(&s, 12002U, &c, &changed) == OPERATOR_ERROR_TASK);
    assert(!s.granted && !s.executing);
  }
  c.free_record_slots = 1U;
  assert(OperatorSession_Grant(&s, 9U, ++seq, 12003U, &c, &t) == OPERATOR_ERROR_CAPACITY);
  c.free_record_slots = 64U;
  c.safe_to_enable = 0U;
  assert(OperatorSession_Grant(&s, 9U, ++seq, 12004U, &c, &t) == OPERATOR_ERROR_STATE);
  assert(OperatorSession_Clear(&s, 9U, ++seq, 12005U, &c) == OPERATOR_ERROR_STATE);
  c.can_clear_fault = 1U;
  assert(OperatorSession_Clear(&s, 9U, ++seq, 12006U, &c) == OPERATOR_OK);
  assert(!s.granted); /* Clear requests never authorize motion. */
  c.safe_to_enable = 1U;
  assert(OperatorSession_Grant(&s, 9U, ++seq, 12007U, &c, &t) == OPERATOR_OK);
  assert(OperatorSession_Stop(&s, 9U, ++seq, 12008U, &c) == OPERATOR_OK);
  assert(!s.granted && s.pending_stop);
  assert(OperatorSession_Grant(&s, 9U, ++seq, 12009U, &c, &t) == OPERATOR_ERROR_STATE);
  OperatorSession_Stopped(&s, 1U);
  OperatorSession_Close(&s);

  assert(OperatorSession_Open(&s, 12U, 13000U, &c) == OPERATOR_OK);
  assert(OperatorSession_Grant(&s, 12U, 1U, 13001U, &c, &t) == OPERATOR_OK);
  assert(OperatorSession_Consume(&s, 13002U, &c, &t) == OPERATOR_OK);
  c.safe_to_enable = 0U;
  OperatorSession_Tick(&s, 13003U, &c);
  assert(s.pending_stop && !s.granted);
  assert(OperatorSession_Clear(&s, 12U, 2U, 13004U, &c) == OPERATOR_ERROR_STATE);
  OperatorSession_Stopped(&s, 1U);
  OperatorSession_Close(&s);
  c.safe_to_enable = 1U;

  /* Exact timeout boundary, tick wrap and stale heartbeat cannot revive. */
  assert(OperatorSession_Open(&s, 10U, 0xFFFFFF00UL, &c) == OPERATOR_OK);
  assert(OperatorSession_Heartbeat(&s, 10U, 1U, 0xFFFFFFF0UL, &c) == OPERATOR_OK);
  OperatorSession_Tick(&s, 0x3D7UL, &c);
  assert(s.connected);
  assert(OperatorSession_Heartbeat(&s, 10U, 2U, 0x3D8UL, &c) == OPERATOR_ERROR_SESSION);
  assert(!s.connected);
  assert(OperatorSession_Open(&s, 11U, 1000U, &c) == OPERATOR_OK);
  assert(OperatorSession_Heartbeat(&s, 11U, 0xFFFFFFFFUL, 1001U, &c) == OPERATOR_OK);
  assert(OperatorSession_Heartbeat(&s, 11U, 1U, 1002U, &c) == OPERATOR_ERROR_SEQUENCE);
  OperatorSession_Tick(&s, 1003U, 0);
  assert(!s.connected);
  puts("operator_session: policy tests passed (host only; no device I/O)");
  return 0;
}
