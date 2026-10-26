#ifndef TELEMETRY_WIRE_CODEC_H
#define TELEMETRY_WIRE_CODEC_H

#include <stdint.h>

/* Read-only KT/TR v1 type 5. Existing task/status frames are unchanged. */
#define TELEMETRY_QUERY_TYPE          (5U)
#define TELEMETRY_REPLY_TYPE          (5U)
#define TELEMETRY_REVISION            (1U)
#define TELEMETRY_QUERY_LENGTH        (30U)
#define TELEMETRY_REPLY_LENGTH        (42U)
#define TELEMETRY_REPLY_PAYLOAD       (34U)
#define TELEMETRY_SOURCE_C620_CAN1    (1U)
#define TELEMETRY_C620_STD_ID         (0x201U)

typedef struct
{
  uint32_t request_id;
  uint32_t nonce;
} Telemetry_QueryTypeDef;

typedef struct
{
  uint32_t request_id;
  uint32_t nonce;
  uint32_t sample_tick_ms;
  uint32_t last_rx_tick_ms;
  uint32_t valid_frame_count;
  uint16_t encoder_raw;
  int16_t speed_rpm;
  int16_t current_raw;
  uint8_t temperature_c;
  uint8_t link_state;
  uint8_t has_valid_feedback;
  uint8_t source_id;
  uint16_t feedback_std_id;
} Telemetry_ReplyTypeDef;

typedef enum
{
  TELEMETRY_WIRE_OK = 0U,
  TELEMETRY_WIRE_NULL = 1U,
  TELEMETRY_WIRE_LENGTH = 2U,
  TELEMETRY_WIRE_HEADER = 3U,
  TELEMETRY_WIRE_CRC = 4U,
  TELEMETRY_WIRE_FIELDS = 5U
} Telemetry_WireResultTypeDef;

Telemetry_WireResultTypeDef Telemetry_QueryDecode(
    const uint8_t *frame, uint16_t length, Telemetry_QueryTypeDef *query);
Telemetry_WireResultTypeDef Telemetry_ReplyEncode(
    const Telemetry_ReplyTypeDef *reply, uint8_t *frame,
    uint16_t capacity, uint16_t *length);
Telemetry_WireResultTypeDef Telemetry_ReplyDecode(
    const uint8_t *frame, uint16_t length, Telemetry_ReplyTypeDef *reply);

#endif
