/**
  ******************************************************************************
  * @file    test_task_runtime.h
  * @brief   Runtime task admission: schema -> safety -> motion gate -> record.
  *
 * This adapter preserves the schema -> Safety -> Motion Gate boundary.  The
 * Motion Gate owns the approved one-shot output and automatic zero stop.
  ******************************************************************************
  */

#ifndef __TEST_TASK_RUNTIME_H__
#define __TEST_TASK_RUNTIME_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "c620_motion_gate.h"
#include "test_record_buffer.h"
#include "test_task_schema.h"

typedef enum
{
  TEST_TASK_RUNTIME_ACCEPTED = 0U,
  TEST_TASK_RUNTIME_ERROR_NULL_ARGUMENT = 1U,
  TEST_TASK_RUNTIME_ERROR_SCHEMA = 2U,
  TEST_TASK_RUNTIME_ERROR_SAFETY = 3U,
  TEST_TASK_RUNTIME_ERROR_MOTION_GATE = 4U,
  TEST_TASK_RUNTIME_ERROR_RECORD_FULL = 5U
} TestTask_RuntimeResultTypeDef;

/** Stable record error values; distinct from CAN IDs and C620 error bytes. */
#define TEST_TASK_RUNTIME_RECORD_SCHEMA_ERROR (0xE1U)
#define TEST_TASK_RUNTIME_RECORD_SAFETY_ERROR (0xE2U)
#define TEST_TASK_RUNTIME_RECORD_MOTION_ERROR (0xE3U)
#define TEST_TASK_RUNTIME_RECORD_BUFFER_ERROR (0xE4U)

typedef struct
{
  TestTask_RuntimeResultTypeDef last_result;
  TestTask_ValidationErrorTypeDef last_schema_error;
  C620_MotionGateErrorTypeDef last_motion_error;
  uint32_t accepted_count;
  uint32_t rejected_count;
  uint32_t record_error_count;
  uint32_t last_request_id;
} TestTask_RuntimeSnapshotTypeDef;

extern volatile TestTask_RuntimeSnapshotTypeDef g_test_task_runtime_snapshot;

void TestTask_RuntimeInit(void);
typedef uint8_t (*TestTask_AuthorizeFn)(const TestTask_DescriptorTypeDef *task, uint32_t now_ms);
typedef void (*TestTask_AdmissionDoneFn)(uint8_t accepted, uint32_t now_ms);
void TestTask_RuntimeBindAuthorization(TestTask_AuthorizeFn authorize,
    TestTask_AdmissionDoneFn done);

/**
  * @brief Admit one task and append an audit record.
  *
  * ACCEPTED means the constrained one-shot request passed every gate and its
  * first fixed control frame was accepted by the bound transport.
  */
TestTask_RuntimeResultTypeDef TestTask_RuntimeSubmit(
    const TestTask_DescriptorTypeDef *task,
    const Safety_SnapshotTypeDef *safety,
    TestRecord_BufferTypeDef *records,
    uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* __TEST_TASK_RUNTIME_H__ */
