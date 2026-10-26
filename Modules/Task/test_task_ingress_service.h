/**
  ******************************************************************************
  * @file    test_task_ingress_service.h
  * @brief   Main-loop service from UART pending task to safe runtime admission.
  *
  * The ISR-facing UART adapter only queues one validated descriptor.  This
  * service removes it in main-loop context, snapshots Safety, submits it to
  * TestTask_Runtime and retains append-only audit records.
  ******************************************************************************
  */

#ifndef __TEST_TASK_INGRESS_SERVICE_H__
#define __TEST_TASK_INGRESS_SERVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "task_uart_adapter.h"
#include "test_task_runtime.h"

typedef enum
{
  TEST_TASK_INGRESS_IDLE = 0U,
  TEST_TASK_INGRESS_NO_TASK = 1U,
  TEST_TASK_INGRESS_TASK_ACCEPTED = 2U,
  TEST_TASK_INGRESS_TASK_REJECTED = 3U,
  TEST_TASK_INGRESS_ERROR_NULL_SAFETY = 4U,
  TEST_TASK_INGRESS_ERROR_RECORD = 5U,
  TEST_TASK_INGRESS_ERROR_RESULT_TX = 6U
} TestTask_IngressResultTypeDef;

typedef struct
{
  TestTask_IngressResultTypeDef last_result;
  TestTask_RuntimeResultTypeDef last_runtime_result;
  TestTask_ValidationErrorTypeDef last_schema_error;
  uint32_t service_count;
  uint32_t no_task_count;
  uint32_t processed_count;
  uint32_t accepted_count;
  uint32_t rejected_count;
  uint32_t record_error_count;
  uint32_t result_response_count;
  uint32_t result_response_error_count;
  uint32_t stop_response_queued_count;
  uint32_t stop_response_error_count;
  uint32_t status_query_count;
  uint32_t status_response_error_count;
  uint32_t last_request_id;
  uint32_t last_service_ms;
  uint16_t record_count;
  uint8_t service_tick_valid;
  TaskUart_TxResultTypeDef last_result_tx_status;
  TaskUart_TxResultTypeDef last_stop_tx_status;
} TestTask_IngressSnapshotTypeDef;

extern volatile TestTask_IngressSnapshotTypeDef g_test_task_ingress_snapshot;
extern TestRecord_BufferTypeDef g_test_task_ingress_records;
extern TestTask_DescriptorTypeDef g_test_task_ingress_last_task;
extern TestTask_ResultDescriptorTypeDef g_test_task_ingress_last_result;

/** Reset the ingress audit buffer and runtime counters. */
void TestTask_IngressInit(void);

/**
  * @brief Process at most one UART-pending task in main-loop context.
  * @note  NO_TASK is a normal polling result and does not erase last_result.
  */
TestTask_IngressResultTypeDef TestTask_IngressService(
    const volatile Safety_SnapshotTypeDef *safety,
    uint32_t now_ms);

/** 成功零帧后，用已预留的槽追加一次动作结束记录。 */
uint8_t TestTask_IngressRecordMotionStop(
    const volatile Safety_SnapshotTypeDef *safety);

#ifdef __cplusplus
}
#endif

#endif /* __TEST_TASK_INGRESS_SERVICE_H__ */
