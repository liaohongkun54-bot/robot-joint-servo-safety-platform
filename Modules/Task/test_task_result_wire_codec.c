/**
  ******************************************************************************
  * @file    test_task_result_wire_codec.c
  * @brief   Fixed binary result frame returned for structured task handling.
  ******************************************************************************
  */

#include "test_task_result_wire_codec.h"

static uint16_t TestTask_ResultReadU16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t TestTask_ResultReadU32(const uint8_t *data)
{
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8U) |
         ((uint32_t)data[2] << 16U) |
         ((uint32_t)data[3] << 24U);
}

static void TestTask_ResultWriteU16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)(value & 0xFFU);
  data[1] = (uint8_t)(value >> 8U);
}

static void TestTask_ResultWriteU32(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)(value & 0xFFUL);
  data[1] = (uint8_t)((value >> 8U) & 0xFFUL);
  data[2] = (uint8_t)((value >> 16U) & 0xFFUL);
  data[3] = (uint8_t)((value >> 24U) & 0xFFUL);
}

static uint16_t TestTask_ResultCrc16(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  uint16_t index;
  uint8_t bit;

  for (index = 0U; index < length; index++)
  {
    crc ^= (uint16_t)data[index] << 8U;
    for (bit = 0U; bit < 8U; bit++)
    {
      crc = ((crc & 0x8000U) != 0U) ?
            (uint16_t)((crc << 1U) ^ 0x1021U) :
            (uint16_t)(crc << 1U);
    }
  }
  return crc;
}

TestTask_ResultWireErrorTypeDef TestTask_ResultWireValidate(
    const TestTask_ResultDescriptorTypeDef *result)
{
  if (result == (const TestTask_ResultDescriptorTypeDef *)0)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_NULL_ARGUMENT;
  }
  if ((result->request_id == 0UL) &&
      (result->stage != TEST_TASK_RESULT_STAGE_TRANSPORT))
  {
    return TEST_TASK_RESULT_WIRE_ERROR_REQUEST_ID;
  }
  if ((result->status < TEST_TASK_RESULT_STATUS_RECEIVED) ||
      (result->status > TEST_TASK_RESULT_STATUS_INTERNAL_ERROR))
  {
    return TEST_TASK_RESULT_WIRE_ERROR_STATUS;
  }
  if ((result->stage < TEST_TASK_RESULT_STAGE_TRANSPORT) ||
      (result->stage > TEST_TASK_RESULT_STAGE_COMPLETE))
  {
    return TEST_TASK_RESULT_WIRE_ERROR_STAGE;
  }
  if ((result->flags & (uint8_t)(~TEST_TASK_RESULT_FLAG_ALLOWED_MASK)) != 0U)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_FLAGS;
  }
  if ((result->status == TEST_TASK_RESULT_STATUS_ACCEPTED) &&
      ((result->stage != TEST_TASK_RESULT_STAGE_COMPLETE) ||
       (result->error_code != 0U)))
  {
    return TEST_TASK_RESULT_WIRE_ERROR_SEMANTICS;
  }
  if ((result->status == TEST_TASK_RESULT_STATUS_REJECTED) &&
      (result->error_code == 0U))
  {
    return TEST_TASK_RESULT_WIRE_ERROR_SEMANTICS;
  }
  return TEST_TASK_RESULT_WIRE_OK;
}

TestTask_ResultWireErrorTypeDef TestTask_ResultWireEncode(
    const TestTask_ResultDescriptorTypeDef *result,
    uint8_t *frame,
    uint16_t frame_capacity,
    uint16_t *frame_length)
{
  TestTask_ResultWireErrorTypeDef validation_error;

  if ((result == (const TestTask_ResultDescriptorTypeDef *)0) ||
      (frame == (uint8_t *)0) ||
      (frame_length == (uint16_t *)0))
  {
    return TEST_TASK_RESULT_WIRE_ERROR_NULL_ARGUMENT;
  }
  if (frame_capacity < TEST_TASK_RESULT_FRAME_LENGTH)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_CAPACITY;
  }
  validation_error = TestTask_ResultWireValidate(result);
  if (validation_error != TEST_TASK_RESULT_WIRE_OK)
  {
    return validation_error;
  }

  TestTask_ResultWriteU16(&frame[0], TEST_TASK_RESULT_WIRE_MAGIC);
  frame[2] = (uint8_t)TEST_TASK_RESULT_WIRE_VERSION;
  frame[3] = (uint8_t)TEST_TASK_RESULT_WIRE_TYPE;
  TestTask_ResultWriteU16(&frame[4], TEST_TASK_RESULT_PAYLOAD_LENGTH);
  TestTask_ResultWriteU32(&frame[6], result->request_id);
  TestTask_ResultWriteU32(&frame[10], result->timestamp_ms);
  TestTask_ResultWriteU16(&frame[14], result->error_code);
  TestTask_ResultWriteU16(&frame[16], result->record_count);
  frame[18] = (uint8_t)result->status;
  frame[19] = (uint8_t)result->stage;
  frame[20] = result->safety_state;
  frame[21] = result->flags;
  TestTask_ResultWriteU16(&frame[22], TestTask_ResultCrc16(frame, 22U));
  *frame_length = TEST_TASK_RESULT_FRAME_LENGTH;
  return TEST_TASK_RESULT_WIRE_OK;
}

