#include "device_info_platform.h"
#include "stm32f4xx_hal.h"
#include "operator_runtime.h"

static uint32_t Digit(char character) { return (uint32_t)(character - '0'); }

void DeviceInfo_PlatformRead(const DeviceInfo_QueryTypeDef *query,
                            uint32_t now_ms, DeviceInfo_ReplyTypeDef *reply)
{
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  static const char date[] = __DATE__;
  static const char clock[] = __TIME__;
  uint32_t month, day, year;
  if ((query == (const DeviceInfo_QueryTypeDef *)0) || (reply == (DeviceInfo_ReplyTypeDef *)0)) { return; }
  for (month = 0UL; month < 12UL; month++)
  {
    if ((months[month * 3UL] == date[0]) && (months[month * 3UL + 1UL] == date[1]) &&
        (months[month * 3UL + 2UL] == date[2])) { break; }
  }
  year = Digit(date[7]) * 1000UL + Digit(date[8]) * 100UL + Digit(date[9]) * 10UL + Digit(date[10]);
  day = ((date[4] == ' ') ? 0UL : Digit(date[4]) * 10UL) + Digit(date[5]);
  reply->request_id = query->request_id; reply->nonce = query->nonce;
  reply->timestamp_ms = now_ms;
  reply->uid_words[0] = HAL_GetUIDw0(); reply->uid_words[1] = HAL_GetUIDw1(); reply->uid_words[2] = HAL_GetUIDw2();
  reply->feature_mask = DEVICE_INFO_FEATURES;
  if (OperatorRuntime_Ready() != 0U) { reply->feature_mask |= 0x0020U; }
  reply->firmware_version = DEVICE_INFO_FIRMWARE_VERSION;
  reply->build_date = year * 10000UL + (month + 1UL) * 100UL + day;
  reply->build_time = (Digit(clock[0]) * 10UL + Digit(clock[1])) * 10000UL +
      (Digit(clock[3]) * 10UL + Digit(clock[4])) * 100UL + Digit(clock[6]) * 10UL + Digit(clock[7]);
}
