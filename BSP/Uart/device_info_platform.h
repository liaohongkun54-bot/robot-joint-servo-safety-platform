#ifndef DEVICE_INFO_PLATFORM_H
#define DEVICE_INFO_PLATFORM_H

#include "device_info_wire_codec.h"

/* Read UID/constant metadata only; no motor, GPIO or Flash-write ownership. */
void DeviceInfo_PlatformRead(const DeviceInfo_QueryTypeDef *query,
                            uint32_t now_ms, DeviceInfo_ReplyTypeDef *reply);
#endif
