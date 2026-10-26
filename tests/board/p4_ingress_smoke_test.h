/**
  ******************************************************************************
  * @file    p4_ingress_smoke_test.h
  * @brief   Board-side software-injection fixture for the P4 task ingress.
  ******************************************************************************
  */

#ifndef __P4_INGRESS_SMOKE_TEST_H__
#define __P4_INGRESS_SMOKE_TEST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "test_task_receiver.h"
#include "test_task_runtime.h"

typedef enum
{
  P4_INGRESS_SMOKE_IDLE = 0U,
  P4_INGRESS_SMOKE_RUNNING = 1U,
  P4_INGRESS_SMOKE_PASSED = 2U,
  P4_INGRESS_SMOKE_FAILED = 3U
} P4_IngressSmokeStateTypeDef;

typedef enum
{
  P4_INGRESS_SMOKE_ERROR_NONE = 0U,
  P4_INGRESS_SMOKE_ERROR_NULL_SAFETY = 1U,
  P4_INGRESS_SMOKE_ERROR_ENCODE = 2U,
  P4_INGRESS_SMOKE_ERROR_RECEIVER = 3U,
  P4_INGRESS_SMOKE_ERROR_TAKE_TASK = 4U,
  P4_INGRESS_SMOKE_ERROR_RUNTIME_RESULT = 5U,
  P4_INGRESS_SMOKE_ERROR_RECORD_COUNT = 6U,
  P4_INGRESS_SMOKE_ERROR_RECORD_CONTENT = 7U,
  P4_INGRESS_SMOKE_ERROR_OUTPUT_CHANGED = 8U
} P4_IngressSmokeErrorTypeDef;

typedef struct
{
  P4_IngressSmokeStateTypeDef state;
  P4_IngressSmokeErrorTypeDef error;
  uint32_t step;
  uint32_t request_id;
  uint16_t frame_length;
  uint16_t record_count;
  uint8_t record_error_code;
  TestTask_WireErrorTypeDef wire_error;
  TestTask_ReceiverResultTypeDef receiver_result;
  TestTask_RuntimeResultTypeDef runtime_result;
  uint32_t sent_frame_count_before;
  uint32_t sent_frame_count_after;
} P4_IngressSmokeSnapshotTypeDef;

extern volatile P4_IngressSmokeSnapshotTypeDef g_p4_ingress_smoke_snapshot;
extern TestRecord_BufferTypeDef g_p4_ingress_smoke_records;
extern TestTask_ReceiverTypeDef g_p4_ingress_smoke_receiver;

/**
  * @brief Run once after Safety_StateMachine_Init(); no peripheral is used.
  * @retval 1 when every expected safe-rejection observation matches, else 0.
  */
uint8_t P4_IngressSmokeTest_Run(
    const volatile Safety_SnapshotTypeDef *safety,
    uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* __P4_INGRESS_SMOKE_TEST_H__ */
