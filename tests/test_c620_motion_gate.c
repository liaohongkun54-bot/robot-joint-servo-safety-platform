/**
  ******************************************************************************
  * @file    test_c620_motion_gate.c
  * @brief   Host-only periodic motion, deadline stop and fault-stop tests.
  ******************************************************************************
  */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "c620_motion_gate.h"

static uint32_t g_transport_count;
static uint32_t g_transport_fail_on_call;
static uint8_t g_last_payload[C620_CONTROL_DATA_LENGTH];

static uint8_t TestTransport(
    const uint8_t data[C620_CONTROL_DATA_LENGTH],
    void *context)
{
  uint8_t index;

  (void)context;
  g_transport_count++;
  for (index = 0U; index < C620_CONTROL_DATA_LENGTH; ++index)
  {
    g_last_payload[index] = data[index];
  }
  if (g_transport_count == g_transport_fail_on_call)
  {
    return 0U;
  }
  return C620_CONTROL_OUTPUT_TRANSPORT_OK;
}

static void PrepareOnlineSafety(uint32_t now_ms)
{
  Safety_InputTypeDef input = {0};

  Safety_StateMachine_Init();
  input.init_ok = 1U;
  input.link_state = C620_LINK_ONLINE;
  input.valid_frame_count = 1U;
  input.now_ms = now_ms;
  Safety_StateMachine_Update(&input);
  input.valid_frame_count = 2U;
  input.now_ms++;
  Safety_StateMachine_Update(&input);
}

static void PrepareArmedOnlineSafety(uint32_t now_ms)
{
  Safety_InputTypeDef input = {0};

  PrepareOnlineSafety(now_ms);

  g_safety_motion_arm_request = 1U;
  input.valid_frame_count = 3U;
  input.link_state = C620_LINK_ONLINE;
  input.init_ok = 1U;
  input.now_ms = now_ms + 2U;
  Safety_StateMachine_Update(&input);
  assert(Safety_StateMachine_IsNonzeroOutputAllowed() == 1U);
}

static void TestBootDisarmedCannotTransmit(void)
{
  C620_MotionRequestTypeDef request = {0};

  C620_MotionGate_Init();
  C620_MotionGate_BindTransport(TestTransport, (void *)0);
  g_transport_count = 0U;
  g_transport_fail_on_call = 0U;
  PrepareOnlineSafety(500U);

  request.current_raw = 1000;
  request.duration_ms = 100U;
  request.start_tick_ms = 600U;
  request.request_id = 7100U;
  request.requested = 1U;
  assert(g_safety_motion_arm_request == 0U);
  assert(Safety_StateMachine_IsNonzeroOutputAllowed() == 0U);
  assert(C620_MotionGate_Service(&request, &g_safety_snapshot) == HAL_ERROR);
  assert(g_transport_count == 0U);
  assert(g_c620_motion_gate_snapshot.sent_frame_count == 0U);
  assert(g_c620_motion_gate_snapshot.last_error ==
         MOTION_GATE_ERROR_SAFETY_STATE);
}

