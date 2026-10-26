#include <stdio.h>
#include <string.h>

#include "control_status_wire_codec.h"

static const uint8_t k_query[30] = {
    0x4b, 0x54, 0x01, 0x04, 0x16, 0x00, 0x78, 0x56,
    0x34, 0x12, 0xf0, 0xde, 0xbc, 0x9a, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xb6, 0x7e
};

static const uint8_t k_reply[38] = {
    0x54, 0x52, 0x01, 0x04, 0x1e, 0x00, 0x78, 0x56,
    0x34, 0x12, 0xf0, 0xde, 0xbc, 0x9a, 0x40, 0x30,
    0x20, 0x10, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x40, 0x00,
    0x03, 0xe0, 0x00, 0x00, 0xf3, 0x8c
};

int main(void)
{
  ControlStatus_QueryTypeDef query;
  ControlStatus_ReplyTypeDef reply = {
      0x12345678UL, 0x9ABCDEF0UL, 0x10203040UL, 1U,
      2U, 0U, 1U, 0U, 0U, 5U, 0xE003UL
  };
  ControlStatus_ReplyTypeDef decoded;
  uint8_t frame[CONTROL_STATUS_REPLY_LENGTH];
  uint8_t corrupt[CONTROL_STATUS_REPLY_LENGTH];
  uint16_t length = 0U;
  if ((ControlStatus_QueryDecode(k_query, sizeof(k_query), &query) !=
       CONTROL_STATUS_WIRE_OK) ||
      (query.request_id != reply.request_id) || (query.nonce != reply.nonce))
  {
    return 1;
  }
  if ((ControlStatus_ReplyEncode(&reply, frame, sizeof(frame), &length) !=
       CONTROL_STATUS_WIRE_OK) ||
      (length != sizeof(k_reply)) || (memcmp(frame, k_reply, length) != 0) ||
      (ControlStatus_ReplyDecode(frame, length, &decoded) !=
       CONTROL_STATUS_WIRE_OK) ||
      (decoded.record_count != 5U) || (decoded.safety_fault_code != 0xE003UL))
  {
    return 2;
  }
  memcpy(corrupt, k_reply, sizeof(corrupt));
  corrupt[22] = 7U;
  if (ControlStatus_ReplyDecode(corrupt, sizeof(corrupt), &decoded) !=
      CONTROL_STATUS_WIRE_CRC)
  {
    return 3;
  }
  memcpy(corrupt, k_query, sizeof(k_query));
  corrupt[17] = 1U;
  if (ControlStatus_QueryDecode(corrupt, sizeof(k_query), &query) !=
      CONTROL_STATUS_WIRE_CRC)
  {
    return 4;
  }
  reply.record_count = 65U;
  if (ControlStatus_ReplyEncode(&reply, frame, sizeof(frame), &length) !=
      CONTROL_STATUS_WIRE_FIELDS)
  {
    return 5;
  }
  (void)printf("control_status_wire_tests=pass\n");
  return 0;
}
