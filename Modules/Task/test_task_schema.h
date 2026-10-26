/**
  ******************************************************************************
  * @file    test_task_schema.h
  * @brief   Versioned, constrained test-task descriptor and validator.
  *
  * This module describes intent only.  It contains no CAN identifier, payload
  * bytes, register address or transmit function.  A later execution layer must
  * perform a second validation against the live Safety snapshot before output.
  ******************************************************************************
  */

#ifndef __TEST_TASK_SCHEMA_H__
#define __TEST_TASK_SCHEMA_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Schema and current single-node scope. */
#define TEST_TASK_SCHEMA_VERSION             (1U)
#define TEST_TASK_TARGET_C620_ID              (1U)

/* Software anti-abuse limits; these are not a motor acceptance result. */
#define TEST_TASK_MAX_AMPLITUDE_RAW          (16384L)
#define TEST_TASK_MAX_DURATION_MS            (1000UL)
#define TEST_TASK_MIN_FEEDBACK_TIMEOUT_MS    (100UL)
#define TEST_TASK_MAX_FEEDBACK_TIMEOUT_MS    (1000UL)

typedef enum
{
  TEST_TASK_ACTION_NONE = 0U,
  TEST_TASK_ACTION_CURRENT_TEST = 1U
} TestTask_ActionTypeDef;

typedef enum
{
  TEST_TASK_TIMEOUT_REJECT = 0U,
  TEST_TASK_TIMEOUT_STOP = 1U
} TestTask_TimeoutPolicyTypeDef;

typedef enum
{
  TEST_TASK_PERMISSION_NONE = 0U,
  TEST_TASK_PERMISSION_OPERATOR = 1U
} TestTask_PermissionTypeDef;

typedef enum
{
  TEST_TASK_VALID = 0U,
  TEST_TASK_ERROR_NULL_ARGUMENT = 1U,
  TEST_TASK_ERROR_SCHEMA_VERSION = 2U,
  TEST_TASK_ERROR_REQUEST_ID = 3U,
  TEST_TASK_ERROR_TARGET_NODE = 4U,
  TEST_TASK_ERROR_ACTION = 5U,
  TEST_TASK_ERROR_AMPLITUDE_RANGE = 6U,
  TEST_TASK_ERROR_AMPLITUDE_LIMIT = 7U,
  TEST_TASK_ERROR_DURATION = 8U,
  TEST_TASK_ERROR_FEEDBACK_TIMEOUT = 9U,
  TEST_TASK_ERROR_TIMEOUT_POLICY = 10U,
  TEST_TASK_ERROR_PERMISSION = 11U
} TestTask_ValidationErrorTypeDef;

/**
  * @brief PC/AI intent after schema conversion.
  *
  * current_raw is a protocol raw quantity, not an arbitrary CAN byte field.
  * The current codebase must keep this descriptor separate from CAN encoding.
  */
typedef struct
{
  uint16_t schema_version;
  uint32_t request_id;
  uint8_t target_node_id;
  TestTask_ActionTypeDef action;
  int16_t amplitude_raw;
  int16_t amplitude_limit_raw;
  uint32_t duration_ms;
  uint32_t feedback_timeout_ms;
  TestTask_TimeoutPolicyTypeDef timeout_policy;
  TestTask_PermissionTypeDef permission;
} TestTask_DescriptorTypeDef;

/** Validate every field and return a specific rejection reason. */
TestTask_ValidationErrorTypeDef TestTask_Validate(
    const TestTask_DescriptorTypeDef *task);

/** Convert a validation error to a stable diagnostic string. */
const char *TestTask_ValidationErrorString(
    TestTask_ValidationErrorTypeDef error_code);

#ifdef __cplusplus
}
#endif

#endif /* __TEST_TASK_SCHEMA_H__ */
