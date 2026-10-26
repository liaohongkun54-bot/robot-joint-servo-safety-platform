/**
  ******************************************************************************
  * @file    task_uart_adapter.h
  * @brief   USART adapter for structured-task RX and bounded result TX.
  *
  * This module owns one receive byte and one TestTask receiver instance.  It
  * does not own CubeMX initialization or the global HAL callback functions.
  * Result transmission accepts only validated task and stop descriptors;
  * it cannot be used as an arbitrary-byte or CAN-frame interface.
  ******************************************************************************
  */

#ifndef __TASK_UART_ADAPTER_H__
#define __TASK_UART_ADAPTER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "stm32f4xx_hal.h"
#include "test_task_receiver.h"
#include "test_task_result_wire_codec.h"
#include "control_status_wire_codec.h"

typedef enum
{
  TASK_UART_ADAPTER_OK = 0U,
  TASK_UART_ADAPTER_RX_ARMED = 1U,
  TASK_UART_ADAPTER_TASK_READY = 2U,
  TASK_UART_ADAPTER_FRAME_REJECTED = 3U,
  TASK_UART_ADAPTER_FRAME_TIMEOUT = 4U,
  TASK_UART_ADAPTER_TASK_PENDING = 5U,
  TASK_UART_ADAPTER_UART_IGNORED = 6U,
  TASK_UART_ADAPTER_UART_ERROR = 7U,
  TASK_UART_ADAPTER_HAL_ERROR = 8U,
  TASK_UART_ADAPTER_ERROR_NULL_ARGUMENT = 9U,
  TASK_UART_ADAPTER_ERROR_NOT_INITIALIZED = 10U,
  TASK_UART_ADAPTER_STATUS_QUERY_READY = 11U,
  TASK_UART_ADAPTER_STATUS_QUERY_REJECTED = 12U,
  TASK_UART_ADAPTER_TELEMETRY_QUERY_READY = 13U,
  TASK_UART_ADAPTER_TELEMETRY_QUERY_REJECTED = 14U,
  TASK_UART_ADAPTER_INFO_QUERY_READY = 15U,
  TASK_UART_ADAPTER_INFO_QUERY_REJECTED = 16U,
  TASK_UART_ADAPTER_OPERATOR_READY = 17U,
  TASK_UART_ADAPTER_OPERATOR_REJECTED = 18U
} TaskUart_AdapterResultTypeDef;

typedef enum
{
  TASK_UART_TX_IDLE = 0U,
  TASK_UART_TX_STARTED = 1U,
  TASK_UART_TX_COMPLETE = 2U,
  TASK_UART_TX_BUSY = 3U,
  TASK_UART_TX_ENCODE_ERROR = 4U,
  TASK_UART_TX_HAL_ERROR = 5U,
  TASK_UART_TX_UART_IGNORED = 6U,
  TASK_UART_TX_ERROR_NULL_ARGUMENT = 7U,
  TASK_UART_TX_ERROR_NOT_INITIALIZED = 8U,
  TASK_UART_TX_QUEUED = 9U
} TaskUart_TxResultTypeDef;

typedef struct
{
  uint32_t rx_complete_count;
  uint32_t arm_count;
  uint32_t arm_error_count;
  uint32_t uart_error_count;
  uint32_t ignored_uart_count;
  uint32_t tx_start_count;
  uint32_t tx_complete_count;
  uint32_t tx_error_count;
  uint32_t tx_drop_count;
  uint32_t transport_result_count;
  uint32_t schema_result_count;
  uint32_t transport_result_drop_count;
  uint32_t stop_queued_count;
  uint32_t stop_tx_start_count;
  uint32_t stop_tx_complete_count;
  uint32_t stop_tx_error_count;
  uint32_t stop_queue_full_count;
  uint32_t status_queued_count;
  uint32_t status_tx_complete_count;
  uint32_t status_drop_count;
  uint32_t telemetry_queued_count;
  uint32_t telemetry_tx_complete_count;
  uint32_t telemetry_drop_count;
  uint32_t result_queued_behind_telemetry_count;
  uint32_t info_queued_count;
  uint32_t info_tx_complete_count;
  uint32_t info_drop_count;
  uint32_t result_queued_behind_info_count;
  uint32_t operator_queued_count;
  uint32_t operator_drop_count;
  uint32_t operator_tx_complete_count;
  uint32_t result_queued_behind_operator_count;
  uint32_t last_hal_error;
  uint32_t last_request_id;
  uint32_t last_tx_request_id;
  uint16_t last_tx_frame_length;
  HAL_StatusTypeDef last_hal_status;
  HAL_StatusTypeDef last_tx_hal_status;
  TaskUart_AdapterResultTypeDef last_result;
  TaskUart_TxResultTypeDef last_tx_result;
  TestTask_ReceiverResultTypeDef last_receiver_result;
  TestTask_ResultWireErrorTypeDef last_tx_wire_error;
  ControlStatus_WireResultTypeDef last_status_wire_error;
  Telemetry_WireResultTypeDef last_telemetry_wire_error;
  DeviceInfo_WireResultTypeDef last_info_wire_error;
} TaskUart_AdapterSnapshotTypeDef;

