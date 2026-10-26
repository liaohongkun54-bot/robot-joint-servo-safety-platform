/**
  ******************************************************************************
  * @file    test_task_wire_codec.c
  * @brief   Fixed binary transport envelope for a structured test task.
  ******************************************************************************
  */

#include "test_task_wire_codec.h"

static uint16_t TestTask_WireReadU16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t TestTask_WireReadU32(const uint8_t *data)
{
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8U) |
         ((uint32_t)data[2] << 16U) |
         ((uint32_t)data[3] << 24U);
}

static int16_t TestTask_WireReadI16(const uint8_t *data)
{
  uint16_t value = TestTask_WireReadU16(data);
  return (int16_t)value;
}

static void TestTask_WireWriteU16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)(value & 0xFFU);
  data[1] = (uint8_t)(value >> 8U);
}

static void TestTask_WireWriteU32(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)(value & 0xFFUL);
  data[1] = (uint8_t)((value >> 8U) & 0xFFUL);
  data[2] = (uint8_t)((value >> 16U) & 0xFFUL);
  data[3] = (uint8_t)((value >> 24U) & 0xFFUL);
}

static uint16_t TestTask_WireCrc16(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  uint16_t index;
  uint8_t bit;

  for (index = 0U; index < length; index++)
  {
    crc ^= (uint16_t)data[index] << 8U;
    for (bit = 0U; bit < 8U; bit++)
    {
      if ((crc & 0x8000U) != 0U)
      {
        crc = (uint16_t)((crc << 1U) ^ 0x1021U);
      }
      else
      {
        crc = (uint16_t)(crc << 1U);
      }
    }
  }
  return crc;
}

TestTask_WireErrorTypeDef TestTask_WireEncode(
    const TestTask_DescriptorTypeDef *task,
    uint8_t *frame,
    uint16_t frame_capacity,
    uint16_t *frame_length)
{
  if ((task == (const TestTask_DescriptorTypeDef *)0) ||
      (frame == (uint8_t *)0) ||
      (frame_length == (uint16_t *)0))
  {
    return TEST_TASK_WIRE_ERROR_NULL_ARGUMENT;
  }

  if (frame_capacity < TEST_TASK_WIRE_FRAME_LENGTH)
  {
    return TEST_TASK_WIRE_ERROR_CAPACITY;
  }

  if (TestTask_Validate(task) != TEST_TASK_VALID)
  {
    return TEST_TASK_WIRE_ERROR_TASK_SCHEMA;
  }

  TestTask_WireWriteU16(&frame[0], TEST_TASK_WIRE_MAGIC);
  frame[2] = (uint8_t)TEST_TASK_WIRE_VERSION;
  frame[3] = (uint8_t)TEST_TASK_WIRE_TYPE_TASK;
  TestTask_WireWriteU16(&frame[4], TEST_TASK_WIRE_PAYLOAD_LENGTH);
  TestTask_WireWriteU16(&frame[6], task->schema_version);
  TestTask_WireWriteU32(&frame[8], task->request_id);
  frame[12] = task->target_node_id;
  frame[13] = (uint8_t)task->action;
  TestTask_WireWriteU16(&frame[14], (uint16_t)task->amplitude_raw);
  TestTask_WireWriteU16(&frame[16], (uint16_t)task->amplitude_limit_raw);
  TestTask_WireWriteU32(&frame[18], task->duration_ms);
  TestTask_WireWriteU32(&frame[22], task->feedback_timeout_ms);
  frame[26] = (uint8_t)task->timeout_policy;
  frame[27] = (uint8_t)task->permission;
  TestTask_WireWriteU16(&frame[28], TestTask_WireCrc16(frame, 28U));
  *frame_length = TEST_TASK_WIRE_FRAME_LENGTH;
  return TEST_TASK_WIRE_OK;
}

TestTask_WireErrorTypeDef TestTask_WireDecode(
    const uint8_t *frame,
    uint16_t frame_length,
    TestTask_DescriptorTypeDef *task,
    TestTask_ValidationErrorTypeDef *schema_error)
{
  uint16_t expected_crc;
  uint16_t received_crc;
  TestTask_ValidationErrorTypeDef validation_error;

  if ((frame == (const uint8_t *)0) ||
      (task == (TestTask_DescriptorTypeDef *)0))
  {
    return TEST_TASK_WIRE_ERROR_NULL_ARGUMENT;
  }

  if (frame_length != TEST_TASK_WIRE_FRAME_LENGTH)
  {
    return TEST_TASK_WIRE_ERROR_LENGTH;
  }
  if (TestTask_WireReadU16(&frame[0]) != TEST_TASK_WIRE_MAGIC)
  {
    return TEST_TASK_WIRE_ERROR_MAGIC;
  }
  if (frame[2] != (uint8_t)TEST_TASK_WIRE_VERSION)
  {
    return TEST_TASK_WIRE_ERROR_VERSION;
  }
  if (frame[3] != (uint8_t)TEST_TASK_WIRE_TYPE_TASK)
  {
    return TEST_TASK_WIRE_ERROR_TYPE;
  }
  if (TestTask_WireReadU16(&frame[4]) != TEST_TASK_WIRE_PAYLOAD_LENGTH)
  {
    return TEST_TASK_WIRE_ERROR_PAYLOAD_LENGTH;
  }

  expected_crc = TestTask_WireCrc16(frame, 28U);
  received_crc = TestTask_WireReadU16(&frame[28]);
  if (expected_crc != received_crc)
  {
    return TEST_TASK_WIRE_ERROR_CRC;
  }

  task->schema_version = TestTask_WireReadU16(&frame[6]);
  task->request_id = TestTask_WireReadU32(&frame[8]);
  task->target_node_id = frame[12];
  task->action = (TestTask_ActionTypeDef)frame[13];
  task->amplitude_raw = TestTask_WireReadI16(&frame[14]);
  task->amplitude_limit_raw = TestTask_WireReadI16(&frame[16]);
  task->duration_ms = TestTask_WireReadU32(&frame[18]);
  task->feedback_timeout_ms = TestTask_WireReadU32(&frame[22]);
  task->timeout_policy = (TestTask_TimeoutPolicyTypeDef)frame[26];
  task->permission = (TestTask_PermissionTypeDef)frame[27];

  validation_error = TestTask_Validate(task);
  if (schema_error != (TestTask_ValidationErrorTypeDef *)0)
  {
    *schema_error = validation_error;
  }
  if (validation_error != TEST_TASK_VALID)
  {
    return TEST_TASK_WIRE_ERROR_TASK_SCHEMA;
  }
  return TEST_TASK_WIRE_OK;
}

const char *TestTask_WireErrorString(TestTask_WireErrorTypeDef error_code)
{
  switch (error_code)
  {
    case TEST_TASK_WIRE_OK:
      return "ok";
    case TEST_TASK_WIRE_ERROR_NULL_ARGUMENT:
      return "null_argument";
    case TEST_TASK_WIRE_ERROR_CAPACITY:
      return "capacity";
    case TEST_TASK_WIRE_ERROR_LENGTH:
      return "length";
    case TEST_TASK_WIRE_ERROR_MAGIC:
      return "magic";
    case TEST_TASK_WIRE_ERROR_VERSION:
      return "version";
    case TEST_TASK_WIRE_ERROR_TYPE:
      return "type";
    case TEST_TASK_WIRE_ERROR_PAYLOAD_LENGTH:
      return "payload_length";
    case TEST_TASK_WIRE_ERROR_CRC:
      return "crc";
    case TEST_TASK_WIRE_ERROR_TASK_SCHEMA:
      return "task_schema";
    default:
      return "unknown";
  }
}
