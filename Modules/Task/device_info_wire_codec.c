#include "device_info_wire_codec.h"

static uint16_t ReadU16(const uint8_t *p)
{ return (uint16_t)p[0] | ((uint16_t)p[1] << 8U); }
static uint32_t ReadU32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U); }
static void WriteU16(uint8_t *p, uint16_t n)
{ p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8U); }
static void WriteU32(uint8_t *p, uint32_t n)
{ p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8U); p[2] = (uint8_t)(n >> 16U); p[3] = (uint8_t)(n >> 24U); }
static uint16_t Crc16(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU, index;
  uint8_t bit;
  for (index = 0U; index < length; index++)
  {
    crc ^= (uint16_t)data[index] << 8U;
    for (bit = 0U; bit < 8U; bit++)
    { crc = ((crc & 0x8000U) != 0U) ? (uint16_t)((crc << 1U) ^ 0x1021U) : (uint16_t)(crc << 1U); }
  }
  return crc;
}

DeviceInfo_WireResultTypeDef DeviceInfo_QueryDecode(
    const uint8_t *frame, uint16_t length, DeviceInfo_QueryTypeDef *query)
{
  uint16_t index;
  if ((frame == (const uint8_t *)0) || (query == (DeviceInfo_QueryTypeDef *)0)) { return DEVICE_INFO_WIRE_NULL; }
  if (length != DEVICE_INFO_QUERY_LENGTH) { return DEVICE_INFO_WIRE_LENGTH; }
  if ((ReadU16(frame) != 0x544BU) || (frame[2] != 1U) ||
      (frame[3] != DEVICE_INFO_QUERY_TYPE) || (ReadU16(&frame[4]) != 22U)) { return DEVICE_INFO_WIRE_HEADER; }
  if (ReadU16(&frame[28]) != Crc16(frame, 28U)) { return DEVICE_INFO_WIRE_CRC; }
  if ((ReadU32(&frame[6]) == 0UL) || (ReadU16(&frame[14]) != DEVICE_INFO_REVISION)) { return DEVICE_INFO_WIRE_FIELDS; }
  for (index = 16U; index < 28U; index++)
  { if (frame[index] != 0U) { return DEVICE_INFO_WIRE_FIELDS; } }
  query->request_id = ReadU32(&frame[6]);
  query->nonce = ReadU32(&frame[10]);
  return DEVICE_INFO_WIRE_OK;
}

static DeviceInfo_WireResultTypeDef ValidateReply(const DeviceInfo_ReplyTypeDef *reply)
{
  static const uint8_t days[] = {31U,28U,31U,30U,31U,30U,31U,31U,30U,31U,30U,31U};
  uint32_t year = reply->build_date / 10000UL;
  uint32_t month = (reply->build_date / 100UL) % 100UL;
  uint32_t day = reply->build_date % 100UL;
  uint32_t maximum;
  if ((reply->request_id == 0UL) || ((reply->feature_mask & DEVICE_INFO_FEATURE_INFO) == 0U) ||
      (reply->firmware_version == 0UL) || (year < 1900UL) || (year > 9999UL) ||
      (month < 1UL) || (month > 12UL) || (day == 0UL) ||
      (reply->build_time / 10000UL > 23UL) || ((reply->build_time / 100UL) % 100UL > 59UL) ||
      (reply->build_time % 100UL > 59UL)) { return DEVICE_INFO_WIRE_FIELDS; }
  maximum = days[month - 1UL];
  if ((month == 2UL) && (year % 4UL == 0UL) && ((year % 100UL != 0UL) || (year % 400UL == 0UL))) { maximum++; }
  return (day > maximum) ? DEVICE_INFO_WIRE_FIELDS : DEVICE_INFO_WIRE_OK;
}

DeviceInfo_WireResultTypeDef DeviceInfo_ReplyEncode(
    const DeviceInfo_ReplyTypeDef *reply, uint8_t *frame, uint16_t capacity, uint16_t *length)
{
  uint16_t index;
  DeviceInfo_WireResultTypeDef result;
  if ((reply == (const DeviceInfo_ReplyTypeDef *)0) || (frame == (uint8_t *)0) || (length == (uint16_t *)0)) { return DEVICE_INFO_WIRE_NULL; }
  if (capacity < DEVICE_INFO_REPLY_LENGTH) { return DEVICE_INFO_WIRE_LENGTH; }
  result = ValidateReply(reply);
  if (result != DEVICE_INFO_WIRE_OK) { return result; }
  WriteU16(frame, 0x5254U); frame[2] = 1U; frame[3] = DEVICE_INFO_REPLY_TYPE;
  WriteU16(&frame[4], DEVICE_INFO_REPLY_PAYLOAD);
  WriteU32(&frame[6], reply->request_id); WriteU32(&frame[10], reply->nonce);
  WriteU32(&frame[14], reply->timestamp_ms);
  for (index = 0U; index < 3U; index++) { WriteU32(&frame[18U + index * 4U], reply->uid_words[index]); }
  WriteU16(&frame[30], DEVICE_INFO_REVISION); WriteU16(&frame[32], reply->feature_mask);
  WriteU32(&frame[34], reply->firmware_version); WriteU32(&frame[38], reply->build_date);
  WriteU32(&frame[42], reply->build_time); WriteU16(&frame[46], Crc16(frame, 46U));
  *length = DEVICE_INFO_REPLY_LENGTH;
  return DEVICE_INFO_WIRE_OK;
}

DeviceInfo_WireResultTypeDef DeviceInfo_ReplyDecode(
    const uint8_t *frame, uint16_t length, DeviceInfo_ReplyTypeDef *reply)
{
  DeviceInfo_ReplyTypeDef parsed;
  DeviceInfo_WireResultTypeDef result;
  uint16_t index;
  if ((frame == (const uint8_t *)0) || (reply == (DeviceInfo_ReplyTypeDef *)0)) { return DEVICE_INFO_WIRE_NULL; }
  if (length != DEVICE_INFO_REPLY_LENGTH) { return DEVICE_INFO_WIRE_LENGTH; }
  if ((ReadU16(frame) != 0x5254U) || (frame[2] != 1U) || (frame[3] != DEVICE_INFO_REPLY_TYPE) ||
      (ReadU16(&frame[4]) != DEVICE_INFO_REPLY_PAYLOAD)) { return DEVICE_INFO_WIRE_HEADER; }
  if (ReadU16(&frame[46]) != Crc16(frame, 46U)) { return DEVICE_INFO_WIRE_CRC; }
  if (ReadU16(&frame[30]) != DEVICE_INFO_REVISION) { return DEVICE_INFO_WIRE_FIELDS; }
  parsed.request_id = ReadU32(&frame[6]); parsed.nonce = ReadU32(&frame[10]);
  parsed.timestamp_ms = ReadU32(&frame[14]);
  for (index = 0U; index < 3U; index++) { parsed.uid_words[index] = ReadU32(&frame[18U + index * 4U]); }
  parsed.feature_mask = ReadU16(&frame[32]); parsed.firmware_version = ReadU32(&frame[34]);
  parsed.build_date = ReadU32(&frame[38]); parsed.build_time = ReadU32(&frame[42]);
  result = ValidateReply(&parsed);
  if (result == DEVICE_INFO_WIRE_OK) { *reply = parsed; }
  return result;
}
