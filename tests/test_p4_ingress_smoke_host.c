#include <stdio.h>

#include "p4_ingress_smoke_test.h"

int main(void)
{
  Safety_SnapshotTypeDef safety = {0};

  C620_MotionGate_Init();
  safety.state = SAFETY_STATE_INIT;
  safety.output_permitted = 0U;
  safety.zero_output_required = 1U;

  if (P4_IngressSmokeTest_Run(&safety, 1000UL) == 0U)
  {
    return 1;
  }
  if ((g_p4_ingress_smoke_snapshot.state != P4_INGRESS_SMOKE_PASSED) ||
      (g_p4_ingress_smoke_snapshot.step != 8UL) ||
      (g_p4_ingress_smoke_snapshot.request_id != 1001UL) ||
      (g_p4_ingress_smoke_snapshot.frame_length != 30U) ||
      (g_p4_ingress_smoke_snapshot.record_count != 1U) ||
      (g_p4_ingress_smoke_snapshot.record_error_code != 0xE2U) ||
      (g_p4_ingress_smoke_snapshot.sent_frame_count_before !=
       g_p4_ingress_smoke_snapshot.sent_frame_count_after))
  {
    return 2;
  }

  (void)printf("p4_ingress_smoke_host_tests=pass\n");
  return 0;
}
