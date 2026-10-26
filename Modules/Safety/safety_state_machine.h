/**
  ******************************************************************************
  * @file    safety_state_machine.h
  * @brief   Local fault-safe state machine for the P3-B stage.
  *
  * This module deliberately contains no CAN transmit operation.  It only
  * decides whether a future motion layer may request output.  During P3-B the
  * answer remains false in every state.
  ******************************************************************************
  */

#ifndef __SAFETY_STATE_MACHINE_H__
#define __SAFETY_STATE_MACHINE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "c620_feedback.h"

#define SAFETY_RECOVERY_REQUIRED_FRAMES (3U)

/*
 * First-motion authorization remains operator-controlled and one-shot.
 * The variable below resets to zero at boot and is cleared automatically
 * after a stop or any fault.  Setting it does not bypass ONLINE/fault/ESTOP
 * checks; it only allows those checks to grant the Motion Gate permission.
 */
#define SAFETY_NONZERO_OUTPUT_ENABLE    (1U)

/* Local diagnostic codes; these are not C620 CAN identifiers. */
#define SAFETY_FAULT_NONE                 (0x0000U)
#define SAFETY_FAULT_ESTOP               (0xE001U)
#define SAFETY_FAULT_INIT_FAILED         (0xE002U)
#define SAFETY_FAULT_FEEDBACK_TIMEOUT    (0xE003U)
#define SAFETY_FAULT_RECOVERY_LOST       (0xE004U)
#define SAFETY_FAULT_INTERNAL            (0xE0FFU)

typedef enum
{
  SAFETY_STATE_INIT = 0U,
  SAFETY_STATE_WAIT_FEEDBACK = 1U,
  SAFETY_STATE_ONLINE = 2U,
  SAFETY_STATE_TIMEOUT = 3U,
  SAFETY_STATE_SAFE = 4U,
  SAFETY_STATE_ESTOP = 5U,
  SAFETY_STATE_RECOVERY = 6U
} Safety_StateTypeDef;

typedef struct
{
  uint32_t now_ms;
  C620_LinkStateTypeDef link_state;
  uint32_t valid_frame_count;
  uint32_t can_error_code;
  uint8_t estop_active;
  uint8_t init_ok;
  uint8_t operator_clear;
} Safety_InputTypeDef;

typedef struct
{
  Safety_StateTypeDef state;
  uint8_t output_permitted;
  uint8_t zero_output_required;
  uint8_t fault_latched;
  uint8_t estop_active;
  uint32_t transition_count;
  uint32_t fault_count;
  uint32_t recovery_valid_frames;
  uint32_t last_transition_ms;
  uint32_t last_fault_code;
} Safety_SnapshotTypeDef;

/** Software test input. Set to 1 in Keil Watch to exercise ESTOP. */
extern volatile uint8_t g_safety_test_estop_active;

/** Software recovery request. Set to 1 after a fault has been cleared. */
extern volatile uint8_t g_safety_operator_clear_request;

/** One-shot operator arm. Set to 1 in Keil Watch only for an approved test. */
extern volatile uint8_t g_safety_motion_arm_request;

/** Watch-visible state snapshot. */
extern volatile Safety_SnapshotTypeDef g_safety_snapshot;

void Safety_StateMachine_Init(void);
void Safety_StateMachine_Update(const Safety_InputTypeDef *input);
uint8_t Safety_StateMachine_IsNonzeroOutputAllowed(void);
void Safety_StateMachine_DisarmMotion(void);
/* Production control owner binds a policy; when bound, Watch arm is ignored.
 * Init resets the binding; install it before servicing any external request. */
typedef uint8_t (*Safety_AuthorizationFn)(void);
void Safety_StateMachine_BindAuthorization(Safety_AuthorizationFn policy);
void Safety_StateMachine_RefreshAuthorization(C620_LinkStateTypeDef link_state);

#ifdef __cplusplus
}
#endif

#endif /* __SAFETY_STATE_MACHINE_H__ */
