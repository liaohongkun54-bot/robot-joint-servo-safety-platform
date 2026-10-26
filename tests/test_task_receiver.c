#include <stdio.h>

#include "test_task_receiver.h"

static int FeedFrame(
    TestTask_ReceiverTypeDef *receiver,
    const uint8_t *frame,
    uint16_t length,
    uint32_t start_ms,
    TestTask_ReceiverResultTypeDef expected)
{
  uint16_t index;
  TestTask_ReceiverResultTypeDef result = TEST_TASK_RECEIVER_NO_EVENT;

  for (index = 0U; index < length; index++)
  {
    result = TestTask_ReceiverPushByte(receiver, frame[index], start_ms + index);
  }
  return (result == expected) ? 0 : 1;
}

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
  TestTask_ReceiverTypeDef receiver;
  TestTask_DescriptorTypeDef task;
  uint8_t frame[TEST_TASK_WIRE_FRAME_LENGTH];
  uint16_t frame_length = 0U;

  TestTask_ReceiverInit(&receiver, 100UL);
  if (TestTask_WireEncode(&source, frame, sizeof(frame), &frame_length) !=
      TEST_TASK_WIRE_OK)
  {
    return 1;
  }

  (void)TestTask_ReceiverPushByte(&receiver, 0x00U, 0UL);
  (void)TestTask_ReceiverPushByte(&receiver, 0x4BU, 1UL);
  (void)TestTask_ReceiverPushByte(&receiver, 0x00U, 2UL);
  if (FeedFrame(&receiver, frame, frame_length, 10UL,
                TEST_TASK_RECEIVER_TASK_READY) != 0)
  {
    return 2;
  }
  if ((TestTask_ReceiverTakeTask(&receiver, &task) == 0U) ||
      (task.request_id != source.request_id))
  {
    return 3;
  }

  frame[20] ^= 0x01U;
  if (FeedFrame(&receiver, frame, frame_length, 100UL,
                TEST_TASK_RECEIVER_FRAME_REJECTED) != 0)
  {
    return 4;
  }
  frame[20] ^= 0x01U;

  (void)TestTask_ReceiverPushByte(&receiver, frame[0], 200UL);
  (void)TestTask_ReceiverPushByte(&receiver, frame[1], 201UL);
  if (TestTask_ReceiverService(&receiver, 300UL) !=
      TEST_TASK_RECEIVER_FRAME_TIMEOUT)
  {
    return 5;
  }

  if (FeedFrame(&receiver, frame, frame_length, 400UL,
                TEST_TASK_RECEIVER_TASK_READY) != 0)
  {
    return 6;
  }
  if (FeedFrame(&receiver, frame, frame_length, 500UL,
                TEST_TASK_RECEIVER_TASK_PENDING) != 0)
  {
    return 7;
  }

  if ((receiver.snapshot.accepted_frame_count != 2UL) ||
      (receiver.snapshot.rejected_frame_count != 2UL) ||
      (receiver.snapshot.timeout_count != 1UL) ||
      (receiver.snapshot.pending_drop_count != 1UL))
  {
    return 8;
  }

  /* A type 5 query is read-only and never enters the task pending slot. */
  {
    static const uint8_t telemetry_query[TELEMETRY_QUERY_LENGTH] = {
        0x4B, 0x54, 0x01, 0x05, 0x16, 0x00, 0x78, 0x56,
        0x34, 0x12, 0xF0, 0xDE, 0xBC, 0x9A, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x3B};
    Telemetry_QueryTypeDef query;
    uint8_t corrupted[TELEMETRY_QUERY_LENGTH];
    uint16_t i;
    TestTask_ReceiverInit(&receiver, 100UL);
    if (FeedFrame(&receiver, telemetry_query, sizeof(telemetry_query), 1000UL,
                  TEST_TASK_RECEIVER_TELEMETRY_QUERY_READY) != 0)
    {
      return 9;
    }
    if ((TestTask_ReceiverTakeTelemetryQuery(&receiver, &query) == 0U) ||
        (query.request_id != 0x12345678UL) ||
        (query.nonce != 0x9ABCDEF0UL) ||
        (receiver.task_pending != 0U))
    {
      return 10;
    }
    for (i = 0U; i < sizeof(corrupted); i++)
    {
      corrupted[i] = telemetry_query[i];
    }
    corrupted[10] ^= 1U;
    if (FeedFrame(&receiver, corrupted, sizeof(corrupted), 1100UL,
                  TEST_TASK_RECEIVER_TELEMETRY_QUERY_REJECTED) != 0)
    {
      return 11;
    }
    if ((receiver.telemetry_query_pending != 0U) ||
        (receiver.task_pending != 0U) ||
        (receiver.snapshot.telemetry_query_reject_count != 1UL))
    {
      return 12;
    }
  }

  (void)printf("task_receiver_host_tests=pass\n");
  return 0;
}
