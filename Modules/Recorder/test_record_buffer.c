/**
  ******************************************************************************
  * @file    test_record_buffer.c
  * @brief   Fixed-capacity append-only test record buffer.
  ******************************************************************************
  */

#include "test_record_buffer.h"

void TestRecord_BufferInit(TestRecord_BufferTypeDef *buffer)
{
  uint16_t index;

  if (buffer == (TestRecord_BufferTypeDef *)0)
  {
    return;
  }

  buffer->count = 0U;
  buffer->reserved_count = 0U;
  buffer->dropped_count = 0UL;
  for (index = 0U; index < TEST_RECORD_BUFFER_CAPACITY; index++)
  {
    buffer->records[index].timestamp_ms = 0UL;
    buffer->records[index].request_id = 0UL;
    buffer->records[index].event = TEST_RECORD_EVENT_NONE;
    buffer->records[index].target_node_id = 0U;
    buffer->records[index].safety_state = 0U;
    buffer->records[index].link_state = 0U;
    buffer->records[index].reserved = 0U;
    buffer->records[index].command_amplitude_raw = 0;
    buffer->records[index].feedback_speed_rpm = 0;
    buffer->records[index].feedback_current_raw = 0;
    buffer->records[index].feedback_encoder_raw = 0U;
    buffer->records[index].feedback_temperature_c = 0U;
buffer->records[index].error_code = 0U;
buffer->records[index].stop_reason = 0U;
buffer->records[index].safety_fault_code = 0UL;
buffer->records[index].observation_abs_travel_raw = 0UL;
buffer->records[index].observation_peak_abs_speed_rpm = 0U;
buffer->records[index].observation_sample_count = 0UL;
  }
}

TestRecord_AppendResultTypeDef TestRecord_Append(
    TestRecord_BufferTypeDef *buffer,
    const TestRecord_RecordTypeDef *record)
{
  if ((buffer == (TestRecord_BufferTypeDef *)0) ||
      (record == (const TestRecord_RecordTypeDef *)0))
  {
    return TEST_RECORD_APPEND_ERROR_NULL_ARGUMENT;
  }

  if (((uint32_t)buffer->count + buffer->reserved_count) >=
    TEST_RECORD_BUFFER_CAPACITY)
  {
    buffer->dropped_count++;
    return TEST_RECORD_APPEND_ERROR_FULL;
  }

  buffer->records[buffer->count] = *record;
  buffer->count++;
  return TEST_RECORD_APPEND_OK;
}

uint8_t TestRecord_Reserve(TestRecord_BufferTypeDef *buffer, uint16_t slots)
{
  if ((buffer == (TestRecord_BufferTypeDef *)0) || (slots == 0U))
  {
    return 0U;
  }

  if (((uint32_t)buffer->count + buffer->reserved_count + slots) >
      TEST_RECORD_BUFFER_CAPACITY)
  {
    return 0U;
  }

  buffer->reserved_count = (uint16_t)(buffer->reserved_count + slots);
  return 1U;
}

TestRecord_AppendResultTypeDef TestRecord_AppendReserved(
    TestRecord_BufferTypeDef *buffer,
    const TestRecord_RecordTypeDef *record)
{
  if ((buffer == (TestRecord_BufferTypeDef *)0) ||
      (record == (const TestRecord_RecordTypeDef *)0))
  {
    return TEST_RECORD_APPEND_ERROR_NULL_ARGUMENT;
  }

  if ((buffer->reserved_count == 0U) ||
      (buffer->count >= TEST_RECORD_BUFFER_CAPACITY))
  {
    return TEST_RECORD_APPEND_ERROR_FULL;
  }

  buffer->records[buffer->count] = *record;
  buffer->count++;
  buffer->reserved_count--;
  return TEST_RECORD_APPEND_OK;
}

void TestRecord_Release(TestRecord_BufferTypeDef *buffer, uint16_t slots)
{
  if ((buffer != (TestRecord_BufferTypeDef *)0) &&
      (slots <= buffer->reserved_count))
  {
    buffer->reserved_count =
        (uint16_t)(buffer->reserved_count - slots);
  }
}

uint16_t TestRecord_Count(const TestRecord_BufferTypeDef *buffer)
{
  if (buffer == (const TestRecord_BufferTypeDef *)0)
  {
    return 0U;
  }

  return buffer->count;
}

const TestRecord_RecordTypeDef *TestRecord_Get(
    const TestRecord_BufferTypeDef *buffer,
    uint16_t index)
{
  if ((buffer == (const TestRecord_BufferTypeDef *)0) ||
      (index >= buffer->count))
  {
    return (const TestRecord_RecordTypeDef *)0;
  }

  return &buffer->records[index];
}