typedef struct
{
  UART_HandleTypeDef *huart;
  TestTask_ReceiverTypeDef receiver;
  uint8_t rx_byte;
  uint8_t tx_frame[OPERATOR_WIRE_LENGTH];
  uint16_t tx_frame_length;
  TestTask_ResultDescriptorTypeDef transport_result;
  TestTask_ResultDescriptorTypeDef pending_result;
  TestTask_StopDescriptorTypeDef pending_stop;
  ControlStatus_ReplyTypeDef pending_status;
  Telemetry_ReplyTypeDef pending_telemetry;
  DeviceInfo_ReplyTypeDef pending_info;
  OperatorWire_Reply pending_operator;
  volatile uint8_t operator_pending;
  volatile uint8_t tx_operator_active;
  uint8_t initialized;
  volatile uint8_t rx_armed;
  volatile uint8_t tx_busy;
  volatile uint8_t transport_result_pending;
  volatile uint8_t result_pending;
  volatile uint8_t stop_pending;
  volatile uint8_t tx_stop_active;
  volatile uint8_t status_pending;
  volatile uint8_t tx_status_active;
  volatile uint8_t telemetry_pending;
  volatile uint8_t tx_telemetry_active;
  volatile uint8_t info_pending;
  volatile uint8_t tx_info_active;
  TaskUart_AdapterSnapshotTypeDef snapshot;
} TaskUart_AdapterTypeDef;

extern TaskUart_AdapterTypeDef g_task_uart_adapter;

/** Bind the adapter to one CubeMX UART handle; does not start hardware RX. */
TaskUart_AdapterResultTypeDef TaskUart_AdapterInit(
    UART_HandleTypeDef *huart,
    uint32_t frame_timeout_ms);

/** Arm the first one-byte interrupt receive after MX_USARTx_UART_Init(). */
TaskUart_AdapterResultTypeDef TaskUart_AdapterStart(void);

/** Forward from HAL_UART_RxCpltCallback(); safe for unrelated UART handles. */
TaskUart_AdapterResultTypeDef TaskUart_AdapterOnRxComplete(
    UART_HandleTypeDef *huart,
    uint32_t now_ms);

/** Forward from HAL_UART_ErrorCallback(); partial frame is discarded. */
TaskUart_AdapterResultTypeDef TaskUart_AdapterOnError(
    UART_HandleTypeDef *huart);

/** Forward from HAL_UART_TxCpltCallback(); safe for unrelated handles. */
TaskUart_TxResultTypeDef TaskUart_AdapterOnTxComplete(
    UART_HandleTypeDef *huart);

/** Call periodically to detect frame timeout and recover after UART errors. */
TaskUart_AdapterResultTypeDef TaskUart_AdapterService(uint32_t now_ms);

/** Copy and remove one validated task from the receiver-owned pending slot. */
uint8_t TaskUart_AdapterTakeTask(TestTask_DescriptorTypeDef *task);
uint8_t TaskUart_AdapterTakeOperator(OperatorWire_Request *request);
TaskUart_TxResultTypeDef TaskUart_AdapterQueueOperator(const OperatorWire_Reply *reply);

/** Encode and start one interrupt-driven, fixed 24-byte task-result frame. */
TaskUart_TxResultTypeDef TaskUart_AdapterSendResult(
    const TestTask_ResultDescriptorTypeDef *result);

/** Copy one completed stop snapshot into a bounded pending slot. */
TaskUart_TxResultTypeDef TaskUart_AdapterQueueStop(
    const TestTask_StopDescriptorTypeDef *stop);

/** Take one validated read-only query; it never changes Safety or Motion. */
uint8_t TaskUart_AdapterTakeStatusQuery(ControlStatus_QueryTypeDef *query);

/** Queue a bounded status snapshot; STOP transmission retains priority. */
TaskUart_TxResultTypeDef TaskUart_AdapterQueueStatus(
    const ControlStatus_ReplyTypeDef *status);

/** Take one read-only feedback query; never changes motion or Safety. */
uint8_t TaskUart_AdapterTakeTelemetryQuery(Telemetry_QueryTypeDef *query);

/** Queue one sampled feedback reply below STOP and status priority. */
TaskUart_TxResultTypeDef TaskUart_AdapterQueueTelemetry(
    const Telemetry_ReplyTypeDef *telemetry);

#ifdef __cplusplus
}
#endif

#endif /* __TASK_UART_ADAPTER_H__ */
