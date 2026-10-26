/**
  ******************************************************************************
  * @file    c620_motion_gate.c
  * @brief   C620 motion-request safety gate for the P3-C preparation stage.
  ******************************************************************************
  */

#include "c620_motion_gate.h"

#include "c620_feedback.h"

volatile C620_MotionGateSnapshotTypeDef g_c620_motion_gate_snapshot;

static C620_ControlOutput_TransportFn g_c620_motion_transport;
static void *g_c620_motion_transport_context;

static void C620_MotionGate_StartObservation(void)
{
  g_c620_motion_gate_snapshot.observation_start_encoder_raw =
      g_c620_feedback.encoder_raw;
  g_c620_motion_gate_snapshot.observation_last_encoder_raw =
      g_c620_feedback.encoder_raw;
  g_c620_motion_gate_snapshot.observation_encoder_delta_raw = 0L;
  g_c620_motion_gate_snapshot.observation_abs_travel_raw = 0UL;
  g_c620_motion_gate_snapshot.observation_peak_abs_speed_rpm = 0U;
  g_c620_motion_gate_snapshot.observation_sample_count = 0UL;
  g_c620_motion_gate_snapshot.observation_start_valid_frame_count =
      g_c620_feedback.valid_frame_count;
  g_c620_motion_gate_snapshot.observation_last_valid_frame_count =
      g_c620_feedback.valid_frame_count;
}

static void C620_MotionGate_ObserveActiveFeedback(void)
{
  int32_t encoder_delta;
  int32_t speed_rpm;

  if ((g_c620_feedback.has_valid_feedback == 0U) ||
      (g_c620_feedback.valid_frame_count ==
       g_c620_motion_gate_snapshot.observation_last_valid_frame_count))
  {
    return;
  }

  encoder_delta = (int32_t)g_c620_feedback.encoder_raw -
                  (int32_t)g_c620_motion_gate_snapshot.
                      observation_last_encoder_raw;
  if (encoder_delta > 4096L)
  {
    encoder_delta -= 8192L;
  }
  else if (encoder_delta < -4096L)
  {
    encoder_delta += 8192L;
  }

  g_c620_motion_gate_snapshot.observation_encoder_delta_raw += encoder_delta;
  if (encoder_delta < 0L)
  {
    g_c620_motion_gate_snapshot.observation_abs_travel_raw +=
        (uint32_t)(-encoder_delta);
  }
  else
  {
    g_c620_motion_gate_snapshot.observation_abs_travel_raw +=
        (uint32_t)encoder_delta;
  }

  speed_rpm = (int32_t)g_c620_feedback.speed_rpm;
  if (speed_rpm < 0L)
  {
    speed_rpm = -speed_rpm;
  }
  if ((uint32_t)speed_rpm >
      (uint32_t)g_c620_motion_gate_snapshot.observation_peak_abs_speed_rpm)
  {
    g_c620_motion_gate_snapshot.observation_peak_abs_speed_rpm =
        (uint16_t)speed_rpm;
  }

  g_c620_motion_gate_snapshot.observation_last_encoder_raw =
      g_c620_feedback.encoder_raw;
  g_c620_motion_gate_snapshot.observation_last_valid_frame_count =
      g_c620_feedback.valid_frame_count;
  g_c620_motion_gate_snapshot.observation_sample_count++;
}

static uint8_t C620_MotionGate_Reject(C620_MotionGateErrorTypeDef error_code)
{
  g_c620_motion_gate_snapshot.last_request_accepted = 0U;
  g_c620_motion_gate_snapshot.last_error = error_code;
  g_c620_motion_gate_snapshot.rejected_request_count++;
  g_c620_motion_gate_snapshot.zero_output_required = 1U;
  return 0U;
}

