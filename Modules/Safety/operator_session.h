#ifndef OPERATOR_SESSION_H
#define OPERATOR_SESSION_H

#include <stdint.h>
#include "test_task_schema.h"

#define OPERATOR_SESSION_TIMEOUT_MS (1000UL)
#define OPERATOR_GRANT_TIMEOUT_MS (10000UL)

/* Pure policy module. Only the control-task owner may call these functions.
 * It never writes Safety globals or CAN. The owner must enact pending_stop
 * before admitting any more output and acknowledge it only after zero succeeds.
 * Session IDs must be issued by the controller; this is not authentication. */
typedef struct {
  uint8_t safe_to_enable;
  uint8_t can_clear_fault;
  uint8_t motion_active;
  uint16_t free_record_slots;
} OperatorSession_Conditions;

typedef enum {
  OPERATOR_OK = 0,
  OPERATOR_ERROR_ARGUMENT,
  OPERATOR_ERROR_SESSION,
  OPERATOR_ERROR_SEQUENCE,
  OPERATOR_ERROR_STATE,
  OPERATOR_ERROR_TASK,
  OPERATOR_ERROR_CAPACITY,
  OPERATOR_ERROR_GRANT
} OperatorSession_Result;

typedef struct {
  uint32_t session_id;
  uint32_t last_sequence;
  uint32_t last_contact_ms;
  uint32_t grant_tick_ms;
  TestTask_DescriptorTypeDef task;
  uint8_t connected;
  uint8_t granted;
  uint8_t executing;
  uint8_t pending_stop;
} OperatorSession;

void OperatorSession_Init(OperatorSession *session);
/* Opening/replacing a session is allowed only while inactive and disarmed.
 * Caller owns globally fresh ID issuance, including its boot-generation policy. */
OperatorSession_Result OperatorSession_Open(OperatorSession *session,
    uint32_t issued_id, uint32_t now_ms, const OperatorSession_Conditions *conditions);
void OperatorSession_Tick(OperatorSession *session, uint32_t now_ms,
    const OperatorSession_Conditions *conditions);
OperatorSession_Result OperatorSession_Heartbeat(OperatorSession *session,
    uint32_t id, uint32_t sequence, uint32_t now_ms,
    const OperatorSession_Conditions *conditions);
OperatorSession_Result OperatorSession_Grant(OperatorSession *session,
    uint32_t id, uint32_t sequence, uint32_t now_ms,
    const OperatorSession_Conditions *conditions, const TestTask_DescriptorTypeDef *task);
OperatorSession_Result OperatorSession_Consume(OperatorSession *session,
    uint32_t now_ms, const OperatorSession_Conditions *conditions,
    const TestTask_DescriptorTypeDef *task);
OperatorSession_Result OperatorSession_Stop(OperatorSession *session,
    uint32_t id, uint32_t sequence, uint32_t now_ms,
    const OperatorSession_Conditions *conditions);
/* Returns OK to request existing Safety recovery, not to report fault cleared. */
OperatorSession_Result OperatorSession_Clear(OperatorSession *session,
    uint32_t id, uint32_t sequence, uint32_t now_ms,
    const OperatorSession_Conditions *conditions);
void OperatorSession_Close(OperatorSession *session);
void OperatorSession_Stopped(OperatorSession *session, uint8_t zero_succeeded);

#endif
