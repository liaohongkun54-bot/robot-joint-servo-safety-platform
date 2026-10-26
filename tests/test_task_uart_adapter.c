#include <stdio.h>

#include "task_uart_adapter.h"
#include "test_task_wire_codec.h"

static uint8_t *g_fake_rx_target;
static HAL_StatusTypeDef g_fake_receive_status = HAL_OK;
static HAL_StatusTypeDef g_fake_transmit_status = HAL_OK;
static uint8_t g_fake_tx_frame[DEVICE_INFO_REPLY_LENGTH];
static uint16_t g_fake_tx_length;

HAL_StatusTypeDef HAL_UART_Receive_IT(
    UART_HandleTypeDef *huart,
    uint8_t *data,
    uint16_t size)
{
  (void)huart;
  if (size != 1U)
  {
    return HAL_ERROR;
  }
  g_fake_rx_target = data;
  return g_fake_receive_status;
}

uint32_t HAL_UART_GetError(const UART_HandleTypeDef *huart)
{
  return huart->ErrorCode;
}

HAL_StatusTypeDef HAL_UART_Transmit_IT(
    UART_HandleTypeDef *huart,
    const uint8_t *data,
    uint16_t size)
{
  uint16_t index;

  (void)huart;
  if (g_fake_transmit_status != HAL_OK)
  {
    return g_fake_transmit_status;
  }
  g_fake_tx_length = size;
  for (index = 0U; index < size; index++)
  {
    g_fake_tx_frame[index] = data[index];
  }
  return HAL_OK;
}

static TaskUart_AdapterResultTypeDef FeedByte(
    UART_HandleTypeDef *huart,
    uint8_t byte,
    uint32_t now_ms)
{
  *g_fake_rx_target = byte;
  return TaskUart_AdapterOnRxComplete(huart, now_ms);
}