void C620_MotionGate_Init(void)
{
  C620_ControlOutput_Init();
  g_c620_motion_transport = (C620_ControlOutput_TransportFn)0;
  g_c620_motion_transport_context = (void *)0;
  g_c620_motion_gate_snapshot.last_request_accepted = 0U;
  g_c620_motion_gate_snapshot.zero_output_required = 1U;
  g_c620_motion_gate_snapshot.last_error = MOTION_GATE_ERROR_NONE;
  g_c620_motion_gate_snapshot.last_stop_reason = C620_MOTION_STOP_NONE;
  g_c620_motion_gate_snapshot.rejected_request_count = 0U;
  g_c620_motion_gate_snapshot.accepted_request_count = 0U;
  g_c620_motion_gate_snapshot.sent_frame_count = 0U;
  g_c620_motion_gate_snapshot.nonzero_frame_count = 0U;
  g_c620_motion_gate_snapshot.refresh_count = 0U;
  g_c620_motion_gate_snapshot.zero_frame_count = 0U;
  g_c620_motion_gate_snapshot.stop_count = 0U;
  g_c620_motion_gate_snapshot.stop_error_count = 0U;
  g_c620_motion_gate_snapshot.last_request_id = 0U;
  g_c620_motion_gate_snapshot.start_tick_ms = 0U;
  g_c620_motion_gate_snapshot.last_refresh_tick_ms = 0U;
  g_c620_motion_gate_snapshot.stop_tick_ms = 0U;
  g_c620_motion_gate_snapshot.active_duration_ms = 0U;
  g_c620_motion_gate_snapshot.active_current_raw = 0;
  g_c620_motion_gate_snapshot.active = 0U;
  g_c620_motion_gate_snapshot.observation_start_encoder_raw = 0U;
  g_c620_motion_gate_snapshot.observation_last_encoder_raw = 0U;
  g_c620_motion_gate_snapshot.observation_encoder_delta_raw = 0L;
  g_c620_motion_gate_snapshot.observation_abs_travel_raw = 0UL;
  g_c620_motion_gate_snapshot.observation_peak_abs_speed_rpm = 0U;
  g_c620_motion_gate_snapshot.observation_sample_count = 0UL;
  g_c620_motion_gate_snapshot.observation_start_valid_frame_count = 0UL;
  g_c620_motion_gate_snapshot.observation_last_valid_frame_count = 0UL;
}

void C620_MotionGate_BindTransport(
    C620_ControlOutput_TransportFn transport,
    void *context)
{
  g_c620_motion_transport = transport;
  g_c620_motion_transport_context = context;
}

uint8_t C620_MotionGate_RequestAllowed(
    const C620_MotionRequestTypeDef *request,
    const volatile Safety_SnapshotTypeDef *safety)
{
#if (MOTION_EXPERIMENT_ENABLE == 0U)
  (void)request;
  (void)safety;
  return C620_MotionGate_Reject(MOTION_GATE_ERROR_DISABLED);
#else
  int32_t current_raw;
  int32_t max_current_raw;

  if ((request == (const C620_MotionRequestTypeDef *)0) ||
      (safety == (const volatile Safety_SnapshotTypeDef *)0))
  {
    return C620_MotionGate_Reject(MOTION_GATE_ERROR_NULL_ARGUMENT);
  }

  g_c620_motion_gate_snapshot.last_request_id = request->request_id;

  if (g_c620_motion_gate_snapshot.active != 0U)
  {
    return C620_MotionGate_Reject(MOTION_GATE_ERROR_BUSY);
  }

  if (g_c620_motion_transport == (C620_ControlOutput_TransportFn)0)
  {
    return C620_MotionGate_Reject(MOTION_GATE_ERROR_TRANSPORT_NOT_BOUND);
  }

  if (MOTION_EXPERIMENT_ENABLE == 0U)
  {
    return C620_MotionGate_Reject(MOTION_GATE_ERROR_DISABLED);
  }

  if ((safety->estop_active != 0U) ||
      (safety->state == SAFETY_STATE_ESTOP))
  {
    return C620_MotionGate_Reject(MOTION_GATE_ERROR_ESTOP);
  }

  if (safety->fault_latched != 0U)
  {
    return C620_MotionGate_Reject(MOTION_GATE_ERROR_FAULT_LATCHED);
  }

  if ((safety->state != SAFETY_STATE_ONLINE) ||
      (safety->output_permitted == 0U) ||
      (safety->zero_output_required != 0U))
  {
    return C620_MotionGate_Reject(MOTION_GATE_ERROR_SAFETY_STATE);
  }

  if (request->requested == 0U)
  {
    return C620_MotionGate_Reject(MOTION_GATE_ERROR_REQUEST_NOT_SET);
  }

  if ((request->duration_ms == 0U) ||
    ((request->duration_ms > MOTION_MAX_TEST_DURATION_MS) &&
     ((request->duration_ms != MOTION_SINGLE_RUN_DURATION_MS) ||
      (request->current_raw != MOTION_MAX_TEST_CURRENT_RAW))))
{
  return C620_MotionGate_Reject(MOTION_GATE_ERROR_DURATION);
}

  current_raw = (int32_t)request->current_raw;
  max_current_raw = (int32_t)MOTION_MAX_TEST_CURRENT_RAW;
  if ((current_raw > max_current_raw) ||
      (current_raw < -max_current_raw))
  {
    return C620_MotionGate_Reject(MOTION_GATE_ERROR_CURRENT_RANGE);
  }

  g_c620_motion_gate_snapshot.last_request_accepted = 1U;
  g_c620_motion_gate_snapshot.last_error = MOTION_GATE_ERROR_NONE;
  g_c620_motion_gate_snapshot.accepted_request_count++;
  g_c620_motion_gate_snapshot.zero_output_required = 1U;
  return 1U;
#endif
}

