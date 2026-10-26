#ifndef OPERATOR_WIRE_CODEC_H
#define OPERATOR_WIRE_CODEC_H
#include <stdint.h>
#include "test_task_wire_codec.h"

#define OPERATOR_WIRE_TYPE (7U)
#define OPERATOR_WIRE_LENGTH (56U)
#define OPERATOR_FEATURE_BIT (0x0020U)
#define OPERATOR_OPEN (1U)
#define OPERATOR_HEARTBEAT (2U)
#define OPERATOR_GRANT (3U)
#define OPERATOR_STOP (4U)
#define OPERATOR_CLEAR (5U)
#define OPERATOR_CLOSE (6U)

typedef enum {
  OPERATOR_WIRE_OK = 0, OPERATOR_WIRE_NULL, OPERATOR_WIRE_LENGTH_ERROR,
  OPERATOR_WIRE_HEADER, OPERATOR_WIRE_CRC, OPERATOR_WIRE_FIELDS
} OperatorWire_Result;

typedef struct {
  uint32_t request_id, nonce, boot_low, boot_high, session_id, sequence;
  uint8_t operation;
  TestTask_DescriptorTypeDef task; /* GRANT only; all-zero wire area otherwise. */
} OperatorWire_Request;

typedef struct {
  uint32_t request_id, nonce, boot_low, boot_high, session_id, sequence;
  uint32_t timestamp_ms, task_request_id, fault_code;
  uint32_t grant_remaining_ms, session_remaining_ms;
  uint8_t operation, result;
  uint16_t flags; /* connected=1, granted=2, executing=4, pending_stop=8 */
} OperatorWire_Reply;

OperatorWire_Result OperatorWire_RequestEncode(const OperatorWire_Request *request,
    uint8_t *frame, uint16_t capacity, uint16_t *length);
OperatorWire_Result OperatorWire_RequestDecode(const uint8_t *frame,
    uint16_t length, OperatorWire_Request *request);
OperatorWire_Result OperatorWire_ReplyEncode(const OperatorWire_Reply *reply,
    uint8_t *frame, uint16_t capacity, uint16_t *length);
OperatorWire_Result OperatorWire_ReplyDecode(const uint8_t *frame,
    uint16_t length, OperatorWire_Reply *reply);
#endif
