/**
  ******************************************************************************
  * @file    c620_control_output.h
  * @brief   Fixed-slot C620 control-output boundary for the P3-C preparation.
  *
  * The module owns no CAN peripheral and exposes no arbitrary CAN identifier.
  * The transport callback is bound by the integration layer to C620 ID 1's
  * fixed control slot (0x200, DLC 8).  Output is additionally constrained by
  * the operator arm, Safety state and Motion Gate limits above this boundary.
  ******************************************************************************
  */

#ifndef __C620_CONTROL_OUTPUT_H__
#define __C620_CONTROL_OUTPUT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "c620_control_codec.h"

/*
 * Enabled only behind the one-shot Safety arm and Motion Gate limits.  This
 * boundary still exposes payload-only transport, so callers cannot choose a
 * CAN identifier or DLC.
 */
#define C620_CONTROL_OUTPUT_ENABLE (1U)
#define C620_CONTROL_OUTPUT_TRANSPORT_OK (1U)

typedef enum
{
  C620_CONTROL_OUTPUT_OK = 0U,
  C620_CONTROL_OUTPUT_NULL_ARGUMENT = 1U,
  C620_CONTROL_OUTPUT_DISABLED = 2U,
  C620_CONTROL_OUTPUT_CODEC_ERROR = 3U,
  C620_CONTROL_OUTPUT_TRANSPORT_ERROR = 4U
} C620_ControlOutput_ResultTypeDef;

/**
  * @brief Send one already-defined C620 control slot.
  *
  * The callback receives only the eight-byte payload.  It cannot select an
  * identifier or DLC; the integration layer must bind it to C620_CONTROL_STD_ID
  * and C620_CONTROL_DATA_LENGTH.  Return C620_CONTROL_OUTPUT_TRANSPORT_OK only
  * when the payload was accepted by the fixed transport.
  */
typedef uint8_t (*C620_ControlOutput_TransportFn)(
    const uint8_t data[C620_CONTROL_DATA_LENGTH],
    void *context);

typedef struct
{
  uint32_t transport_attempt_count;
  uint32_t successful_send_count;
  uint32_t rejected_send_count;
  int16_t last_current_raw;
  uint8_t last_was_zero;
  C620_ControlOutput_ResultTypeDef last_result;
  uint8_t last_payload[C620_CONTROL_DATA_LENGTH];
} C620_ControlOutput_SnapshotTypeDef;

extern volatile C620_ControlOutput_SnapshotTypeDef
    g_c620_control_output_snapshot;

void C620_ControlOutput_Init(void);

/** Send a constrained current value through the fixed transport boundary. */
C620_ControlOutput_ResultTypeDef C620_ControlOutput_SendCurrent(
    int16_t current_raw,
    C620_ControlOutput_TransportFn transport,
    void *context);

/** Explicit zero-current stop path used by timeout, ESTOP and normal stop. */
C620_ControlOutput_ResultTypeDef C620_ControlOutput_SendZero(
    C620_ControlOutput_TransportFn transport,
    void *context);

#ifdef __cplusplus
}
#endif

#endif /* __C620_CONTROL_OUTPUT_H__ */
