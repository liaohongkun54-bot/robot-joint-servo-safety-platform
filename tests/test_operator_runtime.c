#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "operator_runtime.h"
#include "operator_controller.h"
#include "task_uart_adapter.h"
#include "test_task_ingress_service.h"
#include "bsp_can.h"

volatile uint32_t g_bsp_can_hal_error_code;
TestRecord_BufferTypeDef g_test_task_ingress_records;
static uint8_t boot_fail, request_ready, output_fail;
static OperatorWire_Request pending;
static OperatorWire_Reply last_reply;
static unsigned nonzero_count, zero_count;
uint8_t OperatorPlatform_PollBootId(uint32_t *low,uint32_t *high)
{ if(boot_fail) { return 2U; } *low=123U;*high=456U;return 1U; }
uint8_t TaskUart_AdapterTakeOperator(OperatorWire_Request *r)
{ if(!request_ready) { return 0U; } *r=pending;request_ready=0U;return 1U; }
TaskUart_TxResultTypeDef TaskUart_AdapterQueueOperator(const OperatorWire_Reply *r)
{ last_reply=*r;return TASK_UART_TX_QUEUED; }
static uint8_t Output(const uint8_t *payload,void *context)
{
  unsigned i;
  uint8_t nonzero=0U;
  (void)context;
  for(i=0;i<8U;i++) { nonzero |= payload[i]; }
  if(nonzero) { nonzero_count++; }
  else { zero_count++; }
  return output_fail ? C620_CONTROL_OUTPUT_TRANSPORT_ERROR : C620_CONTROL_OUTPUT_TRANSPORT_OK;
}
static void Service(uint32_t now)
{
  g_c620_feedback.last_rx_tick_ms=now;
  g_c620_feedback.valid_frame_count++;
  OperatorRuntime_Service(now);
}
static void Send(uint8_t operation,uint32_t sequence,uint32_t now)
{
  pending.operation=operation;pending.sequence=sequence;pending.request_id++;
  request_ready=1U;Service(now);
}
static Safety_SnapshotTypeDef Snapshot(void)
{
  Safety_SnapshotTypeDef s;
  s.state=g_safety_snapshot.state;s.output_permitted=g_safety_snapshot.output_permitted;
  s.zero_output_required=g_safety_snapshot.zero_output_required;
  s.fault_latched=g_safety_snapshot.fault_latched;s.estop_active=g_safety_snapshot.estop_active;
  s.transition_count=0U;s.fault_count=0U;s.recovery_valid_frames=0U;
  s.last_transition_ms=0U;s.last_fault_code=0U;return s;
}
int main(int argc,char **argv)
{
  TestTask_DescriptorTypeDef task={1U,42U,1U,TEST_TASK_ACTION_CURRENT_TEST,1000,1000,1000U,100U,TEST_TASK_TIMEOUT_STOP,TEST_TASK_PERMISSION_OPERATOR};
  TestTask_DescriptorTypeDef wrong;
  Safety_SnapshotTypeDef safety;
  (void)argv;
  boot_fail=(argc>1) ? 1U : 0U;
  Safety_StateMachine_Init();C620_MotionGate_Init();TestTask_RuntimeInit();
  TestRecord_BufferInit(&g_test_task_ingress_records);
  C620_MotionGate_BindTransport(Output,0);
  g_safety_snapshot.state=SAFETY_STATE_ONLINE;
  g_c620_feedback.has_valid_feedback=1U;g_c620_feedback.link_state=C620_LINK_ONLINE;
  Service(0U);
  g_safety_motion_arm_request=1U;
  Service(1U);
  assert(!Safety_StateMachine_IsNonzeroOutputAllowed());
  assert(!g_safety_snapshot.output_permitted && nonzero_count==0U);
  safety=Snapshot();
  assert(TestTask_RuntimeSubmit(&task,&safety,&g_test_task_ingress_records,1U)==TEST_TASK_RUNTIME_ERROR_SAFETY);
  if(boot_fail) {
    assert(!OperatorRuntime_Ready());
    puts("operator_runtime: unavailable boot identity rejects Watch and task output");return 0;
  }
  Safety_StateMachine_Init();
  g_safety_snapshot.state=SAFETY_STATE_ONLINE;
  TestTask_RuntimeInit();
  g_safety_motion_arm_request=1U;
  Service(1U);
  assert(!Safety_StateMachine_IsNonzeroOutputAllowed());
  assert(OperatorRuntime_Ready());
  Send(OPERATOR_OPEN,0U,2U);assert(last_reply.result==OPERATOR_OK);
  pending.boot_low=last_reply.boot_low;pending.boot_high=last_reply.boot_high;
  pending.session_id=last_reply.session_id;pending.task=task;
  Send(OPERATOR_GRANT,1U,3U);
  assert(last_reply.result==OPERATOR_OK && nonzero_count==0U);
  wrong=task;wrong.request_id++;
  safety=Snapshot();
  assert(TestTask_RuntimeSubmit(&wrong,&safety,&g_test_task_ingress_records,3U)==TEST_TASK_RUNTIME_ERROR_SAFETY);
  assert(nonzero_count==0U);
  Send(OPERATOR_GRANT,2U,4U);safety=Snapshot();
  assert(TestTask_RuntimeSubmit(&task,&safety,&g_test_task_ingress_records,4U)==TEST_TASK_RUNTIME_ACCEPTED);
  assert(nonzero_count==1U && g_c620_motion_gate_snapshot.active);
  output_fail=1U;
  Send(OPERATOR_STOP,3U,5U);
  assert((last_reply.flags & 8U)!=0U && g_c620_motion_gate_snapshot.active);
  assert(!Safety_StateMachine_IsNonzeroOutputAllowed());
  output_fail=0U;Service(6U);
  assert(!g_c620_motion_gate_snapshot.active);
  assert(g_c620_motion_gate_snapshot.last_stop_reason==C620_MOTION_STOP_OPERATOR);
  Send(OPERATOR_GRANT,4U,7U);safety=Snapshot();
  task=pending.task;
  assert(TestTask_RuntimeSubmit(&task,&safety,&g_test_task_ingress_records,7U)==TEST_TASK_RUNTIME_ACCEPTED);
  Service(1007U); /* Fresh CAN, but no PC heartbeat: independent session fault. */
  assert(!g_c620_motion_gate_snapshot.active);
  assert(g_c620_motion_gate_snapshot.last_stop_reason==C620_MOTION_STOP_SESSION_LOST);
  assert(!Safety_StateMachine_IsNonzeroOutputAllowed());
  assert(nonzero_count==2U && zero_count>=3U);
  puts("operator_runtime: Watch bypass, bound task, STOP failure/recovery and session loss passed");
  return 0;
}