HAL_StatusTypeDef C620_MotionGate_Service(
    const C620_MotionRequestTypeDef *request,
    const volatile Safety_SnapshotTypeDef *safety)
{
  C620_ControlOutput_ResultTypeDef output_result;

  if (C620_MotionGate_RequestAllowed(request, safety) == 0U)
  {
    return HAL_ERROR;
  }

  output_result = C620_ControlOutput_SendCurrent(
      request->current_raw,
      g_c620_motion_transport,
      g_c620_motion_transport_context);
  if (output_result != C620_CONTROL_OUTPUT_OK)
  {
    Safety_StateMachine_DisarmMotion();
    (void)C620_MotionGate_Reject(MOTION_GATE_ERROR_OUTPUT);
    return HAL_ERROR;
  }

  g_c620_motion_gate_snapshot.sent_frame_count++;
  if (request->current_raw == 0)
  {
    g_c620_motion_gate_snapshot.zero_frame_count++;
    g_c620_motion_gate_snapshot.zero_output_required = 1U;
    Safety_StateMachine_DisarmMotion();
    return HAL_OK;
  }

  g_c620_motion_gate_snapshot.nonzero_frame_count++;
  g_c620_motion_gate_snapshot.start_tick_ms = request->start_tick_ms;
  g_c620_motion_gate_snapshot.last_refresh_tick_ms = request->start_tick_ms;
  g_c620_motion_gate_snapshot.active_duration_ms = request->duration_ms;
  g_c620_motion_gate_snapshot.active_current_raw = request->current_raw;
  g_c620_motion_gate_snapshot.last_stop_reason = C620_MOTION_STOP_NONE;
  g_c620_motion_gate_snapshot.active = 1U;
  g_c620_motion_gate_snapshot.zero_output_required = 0U;
  C620_MotionGate_StartObservation();
  return HAL_OK;
}

