/**
  ******************************************************************************
  * @file    c620_control_output.c
  * @brief   Fixed-slot C620 control-output boundary for the P3-C preparation.
  ******************************************************************************
  */

#include "c620_control_output.h"

volatile C620_ControlOutput_SnapshotTypeDef g_c620_control_output_snapshot;

static void C620_ControlOutput_RecordResult(
    C620_ControlOutput_ResultTypeDef result)
{
  g_c620_control_output_snapshot.last_result = result;
  if (result != C620_CONTROL_OUTPUT_OK)
  {
    g_c620_control_output_snapshot.rejected_send_count++;
  }
}

void C620_ControlOutput_Init(void)
{
  uint8_t index;

  g_c620_control_output_snapshot.transport_attempt_count = 0UL;
  g_c620_control_output_snapshot.successful_send_count = 0UL;
  g_c620_control_output_snapshot.rejected_send_count = 0UL;
  g_c620_control_output_snapshot.last_current_raw = 0;
  g_c620_control_output_snapshot.last_was_zero = 1U;
  g_c620_control_output_snapshot.last_result = C620_CONTROL_OUTPUT_OK;
  for (index = 0U; index < C620_CONTROL_DATA_LENGTH; ++index)
  {
    g_c620_control_output_snapshot.last_payload[index] = 0U;
  }
}

C620_ControlOutput_ResultTypeDef C620_ControlOutput_SendCurrent(
    int16_t current_raw,
    C620_ControlOutput_TransportFn transport,
    void *context)
{
  uint8_t payload[C620_CONTROL_DATA_LENGTH];
  C620_ControlCodec_ResultTypeDef codec_result;
  uint8_t index;

  if (transport == (C620_ControlOutput_TransportFn)0)
  {
    C620_ControlOutput_RecordResult(C620_CONTROL_OUTPUT_NULL_ARGUMENT);
    return C620_CONTROL_OUTPUT_NULL_ARGUMENT;
  }

  codec_result = C620_ControlCodec_EncodeCurrent(current_raw, payload);
  if (codec_result != C620_CONTROL_CODEC_OK)
  {
    C620_ControlOutput_RecordResult(C620_CONTROL_OUTPUT_CODEC_ERROR);
    return C620_CONTROL_OUTPUT_CODEC_ERROR;
  }

  g_c620_control_output_snapshot.last_current_raw = current_raw;
  g_c620_control_output_snapshot.last_was_zero =
      (current_raw == 0) ? 1U : 0U;
  for (index = 0U; index < C620_CONTROL_DATA_LENGTH; ++index)
  {
    g_c620_control_output_snapshot.last_payload[index] = payload[index];
  }

  if (C620_CONTROL_OUTPUT_ENABLE == 0U)
  {
    C620_ControlOutput_RecordResult(C620_CONTROL_OUTPUT_DISABLED);
    return C620_CONTROL_OUTPUT_DISABLED;
  }

  g_c620_control_output_snapshot.transport_attempt_count++;
  if (transport(payload, context) != C620_CONTROL_OUTPUT_TRANSPORT_OK)
  {
    C620_ControlOutput_RecordResult(C620_CONTROL_OUTPUT_TRANSPORT_ERROR);
    return C620_CONTROL_OUTPUT_TRANSPORT_ERROR;
  }

  g_c620_control_output_snapshot.successful_send_count++;
  C620_ControlOutput_RecordResult(C620_CONTROL_OUTPUT_OK);
  return C620_CONTROL_OUTPUT_OK;
}

C620_ControlOutput_ResultTypeDef C620_ControlOutput_SendZero(
    C620_ControlOutput_TransportFn transport,
    void *context)
{
  return C620_ControlOutput_SendCurrent(0, transport, context);
}
