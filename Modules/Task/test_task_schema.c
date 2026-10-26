/**
  ******************************************************************************
  * @file    test_task_schema.c
  * @brief   Constrained test-task validation; no CAN output implementation.
  ******************************************************************************
  */

#include "test_task_schema.h"

TestTask_ValidationErrorTypeDef TestTask_Validate(
    const TestTask_DescriptorTypeDef *task)
{
  int32_t amplitude;
  int32_t amplitude_limit;

  if (task == (const TestTask_DescriptorTypeDef *)0)
  {
    return TEST_TASK_ERROR_NULL_ARGUMENT;
  }

  if (task->schema_version != TEST_TASK_SCHEMA_VERSION)
  {
    return TEST_TASK_ERROR_SCHEMA_VERSION;
  }

  if (task->request_id == 0UL)
  {
    return TEST_TASK_ERROR_REQUEST_ID;
  }

  if (task->target_node_id != TEST_TASK_TARGET_C620_ID)
  {
    return TEST_TASK_ERROR_TARGET_NODE;
  }

  if (task->action != TEST_TASK_ACTION_CURRENT_TEST)
  {
    return TEST_TASK_ERROR_ACTION;
  }

  amplitude = (int32_t)task->amplitude_raw;
  amplitude_limit = (int32_t)task->amplitude_limit_raw;
  if ((amplitude_limit <= 0L) ||
      (amplitude_limit > TEST_TASK_MAX_AMPLITUDE_RAW))
  {
    return TEST_TASK_ERROR_AMPLITUDE_LIMIT;
  }

  if ((amplitude > TEST_TASK_MAX_AMPLITUDE_RAW) ||
      (amplitude < -TEST_TASK_MAX_AMPLITUDE_RAW))
  {
    return TEST_TASK_ERROR_AMPLITUDE_RANGE;
  }

  if ((amplitude > amplitude_limit) ||
      (amplitude < -amplitude_limit))
  {
    return TEST_TASK_ERROR_AMPLITUDE_LIMIT;
  }

  if ((task->duration_ms == 0UL) ||
      (task->duration_ms > TEST_TASK_MAX_DURATION_MS))
  {
    return TEST_TASK_ERROR_DURATION;
  }

  if ((task->feedback_timeout_ms < TEST_TASK_MIN_FEEDBACK_TIMEOUT_MS) ||
      (task->feedback_timeout_ms > TEST_TASK_MAX_FEEDBACK_TIMEOUT_MS))
  {
    return TEST_TASK_ERROR_FEEDBACK_TIMEOUT;
  }

  if (task->timeout_policy != TEST_TASK_TIMEOUT_STOP)
  {
    return TEST_TASK_ERROR_TIMEOUT_POLICY;
  }

  if (task->permission != TEST_TASK_PERMISSION_OPERATOR)
  {
    return TEST_TASK_ERROR_PERMISSION;
  }

  return TEST_TASK_VALID;
}

const char *TestTask_ValidationErrorString(
    TestTask_ValidationErrorTypeDef error_code)
{
  switch (error_code)
  {
    case TEST_TASK_VALID:
      return "valid";
    case TEST_TASK_ERROR_NULL_ARGUMENT:
      return "null_argument";
    case TEST_TASK_ERROR_SCHEMA_VERSION:
      return "schema_version";
    case TEST_TASK_ERROR_REQUEST_ID:
      return "request_id";
    case TEST_TASK_ERROR_TARGET_NODE:
      return "target_node";
    case TEST_TASK_ERROR_ACTION:
      return "action";
    case TEST_TASK_ERROR_AMPLITUDE_RANGE:
      return "amplitude_range";
    case TEST_TASK_ERROR_AMPLITUDE_LIMIT:
      return "amplitude_limit";
    case TEST_TASK_ERROR_DURATION:
      return "duration";
    case TEST_TASK_ERROR_FEEDBACK_TIMEOUT:
      return "feedback_timeout";
    case TEST_TASK_ERROR_TIMEOUT_POLICY:
      return "timeout_policy";
    case TEST_TASK_ERROR_PERMISSION:
      return "permission";
    default:
      return "unknown";
  }
}
