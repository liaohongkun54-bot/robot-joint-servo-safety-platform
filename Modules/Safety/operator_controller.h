#ifndef OPERATOR_CONTROLLER_H
#define OPERATOR_CONTROLLER_H
#include "operator_session.h"
#include "operator_wire_codec.h"

#define OPERATOR_STOP_LOCAL_SAFETY (2U)
#define OPERATOR_STOP_REQUESTED (4U)
#define OPERATOR_STOP_SESSION_LOST (5U)

/* Callbacks execute synchronously in the control-task owner, never in IRQ.
 * stop returns 1 only after zero output was successfully submitted; clear
 * requests existing local recovery and does not claim it has completed. */
typedef uint8_t (*OperatorController_StopFn)(void *context, uint32_t now_ms, uint8_t reason);
typedef uint8_t (*OperatorController_ClearFn)(void *context);
typedef struct {
  OperatorSession session;
  uint32_t boot_low, boot_high, next_session_id;
  uint32_t processed_count, rejected_count, stop_failure_count;
  OperatorController_StopFn stop;
  OperatorController_ClearFn clear;
  void *context;
  uint8_t ready;
} OperatorController;

/* Platform must supply a fresh controller boot identifier. Zero/failed identity
 * leaves ready=0. This function does not generate entropy or configure clocks. */
void OperatorController_Init(OperatorController *controller, uint32_t boot_low,
    uint32_t boot_high, OperatorController_StopFn stop,
    OperatorController_ClearFn clear, void *context);
void OperatorController_Tick(OperatorController *controller, uint32_t now_ms,
    const OperatorSession_Conditions *conditions);
/* Returns 1 if a reply can be encoded, including rejected commands. */
uint8_t OperatorController_Handle(OperatorController *controller,
    const OperatorWire_Request *request, uint32_t now_ms,
    const OperatorSession_Conditions *conditions, OperatorWire_Reply *reply);
OperatorSession_Result OperatorController_Consume(OperatorController *controller,
    const TestTask_DescriptorTypeDef *task, uint32_t now_ms,
    const OperatorSession_Conditions *conditions);
uint8_t OperatorController_Permitted(const OperatorController *controller);
#endif
