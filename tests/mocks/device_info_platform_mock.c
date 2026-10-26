#include "device_info_platform.h"

void DeviceInfo_PlatformRead(const DeviceInfo_QueryTypeDef *query,
                            uint32_t now_ms, DeviceInfo_ReplyTypeDef *reply)
{
  reply->request_id = query->request_id; reply->nonce = query->nonce;
  reply->timestamp_ms = now_ms;
  reply->uid_words[0] = 0x12345678UL; reply->uid_words[1] = 0x9ABCDEF0UL; reply->uid_words[2] = 0x10203040UL;
  reply->feature_mask = DEVICE_INFO_FEATURES;
  reply->firmware_version = DEVICE_INFO_FIRMWARE_VERSION;
  reply->build_date = 20261019UL; reply->build_time = 123456UL;
}
