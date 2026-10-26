"""Read-only versioned MCU status query; no serial or control operations."""

from __future__ import annotations

from dataclasses import dataclass
import struct

from task_transport import crc16_ccitt

QUERY_TYPE = 4
REPLY_TYPE = 4
CONTRACT_REVISION = 1
FEATURE_STATUS_QUERY = 1
RECORD_CAPACITY = 64
QUERY_LENGTH = 30
REPLY_LENGTH = 38
REPLY_PAYLOAD_LENGTH = 30
_QUERY = struct.Struct("<HBBHIIH12s")
_REPLY = struct.Struct("<HBBHIIIHHBBBBBBHHI")


@dataclass(frozen=True)
class ControlStatus:
    request_id: int
    nonce: int
    timestamp_ms: int
    feature_mask: int
    safety_state: int
    output_permitted: int
    zero_output_required: int
    fault_latched: int
    motion_active: int
    record_count: int
    safety_fault_code: int


def _uint(value: int, bits: int, name: str) -> None:
    if type(value) is not int or not 0 <= value < 1 << bits:
        raise ValueError(f"{name} must be uint{bits}")


def build_status_query(request_id: int, nonce: int) -> bytes:
    _uint(request_id, 32, "request_id")
    _uint(nonce, 32, "nonce")
    if request_id == 0:
        raise ValueError("request_id must be nonzero")
    body = _QUERY.pack(0x544B, 1, QUERY_TYPE, 22, request_id, nonce,
                       CONTRACT_REVISION, bytes(12))
    return body + struct.pack("<H", crc16_ccitt(body))


def decode_status_query(frame: bytes) -> tuple[int, int]:
    if not isinstance(frame, bytes) or len(frame) != QUERY_LENGTH:
        raise ValueError("status query length")
    magic, version, kind, payload, request_id, nonce, revision, reserved = _QUERY.unpack_from(frame)
    if (magic, version, kind, payload, revision, reserved) != (
            0x544B, 1, QUERY_TYPE, 22, CONTRACT_REVISION, bytes(12)):
        raise ValueError("status query header or reserved bytes")
    if request_id == 0 or struct.unpack_from("<H", frame, 28)[0] != crc16_ccitt(frame[:28]):
        raise ValueError("status query request_id or CRC")
    return request_id, nonce


def _validate_status(status: ControlStatus) -> None:
    for name in ("request_id", "nonce", "timestamp_ms", "safety_fault_code"):
        _uint(getattr(status, name), 32, name)
    _uint(status.feature_mask, 16, "feature_mask")
    _uint(status.record_count, 16, "record_count")
    if status.request_id == 0 or status.feature_mask != FEATURE_STATUS_QUERY:
        raise ValueError("status request or features")
    if status.safety_state not in range(7) or status.record_count > RECORD_CAPACITY:
        raise ValueError("status safety state or record capacity")
    for name in ("output_permitted", "zero_output_required", "fault_latched", "motion_active"):
        if type(getattr(status, name)) is not int or getattr(status, name) not in (0, 1):
            raise ValueError(f"{name} must be boolean byte")


def encode_status_reply(status: ControlStatus) -> bytes:
    _validate_status(status)
    body = _REPLY.pack(
        0x5254, 1, REPLY_TYPE, REPLY_PAYLOAD_LENGTH,
        status.request_id, status.nonce, status.timestamp_ms,
        CONTRACT_REVISION, status.feature_mask,
        status.safety_state, status.output_permitted, status.zero_output_required,
        status.fault_latched, status.motion_active, 0,
        status.record_count, RECORD_CAPACITY, status.safety_fault_code,
    )
    return body + struct.pack("<H", crc16_ccitt(body))


def decode_status_reply(frame: bytes) -> ControlStatus:
    if not isinstance(frame, bytes) or len(frame) != REPLY_LENGTH:
        raise ValueError("status reply length")
    fields = _REPLY.unpack_from(frame)
    if fields[:4] != (0x5254, 1, REPLY_TYPE, REPLY_PAYLOAD_LENGTH):
        raise ValueError("status reply header")
    if fields[7] != CONTRACT_REVISION or fields[14] != 0 or fields[16] != RECORD_CAPACITY:
        raise ValueError("status reply revision, reserved bytes or capacity")
    if struct.unpack_from("<H", frame, 36)[0] != crc16_ccitt(frame[:36]):
        raise ValueError("status reply CRC")
    status = ControlStatus(
        request_id=fields[4], nonce=fields[5], timestamp_ms=fields[6],
        feature_mask=fields[8], safety_state=fields[9],
        output_permitted=fields[10], zero_output_required=fields[11],
        fault_latched=fields[12], motion_active=fields[13],
        record_count=fields[15], safety_fault_code=fields[17],
    )
    _validate_status(status)
    return status
