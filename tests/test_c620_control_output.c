/**
  ******************************************************************************
  * @file    test_c620_control_output.c
  * @brief   Host-only tests for the fixed-slot C620 output boundary.
  ******************************************************************************
  */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "c620_control_output.h"

static uint32_t g_callback_count;

static uint8_t TestTransport(const uint8_t data[C620_CONTROL_DATA_LENGTH],
                             void *context)
{
  (void)data;
  (void)context;
  g_callback_count++;
  return C620_CONTROL_OUTPUT_TRANSPORT_OK;
}

static void TestEnabledNonzeroUsesFixedPayload(void)
{
  C620_ControlOutput_Init();
  g_callback_count = 0UL;

  assert(C620_ControlOutput_SendCurrent(100, TestTransport, (void *)0) ==
         C620_CONTROL_OUTPUT_OK);
  assert(g_callback_count == 1UL);
  assert(g_c620_control_output_snapshot.transport_attempt_count == 1UL);
  assert(g_c620_control_output_snapshot.successful_send_count == 1UL);
  assert(g_c620_control_output_snapshot.last_current_raw == 100);
  assert(g_c620_control_output_snapshot.last_was_zero == 0U);
  assert(g_c620_control_output_snapshot.last_payload[0] == 0x00U);
  assert(g_c620_control_output_snapshot.last_payload[1] == 0x64U);
}

static void TestExplicitZeroPath(void)
{
  C620_ControlOutput_Init();
  g_callback_count = 0UL;

  assert(C620_ControlOutput_SendZero(TestTransport, (void *)0) ==
         C620_CONTROL_OUTPUT_OK);
  assert(g_callback_count == 1UL);
  assert(g_c620_control_output_snapshot.last_current_raw == 0);
  assert(g_c620_control_output_snapshot.last_was_zero == 1U);
  assert(g_c620_control_output_snapshot.last_payload[0] == 0x00U);
  assert(g_c620_control_output_snapshot.last_payload[1] == 0x00U);
}

static void TestArgumentsAndRange(void)
{
    g_callback_count = 0UL;

    C620_ControlOutput_Init();
  assert(C620_ControlOutput_SendCurrent(0, (C620_ControlOutput_TransportFn)0,
                                        (void *)0) ==
         C620_CONTROL_OUTPUT_NULL_ARGUMENT);
  assert(C620_ControlOutput_SendCurrent(16385, TestTransport, (void *)0) ==
         C620_CONTROL_OUTPUT_CODEC_ERROR);
  assert(g_callback_count == 0UL);
}

int main(void)
{
  TestEnabledNonzeroUsesFixedPayload();
  TestExplicitZeroPath();
  TestArgumentsAndRange();
  (void)printf("c620_control_output_tests=pass\n");
  return 0;
}
