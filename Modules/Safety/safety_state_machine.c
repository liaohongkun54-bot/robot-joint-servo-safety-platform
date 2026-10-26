/**
  ******************************************************************************
  * @file    safety_state_machine.c
  * @brief   Local fault-safe state machine for the P3-B stage.
  ******************************************************************************
  */

#include "safety_state_machine.h"

volatile uint8_t g_safety_test_estop_active = 0U;
volatile uint8_t g_safety_operator_clear_request = 0U;
volatile uint8_t g_safety_motion_arm_request = 0U;
volatile Safety_SnapshotTypeDef g_safety_snapshot;

static uint32_t g_safety_last_valid_frame_count;
static Safety_AuthorizationFn g_authorization_policy;
static uint8_t Safety_OperatorAuthorized(void)
{
  return (g_authorization_policy != 0) ? g_authorization_policy() :
      ((g_safety_motion_arm_request != 0U) ? 1U : 0U);
}

void Safety_StateMachine_BindAuthorization(Safety_AuthorizationFn policy)
{
  g_authorization_policy = policy;
  Safety_StateMachine_DisarmMotion();
}

static void Safety_ApplyOutputPolicy(C620_LinkStateTypeDef link_state)
{
  if ((SAFETY_NONZERO_OUTPUT_ENABLE != 0U) &&
      (Safety_OperatorAuthorized() != 0U) &&
      (g_safety_snapshot.state == SAFETY_STATE_ONLINE) &&
      (link_state == C620_LINK_ONLINE) &&
      (g_safety_snapshot.fault_latched == 0U) &&
      (g_safety_snapshot.estop_active == 0U))
  {
    g_safety_snapshot.output_permitted = 1U;
    g_safety_snapshot.zero_output_required = 0U;
  }
}

static void Safety_EnterState(Safety_StateTypeDef next_state,
                              uint32_t now_ms,
                              uint32_t fault_code)
{
  if (g_safety_snapshot.state != next_state)
  {
    g_safety_snapshot.state = next_state;
    g_safety_snapshot.transition_count++;
    g_safety_snapshot.last_transition_ms = now_ms;
  }

  if (fault_code != 0U)
  {
    g_safety_motion_arm_request = 0U;
    g_safety_snapshot.fault_latched = 1U;
    /* Count a diagnostic event once, not once per main-loop iteration. */
    if (g_safety_snapshot.last_fault_code != fault_code)
    {
      g_safety_snapshot.fault_count++;
      g_safety_snapshot.last_fault_code = fault_code;
    }
  }

  /* Every state transition is fail-safe; normal ONLINE policy may re-arm later. */
  g_safety_snapshot.output_permitted = 0U;
  g_safety_snapshot.zero_output_required = 1U;
}

void Safety_StateMachine_Init(void)
{
  g_authorization_policy = 0;
  g_safety_snapshot.state = SAFETY_STATE_INIT;
  g_safety_snapshot.output_permitted = 0U;
  g_safety_snapshot.zero_output_required = 1U;
  g_safety_snapshot.fault_latched = 0U;
  g_safety_snapshot.estop_active = 0U;
  g_safety_snapshot.transition_count = 0U;
  g_safety_snapshot.fault_count = 0U;
  g_safety_snapshot.recovery_valid_frames = 0U;
  g_safety_snapshot.last_transition_ms = 0U;
  g_safety_snapshot.last_fault_code = 0U;
  g_safety_last_valid_frame_count = 0U;
  g_safety_test_estop_active = 0U;
  g_safety_operator_clear_request = 0U;
  g_safety_motion_arm_request = 0U;
}