static void TestPeriodicRefreshStopsAtDeadline(int16_t current_raw)
{
  C620_MotionRequestTypeDef request = {0};
  uint32_t now_ms;
  uint8_t index;

  C620_MotionGate_Init();
  C620_MotionGate_BindTransport(TestTransport, (void *)0);
  g_transport_count = 0U;
  g_transport_fail_on_call = 0U;
  PrepareArmedOnlineSafety(900U);

  request.current_raw = current_raw;
  request.duration_ms = 100U;
  request.start_tick_ms = 1000U;
  request.request_id = 7101U;
  request.requested = 1U;
  assert(C620_MotionGate_Service(&request, &g_safety_snapshot) == HAL_OK);
  assert(g_transport_count == 1U);
  assert(g_last_payload[0] == (uint8_t)((uint16_t)current_raw >> 8));
  assert(g_last_payload[1] == (uint8_t)current_raw);
  assert(g_c620_motion_gate_snapshot.active == 1U);

  for (now_ms = 1001U; now_ms < 1100U; ++now_ms)
  {
    C620_MotionGate_Update(&g_safety_snapshot, now_ms);
    assert(g_last_payload[0] == (uint8_t)((uint16_t)current_raw >> 8));
    assert(g_last_payload[1] == (uint8_t)current_raw);
    for (index = 2U; index < C620_CONTROL_DATA_LENGTH; ++index)
    {
      assert(g_last_payload[index] == 0U);
    }
  }
  assert(g_transport_count == 10U);
  assert(g_c620_motion_gate_snapshot.sent_frame_count == 10U);
  assert(g_c620_motion_gate_snapshot.nonzero_frame_count == 10U);
  assert(g_c620_motion_gate_snapshot.refresh_count == 9U);
  assert(g_c620_motion_gate_snapshot.last_refresh_tick_ms == 1090U);
  C620_MotionGate_Update(&g_safety_snapshot, 1100U);
  assert(g_transport_count == 11U);
  for (index = 0U; index < C620_CONTROL_DATA_LENGTH; ++index)
  {
    assert(g_last_payload[index] == 0U);
  }
  assert(g_c620_motion_gate_snapshot.active == 0U);
  assert(g_c620_motion_gate_snapshot.active_current_raw == 0);
  assert(g_c620_motion_gate_snapshot.sent_frame_count == 11U);
  assert(g_c620_motion_gate_snapshot.nonzero_frame_count == 10U);
  assert(g_c620_motion_gate_snapshot.zero_frame_count == 1U);
  assert(g_c620_motion_gate_snapshot.stop_count == 1U);
  assert(g_c620_motion_gate_snapshot.stop_tick_ms == 1100U);
  assert(g_safety_motion_arm_request == 0U);
  C620_MotionGate_Update(&g_safety_snapshot, 1200U);
  assert(g_transport_count == 11U);
}

static void TestApprovedCurrentAndDurationLimits(void)
{
  C620_MotionRequestTypeDef request = {0};
  const int16_t rejected_currents[] = {1001, -1001, INT16_MAX, INT16_MIN};
  const uint32_t rejected_durations[] = {0U, 101U};
  uint32_t index;

  C620_MotionGate_Init();
  C620_MotionGate_BindTransport(TestTransport, (void *)0);
  g_transport_count = 0U;
  g_transport_fail_on_call = 0U;
  PrepareArmedOnlineSafety(1200U);

  request.current_raw = -1000;
  request.duration_ms = 100U;
  request.start_tick_ms = 1300U;
  request.request_id = 7401U;
  request.requested = 1U;
  assert(C620_MotionGate_Service(&request, &g_safety_snapshot) == HAL_OK);
  assert(g_transport_count == 1U);
  assert(g_last_payload[0] == 0xFCU);
  assert(g_last_payload[1] == 0x18U);
  assert(C620_MotionGate_ForceZero(1301U) == HAL_OK);

  C620_MotionGate_Init();
  C620_MotionGate_BindTransport(TestTransport, (void *)0);
  g_transport_count = 0U;
  PrepareArmedOnlineSafety(1400U);
  request.start_tick_ms = 1500U;
  request.request_id = 7402U;
  for (index = 0U; index < sizeof(rejected_currents) / sizeof(rejected_currents[0]); ++index)
  {
    request.current_raw = rejected_currents[index];
    assert(C620_MotionGate_Service(&request, &g_safety_snapshot) == HAL_ERROR);
    assert(g_transport_count == 0U);
    assert(g_c620_motion_gate_snapshot.last_error ==
           MOTION_GATE_ERROR_CURRENT_RANGE);
  }
  request.current_raw = 1000;
  for (index = 0U; index < sizeof(rejected_durations) / sizeof(rejected_durations[0]); ++index)
  {
    request.duration_ms = rejected_durations[index];
    assert(C620_MotionGate_Service(&request, &g_safety_snapshot) == HAL_ERROR);
    assert(g_transport_count == 0U);
    assert(g_c620_motion_gate_snapshot.last_error == MOTION_GATE_ERROR_DURATION);
  }
}

