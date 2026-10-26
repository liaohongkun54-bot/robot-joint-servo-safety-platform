#include "operator_runtime.h"
#include "operator_controller.h"
#include "operator_platform.h"
#include "task_uart_adapter.h"
#include "test_task_ingress_service.h"
#include "bsp_can.h"

static OperatorController controller;
static uint8_t initialized, boot_done;
static uint32_t boot_start;

static OperatorSession_Conditions Conditions(uint32_t now)
{
  OperatorSession_Conditions c;
  uint32_t used = (uint32_t)g_test_task_ingress_records.count +
      g_test_task_ingress_records.reserved_count;
  uint8_t fresh = (g_c620_feedback.has_valid_feedback != 0U &&
      g_c620_feedback.link_state == C620_LINK_ONLINE &&
      (uint32_t)(now - g_c620_feedback.last_rx_tick_ms) < C620_FEEDBACK_TIMEOUT_MS &&
      g_bsp_can_hal_error_code == 0U) ? 1U : 0U;
  c.safe_to_enable = (fresh != 0U && g_safety_snapshot.state == SAFETY_STATE_ONLINE &&
      g_safety_snapshot.fault_latched == 0U && g_safety_snapshot.estop_active == 0U &&
      g_safety_test_estop_active == 0U) ? 1U : 0U;
  c.motion_active = g_c620_motion_gate_snapshot.active;
  c.can_clear_fault = (fresh != 0U && c.motion_active == 0U &&
      g_safety_snapshot.state == SAFETY_STATE_SAFE &&
      g_safety_snapshot.estop_active == 0U && g_safety_test_estop_active == 0U) ? 1U : 0U;
  c.free_record_slots = (used < TEST_RECORD_BUFFER_CAPACITY) ?
      (uint16_t)(TEST_RECORD_BUFFER_CAPACITY - used) : 0U;
  return c;
}

static uint8_t Stop(void *context, uint32_t now, uint8_t reason)
{
  (void)context;
  if (g_c620_motion_gate_snapshot.active != 0U &&
      g_c620_motion_gate_snapshot.last_stop_reason == C620_MOTION_STOP_NONE)
  { g_c620_motion_gate_snapshot.last_stop_reason = (C620_MotionStopReasonTypeDef)reason; }
  return (C620_MotionGate_ForceZero(now) == HAL_OK) ? 1U : 0U;
}
static uint8_t Clear(void *context)
{
  (void)context;
  /* Single-cycle recovery request; never alter fault fields directly. */
  g_safety_operator_clear_request = 1U;
  return 1U;
}
static uint8_t Permitted(void)
{ return OperatorController_Permitted(&controller); }
static uint8_t Authorize(const TestTask_DescriptorTypeDef *task, uint32_t now)
{
  OperatorSession_Conditions c = Conditions(now);
  return (OperatorController_Consume(&controller,task,now,&c) == OPERATOR_OK) ? 1U : 0U;
}
static void AdmissionDone(uint8_t accepted, uint32_t now)
{
  if (accepted == 0U)
  {
    /* A failed admission must not leave consumed permission executable. */
    controller.session.pending_stop = 1U;
    {
      OperatorSession_Conditions c = Conditions(now);
      OperatorController_Tick(&controller,now,&c);
    }
  }
}

uint8_t OperatorRuntime_Ready(void) { return controller.ready; }

void OperatorRuntime_Service(uint32_t now)
{
  OperatorSession_Conditions c;
  OperatorWire_Request request;
  OperatorWire_Reply reply;
  uint32_t low=0U,high=0U;
  uint8_t boot_result;
  if (initialized == 0U)
  {
    OperatorController_Init(&controller,0U,0U,Stop,Clear,0);
    initialized=1U;boot_start=now;
  }
  /* Reinstall after any local module reinitialization; never leave a Watch
   * fallback in the production control path. No output occurs in binding. */
  Safety_StateMachine_BindAuthorization(Permitted);
  TestTask_RuntimeBindAuthorization(Authorize,AdmissionDone);
  if (boot_done == 0U)
  {
    boot_result=OperatorPlatform_PollBootId(&low,&high);
    if (boot_result==1U) { OperatorController_Init(&controller,low,high,Stop,Clear,0); boot_done=1U; }
    else if (boot_result==2U || (uint32_t)(now-boot_start)>=100U) { boot_done=1U; }
  }
  c=Conditions(now);
  /* Gate completed a deadline/fault zero since the previous owner iteration. */
  if (controller.session.executing != 0U && c.motion_active == 0U &&
      controller.session.pending_stop == 0U)
  { OperatorSession_Stopped(&controller.session,1U); }
  OperatorController_Tick(&controller,now,&c);
  /* One bounded request per cycle; receiver returns STOP/CLOSE first. */
  if (TaskUart_AdapterTakeOperator(&request) != 0U &&
      OperatorController_Handle(&controller,&request,now,&c,&reply) != 0U)
  {
    reply.fault_code=g_safety_snapshot.last_fault_code;
    if (TaskUart_AdapterQueueOperator(&reply) != TASK_UART_TX_QUEUED)
    {
      /* Unknown permission delivery is never left armed. */
      OperatorSession_Close(&controller.session);
      OperatorController_Tick(&controller,now,&c);
    }
  }
  Safety_StateMachine_RefreshAuthorization(g_c620_feedback.link_state);
}