static uint16_t TestCrc16(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  uint16_t index;
  uint8_t bit;

  for (index = 0U; index < length; index++)
  {
    crc ^= (uint16_t)data[index] << 8U;
    for (bit = 0U; bit < 8U; bit++)
    {
      crc = ((crc & 0x8000U) != 0U) ?
            (uint16_t)((crc << 1U) ^ 0x1021U) :
            (uint16_t)(crc << 1U);
    }
  }
  return crc;
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
  UART_HandleTypeDef task_uart = {0};
  UART_HandleTypeDef other_uart = {0};
  TestTask_DescriptorTypeDef decoded;
  uint8_t frame[TEST_TASK_WIRE_FRAME_LENGTH];
  uint16_t frame_length = 0U;
  uint16_t index;
  TaskUart_AdapterResultTypeDef result = TASK_UART_ADAPTER_OK;
  TestTask_ResultDescriptorTypeDef result_source = {
      1001UL,
      50UL,
      0x00E2U,
      1U,
      TEST_TASK_RESULT_STATUS_REJECTED,
      TEST_TASK_RESULT_STAGE_SAFETY,
      0U,
      TEST_TASK_RESULT_FLAG_RECORD_APPENDED |
          TEST_TASK_RESULT_FLAG_OUTPUT_UNCHANGED};
  TestTask_ResultDescriptorTypeDef result_decoded;

  if (TaskUart_AdapterInit(&task_uart, 100UL) != TASK_UART_ADAPTER_OK)
  {
    return 1;
  }
  if (TaskUart_AdapterStart() != TASK_UART_ADAPTER_RX_ARMED)
  {
    return 2;
  }
  if (TaskUart_AdapterOnRxComplete(&other_uart, 0UL) !=
      TASK_UART_ADAPTER_UART_IGNORED)
  {
    return 3;
  }
  if (TestTask_WireEncode(&source, frame, sizeof(frame), &frame_length) !=
      TEST_TASK_WIRE_OK)
  {
    return 4;
  }

  for (index = 0U; index < frame_length; index++)
  {
    result = FeedByte(&task_uart, frame[index], (uint32_t)index);
  }
  if ((result != TASK_UART_ADAPTER_TASK_READY) ||
      (TaskUart_AdapterTakeTask(&decoded) == 0U) ||
      (decoded.request_id != source.request_id))
  {
    return 5;
  }

  if ((TaskUart_AdapterSendResult(&result_source) != TASK_UART_TX_STARTED) ||
      (g_fake_tx_length != TEST_TASK_RESULT_FRAME_LENGTH) ||
      (TestTask_ResultWireDecode(
          g_fake_tx_frame,
          g_fake_tx_length,
          &result_decoded) != TEST_TASK_RESULT_WIRE_OK) ||
      (result_decoded.request_id != result_source.request_id))
  {
    return 6;
  }
  if (TaskUart_AdapterSendResult(&result_source) != TASK_UART_TX_BUSY)
  {
    return 7;
  }
  if (TaskUart_AdapterOnTxComplete(&task_uart) != TASK_UART_TX_COMPLETE)
  {
    return 8;
  }

  frame[27] = 0U;
  {
    uint16_t crc = TestCrc16(frame, 28U);
    frame[28] = (uint8_t)(crc & 0xFFU);
    frame[29] = (uint8_t)(crc >> 8U);
  }
  for (index = 0U; index < frame_length; index++)
  {
    result = FeedByte(&task_uart, frame[index], 40UL + (uint32_t)index);
  }
  if ((result != TASK_UART_ADAPTER_FRAME_REJECTED) ||
      (TaskUart_AdapterService(75UL) != TASK_UART_ADAPTER_OK) ||
      (TestTask_ResultWireDecode(
          g_fake_tx_frame,
          g_fake_tx_length,
          &result_decoded) != TEST_TASK_RESULT_WIRE_OK) ||
      (result_decoded.request_id != 1001UL) ||
      (result_decoded.stage != TEST_TASK_RESULT_STAGE_SCHEMA) ||
      (result_decoded.error_code != TEST_TASK_ERROR_PERMISSION))
  {
    return 9;
  }
  (void)TaskUart_AdapterOnTxComplete(&task_uart);

  frame[27] = (uint8_t)TEST_TASK_PERMISSION_OPERATOR;
  {
    uint16_t crc = TestCrc16(frame, 28U);
    frame[28] = (uint8_t)(crc & 0xFFU);
    frame[29] = (uint8_t)(crc >> 8U);
  }
  frame[28] ^= 0x01U;
  for (index = 0U; index < frame_length; index++)
  {
    result = FeedByte(&task_uart, frame[index], 50UL + (uint32_t)index);
  }
  frame[28] ^= 0x01U;
  if ((result != TASK_UART_ADAPTER_FRAME_REJECTED) ||
      (TaskUart_AdapterService(90UL) != TASK_UART_ADAPTER_OK) ||
      (g_fake_tx_length != TEST_TASK_RESULT_FRAME_LENGTH) ||
      (TestTask_ResultWireDecode(
          g_fake_tx_frame,
          g_fake_tx_length,
          &result_decoded) != TEST_TASK_RESULT_WIRE_OK) ||
      (result_decoded.request_id != 0UL) ||
      (result_decoded.stage != TEST_TASK_RESULT_STAGE_TRANSPORT) ||
      (result_decoded.error_code != TEST_TASK_WIRE_ERROR_CRC))
  {
    return 10;
  }
  (void)TaskUart_AdapterOnTxComplete(&task_uart);

  (void)FeedByte(&task_uart, frame[0], 100UL);
  (void)FeedByte(&task_uart, frame[1], 101UL);
  if ((TaskUart_AdapterService(201UL) !=
       TASK_UART_ADAPTER_FRAME_TIMEOUT) ||
      (TestTask_ResultWireDecode(
          g_fake_tx_frame,
          g_fake_tx_length,
          &result_decoded) != TEST_TASK_RESULT_WIRE_OK) ||
      (result_decoded.stage != TEST_TASK_RESULT_STAGE_TRANSPORT) ||
      (result_decoded.error_code != TEST_TASK_WIRE_ERROR_LENGTH))
  {
    return 11;
  }
  (void)TaskUart_AdapterOnTxComplete(&task_uart);

  (void)FeedByte(&task_uart, frame[0], 300UL);
  (void)FeedByte(&task_uart, frame[1], 301UL);
  task_uart.ErrorCode = 0x08UL;
  if (TaskUart_AdapterOnError(&task_uart) != TASK_UART_ADAPTER_UART_ERROR)
  {
    return 12;
  }
  if (TaskUart_AdapterService(302UL) != TASK_UART_ADAPTER_OK)
  {
    return 13;
  }

  g_fake_receive_status = HAL_BUSY;
  g_task_uart_adapter.rx_armed = 0U;
  if (TaskUart_AdapterService(303UL) != TASK_UART_ADAPTER_HAL_ERROR)
  {
    return 14;
  }

  if ((g_task_uart_adapter.snapshot.rx_complete_count != 94UL) ||
      (g_task_uart_adapter.snapshot.uart_error_count != 1UL) ||
      (g_task_uart_adapter.snapshot.ignored_uart_count != 1UL) ||
      (g_task_uart_adapter.snapshot.arm_error_count != 1UL) ||
      (g_task_uart_adapter.snapshot.tx_start_count != 4UL) ||
      (g_task_uart_adapter.snapshot.tx_complete_count != 4UL) ||
      (g_task_uart_adapter.snapshot.tx_drop_count != 1UL) ||
      (g_task_uart_adapter.snapshot.transport_result_count != 2UL) ||
      (g_task_uart_adapter.snapshot.schema_result_count != 1UL))
  {
    return 15;
  }

  {
    const TestTask_StopDescriptorTypeDef stop_source = {
        7601UL, 64284UL, 1000, 487, 55, 4194U, 31U,
        1U, 2U, 1U, 0UL, 2602UL, 487U, 100UL};
    TestTask_StopDescriptorTypeDef stop_decoded;

    g_fake_receive_status = HAL_OK;
    if ((TaskUart_AdapterInit(&task_uart, 100UL) != TASK_UART_ADAPTER_OK) ||
        (TaskUart_AdapterStart() != TASK_UART_ADAPTER_RX_ARMED) ||
        (TaskUart_AdapterSendResult(&result_source) != TASK_UART_TX_STARTED) ||
        (TaskUart_AdapterQueueStop(&stop_source) != TASK_UART_TX_QUEUED) ||
        (TaskUart_AdapterQueueStop(&stop_source) != TASK_UART_TX_BUSY) ||
        (TaskUart_AdapterService(400UL) != TASK_UART_ADAPTER_OK) ||
        (g_fake_tx_length != TEST_TASK_RESULT_FRAME_LENGTH))
    {
      return 16;
    }
    if (TaskUart_AdapterOnTxComplete(&task_uart) != TASK_UART_TX_COMPLETE)
    {
      return 17;
    }
    g_fake_transmit_status = HAL_BUSY;
    if ((TaskUart_AdapterService(401UL) != TASK_UART_ADAPTER_OK) ||
        (g_task_uart_adapter.stop_pending != 1U) ||
        (g_task_uart_adapter.snapshot.last_tx_result != TASK_UART_TX_BUSY))
    {
      return 17;
    }
    g_fake_transmit_status = HAL_OK;
    if ((TaskUart_AdapterService(402UL) != TASK_UART_ADAPTER_OK) ||
        (g_fake_tx_length != TEST_TASK_STOP_FRAME_LENGTH) ||
        (TestTask_StopWireDecode(g_fake_tx_frame, g_fake_tx_length,
                                 &stop_decoded) != TEST_TASK_RESULT_WIRE_OK) ||
        (stop_decoded.request_id != 7601UL) ||
        (g_task_uart_adapter.snapshot.stop_queue_full_count != 1UL))
    {
      return 17;
    }
    if ((TaskUart_AdapterOnTxComplete(&task_uart) != TASK_UART_TX_COMPLETE) ||
        (g_task_uart_adapter.snapshot.stop_tx_complete_count != 1UL) ||
        (g_task_uart_adapter.stop_pending != 0U))
    {
      return 18;
    }
  }

  {
    static const uint8_t query_frame[CONTROL_STATUS_QUERY_LENGTH] = {
        0x4b, 0x54, 0x01, 0x04, 0x16, 0x00, 0x78, 0x56,
        0x34, 0x12, 0xf0, 0xde, 0xbc, 0x9a, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xb6, 0x7e
    };
    ControlStatus_QueryTypeDef query;
    ControlStatus_ReplyTypeDef status = {
        0x12345678UL, 0x9ABCDEF0UL, 0x10203040UL, 1U,
        2U, 0U, 1U, 0U, 0U, 5U, 0xE003UL
    };
    ControlStatus_ReplyTypeDef decoded_status;
    const TestTask_StopDescriptorTypeDef stop_source = {
        7601UL, 64284UL, 1000, 487, 55, 4194U, 31U,
        1U, 2U, 1U, 0UL, 2602UL, 487U, 100UL};
    TestTask_StopDescriptorTypeDef stop_decoded;
    uint16_t index;
    g_fake_tx_length = 0U;
    g_fake_receive_status = HAL_OK;
    g_fake_transmit_status = HAL_OK;
    if ((TaskUart_AdapterInit(&task_uart, 100UL) != TASK_UART_ADAPTER_OK) ||
        (TaskUart_AdapterStart() != TASK_UART_ADAPTER_RX_ARMED))
    {
      return 19;
    }
    for (index = 0U; index < sizeof(query_frame); index++)
    {
      TaskUart_AdapterResultTypeDef outcome =
          FeedByte(&task_uart, query_frame[index], 500UL + index);
      if ((index == sizeof(query_frame) - 1U) &&
          (outcome != TASK_UART_ADAPTER_STATUS_QUERY_READY))
      {
        return 20;
      }
    }
    if ((TaskUart_AdapterTakeStatusQuery(&query) == 0U) ||
        (query.request_id != status.request_id) ||
        (query.nonce != status.nonce) ||
        (TaskUart_AdapterTakeTask(&decoded) != 0U) ||
        (TaskUart_AdapterQueueStatus(&status) != TASK_UART_TX_QUEUED) ||
        (TaskUart_AdapterQueueStop(&stop_source) != TASK_UART_TX_QUEUED) ||
        (TaskUart_AdapterService(600UL) != TASK_UART_ADAPTER_OK) ||
        (g_fake_tx_length != TEST_TASK_STOP_FRAME_LENGTH) ||
        (TestTask_StopWireDecode(g_fake_tx_frame, g_fake_tx_length, &stop_decoded) !=
         TEST_TASK_RESULT_WIRE_OK) ||
        (stop_decoded.request_id != stop_source.request_id) ||
        (g_task_uart_adapter.status_pending != 1U) ||
        (TaskUart_AdapterOnTxComplete(&task_uart) != TASK_UART_TX_COMPLETE) ||
        (TaskUart_AdapterService(601UL) != TASK_UART_ADAPTER_OK) ||
        (g_fake_tx_length != CONTROL_STATUS_REPLY_LENGTH) ||
        (ControlStatus_ReplyDecode(g_fake_tx_frame, g_fake_tx_length, &decoded_status) !=
         CONTROL_STATUS_WIRE_OK) ||
        (decoded_status.request_id != status.request_id) ||
        (g_task_uart_adapter.snapshot.status_queued_count != 1UL))
    {
      return 21;
    }
    if ((TaskUart_AdapterOnTxComplete(&task_uart) != TASK_UART_TX_COMPLETE) ||
        (g_task_uart_adapter.snapshot.status_tx_complete_count != 1UL) ||
        (g_task_uart_adapter.snapshot.stop_tx_complete_count != 1UL))
    {
      return 22;
    }
  }

  {
    static const uint8_t query_frame[TELEMETRY_QUERY_LENGTH] = {
      0x4b,0x54,0x01,0x05,0x16,0x00,0x78,0x56,0x34,0x12,
      0xf0,0xde,0xbc,0x9a,0x01,0x00,0x00,0x00,0x00,0x00,
      0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x3b
    };
    Telemetry_QueryTypeDef query;
    Telemetry_ReplyTypeDef telemetry = {0};
    Telemetry_ReplyTypeDef decoded_telemetry;
    TestTask_ResultDescriptorTypeDef queued_result = {
        0x12345678UL, 777UL, 0x00E2U, 1U,
        TEST_TASK_RESULT_STATUS_REJECTED, TEST_TASK_RESULT_STAGE_SAFETY,
        0U, TEST_TASK_RESULT_FLAG_OUTPUT_UNCHANGED};
    TestTask_ResultDescriptorTypeDef decoded_result;
    const TestTask_StopDescriptorTypeDef stop_source = {
        7601UL, 64284UL, 1000, 487, 55, 4194U, 31U,
        1U, 2U, 1U, 0UL, 2602UL, 487U, 100UL};
    uint16_t query_index;

    if ((TaskUart_AdapterInit(&task_uart, 100UL) != TASK_UART_ADAPTER_OK) ||
        (TaskUart_AdapterStart() != TASK_UART_ADAPTER_RX_ARMED))
    {
      return 23;
    }
    for (query_index = 0U; query_index < sizeof(query_frame); query_index++)
    {
      result = FeedByte(&task_uart, query_frame[query_index],
                        800UL + query_index);
    }
    telemetry.request_id = 0x12345678UL;
    telemetry.nonce = 0x9ABCDEF0UL;
    telemetry.sample_tick_ms = 0x10203040UL;
    telemetry.last_rx_tick_ms = 0x1020303FUL;
    telemetry.valid_frame_count = 7UL;
    telemetry.encoder_raw = 4615U;
    telemetry.speed_rpm = -1964;
    telemetry.current_raw = 1114;
    telemetry.temperature_c = 30U;
    telemetry.link_state = 1U;
    telemetry.has_valid_feedback = 1U;
    telemetry.source_id = TELEMETRY_SOURCE_C620_CAN1;
    telemetry.feedback_std_id = TELEMETRY_C620_STD_ID;
    if ((result != TASK_UART_ADAPTER_TELEMETRY_QUERY_READY) ||
        (TaskUart_AdapterTakeTelemetryQuery(&query) == 0U) ||
        (query.request_id != telemetry.request_id) ||
        (query.nonce != telemetry.nonce) ||
        (TaskUart_AdapterQueueTelemetry(&telemetry) != TASK_UART_TX_QUEUED) ||
        (TaskUart_AdapterQueueTelemetry(&telemetry) != TASK_UART_TX_BUSY) ||
        (TaskUart_AdapterQueueStop(&stop_source) != TASK_UART_TX_QUEUED) ||
        (TaskUart_AdapterService(900UL) != TASK_UART_ADAPTER_OK) ||
        (g_fake_tx_length != TEST_TASK_STOP_FRAME_LENGTH))
    {
      return 24;
    }
    (void)TaskUart_AdapterOnTxComplete(&task_uart);
    if ((TaskUart_AdapterService(901UL) != TASK_UART_ADAPTER_OK) ||
        (g_fake_tx_length != TELEMETRY_REPLY_LENGTH) ||
        (Telemetry_ReplyDecode(g_fake_tx_frame, g_fake_tx_length,
                               &decoded_telemetry) != TELEMETRY_WIRE_OK) ||
        (decoded_telemetry.encoder_raw != 4615U) ||
        (TaskUart_AdapterSendResult(&queued_result) != TASK_UART_TX_QUEUED) ||
        (g_task_uart_adapter.result_pending != 1U))
    {
      return 25;
    }
    (void)TaskUart_AdapterOnTxComplete(&task_uart);
    if ((TaskUart_AdapterService(902UL) != TASK_UART_ADAPTER_OK) ||
        (g_fake_tx_length != TEST_TASK_RESULT_FRAME_LENGTH) ||
        (TestTask_ResultWireDecode(g_fake_tx_frame, g_fake_tx_length,
                                   &decoded_result) != TEST_TASK_RESULT_WIRE_OK) ||
        (decoded_result.request_id != queued_result.request_id) ||
        (g_task_uart_adapter.result_pending != 0U) ||
        (g_task_uart_adapter.snapshot.telemetry_tx_complete_count != 1UL) ||
        (g_task_uart_adapter.snapshot.result_queued_behind_telemetry_count != 1UL))
    {
      return 26;
    }
    (void)TaskUart_AdapterOnTxComplete(&task_uart);
  }

  {
    const uint8_t info_query[] = {0x4b,0x54,0x01,0x06,0x16,0x00,0x78,0x56,0x34,0x12,0xf0,0xde,0xbc,0x9a,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xde,0xf5};
    DeviceInfo_ReplyTypeDef info;
    uint8_t saved[48];
    TestTask_ResultDescriptorTypeDef admission = {
        1001UL, 990UL, 0x00E2U, 1U, TEST_TASK_RESULT_STATUS_REJECTED,
        TEST_TASK_RESULT_STAGE_SAFETY, 0U, TEST_TASK_RESULT_FLAG_OUTPUT_UNCHANGED};
    const TestTask_StopDescriptorTypeDef stop_source = {
        7601UL, 64284UL, 1000, 487, 55, 4194U, 31U,
        1U, 2U, 1U, 0UL, 2602UL, 487U, 100UL};
    uint16_t j;
    (void)TaskUart_AdapterInit(&task_uart,100UL);
    (void)TaskUart_AdapterStart();
    for(j=0U;j<30U;j++) { result=FeedByte(&task_uart,info_query[j],900UL+j); }
    if(result!=TASK_UART_ADAPTER_INFO_QUERY_READY) { return 27; }
    /* STOP wins over queued information; metadata is read only in Service. */
    if(g_task_uart_adapter.info_pending!=0U || TaskUart_AdapterQueueStop(&stop_source)!=TASK_UART_TX_QUEUED) { return 28; }
    (void)TaskUart_AdapterService(950UL);
    if(g_fake_tx_length!=TEST_TASK_STOP_FRAME_LENGTH || !g_task_uart_adapter.info_pending) { return 29; }
    (void)TaskUart_AdapterOnTxComplete(&task_uart);
    (void)TaskUart_AdapterService(951UL);
    if(DeviceInfo_ReplyDecode(g_fake_tx_frame,g_fake_tx_length,&info)!=DEVICE_INFO_WIRE_OK ||
       info.uid_words[0]!=0x12345678UL || info.nonce!=0x9ABCDEF0UL || info.timestamp_ms!=950UL) { return 30; }
    for(j=0U;j<48U;j++) { saved[j]=g_task_uart_adapter.tx_frame[j]; }
    /* RX error cannot release or mutate the HAL-owned info TX buffer. */
    (void)TaskUart_AdapterOnError(&task_uart);
    if(!g_task_uart_adapter.tx_busy || !g_task_uart_adapter.tx_info_active) { return 31; }
    for(j=0U;j<48U;j++) { if(saved[j]!=g_task_uart_adapter.tx_frame[j]) { return 32; } }
    if(TaskUart_AdapterSendResult(&admission)!=TASK_UART_TX_QUEUED ||
       TaskUart_AdapterSendResult(&admission)!=TASK_UART_TX_BUSY) { return 33; }
    (void)TaskUart_AdapterOnTxComplete(&task_uart);
    (void)TaskUart_AdapterService(990UL);
    if(g_fake_tx_length!=TEST_TASK_RESULT_FRAME_LENGTH ||
       g_task_uart_adapter.snapshot.result_queued_behind_info_count!=1UL ||
       g_task_uart_adapter.snapshot.info_tx_complete_count!=1UL) { return 34; }
    (void)TaskUart_AdapterOnTxComplete(&task_uart);
    /* Pending task/partial RX prevent starting low priority info TX. */
    for(j=0U;j<30U;j++) { (void)FeedByte(&task_uart,info_query[j],1000UL+j); }
    g_task_uart_adapter.receiver.task_pending=1U;
    (void)TaskUart_AdapterService(1040UL);
    if(g_task_uart_adapter.tx_busy || !g_task_uart_adapter.info_pending) { return 35; }
    g_task_uart_adapter.receiver.task_pending=0U;
    (void)FeedByte(&task_uart,0x4BU,1041UL);
    (void)TaskUart_AdapterService(1042UL);
    if(g_task_uart_adapter.tx_busy) { return 36; }
    (void)TestTask_ReceiverAbortFrame(&g_task_uart_adapter.receiver);
    g_fake_transmit_status=HAL_BUSY;
    (void)TaskUart_AdapterService(1043UL);
    if(g_task_uart_adapter.tx_busy || !g_task_uart_adapter.info_pending) { return 37; }
    g_fake_transmit_status=HAL_OK;
    (void)TaskUart_AdapterService(1044UL);
    if(!g_task_uart_adapter.tx_info_active) { return 38; }
    (void)TaskUart_AdapterOnTxComplete(&task_uart);
  }

  {
    TestTask_StopDescriptorTypeDef source = {
        8801UL, 1000UL, 1000, 200, 55, 4194U, 31U,
        4U, 2U, 1U, 0UL, 100UL, 200U, 10UL};
    TestTask_StopDescriptorTypeDef decoded;
    uint8_t reason;
    (void)TaskUart_AdapterInit(&task_uart, 100UL);
    for (reason = 4U; reason <= 5U; reason++)
    {
      source.stop_reason = reason;
      if (TaskUart_AdapterQueueStop(&source) != TASK_UART_TX_QUEUED) { return 39; }
      (void)TaskUart_AdapterService(1000UL);
      if (TestTask_StopWireDecode(g_fake_tx_frame, g_fake_tx_length, &decoded) != TEST_TASK_RESULT_WIRE_OK ||
          decoded.stop_reason != reason || decoded.request_id != source.request_id) { return 40; }
      (void)TaskUart_AdapterOnTxComplete(&task_uart);
    }
    source.stop_reason = 6U;
    if (TaskUart_AdapterQueueStop(&source) != TASK_UART_TX_ENCODE_ERROR) { return 41; }
    source.stop_reason = 0U;
    if (TaskUart_AdapterQueueStop(&source) != TASK_UART_TX_ENCODE_ERROR) { return 42; }
  }
  (void)printf("task_uart_adapter_host_tests=pass\n");
  return 0;
}