TestTask_ResultWireErrorTypeDef TestTask_ResultWireDecode(
    const uint8_t *frame,
    uint16_t frame_length,
    TestTask_ResultDescriptorTypeDef *result)
{
  if ((frame == (const uint8_t *)0) ||
      (result == (TestTask_ResultDescriptorTypeDef *)0))
  {
    return TEST_TASK_RESULT_WIRE_ERROR_NULL_ARGUMENT;
  }
  if (frame_length != TEST_TASK_RESULT_FRAME_LENGTH)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_LENGTH;
  }
  if (TestTask_ResultReadU16(&frame[0]) != TEST_TASK_RESULT_WIRE_MAGIC)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_MAGIC;
  }
  if (frame[2] != (uint8_t)TEST_TASK_RESULT_WIRE_VERSION)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_VERSION;
  }
  if (frame[3] != (uint8_t)TEST_TASK_RESULT_WIRE_TYPE)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_TYPE;
  }
  if (TestTask_ResultReadU16(&frame[4]) != TEST_TASK_RESULT_PAYLOAD_LENGTH)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_PAYLOAD_LENGTH;
  }
  if (TestTask_ResultReadU16(&frame[22]) !=
      TestTask_ResultCrc16(frame, 22U))
  {
    return TEST_TASK_RESULT_WIRE_ERROR_CRC;
  }

  result->request_id = TestTask_ResultReadU32(&frame[6]);
  result->timestamp_ms = TestTask_ResultReadU32(&frame[10]);
  result->error_code = TestTask_ResultReadU16(&frame[14]);
  result->record_count = TestTask_ResultReadU16(&frame[16]);
  result->status = (TestTask_ResultStatusTypeDef)frame[18];
  result->stage = (TestTask_ResultStageTypeDef)frame[19];
  result->safety_state = frame[20];
  result->flags = frame[21];
  return TestTask_ResultWireValidate(result);
}

TestTask_ResultWireErrorTypeDef TestTask_StopWireEncode(
    const TestTask_StopDescriptorTypeDef *stop,
    uint8_t *frame,
    uint16_t frame_capacity,
    uint16_t *frame_length)
{
  if ((stop == (const TestTask_StopDescriptorTypeDef *)0) ||
      (frame == (uint8_t *)0) || (frame_length == (uint16_t *)0))
  {
    return TEST_TASK_RESULT_WIRE_ERROR_NULL_ARGUMENT;
  }
  if (frame_capacity < TEST_TASK_STOP_FRAME_LENGTH)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_CAPACITY;
  }
  if (stop->request_id == 0UL)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_REQUEST_ID;
  }
  if ((stop->stop_reason < 1U) || (stop->stop_reason > 5U))
  {
    return TEST_TASK_RESULT_WIRE_ERROR_SEMANTICS;
  }

  TestTask_ResultWriteU16(&frame[0], TEST_TASK_RESULT_WIRE_MAGIC);
  frame[2] = (uint8_t)TEST_TASK_RESULT_WIRE_VERSION;
  frame[3] = (uint8_t)TEST_TASK_STOP_WIRE_TYPE;
  TestTask_ResultWriteU16(&frame[4], TEST_TASK_STOP_PAYLOAD_LENGTH);
  TestTask_ResultWriteU32(&frame[6], stop->request_id);
  TestTask_ResultWriteU32(&frame[10], stop->timestamp_ms);
  TestTask_ResultWriteU16(&frame[14], (uint16_t)stop->command_amplitude_raw);
  TestTask_ResultWriteU16(&frame[16], (uint16_t)stop->feedback_speed_rpm);
  TestTask_ResultWriteU16(&frame[18], (uint16_t)stop->feedback_current_raw);
  TestTask_ResultWriteU16(&frame[20], stop->feedback_encoder_raw);
  frame[22] = stop->feedback_temperature_c;
  frame[23] = stop->stop_reason;
  frame[24] = stop->safety_state;
  frame[25] = stop->link_state;
  TestTask_ResultWriteU32(&frame[26], stop->safety_fault_code);
  TestTask_ResultWriteU32(&frame[30], stop->observation_abs_travel_raw);
  TestTask_ResultWriteU16(&frame[34], stop->observation_peak_abs_speed_rpm);
  TestTask_ResultWriteU32(&frame[36], stop->observation_sample_count);
  TestTask_ResultWriteU16(&frame[40], TestTask_ResultCrc16(frame, 40U));
  *frame_length = TEST_TASK_STOP_FRAME_LENGTH;
  return TEST_TASK_RESULT_WIRE_OK;
}

