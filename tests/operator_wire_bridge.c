/* Test-only stdin-free bridge: decode/re-encode one hex frame, no device I/O. */
#include <stdio.h>
#include <string.h>
#include "operator_wire_codec.h"
int main(int argc, char **argv)
{
  uint8_t frame[OPERATOR_WIRE_LENGTH], encoded[OPERATOR_WIRE_LENGTH];
  unsigned byte, i;
  uint16_t n=0U;
  OperatorWire_Request request;
  OperatorWire_Reply reply;
  OperatorWire_Result result;
  if(argc!=3 || strlen(argv[2])!=OPERATOR_WIRE_LENGTH*2U) { return 2; }
  for(i=0U;i<OPERATOR_WIRE_LENGTH;i++) {
    if(sscanf(argv[2]+i*2U,"%2x",&byte)!=1) { return 2; }
    frame[i]=(uint8_t)byte;
  }
  if(strcmp(argv[1],"request")==0) {
    result=OperatorWire_RequestDecode(frame,sizeof(frame),&request);
    if(result!=OPERATOR_WIRE_OK) { return 2; }
    result=OperatorWire_RequestEncode(&request,encoded,sizeof(encoded),&n);
  } else if(strcmp(argv[1],"reply")==0) {
    result=OperatorWire_ReplyDecode(frame,sizeof(frame),&reply);
    if(result!=OPERATOR_WIRE_OK) { return 2; }
    result=OperatorWire_ReplyEncode(&reply,encoded,sizeof(encoded),&n);
  } else { return 2; }
  if(result!=OPERATOR_WIRE_OK) { return 3; }
  for(i=0U;i<n;i++) { printf("%02x",encoded[i]); }
  puts("");
  return 0;
}
