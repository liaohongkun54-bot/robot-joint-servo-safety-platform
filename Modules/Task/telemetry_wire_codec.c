#include "telemetry_wire_codec.h"

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

Telemetry_WireResultTypeDef Telemetry_QueryDecode(
    const uint8_t *frame, uint16_t length, Telemetry_QueryTypeDef *query)
{
  uint16_t index;
  if ((frame == (const uint8_t *)0) || (query == (Telemetry_QueryTypeDef *)0))
  {
    return TELEMETRY_WIRE_NULL;
  }
  if (length != TELEMETRY_QUERY_LENGTH)
  {
    return TELEMETRY_WIRE_LENGTH;
  }
  if ((ReadU16(frame) != 0x544BU) || (frame[2] != 1U) ||
      (frame[3] != TELEMETRY_QUERY_TYPE) || (ReadU16(&frame[4]) != 22U))
  {
    return TELEMETRY_WIRE_HEADER;
  }
  if (ReadU16(&frame[28]) != Crc16(frame, 28U))
  {
    return TELEMETRY_WIRE_CRC;
  }
  if ((ReadU32(&frame[6]) == 0UL) ||
      (ReadU16(&frame[14]) != TELEMETRY_REVISION))
  {
    return TELEMETRY_WIRE_FIELDS;
  }
  for (index = 16U; index < 28U; index++)
  {
    if (frame[index] != 0U)
    {
      return TELEMETRY_WIRE_FIELDS;
    }
  }
  query->request_id = ReadU32(&frame[6]);
  query->nonce = ReadU32(&frame[10]);
  return TELEMETRY_WIRE_OK;
}

static Telemetry_WireResultTypeDef ValidateReply(
    const Telemetry_ReplyTypeDef *reply)
{
  if ((reply->request_id == 0UL) || (reply->encoder_raw > 8191U) ||
      (reply->link_state > 2U) || (reply->has_valid_feedback > 1U) ||
      (reply->source_id != TELEMETRY_SOURCE_C620_CAN1) ||
      (reply->feedback_std_id != TELEMETRY_C620_STD_ID))
  {
    return TELEMETRY_WIRE_FIELDS;
  }
  return TELEMETRY_WIRE_OK;
}

Telemetry_WireResultTypeDef Telemetry_ReplyEncode(
    const Telemetry_ReplyTypeDef *reply, uint8_t *frame,
    uint16_t capacity, uint16_t *length)
{
  Telemetry_WireResultTypeDef result;
  if ((reply == (const Telemetry_ReplyTypeDef *)0) ||
      (frame == (uint8_t *)0) || (length == (uint16_t *)0))
  {
    return TELEMETRY_WIRE_NULL;
  }
  if (capacity < TELEMETRY_REPLY_LENGTH)
  {
    return TELEMETRY_WIRE_LENGTH;
  }
  result = ValidateReply(reply);
  if (result != TELEMETRY_WIRE_OK)
  {
    return result;
  }
  WriteU16(&frame[0], 0x5254U);
  frame[2] = 1U;
  frame[3] = TELEMETRY_REPLY_TYPE;
  WriteU16(&frame[4], TELEMETRY_REPLY_PAYLOAD);
  WriteU32(&frame[6], reply->request_id);
  WriteU32(&frame[10], reply->nonce);
  WriteU32(&frame[14], reply->sample_tick_ms);
  WriteU32(&frame[18], reply->last_rx_tick_ms);
  WriteU32(&frame[22], reply->valid_frame_count);
  WriteU16(&frame[26], reply->encoder_raw);
  WriteU16(&frame[28], (uint16_t)reply->speed_rpm);
  WriteU16(&frame[30], (uint16_t)reply->current_raw);
  frame[32] = reply->temperature_c;
  frame[33] = reply->link_state;
  frame[34] = reply->has_valid_feedback;
  frame[35] = reply->source_id;
  WriteU16(&frame[36], reply->feedback_std_id);
  WriteU16(&frame[38], 0U);
  WriteU16(&frame[40], Crc16(frame, 40U));
  *length = TELEMETRY_REPLY_LENGTH;
  return TELEMETRY_WIRE_OK;
}

Telemetry_WireResultTypeDef Telemetry_ReplyDecode(
    const uint8_t *frame, uint16_t length, Telemetry_ReplyTypeDef *reply)
{
  if ((frame == (const uint8_t *)0) || (reply == (Telemetry_ReplyTypeDef *)0))
  {
    return TELEMETRY_WIRE_NULL;
  }
  if (length != TELEMETRY_REPLY_LENGTH)
  {
    return TELEMETRY_WIRE_LENGTH;
  }
  if ((ReadU16(frame) != 0x5254U) || (frame[2] != 1U) ||
      (frame[3] != TELEMETRY_REPLY_TYPE) ||
      (ReadU16(&frame[4]) != TELEMETRY_REPLY_PAYLOAD))
  {
    return TELEMETRY_WIRE_HEADER;
  }
  if (ReadU16(&frame[40]) != Crc16(frame, 40U))
  {
    return TELEMETRY_WIRE_CRC;
  }
  if (ReadU16(&frame[38]) != 0U)
  {
    return TELEMETRY_WIRE_FIELDS;
  }
  reply->request_id = ReadU32(&frame[6]);
  reply->nonce = ReadU32(&frame[10]);
  reply->sample_tick_ms = ReadU32(&frame[14]);
  reply->last_rx_tick_ms = ReadU32(&frame[18]);
  reply->valid_frame_count = ReadU32(&frame[22]);
  reply->encoder_raw = ReadU16(&frame[26]);
  reply->speed_rpm = (int16_t)ReadU16(&frame[28]);
  reply->current_raw = (int16_t)ReadU16(&frame[30]);
  reply->temperature_c = frame[32];
  reply->link_state = frame[33];
  reply->has_valid_feedback = frame[34];
  reply->source_id = frame[35];
  reply->feedback_std_id = ReadU16(&frame[36]);
  return ValidateReply(reply);
}