static void TestActiveFeedbackObservationTracksWrapAndPeak(void)
{
  C620_MotionRequestTypeDef request = {0};

  C620_MotionGate_Init();
  C620_MotionGate_BindTransport(TestTransport, (void *)0);
  g_transport_count = 0U;
  g_transport_fail_on_call = 0U;
  PrepareArmedOnlineSafety(4900U);

  g_c620_feedback.has_valid_feedback = 1U;
  g_c620_feedback.link_state = C620_LINK_ONLINE;
  g_c620_feedback.encoder_raw = 8190U;
  g_c620_feedback.speed_rpm = 0;
  g_c620_feedback.valid_frame_count = 10U;

  request.current_raw = 200;
  request.duration_ms = 100U;
  request.start_tick_ms = 5000U;
  request.request_id = 7403U;
  request.requested = 1U;
  assert(C620_MotionGate_Service(&request, &g_safety_snapshot) == HAL_OK);
  assert(g_c620_motion_gate_snapshot.observation_start_encoder_raw == 8190U);
  assert(g_c620_motion_gate_snapshot.observation_sample_count == 0U);

  g_c620_feedback.encoder_raw = 2U;
  g_c620_feedback.speed_rpm = 50;
  g_c620_feedback.valid_frame_count = 11U;
  C620_MotionGate_Update(&g_safety_snapshot, 5001U);

  g_c620_feedback.encoder_raw = 1U;
  g_c620_feedback.speed_rpm = -80;
  g_c620_feedback.valid_frame_count = 12U;
  C620_MotionGate_Update(&g_safety_snapshot, 5002U);

  g_c620_feedback.encoder_raw = 5U;
  g_c620_feedback.speed_rpm = 0;
  g_c620_feedback.valid_frame_count = 13U;
  C620_MotionGate_Update(&g_safety_snapshot, 5100U);

  assert(g_c620_motion_gate_snapshot.active == 0U);
  assert(g_c620_motion_gate_snapshot.observation_last_encoder_raw == 5U);
  assert(g_c620_motion_gate_snapshot.observation_encoder_delta_raw == 7L);
  assert(g_c620_motion_gate_snapshot.observation_abs_travel_raw == 9U);
  assert(g_c620_motion_gate_snapshot.observation_peak_abs_speed_rpm == 80U);
  assert(g_c620_motion_gate_snapshot.observation_sample_count == 3U);
  assert(g_c620_motion_gate_snapshot.observation_start_valid_frame_count == 10U);
  assert(g_c620_motion_gate_snapshot.observation_last_valid_frame_count == 13U);
}

static void TestRefreshFailureForcesZero(void)
{
  C620_MotionRequestTypeDef request = {0};

  C620_MotionGate_Init();
  C620_MotionGate_BindTransport(TestTransport, (void *)0);
  g_transport_count = 0U;
  g_transport_fail_on_call = 2U;
  PrepareArmedOnlineSafety(2400U);

  request.current_raw = 1000;
  request.duration_ms = 100U;
  request.start_tick_ms = 2500U;
  request.request_id = 7104U;
  request.requested = 1U;
  assert(C620_MotionGate_Service(&request, &g_safety_snapshot) == HAL_OK);

  C620_MotionGate_Update(&g_safety_snapshot, 2510U);
  assert(g_transport_count == 3U);
  assert(g_last_payload[0] == 0U);
  assert(g_last_payload[1] == 0U);
  assert(g_c620_motion_gate_snapshot.active == 0U);
  assert(g_c620_motion_gate_snapshot.sent_frame_count == 2U);
  assert(g_c620_motion_gate_snapshot.nonzero_frame_count == 1U);
  assert(g_c620_motion_gate_snapshot.refresh_count == 0U);
  assert(g_c620_motion_gate_snapshot.zero_frame_count == 1U);
  assert(g_c620_motion_gate_snapshot.stop_count == 1U);
  assert(g_safety_motion_arm_request == 0U);
}

static void TestManualDisarmForcesZero(void)
{
  C620_MotionRequestTypeDef request = {0};

  C620_MotionGate_Init();
  C620_MotionGate_BindTransport(TestTransport, (void *)0);
  g_transport_count = 0U;
  g_transport_fail_on_call = 0U;
  PrepareArmedOnlineSafety(2600U);

  request.current_raw = 1000;
  request.duration_ms = 100U;
  request.start_tick_ms = 2700U;
  request.request_id = 7106U;
  request.requested = 1U;
  assert(C620_MotionGate_Service(&request, &g_safety_snapshot) == HAL_OK);
  assert(g_safety_motion_arm_request == 1U);
  assert(g_c620_motion_gate_snapshot.active == 1U);

  g_safety_motion_arm_request = 0U;
  C620_MotionGate_Update(&g_safety_snapshot, 2705U);
  assert(g_transport_count == 2U);
  assert(g_last_payload[0] == 0U);
  assert(g_last_payload[1] == 0U);
  assert(g_c620_motion_gate_snapshot.active == 0U);
  assert(g_c620_motion_gate_snapshot.refresh_count == 0U);
  assert(g_c620_motion_gate_snapshot.zero_frame_count == 1U);
  assert(g_c620_motion_gate_snapshot.stop_tick_ms == 2705U);
}

