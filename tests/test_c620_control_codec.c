/**
  ******************************************************************************
  * @file    test_c620_control_codec.c
  * @brief   Host-only tests for the ID 1 C620 control-byte encoder.
  *
  * This test never opens hardware and never transmits CAN.
  ******************************************************************************
  */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "c620_control_codec.h"

static void AssertPayloadTailIsZero(const uint8_t data[8])
{
  uint8_t index;

  for (index = 2U; index < 8U; ++index)
  {
    assert(data[index] == 0U);
  }
}

static void TestFrameConstants(void)
{
  assert(C620_CONTROL_STD_ID == 0x200U);
  assert(C620_CONTROL_DATA_LENGTH == 8U);
}

static void TestZeroAndSigns(void)
{
  uint8_t data[8] = {0xA5U, 0xA5U, 0xA5U, 0xA5U,
                     0xA5U, 0xA5U, 0xA5U, 0xA5U};

  assert(C620_ControlCodec_EncodeCurrent(0, data) ==
         C620_CONTROL_CODEC_OK);
  assert(data[0] == 0x00U);
  assert(data[1] == 0x00U);
  AssertPayloadTailIsZero(data);

  assert(C620_ControlCodec_EncodeCurrent(1, data) ==
         C620_CONTROL_CODEC_OK);
  assert(data[0] == 0x00U);
  assert(data[1] == 0x01U);

  assert(C620_ControlCodec_EncodeCurrent(-1, data) ==
         C620_CONTROL_CODEC_OK);
  assert(data[0] == 0xFFU);
  assert(data[1] == 0xFFU);
}

static void TestBounds(void)
{
  uint8_t data[8] = {0xA5U, 0xA5U, 0xA5U, 0xA5U,
                     0xA5U, 0xA5U, 0xA5U, 0xA5U};

  assert(C620_ControlCodec_EncodeCurrent(16384, data) ==
         C620_CONTROL_CODEC_OK);
  assert(data[0] == 0x40U);
  assert(data[1] == 0x00U);
  AssertPayloadTailIsZero(data);

  assert(C620_ControlCodec_EncodeCurrent(-16384, data) ==
         C620_CONTROL_CODEC_OK);
  assert(data[0] == 0xC0U);
  assert(data[1] == 0x00U);
  AssertPayloadTailIsZero(data);

  assert(C620_ControlCodec_EncodeCurrent(16385, data) ==
         C620_CONTROL_CODEC_CURRENT_RANGE);
  assert(data[0] == 0xC0U);
  assert(data[1] == 0x00U);
  assert(C620_ControlCodec_EncodeCurrent(-16385, data) ==
         C620_CONTROL_CODEC_CURRENT_RANGE);
  assert(data[0] == 0xC0U);
  assert(data[1] == 0x00U);
}

static void TestNull(void)
{
  assert(C620_ControlCodec_EncodeCurrent(0, (uint8_t *)0) ==
         C620_CONTROL_CODEC_NULL_ARGUMENT);
}

int main(void)
{
  TestFrameConstants();
  TestZeroAndSigns();
  TestBounds();
  TestNull();
  (void)printf("c620_control_codec_tests=pass\n");
  return 0;
}
