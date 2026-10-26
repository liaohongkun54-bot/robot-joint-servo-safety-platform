#include <stdio.h>

#include "test_task_result_wire_codec.h"

int main(void)
{
  const TestTask_ResultDescriptorTypeDef source = {
      3001UL,
      1234UL,
      0x00E2U,
      1U,
      TEST_TASK_RESULT_STATUS_REJECTED,
      TEST_TASK_RESULT_STAGE_SAFETY,
      0U,
      TEST_TASK_RESULT_FLAG_RECORD_APPENDED |
          TEST_TASK_RESULT_FLAG_OUTPUT_UNCHANGED};
  TestTask_ResultDescriptorTypeDef decoded;
  uint8_t frame[TEST_TASK_RESULT_FRAME_LENGTH];
  uint16_t frame_length = 0U;

  if (TestTask_ResultWireEncode(
          &source, frame, sizeof(frame), &frame_length) !=
      TEST_TASK_RESULT_WIRE_OK)
  {
    return 1;
  }
  if ((frame_length != TEST_TASK_RESULT_FRAME_LENGTH) ||
      (TestTask_ResultWireDecode(frame, frame_length, &decoded) !=
       TEST_TASK_RESULT_WIRE_OK))
  {
    return 2;
  }
  if ((decoded.request_id != source.request_id) ||
      (decoded.timestamp_ms != source.timestamp_ms) ||
      (decoded.error_code != source.error_code) ||
      (decoded.record_count != source.record_count) ||
      (decoded.status != source.status) ||
      (decoded.stage != source.stage) ||
      (decoded.safety_state != source.safety_state) ||
      (decoded.flags != source.flags))
  {
    return 3;
  }

  frame[22] ^= 1U;
  if (TestTask_ResultWireDecode(frame, frame_length, &decoded) !=
      TEST_TASK_RESULT_WIRE_ERROR_CRC)
  {
    return 4;
  }
  frame[22] ^= 1U;

  frame[0] ^= 1U;
  if (TestTask_ResultWireDecode(frame, frame_length, &decoded) !=
      TEST_TASK_RESULT_WIRE_ERROR_MAGIC)
  {
    return 5;
  }
  frame[0] ^= 1U;

  frame[2]++;
  if (TestTask_ResultWireDecode(frame, frame_length, &decoded) !=
      TEST_TASK_RESULT_WIRE_ERROR_VERSION)
  {
    return 6;
  }
  frame[2]--;

  frame[3]++;
  if (TestTask_ResultWireDecode(frame, frame_length, &decoded) !=
      TEST_TASK_RESULT_WIRE_ERROR_TYPE)
  {
    return 7;
  }
  frame[3]--;

  frame[4]++;
  if (TestTask_ResultWireDecode(frame, frame_length, &decoded) !=
      TEST_TASK_RESULT_WIRE_ERROR_PAYLOAD_LENGTH)
  {
    return 8;
  }
  frame[4]--;

  if (TestTask_ResultWireDecode(frame, frame_length - 1U, &decoded) !=
      TEST_TASK_RESULT_WIRE_ERROR_LENGTH)
  {
    return 9;
  }
  if (TestTask_ResultWireEncode(
          &source,
          frame,
          TEST_TASK_RESULT_FRAME_LENGTH - 1U,
          &frame_length) != TEST_TASK_RESULT_WIRE_ERROR_CAPACITY)
  {
    return 10;
  }

  decoded = source;
  decoded.error_code = 0U;
  if (TestTask_ResultWireValidate(&decoded) !=
      TEST_TASK_RESULT_WIRE_ERROR_SEMANTICS)
  {
    return 11;
  }

  {
    const TestTask_StopDescriptorTypeDef stop_source = {
        7601UL, 64284UL, 1000, -487, -55, 4194U, 31U,
        1U, 2U, 1U, 0UL, 2602UL, 487U, 100UL};
    TestTask_StopDescriptorTypeDef stop_decoded;
    uint8_t stop_frame[TEST_TASK_STOP_FRAME_LENGTH];

    if ((TestTask_StopWireEncode(&stop_source, stop_frame,
                                 sizeof(stop_frame), &frame_length) !=
         TEST_TASK_RESULT_WIRE_OK) ||
        (frame_length != TEST_TASK_STOP_FRAME_LENGTH) ||
        (stop_frame[3] != TEST_TASK_STOP_WIRE_TYPE) ||
        (stop_frame[40] != 0xCBU) || (stop_frame[41] != 0xB9U) ||
        (TestTask_StopWireDecode(stop_frame, frame_length, &stop_decoded) !=
         TEST_TASK_RESULT_WIRE_OK) ||
        (stop_decoded.request_id != 7601UL) ||
        (stop_decoded.feedback_speed_rpm != -487) ||
        (stop_decoded.observation_sample_count != 100UL))
    {
      return 12;
    }
    stop_frame[40] ^= 1U;
    if (TestTask_StopWireDecode(stop_frame, frame_length, &stop_decoded) !=
        TEST_TASK_RESULT_WIRE_ERROR_CRC)
    {
      return 13;
    }
    stop_frame[40] ^= 1U;
    stop_frame[3] = (uint8_t)TEST_TASK_RESULT_WIRE_TYPE;
    if (TestTask_StopWireDecode(stop_frame, frame_length, &stop_decoded) !=
        TEST_TASK_RESULT_WIRE_ERROR_TYPE)
    {
      return 14;
    }
  }

  (void)printf("task_result_wire_tests=pass\n");
  return 0;
}
