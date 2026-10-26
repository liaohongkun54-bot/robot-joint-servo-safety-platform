#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "device_info_wire_codec.h"
#include "test_task_receiver.h"
static const uint8_t k_query[] = {0x4b,0x54,0x01,0x06,0x16,0x00,0x78,0x56,0x34,0x12,0xf0,0xde,0xbc,0x9a,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xde,0xf5};
static const uint8_t k_reply[] = {0x54,0x52,0x01,0x06,0x28,0x00,0x78,0x56,0x34,0x12,0xf0,0xde,0xbc,0x9a,0x40,0x30,0x20,0x10,0x78,0x56,0x34,0x12,0xf0,0xde,0xbc,0x9a,0x40,0x30,0x20,0x10,0x01,0x00,0x1f,0x00,0x00,0x00,0x01,0x00,0x9b,0x28,0x35,0x01,0x40,0xe2,0x01,0x00,0x1f,0xea};
static uint16_t Crc(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU, i; uint8_t bit;
  for (i=0U; i<length; i++) { crc ^= (uint16_t)data[i]<<8U;
    for(bit=0U;bit<8U;bit++) { crc=(crc&0x8000U)?(uint16_t)((crc<<1U)^0x1021U):(uint16_t)(crc<<1U); } }
  return crc;
}
static void FixCrc(uint8_t *frame, uint16_t length)
{ uint16_t crc=Crc(frame,length-2U);frame[length-2U]=(uint8_t)crc;frame[length-1U]=(uint8_t)(crc>>8U); }
int main(void)
{
  DeviceInfo_QueryTypeDef query = {0};
  DeviceInfo_ReplyTypeDef reply, copy;
  TestTask_ReceiverTypeDef rx;
  uint8_t frame[48]; uint16_t length=0U, i;
  assert(DeviceInfo_QueryDecode(k_query,30U,&query)==DEVICE_INFO_WIRE_OK);
  assert(query.request_id==0x12345678UL && query.nonce==0x9ABCDEF0UL);
  assert(DeviceInfo_ReplyDecode(k_reply,48U,&reply)==DEVICE_INFO_WIRE_OK);
  assert(reply.uid_words[2]==0x10203040UL && reply.build_date==20261019UL);
  assert(DeviceInfo_ReplyEncode(&reply,frame,48U,&length)==DEVICE_INFO_WIRE_OK);
  assert(length==48U && memcmp(frame,k_reply,48U)==0);
  copy=reply;
  frame[47]^=1U;
  assert(DeviceInfo_ReplyDecode(frame,48U,&reply)==DEVICE_INFO_WIRE_CRC);
  assert(memcmp(&copy,&reply,sizeof(reply))==0);
  reply.build_date=19000229UL;
  assert(DeviceInfo_ReplyEncode(&reply,frame,48U,&length)==DEVICE_INFO_WIRE_FIELDS);
  reply.build_date=20000229UL;
  assert(DeviceInfo_ReplyEncode(&reply,frame,48U,&length)==DEVICE_INFO_WIRE_OK);
  reply.build_time=240000UL;
  assert(DeviceInfo_ReplyEncode(&reply,frame,48U,&length)==DEVICE_INFO_WIRE_FIELDS);
  assert(DeviceInfo_QueryDecode(k_query,29U,&query)==DEVICE_INFO_WIRE_LENGTH);
  assert(DeviceInfo_QueryDecode(k_query,30U,0)==DEVICE_INFO_WIRE_NULL);
  memcpy(frame,k_query,30U); frame[16]=1U; FixCrc(frame,30U);
  assert(DeviceInfo_QueryDecode(frame,30U,&query)==DEVICE_INFO_WIRE_FIELDS);
  TestTask_ReceiverInit(&rx,100UL);
  /* Query must leave an existing validated task slot untouched. */
  rx.task_pending=1U; rx.pending_task.request_id=7788UL;
  for(i=0U;i<30U;i++) { (void)TestTask_ReceiverPushByte(&rx,k_query[i],i); }
  assert(rx.info_query_pending==1U && rx.task_pending==1U);
  assert(rx.pending_task.request_id==7788UL && rx.snapshot.accepted_frame_count==0UL);
  for(i=0U;i<30U;i++) { (void)TestTask_ReceiverPushByte(&rx,k_query[i],30UL+i); }
  assert(rx.snapshot.info_query_drop_count==1UL);
  assert(TestTask_ReceiverTakeInfoQuery(&rx,&query)==1U);
  assert(TestTask_ReceiverTakeInfoQuery(&rx,&query)==0U);
  for(i=0U;i<30U;i++) { (void)TestTask_ReceiverPushByte(&rx,frame[i],60UL+i); }
  assert(rx.info_query_pending==0U && rx.snapshot.info_query_reject_count==1UL);
  for(i=0U;i<10U;i++) { (void)TestTask_ReceiverPushByte(&rx,k_query[i],100UL+i); }
  assert(TestTask_ReceiverService(&rx,200UL)==TEST_TASK_RECEIVER_INFO_QUERY_REJECTED);
  assert(rx.snapshot.info_query_reject_count==2UL && rx.task_pending==1U);
  puts("device_info_codec_receiver_tests=pass");
  return 0;
}
