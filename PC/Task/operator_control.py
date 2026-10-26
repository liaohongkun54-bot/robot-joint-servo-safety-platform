"""Operator session protocol. Encoding a request never authorizes device I/O."""
from __future__ import annotations

from dataclasses import dataclass, fields
from enum import IntEnum
import struct

from task_codec import TaskDescriptor
from task_transport import crc16_ccitt, encode_task_frame, decode_task_frame

MESSAGE_TYPE = 7
FRAME_LENGTH = 56
FEATURE_OPERATOR = 0x20
_REQUEST = struct.Struct('<6IBB22s')
_REPLY = struct.Struct('<7IBBH4I')


class Operation(IntEnum):
    OPEN = 1
    HEARTBEAT = 2
    GRANT = 3
    STOP = 4
    CLEAR = 5
    CLOSE = 6


@dataclass(frozen=True)
class OperatorRequest:
    request_id: int
    nonce: int
    boot_low: int
    boot_high: int
    session_id: int
    sequence: int
    operation: int
    task: TaskDescriptor | None = None


@dataclass(frozen=True)
class OperatorReply:
    request_id: int
    nonce: int
    boot_low: int
    boot_high: int
    session_id: int
    sequence: int
    timestamp_ms: int
    operation: int
    result: int
    flags: int
    task_request_id: int
    fault_code: int
    grant_remaining_ms: int
    session_remaining_ms: int


def _uint(value: int, bits: int = 32) -> None:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value < (1 << bits):
        raise ValueError(f'expected uint{bits}')


def _envelope(magic: int, payload: bytes) -> bytes:
    body = struct.pack('<HBBH', magic, 1, MESSAGE_TYPE, 48) + payload
    if len(body) != 54:
        raise ValueError('invalid payload size')
    return body + struct.pack('<H', crc16_ccitt(body))


def _payload(frame: bytes, magic: int) -> bytes:
    if not isinstance(frame, bytes) or len(frame) != FRAME_LENGTH:
        raise ValueError('invalid frame length')
    if struct.unpack_from('<HBBH', frame) != (magic, 1, MESSAGE_TYPE, 48):
        raise ValueError('invalid envelope')
    if struct.unpack_from('<H', frame, 54)[0] != crc16_ccitt(frame[:54]):
        raise ValueError('invalid CRC')
    return frame[6:54]


def encode_request(request: OperatorRequest) -> bytes:
    for name in ('request_id', 'nonce', 'boot_low', 'boot_high', 'session_id', 'sequence'):
        _uint(getattr(request, name))
    _uint(request.operation, 8)
    if not request.request_id or request.operation not in Operation:
        raise ValueError('invalid request or operation')
    if request.operation == Operation.OPEN:
        if request.boot_low or request.boot_high or request.session_id or request.sequence:
            raise ValueError('OPEN requires zero boot/session/sequence')
    elif not (request.boot_low or request.boot_high) or not request.session_id or not request.sequence:
        raise ValueError('missing controller-issued session')
    task = bytes(22)
    if request.operation == Operation.GRANT:
        if request.task is None:
            raise ValueError('GRANT requires an exact task')
        task = encode_task_frame(request.task)[6:28]
    return _envelope(0x544B, _REQUEST.pack(request.request_id, request.nonce,
        request.boot_low, request.boot_high, request.session_id, request.sequence,
        request.operation, 0, task))


def decode_request(frame: bytes) -> OperatorRequest:
    values = _REQUEST.unpack(_payload(frame, 0x544B))
    if values[7]:
        raise ValueError('nonzero reserved field')
    task = None
    if values[6] == Operation.GRANT:
        body = struct.pack('<HBBH', 0x544B, 1, 1, 22) + values[8]
        task = decode_task_frame(body + struct.pack('<H', crc16_ccitt(body)))
    elif values[8] != bytes(22):
        raise ValueError('unexpected task payload')
    request = OperatorRequest(*values[:7], task)
    encode_request(request)
    return request


def encode_reply(reply: OperatorReply) -> bytes:
    for field in fields(reply):
        bits = 8 if field.name in ('operation', 'result') else 16 if field.name == 'flags' else 32
        _uint(getattr(reply, field.name), bits)
    if (not reply.request_id or reply.operation not in Operation or reply.result > 7
            or reply.flags > 15 or not (reply.boot_low or reply.boot_high)
            or reply.grant_remaining_ms > 10000 or reply.session_remaining_ms > 1000):
        raise ValueError('invalid reply fields')
    if reply.flags & 2 and (not reply.flags & 1 or reply.flags & 12 or not reply.task_request_id):
        raise ValueError('inconsistent grant state')
    if not reply.flags & 2 and reply.grant_remaining_ms:
        raise ValueError('grant TTL without grant')
    if not reply.flags & 1 and reply.session_remaining_ms:
        raise ValueError('session TTL without session')
    return _envelope(0x5254, _REPLY.pack(*(getattr(reply, f.name) for f in fields(reply))))


def decode_reply(frame: bytes) -> OperatorReply:
    reply = OperatorReply(*_REPLY.unpack(_payload(frame, 0x5254)))
    encode_reply(reply)
    return reply
