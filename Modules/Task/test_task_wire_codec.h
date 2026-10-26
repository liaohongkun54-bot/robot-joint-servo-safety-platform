/**
  ******************************************************************************
  * @file    test_task_wire_codec.h
  * @brief   Fixed binary transport envelope for a structured test task.
  *
  * This module carries task intent only.  It contains no CAN identifier,
  * payload bytes, register address or transmit operation.
  ******************************************************************************
  */

#ifndef __TEST_TASK_WIRE_CODEC_H__
#define __TEST_TASK_WIRE_CODEC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "test_task_schema.h"

/* Numeric magic; little-endian wire bytes are 4B 54. */
#define TEST_TASK_WIRE_MAGIC             (0x544BU)
#define TEST_TASK_WIRE_VERSION           (1U)
#define TEST_TASK_WIRE_TYPE_TASK         (1U)
#define TEST_TASK_WIRE_PAYLOAD_LENGTH    (22U)
#define TEST_TASK_WIRE_FRAME_LENGTH      (30U)

typedef enum
{
  TEST_TASK_WIRE_OK = 0U,
  TEST_TASK_WIRE_ERROR_NULL_ARGUMENT = 1U,
  TEST_TASK_WIRE_ERROR_CAPACITY = 2U,
  TEST_TASK_WIRE_ERROR_LENGTH = 3U,
  TEST_TASK_WIRE_ERROR_MAGIC = 4U,
  TEST_TASK_WIRE_ERROR_VERSION = 5U,
  TEST_TASK_WIRE_ERROR_TYPE = 6U,
  TEST_TASK_WIRE_ERROR_PAYLOAD_LENGTH = 7U,
  TEST_TASK_WIRE_ERROR_CRC = 8U,
  TEST_TASK_WIRE_ERROR_TASK_SCHEMA = 9U
} TestTask_WireErrorTypeDef;

/** Encode one validated descriptor into a fixed 30-byte little-endian frame. */
TestTask_WireErrorTypeDef TestTask_WireEncode(
    const TestTask_DescriptorTypeDef *task,
    uint8_t *frame,
    uint16_t frame_capacity,
    uint16_t *frame_length);

/** Decode and validate one complete frame; no application action is performed. */
TestTask_WireErrorTypeDef TestTask_WireDecode(
    const uint8_t *frame,
    uint16_t frame_length,
    TestTask_DescriptorTypeDef *task,
    TestTask_ValidationErrorTypeDef *schema_error);

/** Stable diagnostic text for transport-level failures. */
const char *TestTask_WireErrorString(TestTask_WireErrorTypeDef error_code);

#ifdef __cplusplus
}
#endif

#endif /* __TEST_TASK_WIRE_CODEC_H__ */