static void TestFaultForcesEarlyZero(void)
{
  C620_MotionRequestTypeDef request = {0};
  Safety_InputTypeDef input = {0};

  C620_MotionGate_Init();
  C620_MotionGate_BindTransport(TestTransport, (void *)0);
  g_transport_count = 0U;
  g_transport_fail_on_call = 0U;
  PrepareArmedOnlineSafety(1900U);

  request.current_raw = 1000;
  request.duration_ms = 100U;
  request.start_tick_ms = 2000U;
  request.request_id = 7102U;
  request.requested = 1U;
  assert(C620_MotionGate_Service(&request, &g_safety_snapshot) == HAL_OK);

  input.now_ms = 2020U;
  input.link_state = C620_LINK_ONLINE;
  input.valid_frame_count = 4U;
  input.init_ok = 1U;
  input.estop_active = 1U;
  Safety_StateMachine_Update(&input);
  C620_MotionGate_Update(&g_safety_snapshot, input.now_ms);
  assert(g_transport_count == 2U);
  assert(g_last_payload[0] == 0U);
  assert(g_last_payload[1] == 0U);
  assert(g_c620_motion_gate_snapshot.active == 0U);
  assert(g_c620_motion_gate_snapshot.stop_tick_ms == 2020U);
  assert(g_safety_motion_arm_request == 0U);
}

static void TestFailedStopRetriesZero(void)
{
  C620_MotionRequestTypeDef request = {0};

  C620_MotionGate_Init();
  C620_MotionGate_BindTransport(TestTransport, (void *)0);
  g_transport_count = 0U;
  g_transport_fail_on_call = 2U;
  PrepareArmedOnlineSafety(2900U);

  request.current_raw = 1000;
  request.duration_ms = 100U;
  request.start_tick_ms = 3000U;
  request.request_id = 7103U;
  request.requested = 1U;
  assert(C620_MotionGate_Service(&request, &g_safety_snapshot) == HAL_OK);

  C620_MotionGate_Update(&g_safety_snapshot, 3100U);
  assert(g_transport_count == 2U);
  assert(g_c620_motion_gate_snapshot.active == 1U);
  assert(g_c620_motion_gate_snapshot.stop_error_count == 1U);
  assert(g_safety_motion_arm_request == 0U);

  g_transport_fail_on_call = 0U;
  C620_MotionGate_Update(&g_safety_snapshot, 3101U);
  assert(g_transport_count == 3U);
  assert(g_last_payload[0] == 0U);
  assert(g_last_payload[1] == 0U);
  assert(g_c620_motion_gate_snapshot.active == 0U);
  assert(g_c620_motion_gate_snapshot.stop_count == 1U);
}

static void TestTickWrapRefreshAndDeadline(void)
{
  C620_MotionRequestTypeDef request = {0};
  const uint32_t start_tick_ms = UINT32_MAX - 5U;

  C620_MotionGate_Init();
  C620_MotionGate_BindTransport(TestTransport, (void *)0);
  g_transport_count = 0U;
  g_transport_fail_on_call = 0U;
  PrepareArmedOnlineSafety(3900U);

  request.current_raw = 1000;
  request.duration_ms = 100U;
  request.start_tick_ms = start_tick_ms;
  request.request_id = 7105U;
  request.requested = 1U;
  assert(C620_MotionGate_Service(&request, &g_safety_snapshot) == HAL_OK);

  C620_MotionGate_Update(&g_safety_snapshot, 3U);
  assert(g_transport_count == 1U);
  C620_MotionGate_Update(&g_safety_snapshot, 4U);
  assert(g_transport_count == 2U);
  assert(g_c620_motion_gate_snapshot.refresh_count == 1U);
  C620_MotionGate_Update(&g_safety_snapshot, 93U);
  assert(g_transport_count == 3U);
  assert(g_c620_motion_gate_snapshot.active == 1U);
  C620_MotionGate_Update(&g_safety_snapshot, 94U);
  assert(g_transport_count == 4U);
  assert(g_last_payload[0] == 0U);
  assert(g_last_payload[1] == 0U);
  assert(g_c620_motion_gate_snapshot.active == 0U);
  assert(g_c620_motion_gate_snapshot.stop_tick_ms == 94U);
}