TestTask_ResultWireErrorTypeDef TestTask_StopWireDecode(
    const uint8_t *frame,
    uint16_t frame_length,
    TestTask_StopDescriptorTypeDef *stop)
{
  if ((frame == (const uint8_t *)0) ||
      (stop == (TestTask_StopDescriptorTypeDef *)0))
  {
    return TEST_TASK_RESULT_WIRE_ERROR_NULL_ARGUMENT;
  }
  if (frame_length != TEST_TASK_STOP_FRAME_LENGTH)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_LENGTH;
  }
  if (TestTask_ResultReadU16(&frame[0]) != TEST_TASK_RESULT_WIRE_MAGIC)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_MAGIC;
  }
  if (frame[2] != (uint8_t)TEST_TASK_RESULT_WIRE_VERSION)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_VERSION;
  }
  if (frame[3] != (uint8_t)TEST_TASK_STOP_WIRE_TYPE)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_TYPE;
  }
  if (TestTask_ResultReadU16(&frame[4]) != TEST_TASK_STOP_PAYLOAD_LENGTH)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_PAYLOAD_LENGTH;
  }
  if (TestTask_ResultReadU16(&frame[40]) != TestTask_ResultCrc16(frame, 40U))
  {
    return TEST_TASK_RESULT_WIRE_ERROR_CRC;
  }

  stop->request_id = TestTask_ResultReadU32(&frame[6]);
  stop->timestamp_ms = TestTask_ResultReadU32(&frame[10]);
  stop->command_amplitude_raw = (int16_t)TestTask_ResultReadU16(&frame[14]);
  stop->feedback_speed_rpm = (int16_t)TestTask_ResultReadU16(&frame[16]);
  stop->feedback_current_raw = (int16_t)TestTask_ResultReadU16(&frame[18]);
  stop->feedback_encoder_raw = TestTask_ResultReadU16(&frame[20]);
  stop->feedback_temperature_c = frame[22];
  stop->stop_reason = frame[23];
  stop->safety_state = frame[24];
  stop->link_state = frame[25];
  stop->safety_fault_code = TestTask_ResultReadU32(&frame[26]);
  stop->observation_abs_travel_raw = TestTask_ResultReadU32(&frame[30]);
  stop->observation_peak_abs_speed_rpm = TestTask_ResultReadU16(&frame[34]);
  stop->observation_sample_count = TestTask_ResultReadU32(&frame[36]);
  if (stop->request_id == 0UL)
  {
    return TEST_TASK_RESULT_WIRE_ERROR_REQUEST_ID;
  }
  if ((stop->stop_reason < 1U) || (stop->stop_reason > 5U))
  {
    return TEST_TASK_RESULT_WIRE_ERROR_SEMANTICS;
  }
  return TEST_TASK_RESULT_WIRE_OK;
}

const char *TestTask_ResultWireErrorString(
    TestTask_ResultWireErrorTypeDef error_code)
{
  switch (error_code)
  {
    case TEST_TASK_RESULT_WIRE_OK: return "ok";
    case TEST_TASK_RESULT_WIRE_ERROR_NULL_ARGUMENT: return "null_argument";
    case TEST_TASK_RESULT_WIRE_ERROR_CAPACITY: return "capacity";
    case TEST_TASK_RESULT_WIRE_ERROR_LENGTH: return "length";
    case TEST_TASK_RESULT_WIRE_ERROR_MAGIC: return "magic";
    case TEST_TASK_RESULT_WIRE_ERROR_VERSION: return "version";
    case TEST_TASK_RESULT_WIRE_ERROR_TYPE: return "type";
    case TEST_TASK_RESULT_WIRE_ERROR_PAYLOAD_LENGTH: return "payload_length";
    case TEST_TASK_RESULT_WIRE_ERROR_CRC: return "crc";
    case TEST_TASK_RESULT_WIRE_ERROR_REQUEST_ID: return "request_id";
    case TEST_TASK_RESULT_WIRE_ERROR_STATUS: return "status";
    case TEST_TASK_RESULT_WIRE_ERROR_STAGE: return "stage";
    case TEST_TASK_RESULT_WIRE_ERROR_FLAGS: return "flags";
    case TEST_TASK_RESULT_WIRE_ERROR_SEMANTICS: return "semantics";
    default: return "unknown";
  }
}
