/**
  ******************************************************************************
  * @file    c620_motion_gate.h
  * @brief   C620 motion-request safety gate for the P3-C preparation stage.
  *
  * This module validates a constrained request against the local Safety
  * snapshot, refreshes one bounded control request through a payload-only
  * transport, and sends an explicit zero payload at the deadline or on a fault.
  ******************************************************************************
  */

#ifndef __C620_MOTION_GATE_H__
#define __C620_MOTION_GATE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "c620_control_output.h"
#include "safety_state_machine.h"

/*
 * Enabled for explicitly approved, fixed ID 1 low-current experiments.
 * Boot remains disarmed; g_safety_motion_arm_request must be set manually.
 */
#define MOTION_EXPERIMENT_ENABLE       (1U)

/*
 * Operator-approved 2026-09-25 step: +1000 raw for 100 ms.
 * The gate enforces a symmetric +/-1000 raw limit immediately before the
 * fixed transport; the PC profile selects only the approved positive step.
 */
#define MOTION_MAX_TEST_CURRENT_RAW    (1000L)
#define MOTION_MAX_TEST_DURATION_MS    (100UL)
#define MOTION_SINGLE_RUN_DURATION_MS (1000UL)
#define MOTION_CONTROL_REFRESH_PERIOD_MS (10UL)

typedef enum
{
  MOTION_GATE_ERROR_NONE = 0U,
  MOTION_GATE_ERROR_NULL_ARGUMENT = 1U,
  MOTION_GATE_ERROR_DISABLED = 2U,
  MOTION_GATE_ERROR_ESTOP = 3U,
  MOTION_GATE_ERROR_FAULT_LATCHED = 4U,
  MOTION_GATE_ERROR_SAFETY_STATE = 5U,
  MOTION_GATE_ERROR_REQUEST_NOT_SET = 6U,
  MOTION_GATE_ERROR_DURATION = 7U,
  MOTION_GATE_ERROR_CURRENT_RANGE = 8U,
  MOTION_GATE_ERROR_OUTPUT_NOT_IMPLEMENTED = 9U,
  MOTION_GATE_ERROR_BUSY = 10U,
  MOTION_GATE_ERROR_TRANSPORT_NOT_BOUND = 11U,
  MOTION_GATE_ERROR_OUTPUT = 12U
} C620_MotionGateErrorTypeDef;

typedef enum
{
  C620_MOTION_STOP_NONE = 0U,
  C620_MOTION_STOP_DEADLINE = 1U,
  C620_MOTION_STOP_SAFETY = 2U,
  C620_MOTION_STOP_OUTPUT_ERROR = 3U,
  C620_MOTION_STOP_OPERATOR = 4U,
  C620_MOTION_STOP_SESSION_LOST = 5U
} C620_MotionStopReasonTypeDef;

/** A future constrained request; it does not contain an arbitrary CAN frame. */
typedef struct
{
  int16_t current_raw;
  uint32_t duration_ms;
  uint32_t start_tick_ms;
  uint32_t request_id;
  uint8_t requested;
} C620_MotionRequestTypeDef;

/** Watch-visible state for request admission, transmission and forced stop. */
typedef struct
{
  uint8_t last_request_accepted;
  uint8_t zero_output_required;
  C620_MotionGateErrorTypeDef last_error;
  C620_MotionStopReasonTypeDef last_stop_reason;
  uint32_t rejected_request_count;
  uint32_t accepted_request_count;
  uint32_t sent_frame_count;
  uint32_t nonzero_frame_count;
  uint32_t refresh_count;
  uint32_t zero_frame_count;
  uint32_t stop_count;
  uint32_t stop_error_count;
  uint32_t last_request_id;
  uint32_t start_tick_ms;
  uint32_t last_refresh_tick_ms;
  uint32_t stop_tick_ms;
  uint32_t active_duration_ms;
  int16_t active_current_raw;
  uint8_t active;
  uint16_t observation_start_encoder_raw;
  uint16_t observation_last_encoder_raw;
  int32_t observation_encoder_delta_raw;
  uint32_t observation_abs_travel_raw;
  uint16_t observation_peak_abs_speed_rpm;
  uint32_t observation_sample_count;
  uint32_t observation_start_valid_frame_count;
  uint32_t observation_last_valid_frame_count;
} C620_MotionGateSnapshotTypeDef;

extern volatile C620_MotionGateSnapshotTypeDef g_c620_motion_gate_snapshot;

void C620_MotionGate_Init(void);

/** Bind the payload-only transport to a fixed 0x200/DLC8 BSP function. */
void C620_MotionGate_BindTransport(
    C620_ControlOutput_TransportFn transport,
    void *context);

/**
  * @brief Validate a constrained request against the local safety snapshot.
  * @retval 1 Only if every gate, the one-shot arm and first-test limits pass.
  * @retval 0 Request rejected.
  */
uint8_t C620_MotionGate_RequestAllowed(
    const C620_MotionRequestTypeDef *request,
    const volatile Safety_SnapshotTypeDef *safety);

/**
  * @brief Service a validated request.
  *
  * On success one fixed-slot payload is accepted by the bound transport.  The
  * periodic Update call owns refresh, deadline and fault stopping.
  */
HAL_StatusTypeDef C620_MotionGate_Service(
    const C620_MotionRequestTypeDef *request,
    const volatile Safety_SnapshotTypeDef *safety);

/** Refresh an active request, or stop it at its deadline/on a Safety fault. */
void C620_MotionGate_Update(
    const volatile Safety_SnapshotTypeDef *safety,
    uint32_t now_ms);

/** Force the gate back to an observable zero-output condition. */
HAL_StatusTypeDef C620_MotionGate_ForceZero(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* __C620_MOTION_GATE_H__ */
