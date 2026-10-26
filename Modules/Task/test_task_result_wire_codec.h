/**
  ******************************************************************************
  * @file    test_task_result_wire_codec.h
  * @brief   Fixed binary result frame returned for structured task handling.
  *
  * This module only encodes/decodes result data.  It owns no UART peripheral,
  * performs no transmission and contains no CAN operation.
  ******************************************************************************
  */

#ifndef __TEST_TASK_RESULT_WIRE_CODEC_H__
#define __TEST_TASK_RESULT_WIRE_CODEC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define TEST_TASK_RESULT_WIRE_MAGIC          (0x5254U)
#define TEST_TASK_RESULT_WIRE_VERSION        (1U)
#define TEST_TASK_RESULT_WIRE_TYPE           (2U)
#define TEST_TASK_RESULT_PAYLOAD_LENGTH      (16U)
#define TEST_TASK_RESULT_FRAME_LENGTH        (24U)
#define TEST_TASK_STOP_WIRE_TYPE             (3U)
#define TEST_TASK_STOP_PAYLOAD_LENGTH        (34U)
#define TEST_TASK_STOP_FRAME_LENGTH          (42U)

#define TEST_TASK_RESULT_FLAG_RECORD_APPENDED   (0x01U)
#define TEST_TASK_RESULT_FLAG_OUTPUT_UNCHANGED  (0x02U)
#define TEST_TASK_RESULT_FLAG_ALLOWED_MASK      (0x03U)

typedef enum
{
  TEST_TASK_RESULT_STATUS_RECEIVED = 1U,
  TEST_TASK_RESULT_STATUS_ACCEPTED = 2U,
  TEST_TASK_RESULT_STATUS_REJECTED = 3U,
  TEST_TASK_RESULT_STATUS_INTERNAL_ERROR = 4U
} TestTask_ResultStatusTypeDef;

typedef enum
{
  TEST_TASK_RESULT_STAGE_TRANSPORT = 1U,
  TEST_TASK_RESULT_STAGE_SCHEMA = 2U,
  TEST_TASK_RESULT_STAGE_SAFETY = 3U,
  TEST_TASK_RESULT_STAGE_MOTION = 4U,
  TEST_TASK_RESULT_STAGE_RECORD = 5U,
  TEST_TASK_RESULT_STAGE_COMPLETE = 6U
} TestTask_ResultStageTypeDef;

typedef struct
{
  uint32_t request_id;
  uint32_t timestamp_ms;
  uint16_t error_code;
  uint16_t record_count;
  TestTask_ResultStatusTypeDef status;
  TestTask_ResultStageTypeDef stage;
  uint8_t safety_state;
  uint8_t flags;
} TestTask_ResultDescriptorTypeDef;

/* Snapshot captured after the Motion Gate successfully sends its zero frame. */
typedef struct
{
  uint32_t request_id;
  uint32_t timestamp_ms;
  int16_t command_amplitude_raw;
  int16_t feedback_speed_rpm;
  int16_t feedback_current_raw;
  uint16_t feedback_encoder_raw;
  uint8_t feedback_temperature_c;
  uint8_t stop_reason;
  uint8_t safety_state;
  uint8_t link_state;
  uint32_t safety_fault_code;
  uint32_t observation_abs_travel_raw;
  uint16_t observation_peak_abs_speed_rpm;
  uint32_t observation_sample_count;
} TestTask_StopDescriptorTypeDef;

typedef enum
{
  TEST_TASK_RESULT_WIRE_OK = 0U,
  TEST_TASK_RESULT_WIRE_ERROR_NULL_ARGUMENT = 1U,
  TEST_TASK_RESULT_WIRE_ERROR_CAPACITY = 2U,
  TEST_TASK_RESULT_WIRE_ERROR_LENGTH = 3U,
  TEST_TASK_RESULT_WIRE_ERROR_MAGIC = 4U,
  TEST_TASK_RESULT_WIRE_ERROR_VERSION = 5U,
  TEST_TASK_RESULT_WIRE_ERROR_TYPE = 6U,
  TEST_TASK_RESULT_WIRE_ERROR_PAYLOAD_LENGTH = 7U,
  TEST_TASK_RESULT_WIRE_ERROR_CRC = 8U,
  TEST_TASK_RESULT_WIRE_ERROR_REQUEST_ID = 9U,
  TEST_TASK_RESULT_WIRE_ERROR_STATUS = 10U,
  TEST_TASK_RESULT_WIRE_ERROR_STAGE = 11U,
  TEST_TASK_RESULT_WIRE_ERROR_FLAGS = 12U,
  TEST_TASK_RESULT_WIRE_ERROR_SEMANTICS = 13U
} TestTask_ResultWireErrorTypeDef;

TestTask_ResultWireErrorTypeDef TestTask_ResultWireValidate(
    const TestTask_ResultDescriptorTypeDef *result);

TestTask_ResultWireErrorTypeDef TestTask_ResultWireEncode(
    const TestTask_ResultDescriptorTypeDef *result,
    uint8_t *frame,
    uint16_t frame_capacity,
    uint16_t *frame_length);

TestTask_ResultWireErrorTypeDef TestTask_ResultWireDecode(
    const uint8_t *frame,
    uint16_t frame_length,
    TestTask_ResultDescriptorTypeDef *result);

TestTask_ResultWireErrorTypeDef TestTask_StopWireEncode(
    const TestTask_StopDescriptorTypeDef *stop,
    uint8_t *frame,
    uint16_t frame_capacity,
    uint16_t *frame_length);

TestTask_ResultWireErrorTypeDef TestTask_StopWireDecode(
    const uint8_t *frame,
    uint16_t frame_length,
    TestTask_StopDescriptorTypeDef *stop);

const char *TestTask_ResultWireErrorString(
    TestTask_ResultWireErrorTypeDef error_code);

#ifdef __cplusplus
}
#endif

#endif /* __TEST_TASK_RESULT_WIRE_CODEC_H__ */
