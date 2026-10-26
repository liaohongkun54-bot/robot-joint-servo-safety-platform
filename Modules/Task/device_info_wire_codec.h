#ifndef DEVICE_INFO_WIRE_CODEC_H
#define DEVICE_INFO_WIRE_CODEC_H

#include <stdint.h>

#define DEVICE_INFO_QUERY_TYPE        (6U)
#define DEVICE_INFO_REPLY_TYPE        (6U)
#define DEVICE_INFO_REVISION          (1U)
#define DEVICE_INFO_QUERY_LENGTH      (30U)
#define DEVICE_INFO_REPLY_LENGTH      (48U)
#define DEVICE_INFO_REPLY_PAYLOAD     (40U)
#define DEVICE_INFO_FEATURE_STATUS    (0x0001U)
#define DEVICE_INFO_FEATURE_TELEMETRY (0x0002U)
#define DEVICE_INFO_FEATURE_INFO      (0x0004U)
#define DEVICE_INFO_FEATURE_TASK_V1   (0x0008U)
#define DEVICE_INFO_FEATURE_STOP_LOG  (0x0010U)
#define DEVICE_INFO_FEATURES          (0x001FU)
#define DEVICE_INFO_FIRMWARE_VERSION  (0x00010101UL)

typedef struct { uint32_t request_id; uint32_t nonce; } DeviceInfo_QueryTypeDef;
typedef struct
{
  uint32_t request_id;
  uint32_t nonce;
  uint32_t timestamp_ms;
  uint32_t uid_words[3];
  uint16_t feature_mask;
  uint32_t firmware_version;
  uint32_t build_date;
  uint32_t build_time;
} DeviceInfo_ReplyTypeDef;

typedef enum
{
  DEVICE_INFO_WIRE_OK = 0U, DEVICE_INFO_WIRE_NULL = 1U,
  DEVICE_INFO_WIRE_LENGTH = 2U, DEVICE_INFO_WIRE_HEADER = 3U,
  DEVICE_INFO_WIRE_CRC = 4U, DEVICE_INFO_WIRE_FIELDS = 5U
} DeviceInfo_WireResultTypeDef;

DeviceInfo_WireResultTypeDef DeviceInfo_QueryDecode(
    const uint8_t *frame, uint16_t length, DeviceInfo_QueryTypeDef *query);
DeviceInfo_WireResultTypeDef DeviceInfo_ReplyEncode(
    const DeviceInfo_ReplyTypeDef *reply, uint8_t *frame, uint16_t capacity, uint16_t *length);
DeviceInfo_WireResultTypeDef DeviceInfo_ReplyDecode(
    const uint8_t *frame, uint16_t length, DeviceInfo_ReplyTypeDef *reply);

#endif
