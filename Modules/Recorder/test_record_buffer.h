/**
  ******************************************************************************
  * @file    test_record_buffer.h
  * @brief   Fixed-capacity append-only in-memory test record buffer.
  *
  * Records are snapshots for later transport or persistence.  This module does
  * not overwrite old records, allocate memory, format CSV or transmit CAN.
  ******************************************************************************
  */

#ifndef __TEST_RECORD_BUFFER_H__
#define __TEST_RECORD_BUFFER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define TEST_RECORD_BUFFER_CAPACITY (64U)

typedef enum
{
  TEST_RECORD_EVENT_NONE = 0U,
  TEST_RECORD_EVENT_TASK_ACCEPTED = 1U,
  TEST_RECORD_EVENT_TASK_REJECTED = 2U,
  TEST_RECORD_EVENT_FEEDBACK = 3U,
  TEST_RECORD_EVENT_STATE_CHANGE = 4U,
  TEST_RECORD_EVENT_FAULT = 5U,
  TEST_RECORD_EVENT_ESTOP = 6U,
  TEST_RECORD_EVENT_MOTION_STOP = 7U
} TestRecord_EventTypeDef;

typedef enum
{
  TEST_RECORD_APPEND_OK = 0U,
  TEST_RECORD_APPEND_ERROR_NULL_ARGUMENT = 1U,
  TEST_RECORD_APPEND_ERROR_FULL = 2U
} TestRecord_AppendResultTypeDef;

/** One timestamped, unit-labelled snapshot suitable for later CSV export. */
typedef struct
{
  uint32_t timestamp_ms;
  uint32_t request_id;
  TestRecord_EventTypeDef event;
  uint8_t target_node_id;
  uint8_t safety_state;
  uint8_t link_state;
  uint8_t reserved;
  int16_t command_amplitude_raw;
  int16_t feedback_speed_rpm;
  int16_t feedback_current_raw;
  uint16_t feedback_encoder_raw;
  uint8_t feedback_temperature_c;
  uint8_t error_code;
  uint8_t stop_reason;
  uint32_t safety_fault_code;
  uint32_t observation_abs_travel_raw;
  uint16_t observation_peak_abs_speed_rpm;
  uint32_t observation_sample_count;
} TestRecord_RecordTypeDef;

typedef struct
{
  TestRecord_RecordTypeDef records[TEST_RECORD_BUFFER_CAPACITY];
  uint16_t reserved_count;
  uint16_t count;
  uint32_t dropped_count;
} TestRecord_BufferTypeDef;

void TestRecord_BufferInit(TestRecord_BufferTypeDef *buffer);

TestRecord_AppendResultTypeDef TestRecord_Append(
    TestRecord_BufferTypeDef *buffer,
    const TestRecord_RecordTypeDef *record);

uint8_t TestRecord_Reserve(TestRecord_BufferTypeDef *buffer, uint16_t slots);

TestRecord_AppendResultTypeDef TestRecord_AppendReserved(
    TestRecord_BufferTypeDef *buffer,
    const TestRecord_RecordTypeDef *record);

void TestRecord_Release(TestRecord_BufferTypeDef *buffer, uint16_t slots);

uint16_t TestRecord_Count(const TestRecord_BufferTypeDef *buffer);

const TestRecord_RecordTypeDef *TestRecord_Get(
    const TestRecord_BufferTypeDef *buffer,
    uint16_t index);

#ifdef __cplusplus
}
#endif

#endif /* __TEST_RECORD_BUFFER_H__ */