void Safety_StateMachine_Update(const Safety_InputTypeDef *input)
{
  uint32_t new_valid_frames;

  if (input == (const Safety_InputTypeDef *)0)
  {
    return;
  }

  /* Start every service cycle fail-safe, then grant only the explicit arm path. */
  g_safety_snapshot.output_permitted = 0U;
  g_safety_snapshot.zero_output_required = 1U;
  g_safety_snapshot.estop_active = input->estop_active;
  /* A counter reset/reinitialization must not wrap into a huge frame burst. */
  if (input->valid_frame_count >= g_safety_last_valid_frame_count)
  {
    new_valid_frames = input->valid_frame_count -
                       g_safety_last_valid_frame_count;
  }
  else
  {
    new_valid_frames = input->valid_frame_count;
  }
  g_safety_last_valid_frame_count = input->valid_frame_count;

  if (input->estop_active != 0U)
  {
    Safety_EnterState(SAFETY_STATE_ESTOP, input->now_ms, SAFETY_FAULT_ESTOP);
    return;
  }

  if (input->init_ok == 0U)
  {
    Safety_EnterState(SAFETY_STATE_SAFE, input->now_ms, SAFETY_FAULT_INIT_FAILED);
    return;
  }

  if (input->can_error_code != 0U)
  {
    Safety_EnterState(SAFETY_STATE_SAFE, input->now_ms, input->can_error_code);
    return;
  }

  switch (g_safety_snapshot.state)
  {
    case SAFETY_STATE_INIT:
      Safety_EnterState(SAFETY_STATE_WAIT_FEEDBACK, input->now_ms, 0U);
      break;

    case SAFETY_STATE_WAIT_FEEDBACK:
      if (input->link_state == C620_LINK_ONLINE)
      {
        Safety_EnterState(SAFETY_STATE_ONLINE, input->now_ms, 0U);
      }
      break;

    case SAFETY_STATE_ONLINE:
      if (input->link_state == C620_LINK_TIMEOUT)
      {
        /* Keep TIMEOUT observable for one service cycle, while already
         * forcing zero output. The next cycle enters the latched SAFE state. */
        Safety_EnterState(SAFETY_STATE_TIMEOUT,
                          input->now_ms,
                          SAFETY_FAULT_FEEDBACK_TIMEOUT);
      }
      break;

    case SAFETY_STATE_TIMEOUT:
      Safety_EnterState(SAFETY_STATE_SAFE,
                        input->now_ms,
                        SAFETY_FAULT_FEEDBACK_TIMEOUT);
      break;

    case SAFETY_STATE_ESTOP:
      /* Releasing ESTOP never returns directly to ONLINE. */
      Safety_EnterState(SAFETY_STATE_SAFE, input->now_ms, SAFETY_FAULT_ESTOP);
      break;

    case SAFETY_STATE_SAFE:
      if ((input->operator_clear != 0U) &&
          (input->link_state == C620_LINK_ONLINE) &&
          (new_valid_frames > 0U))
      {
        g_safety_operator_clear_request = 0U;
        g_safety_snapshot.recovery_valid_frames = new_valid_frames;
        Safety_EnterState(SAFETY_STATE_RECOVERY, input->now_ms, 0U);
      }
      break;

    case SAFETY_STATE_RECOVERY:
      if (input->link_state != C620_LINK_ONLINE)
      {
        Safety_EnterState(SAFETY_STATE_SAFE,
                          input->now_ms,
                          SAFETY_FAULT_RECOVERY_LOST);
      }
      else
      {
        g_safety_snapshot.recovery_valid_frames += new_valid_frames;
        if (g_safety_snapshot.recovery_valid_frames >= SAFETY_RECOVERY_REQUIRED_FRAMES)
        {
          g_safety_snapshot.state = SAFETY_STATE_ONLINE;
          g_safety_snapshot.transition_count++;
          g_safety_snapshot.last_transition_ms = input->now_ms;
          g_safety_snapshot.fault_latched = 0U;
          g_safety_snapshot.last_fault_code = SAFETY_FAULT_NONE;
          g_safety_snapshot.output_permitted = 0U;
          g_safety_snapshot.zero_output_required = 1U;
        }
      }
      break;

    default:
      Safety_EnterState(SAFETY_STATE_SAFE,
                        input->now_ms,
                        SAFETY_FAULT_INTERNAL);
      break;
  }

  Safety_ApplyOutputPolicy(input->link_state);
}

uint8_t Safety_StateMachine_IsNonzeroOutputAllowed(void)
{
  return ((SAFETY_NONZERO_OUTPUT_ENABLE != 0U) &&
          (Safety_OperatorAuthorized() != 0U) &&
          (g_safety_snapshot.state == SAFETY_STATE_ONLINE) &&
          (g_safety_snapshot.output_permitted != 0U) &&
          (g_safety_snapshot.zero_output_required == 0U) &&
          (g_safety_snapshot.fault_latched == 0U) &&
          (g_safety_snapshot.estop_active == 0U)) ? 1U : 0U;
}

void Safety_StateMachine_DisarmMotion(void)
{
  g_safety_motion_arm_request = 0U;
  g_safety_snapshot.output_permitted = 0U;
  g_safety_snapshot.zero_output_required = 1U;
}

void Safety_StateMachine_RefreshAuthorization(C620_LinkStateTypeDef link_state)
{
  g_safety_snapshot.output_permitted = 0U;
  g_safety_snapshot.zero_output_required = 1U;
  Safety_ApplyOutputPolicy(link_state);
}
