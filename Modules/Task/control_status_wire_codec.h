#ifndef CONTROL_STATUS_WIRE_CODEC_H
#define CONTROL_STATUS_WIRE_CODEC_H

#include <stdint.h>

/* This read-only extension shares the existing 30-byte KT v1 request envelope. */
#define CONTROL_STATUS_QUERY_TYPE       (4U)
#define CONTROL_STATUS_REPLY_TYPE       (4U)
#define CONTROL_STATUS_REVISION         (1U)
#define CONTROL_STATUS_QUERY_LENGTH     (30U)
#define CONTROL_STATUS_REPLY_LENGTH     (38U)
#define CONTROL_STATUS_REPLY_PAYLOAD    (30U)
#define CONTROL_STATUS_FEATURE_QUERY    (0x0001U)
#define CONTROL_STATUS_RECORD_CAPACITY  (64U)

typedef struct
{
  uint32_t request_id;
  uint32_t nonce;
} ControlStatus_QueryTypeDef;

typedef struct
{
  uint32_t request_id;
  uint32_t nonce;
  uint32_t timestamp_ms;
  uint16_t feature_mask;
  uint8_t safety_state;
  uint8_t output_permitted;
  uint8_t zero_output_required;
  uint8_t fault_latched;
  uint8_t motion_active;
  uint16_t record_count;
  uint32_t safety_fault_code;
} ControlStatus_ReplyTypeDef;

typedef enum
{
  CONTROL_STATUS_WIRE_OK = 0U,
  CONTROL_STATUS_WIRE_NULL = 1U,
  CONTROL_STATUS_WIRE_LENGTH = 2U,
  CONTROL_STATUS_WIRE_HEADER = 3U,
  CONTROL_STATUS_WIRE_CRC = 4U,
  CONTROL_STATUS_WIRE_FIELDS = 5U
} ControlStatus_WireResultTypeDef;

ControlStatus_WireResultTypeDef ControlStatus_QueryDecode(
    const uint8_t *frame, uint16_t length, ControlStatus_QueryTypeDef *query);
ControlStatus_WireResultTypeDef ControlStatus_ReplyEncode(
    const ControlStatus_ReplyTypeDef *reply, uint8_t *frame,
    uint16_t capacity, uint16_t *length);
ControlStatus_WireResultTypeDef ControlStatus_ReplyDecode(
    const uint8_t *frame, uint16_t length, ControlStatus_ReplyTypeDef *reply);

#endif
