#include <stdint.h>
#include <string.h>

#include "telemetry_wire_codec.h"

static const uint8_t k_query[TELEMETRY_QUERY_LENGTH] = {
  0x4b,0x54,0x01,0x05,0x16,0x00,0x78,0x56,0x34,0x12,
  0xf0,0xde,0xbc,0x9a,0x01,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x3b
};
static const uint8_t k_reply[TELEMETRY_REPLY_LENGTH] = {
  0x54,0x52,0x01,0x05,0x22,0x00,0x78,0x56,0x34,0x12,
  0xf0,0xde,0xbc,0x9a,0x40,0x30,0x20,0x10,0x3f,0x30,
  0x20,0x10,0x07,0x00,0x00,0x00,0x07,0x12,0x54,0xf8,
  0x5a,0x04,0x1e,0x01,0x01,0x01,0x01,0x02,0x00,0x00,
  0xe4,0xd7
};

int main(void)
{
  Telemetry_QueryTypeDef query;
  Telemetry_ReplyTypeDef reply = {0};
  Telemetry_ReplyTypeDef decoded;
  uint8_t frame[TELEMETRY_REPLY_LENGTH];
  uint16_t length = 0U;

  if (Telemetry_QueryDecode(k_query, sizeof(k_query), &query) !=
          TELEMETRY_WIRE_OK ||
      query.request_id != 0x12345678UL || query.nonce != 0x9ABCDEF0UL)
  {
    return 1;
  }
  reply.request_id = query.request_id;
  reply.nonce = query.nonce;
  reply.sample_tick_ms = 0x10203040UL;
  reply.last_rx_tick_ms = 0x1020303FUL;
  reply.valid_frame_count = 7UL;
  reply.encoder_raw = 4615U;
  reply.speed_rpm = -1964;
  reply.current_raw = 1114;
  reply.temperature_c = 30U;
  reply.link_state = 1U;
  reply.has_valid_feedback = 1U;
  reply.source_id = TELEMETRY_SOURCE_C620_CAN1;
  reply.feedback_std_id = TELEMETRY_C620_STD_ID;
  if (Telemetry_ReplyEncode(&reply, frame, sizeof(frame), &length) !=
          TELEMETRY_WIRE_OK || length != sizeof(k_reply) ||
      memcmp(frame, k_reply, sizeof(k_reply)) != 0 ||
      Telemetry_ReplyDecode(k_reply, sizeof(k_reply), &decoded) !=
          TELEMETRY_WIRE_OK || decoded.speed_rpm != -1964 ||
      decoded.encoder_raw != 4615U)
  {
    return 2;
  }
  frame[39] = 1U;
  if (Telemetry_ReplyDecode(frame, sizeof(frame), &decoded) !=
      TELEMETRY_WIRE_CRC)
  {
    return 3;
  }
  if (Telemetry_QueryDecode(k_query, sizeof(k_query) - 1U, &query) !=
      TELEMETRY_WIRE_LENGTH)
  {
    return 4;
  }
  return 0;
}
