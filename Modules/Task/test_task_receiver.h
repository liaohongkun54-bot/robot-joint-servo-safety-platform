/**
  ******************************************************************************
  * @file    test_task_receiver.h
  * @brief   Byte-stream receiver for fixed structured-task frames.
  *
  * The receiver is transport-agnostic.  UART, USB or another upper link may
  * feed bytes into it later; this module owns no peripheral and sends nothing.
  ******************************************************************************
  */

#ifndef __TEST_TASK_RECEIVER_H__
#define __TEST_TASK_RECEIVER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "test_task_wire_codec.h"
#include "control_status_wire_codec.h"
#include "telemetry_wire_codec.h"
#include "device_info_wire_codec.h"
#include "operator_wire_codec.h"

#define TEST_TASK_RECEIVER_DEFAULT_TIMEOUT_MS (100UL)

typedef enum
{
  TEST_TASK_RECEIVER_NO_EVENT = 0U,
  TEST_TASK_RECEIVER_TASK_READY = 1U,
  TEST_TASK_RECEIVER_FRAME_REJECTED = 2U,
  TEST_TASK_RECEIVER_FRAME_TIMEOUT = 3U,
  TEST_TASK_RECEIVER_TASK_PENDING = 4U,
  TEST_TASK_RECEIVER_ERROR_NULL_ARGUMENT = 5U,
  TEST_TASK_RECEIVER_STATUS_QUERY_READY = 6U,
  TEST_TASK_RECEIVER_STATUS_QUERY_REJECTED = 7U,
  TEST_TASK_RECEIVER_TELEMETRY_QUERY_READY = 8U,
  TEST_TASK_RECEIVER_TELEMETRY_QUERY_REJECTED = 9U,
  TEST_TASK_RECEIVER_INFO_QUERY_READY = 10U,
  TEST_TASK_RECEIVER_INFO_QUERY_REJECTED = 11U,
  TEST_TASK_RECEIVER_OPERATOR_READY = 12U,
  TEST_TASK_RECEIVER_OPERATOR_REJECTED = 13U
} TestTask_ReceiverResultTypeDef;

typedef struct
{
  uint32_t received_byte_count;
  uint32_t accepted_frame_count;
  uint32_t rejected_frame_count;
  uint32_t timeout_count;
  uint32_t pending_drop_count;
  uint32_t last_frame_tick_ms;
  uint32_t last_request_id;
  uint32_t status_query_count;
  uint32_t status_query_reject_count;
  uint32_t status_query_drop_count;
  uint32_t telemetry_query_count;
  uint32_t telemetry_query_reject_count;
  uint32_t telemetry_query_drop_count;
  uint32_t info_query_count;
  uint32_t info_query_reject_count;
  uint32_t info_query_drop_count;
  uint32_t operator_count;
  uint32_t operator_reject_count;
  uint32_t operator_drop_count;
  TestTask_WireErrorTypeDef last_wire_error;
  TestTask_ValidationErrorTypeDef last_schema_error;
} TestTask_ReceiverSnapshotTypeDef;

typedef struct
{
  uint8_t frame[OPERATOR_WIRE_LENGTH];
  uint16_t frame_index;
  uint32_t frame_start_tick_ms;
  uint32_t frame_timeout_ms;
  uint8_t receiving;
  uint8_t task_pending;
  uint8_t status_query_pending;
  uint8_t telemetry_query_pending;
  uint8_t info_query_pending;
  uint8_t operator_pending;
  uint8_t operator_stop_pending;
  OperatorWire_Request pending_operator;
  OperatorWire_Request pending_operator_stop;
  TestTask_DescriptorTypeDef pending_task;
  ControlStatus_QueryTypeDef pending_status_query;
  Telemetry_QueryTypeDef pending_telemetry_query;
  DeviceInfo_QueryTypeDef pending_info_query;
  TestTask_ReceiverSnapshotTypeDef snapshot;
} TestTask_ReceiverTypeDef;

/** Initialize caller-owned receiver storage.  timeout_ms=0 selects 100 ms. */
void TestTask_ReceiverInit(
    TestTask_ReceiverTypeDef *receiver,
    uint32_t timeout_ms);

/** Feed one byte.  A ready task must later be removed with TakeTask(). */
TestTask_ReceiverResultTypeDef TestTask_ReceiverPushByte(
    TestTask_ReceiverTypeDef *receiver,
    uint8_t byte,
    uint32_t now_ms);

/** Reject an incomplete frame after its configured receive timeout. */
TestTask_ReceiverResultTypeDef TestTask_ReceiverService(
    TestTask_ReceiverTypeDef *receiver,
    uint32_t now_ms);

/** Discard only the partial input frame; pending validated task is preserved. */
uint8_t TestTask_ReceiverAbortFrame(TestTask_ReceiverTypeDef *receiver);

/** Copy and remove the single pending task; returns 1 on success, otherwise 0. */
uint8_t TestTask_ReceiverTakeTask(
    TestTask_ReceiverTypeDef *receiver,
    TestTask_DescriptorTypeDef *task);

uint8_t TestTask_ReceiverTakeStatusQuery(
    TestTask_ReceiverTypeDef *receiver,
    ControlStatus_QueryTypeDef *query);

uint8_t TestTask_ReceiverTakeTelemetryQuery(
    TestTask_ReceiverTypeDef *receiver,
    Telemetry_QueryTypeDef *query);

uint8_t TestTask_ReceiverTakeInfoQuery(
    TestTask_ReceiverTypeDef *receiver, DeviceInfo_QueryTypeDef *query);

/* STOP/CLOSE has a dedicated slot and is returned before ordinary controls.
 * Caller must use the same existing IRQ critical section as TakeTask. */
uint8_t TestTask_ReceiverTakeOperator(TestTask_ReceiverTypeDef *receiver,
    OperatorWire_Request *request);

#ifdef __cplusplus
}
#endif

#endif /* __TEST_TASK_RECEIVER_H__ */