void C620_MotionGate_Update(
    const volatile Safety_SnapshotTypeDef *safety,
    uint32_t now_ms)
{
  uint8_t safety_stop;
  uint8_t deadline_stop;
  uint8_t refresh_due;
  C620_ControlOutput_ResultTypeDef output_result;

  if (g_c620_motion_gate_snapshot.active == 0U)
  {
    return;
  }

  C620_MotionGate_ObserveActiveFeedback();

  safety_stop = ((safety == (const volatile Safety_SnapshotTypeDef *)0) ||
                 (safety->state != SAFETY_STATE_ONLINE) ||
                 (safety->output_permitted == 0U) ||
                 (safety->zero_output_required != 0U) ||
                 (safety->fault_latched != 0U) ||
                 (safety->estop_active != 0U) ||
                 (Safety_StateMachine_IsNonzeroOutputAllowed() == 0U)) ?
                1U : 0U;
  deadline_stop =
      ((uint32_t)(now_ms - g_c620_motion_gate_snapshot.start_tick_ms) >=
       g_c620_motion_gate_snapshot.active_duration_ms) ? 1U : 0U;

  if ((safety_stop != 0U) || (deadline_stop != 0U))
  {
    if (g_c620_motion_gate_snapshot.last_stop_reason ==
        C620_MOTION_STOP_NONE)
    {
      g_c620_motion_gate_snapshot.last_stop_reason =
          (safety_stop != 0U) ?
          C620_MOTION_STOP_SAFETY : C620_MOTION_STOP_DEADLINE;
    }
    (void)C620_MotionGate_ForceZero(now_ms);
    return;
  }

  refresh_due =
      ((uint32_t)(now_ms -
                  g_c620_motion_gate_snapshot.last_refresh_tick_ms) >=
       MOTION_CONTROL_REFRESH_PERIOD_MS) ? 1U : 0U;
  if (refresh_due == 0U)
  {
    return;
  }

  output_result = C620_ControlOutput_SendCurrent(
      g_c620_motion_gate_snapshot.active_current_raw,
      g_c620_motion_transport,
      g_c620_motion_transport_context);
  if (output_result != C620_CONTROL_OUTPUT_OK)
  {
    g_c620_motion_gate_snapshot.last_error = MOTION_GATE_ERROR_OUTPUT;
    g_c620_motion_gate_snapshot.last_stop_reason =
        C620_MOTION_STOP_OUTPUT_ERROR;
    g_c620_motion_gate_snapshot.zero_output_required = 1U;
    Safety_StateMachine_DisarmMotion();
    (void)C620_MotionGate_ForceZero(now_ms);
    return;
  }

  g_c620_motion_gate_snapshot.sent_frame_count++;
  g_c620_motion_gate_snapshot.nonzero_frame_count++;
  g_c620_motion_gate_snapshot.refresh_count++;
  g_c620_motion_gate_snapshot.last_refresh_tick_ms = now_ms;
}

HAL_StatusTypeDef C620_MotionGate_ForceZero(uint32_t now_ms)
{
  C620_ControlOutput_ResultTypeDef output_result;

  g_c620_motion_gate_snapshot.last_request_accepted = 0U;
  g_c620_motion_gate_snapshot.zero_output_required = 1U;
  Safety_StateMachine_DisarmMotion();

  if (g_c620_motion_transport == (C620_ControlOutput_TransportFn)0)
  {
    g_c620_motion_gate_snapshot.last_error =
        MOTION_GATE_ERROR_TRANSPORT_NOT_BOUND;
    g_c620_motion_gate_snapshot.stop_error_count++;
    return HAL_ERROR;
  }

  output_result = C620_ControlOutput_SendZero(
      g_c620_motion_transport,
      g_c620_motion_transport_context);
  if (output_result != C620_CONTROL_OUTPUT_OK)
  {
    g_c620_motion_gate_snapshot.last_error = MOTION_GATE_ERROR_OUTPUT;
    g_c620_motion_gate_snapshot.stop_error_count++;
    return HAL_ERROR;
  }

  g_c620_motion_gate_snapshot.sent_frame_count++;
  g_c620_motion_gate_snapshot.zero_frame_count++;
  g_c620_motion_gate_snapshot.stop_count++;
  g_c620_motion_gate_snapshot.stop_tick_ms = now_ms;
  g_c620_motion_gate_snapshot.active_current_raw = 0;
  g_c620_motion_gate_snapshot.active = 0U;
  g_c620_motion_gate_snapshot.last_error = MOTION_GATE_ERROR_NONE;
  return HAL_OK;
}
