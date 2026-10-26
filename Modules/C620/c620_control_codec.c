/**
  ******************************************************************************
  * @file    c620_control_codec.c
  * @brief   Offline encoder for the ID 1 C620 current-control slot.
  ******************************************************************************
  */

#include "c620_control_codec.h"

C620_ControlCodec_ResultTypeDef C620_ControlCodec_EncodeCurrent(
    int16_t current_raw,
    uint8_t data[C620_CONTROL_DATA_LENGTH])
{
  uint16_t encoded_current;
  uint8_t index;

  if (data == (uint8_t *)0)
  {
    return C620_CONTROL_CODEC_NULL_ARGUMENT;
  }

  if ((current_raw < C620_CONTROL_CURRENT_MIN_RAW) ||
      (current_raw > C620_CONTROL_CURRENT_MAX_RAW))
  {
    return C620_CONTROL_CODEC_CURRENT_RANGE;
  }

  encoded_current = (uint16_t)current_raw;
  data[0] = (uint8_t)(encoded_current >> 8U);
  data[1] = (uint8_t)(encoded_current & 0xFFU);
  for (index = 2U; index < C620_CONTROL_DATA_LENGTH; ++index)
  {
    data[index] = 0U;
  }

  return C620_CONTROL_CODEC_OK;
}
