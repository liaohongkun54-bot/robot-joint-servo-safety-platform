"""Generate deterministic synthetic protocol fixtures. No hardware or private log input."""
from dataclasses import asdict
from pathlib import Path
import json, sys
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'PC/Task'))
from device_info import DeviceInfo,encode_info_reply,build_info_query
from control_status import ControlStatus,encode_status_reply,build_status_query
from telemetry import TelemetrySample,encode_telemetry_reply,build_telemetry_query
from task_result import TaskResult,encode_result
from task_stop_result import MotionStopResult,encode_stop
from operator_control import OperatorReply,encode_reply
T=Path(__file__).resolve().parent
pairs=[(6,DeviceInfo(100,200,1000,0x11111111,0x22222222,0x33333333,63,0x10101,20000101,120000),encode_info_reply),
(4,ControlStatus(101,201,1010,1,2,0,1,0,0,2,0),encode_status_reply),
(7,OperatorReply(102,202,1,2,3,0,1020,1,0,1,0,0,0,1000),encode_reply),
(2,TaskResult(900,2000,0,3,2,6,2,1),encode_result),
(3,MotionStopResult(900,2697,1000,120,20,100,25,5,2,1,0,200,150,60),encode_stop)]
for i in range(44):pairs.append((5,TelemetrySample(1000+i,2000+i,3000+100*i,2990+100*i,i+1,100+i,0,0,25,1,1,1,0x201),encode_telemetry_reply))
responses=[{'type':kind,'hex':encoder(value).hex(),'fields':asdict(value)} for kind,value,encoder in pairs]
queries=[{'type':kind,'request':0x12345678,'nonce':0x98abcdef,'hex':fn(0x12345678,0x98abcdef).hex()} for kind,fn in [(4,build_status_query),(5,build_telemetry_query),(6,build_info_query)]]
(T/'synthetic-wire-vectors.json').write_text(json.dumps({'synthetic_only':True,'queries':queries,'responses':responses},indent=2)+'\n',encoding='utf-8')
rows=[{'event':'session_start','fixture':'synthetic_only','port':'SYNTHETIC:'}]
for kind,value,encoder in pairs:
 if kind==5:
  rows.append({'event':'tx_telemetry_intent','frame_hex':build_telemetry_query(value.request_id,value.nonce).hex(),'fixture':'synthetic_only'})
  rows.append({'event':'rx_chunk','frame_hex':encoder(value).hex(),'fixture':'synthetic_only'})
(T/'synthetic-feedback.jsonl').write_text(''.join(json.dumps(x,separators=(',',':'))+'\n' for x in rows),encoding='utf-8')
print('Generated 49 synthetic responses and 44 synthetic feedback points; no serial I/O')
