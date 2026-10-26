/**
  ******************************************************************************
  * @file    c620_control_codec.h
  * @brief   Offline encoder for the ID 1 C620 current-control slot.
  *
  * This module only converts a constrained protocol quantity to eight data
  * bytes.  It does not own a CAN peripheral, choose an arbitrary identifier,
  * or transmit a frame.
  ******************************************************************************
  */

#ifndef __C620_CONTROL_CODEC_H__
#define __C620_CONTROL_CODEC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** Current C620 node is ID 1, therefore its control group is 0x200. */
#define C620_CONTROL_STD_ID             (0x200U)
#define C620_CONTROL_DATA_LENGTH        (8U)
#define C620_CONTROL_CURRENT_MIN_RAW    (-16384)
#define C620_CONTROL_CURRENT_MAX_RAW    (16384)

typedef enum
{
  C620_CONTROL_CODEC_OK = 0U,
  C620_CONTROL_CODEC_NULL_ARGUMENT = 1U,
  C620_CONTROL_CODEC_CURRENT_RANGE = 2U
} C620_ControlCodec_ResultTypeDef;

/**
  * @brief Encode ID 1's current slot in big-endian order.
  *
  * DATA[0:1] receives the signed raw current for node ID 1. DATA[2:7] is
  * cleared to zero. The destination is not modified when validation fails.
  */
C620_ControlCodec_ResultTypeDef C620_ControlCodec_EncodeCurrent(
    int16_t current_raw,
    uint8_t data[C620_CONTROL_DATA_LENGTH]);

#ifdef __cplusplus
}
#endif

#endif /* __C620_CONTROL_CODEC_H__ */
