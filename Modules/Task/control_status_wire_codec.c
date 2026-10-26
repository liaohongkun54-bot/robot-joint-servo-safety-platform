#include "control_status_wire_codec.h"

static uint16_t ReadU16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t ReadU32(const uint8_t *data)
{
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
         ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void WriteU16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8U);
}

static void WriteU32(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8U);
  data[2] = (uint8_t)(value >> 16U);
  data[3] = (uint8_t)(value >> 24U);
}

static uint16_t Crc16(const uint8_t *data, uint16_t length)
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
          (uint16_t)((crc << 1U) ^ 0x1021U) : (uint16_t)(crc << 1U);
    }
  }
  return crc;
}

ControlStatus_WireResultTypeDef ControlStatus_QueryDecode(
    const uint8_t *frame, uint16_t length, ControlStatus_QueryTypeDef *query)
{
  uint16_t index;
  if ((frame == (const uint8_t *)0) || (query == (ControlStatus_QueryTypeDef *)0))
  {
    return CONTROL_STATUS_WIRE_NULL;
  }
  if (length != CONTROL_STATUS_QUERY_LENGTH)
  {
    return CONTROL_STATUS_WIRE_LENGTH;
  }
  if ((ReadU16(frame) != 0x544BU) || (frame[2] != 1U) ||
      (frame[3] != CONTROL_STATUS_QUERY_TYPE) || (ReadU16(&frame[4]) != 22U))
  {
    return CONTROL_STATUS_WIRE_HEADER;
  }
  if (ReadU16(&frame[28]) != Crc16(frame, 28U))
  {
    return CONTROL_STATUS_WIRE_CRC;
  }
  if ((ReadU32(&frame[6]) == 0UL) ||
      (ReadU16(&frame[14]) != CONTROL_STATUS_REVISION))
  {
    return CONTROL_STATUS_WIRE_FIELDS;
  }
  for (index = 16U; index < 28U; index++)
  {
    if (frame[index] != 0U)
    {
      return CONTROL_STATUS_WIRE_FIELDS;
    }
  }
  query->request_id = ReadU32(&frame[6]);
  query->nonce = ReadU32(&frame[10]);
  return CONTROL_STATUS_WIRE_OK;
}

static ControlStatus_WireResultTypeDef ValidateReply(
    const ControlStatus_ReplyTypeDef *reply)
{
  if ((reply->request_id == 0UL) ||
      (reply->feature_mask != CONTROL_STATUS_FEATURE_QUERY) ||
      (reply->safety_state > 6U) ||
      (reply->output_permitted > 1U) ||
      (reply->zero_output_required > 1U) ||
      (reply->fault_latched > 1U) ||
      (reply->motion_active > 1U) ||
      (reply->record_count > CONTROL_STATUS_RECORD_CAPACITY))
  {
    return CONTROL_STATUS_WIRE_FIELDS;
  }
  return CONTROL_STATUS_WIRE_OK;
}

ControlStatus_WireResultTypeDef ControlStatus_ReplyEncode(
    const ControlStatus_ReplyTypeDef *reply, uint8_t *frame,
    uint16_t capacity, uint16_t *length)
{
  ControlStatus_WireResultTypeDef result;
  if ((reply == (const ControlStatus_ReplyTypeDef *)0) ||
      (frame == (uint8_t *)0) || (length == (uint16_t *)0))
  {
    return CONTROL_STATUS_WIRE_NULL;
  }
  if (capacity < CONTROL_STATUS_REPLY_LENGTH)
  {
    return CONTROL_STATUS_WIRE_LENGTH;
  }
  result = ValidateReply(reply);
  if (result != CONTROL_STATUS_WIRE_OK)
  {
    return result;
  }
  WriteU16(&frame[0], 0x5254U);
  frame[2] = 1U;
  frame[3] = CONTROL_STATUS_REPLY_TYPE;
  WriteU16(&frame[4], CONTROL_STATUS_REPLY_PAYLOAD);
  WriteU32(&frame[6], reply->request_id);
  WriteU32(&frame[10], reply->nonce);
  WriteU32(&frame[14], reply->timestamp_ms);
  WriteU16(&frame[18], CONTROL_STATUS_REVISION);
  WriteU16(&frame[20], reply->feature_mask);
  frame[22] = reply->safety_state;
  frame[23] = reply->output_permitted;
  frame[24] = reply->zero_output_required;
  frame[25] = reply->fault_latched;
  frame[26] = reply->motion_active;
  frame[27] = 0U;
  WriteU16(&frame[28], reply->record_count);
  WriteU16(&frame[30], CONTROL_STATUS_RECORD_CAPACITY);
  WriteU32(&frame[32], reply->safety_fault_code);
  WriteU16(&frame[36], Crc16(frame, 36U));
  *length = CONTROL_STATUS_REPLY_LENGTH;
  return CONTROL_STATUS_WIRE_OK;
}

ControlStatus_WireResultTypeDef ControlStatus_ReplyDecode(
    const uint8_t *frame, uint16_t length, ControlStatus_ReplyTypeDef *reply)
{
  if ((frame == (const uint8_t *)0) || (reply == (ControlStatus_ReplyTypeDef *)0))
  {
    return CONTROL_STATUS_WIRE_NULL;
  }
  if (length != CONTROL_STATUS_REPLY_LENGTH)
  {
    return CONTROL_STATUS_WIRE_LENGTH;
  }
  if ((ReadU16(frame) != 0x5254U) || (frame[2] != 1U) ||
      (frame[3] != CONTROL_STATUS_REPLY_TYPE) ||
      (ReadU16(&frame[4]) != CONTROL_STATUS_REPLY_PAYLOAD))
  {
    return CONTROL_STATUS_WIRE_HEADER;
  }
  if (ReadU16(&frame[36]) != Crc16(frame, 36U))
  {
    return CONTROL_STATUS_WIRE_CRC;
  }
  if ((ReadU16(&frame[18]) != CONTROL_STATUS_REVISION) ||
      (frame[27] != 0U) ||
      (ReadU16(&frame[30]) != CONTROL_STATUS_RECORD_CAPACITY))
  {
    return CONTROL_STATUS_WIRE_FIELDS;
  }
  reply->request_id = ReadU32(&frame[6]);
  reply->nonce = ReadU32(&frame[10]);
  reply->timestamp_ms = ReadU32(&frame[14]);
  reply->feature_mask = ReadU16(&frame[20]);
  reply->safety_state = frame[22];
  reply->output_permitted = frame[23];
  reply->zero_output_required = frame[24];
  reply->fault_latched = frame[25];
  reply->motion_active = frame[26];
  reply->record_count = ReadU16(&frame[28]);
  reply->safety_fault_code = ReadU32(&frame[32]);
  return ValidateReply(reply);
}