static void TestOneSecondRunStopsAndCannotRestart(void)
{
  C620_MotionRequestTypeDef request = {0};
  uint32_t now_ms;
  uint8_t index;

  C620_MotionGate_Init();
  C620_MotionGate_BindTransport(TestTransport, (void *)0);
  g_transport_count = 0U;
  g_transport_fail_on_call = 0U;
  PrepareArmedOnlineSafety(900U);

  request.current_raw = 1000;
  request.duration_ms = 1000U;
  request.start_tick_ms = 1000U;
  request.request_id = 7504U;
  request.requested = 1U;

  assert(C620_MotionGate_Service(&request, &g_safety_snapshot) == HAL_OK);

  for (now_ms = 1001U; now_ms < 2000U; ++now_ms)
  {
    C620_MotionGate_Update(&g_safety_snapshot, now_ms);
  }
  assert(g_c620_motion_gate_snapshot.active == 1U);
  assert(g_c620_motion_gate_snapshot.nonzero_frame_count == 100U);
  assert(g_c620_motion_gate_snapshot.refresh_count == 99U);
  assert(g_c620_motion_gate_snapshot.zero_frame_count == 0U);

  C620_MotionGate_Update(&g_safety_snapshot, 2000U);
  assert(g_transport_count == 101U);
  assert(g_c620_motion_gate_snapshot.zero_frame_count == 1U);
  assert(g_c620_motion_gate_snapshot.stop_count == 1U);
  assert(g_c620_motion_gate_snapshot.stop_tick_ms == 2000U);
  assert(g_c620_motion_gate_snapshot.stop_error_count == 0U);
  assert(g_c620_motion_gate_snapshot.active == 0U);
  assert(g_safety_motion_arm_request == 0U);
  assert(g_safety_snapshot.output_permitted == 0U);
  for (index = 0U; index < C620_CONTROL_DATA_LENGTH; ++index)
  {
    assert(g_last_payload[index] == 0U);
  }

  C620_MotionGate_Update(&g_safety_snapshot, 2001U);
  assert(g_transport_count == 101U);
  assert(C620_MotionGate_Service(&request, &g_safety_snapshot) == HAL_ERROR);
  assert(g_transport_count == 101U);
}

static void TestOneSecondWhitelistRejectsNeighbors(void)
{
  const int16_t currents[] = {1000, 1000, -1000, 1001};
  const uint32_t durations[] = {999U, 1001U, 1000U, 100U};
  const C620_MotionGateErrorTypeDef errors[] = {
      MOTION_GATE_ERROR_DURATION,
      MOTION_GATE_ERROR_DURATION,
      MOTION_GATE_ERROR_DURATION,
      MOTION_GATE_ERROR_CURRENT_RANGE
  };
  C620_MotionRequestTypeDef request = {0};
  uint32_t index;

  for (index = 0U; index < 4U; ++index)
  {
    C620_MotionGate_Init();
    C620_MotionGate_BindTransport(TestTransport, (void *)0);
    g_transport_count = 0U;
    g_transport_fail_on_call = 0U;
    PrepareArmedOnlineSafety(2900U);

    request.current_raw = currents[index];
    request.duration_ms = durations[index];
    request.start_tick_ms = 3000U;
    request.request_id = 7600U + index;
    request.requested = 1U;

    assert(C620_MotionGate_Service(&request, &g_safety_snapshot) == HAL_ERROR);
    assert(g_c620_motion_gate_snapshot.last_error == errors[index]);
    assert(g_transport_count == 0U);
    assert(g_c620_motion_gate_snapshot.sent_frame_count == 0U);
  }
}

int main(void)
{
  TestBootDisarmedCannotTransmit();
  TestPeriodicRefreshStopsAtDeadline(100);
  TestPeriodicRefreshStopsAtDeadline(200);
  TestPeriodicRefreshStopsAtDeadline(1000);
  TestApprovedCurrentAndDurationLimits();
  TestActiveFeedbackObservationTracksWrapAndPeak();
  TestFaultForcesEarlyZero();
  TestRefreshFailureForcesZero();
  TestManualDisarmForcesZero();
  TestFailedStopRetriesZero();
  TestTickWrapRefreshAndDeadline();
  TestOneSecondRunStopsAndCannotRestart();
  TestOneSecondWhitelistRejectsNeighbors();
  (void)printf("c620_motion_gate_tests=pass\n");
  return 0;
}
