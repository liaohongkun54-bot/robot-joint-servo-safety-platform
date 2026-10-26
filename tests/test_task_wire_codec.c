#include <stdio.h>

#include "test_task_wire_codec.h"

int main(void)
{
  const TestTask_DescriptorTypeDef source = {
      TEST_TASK_SCHEMA_VERSION,
      1001UL,
      TEST_TASK_TARGET_C620_ID,
      TEST_TASK_ACTION_CURRENT_TEST,
      0,
      100,
      100UL,
      100UL,
      TEST_TASK_TIMEOUT_STOP,
      TEST_TASK_PERMISSION_OPERATOR};
  TestTask_DescriptorTypeDef decoded;
  TestTask_ValidationErrorTypeDef schema_error = TEST_TASK_VALID;
  uint8_t frame[TEST_TASK_WIRE_FRAME_LENGTH];
  uint16_t frame_length = 0U;
  TestTask_WireErrorTypeDef result;

  result = TestTask_WireEncode(&source, frame, sizeof(frame), &frame_length);
  if ((result != TEST_TASK_WIRE_OK) ||
      (frame_length != TEST_TASK_WIRE_FRAME_LENGTH))
  {
    return 1;
  }

  result = TestTask_WireDecode(frame, frame_length, &decoded, &schema_error);
  if ((result != TEST_TASK_WIRE_OK) ||
      (schema_error != TEST_TASK_VALID) ||
      (decoded.schema_version != source.schema_version) ||
      (decoded.request_id != source.request_id) ||
      (decoded.target_node_id != source.target_node_id) ||
      (decoded.action != source.action) ||
      (decoded.amplitude_raw != source.amplitude_raw) ||
      (decoded.amplitude_limit_raw != source.amplitude_limit_raw) ||
      (decoded.duration_ms != source.duration_ms) ||
      (decoded.feedback_timeout_ms != source.feedback_timeout_ms) ||
      (decoded.timeout_policy != source.timeout_policy) ||
      (decoded.permission != source.permission))
  {
    return 2;
  }

  frame[28] ^= 0x01U;
  result = TestTask_WireDecode(frame, frame_length, &decoded, &schema_error);
  if (result != TEST_TASK_WIRE_ERROR_CRC)
  {
    return 3;
  }
  frame[28] ^= 0x01U;

  frame[0] ^= 0x01U;
  if (TestTask_WireDecode(frame, frame_length, &decoded, &schema_error) !=
      TEST_TASK_WIRE_ERROR_MAGIC)
  {
    return 4;
  }
  frame[0] ^= 0x01U;

  frame[2]++;
  if (TestTask_WireDecode(frame, frame_length, &decoded, &schema_error) !=
      TEST_TASK_WIRE_ERROR_VERSION)
  {
    return 5;
  }
  frame[2]--;

  frame[3]++;
  if (TestTask_WireDecode(frame, frame_length, &decoded, &schema_error) !=
      TEST_TASK_WIRE_ERROR_TYPE)
  {
    return 6;
  }
  frame[3]--;

  frame[4]++;
  if (TestTask_WireDecode(frame, frame_length, &decoded, &schema_error) !=
      TEST_TASK_WIRE_ERROR_PAYLOAD_LENGTH)
  {
    return 7;
  }
  frame[4]--;

  if (TestTask_WireDecode(
          frame, frame_length - 1U, &decoded, &schema_error) !=
      TEST_TASK_WIRE_ERROR_LENGTH)
  {
    return 8;
  }
  if (TestTask_WireEncode(
          &source, frame, TEST_TASK_WIRE_FRAME_LENGTH - 1U, &frame_length) !=
      TEST_TASK_WIRE_ERROR_CAPACITY)
  {
    return 9;
  }

  (void)printf("task_wire_host_tests=pass\n");
  return 0;
}
