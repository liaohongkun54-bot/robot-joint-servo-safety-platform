"""Fixed 24-byte MCU task-result frame codec; no serial I/O."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import struct

from task_transport import crc16_ccitt

MAGIC = 0x5254
VERSION = 1
MESSAGE_TYPE = 2
PAYLOAD_LENGTH = 16
FRAME_LENGTH = 24
FLAG_RECORD_APPENDED = 0x01
FLAG_OUTPUT_UNCHANGED = 0x02
ALLOWED_FLAGS = FLAG_RECORD_APPENDED | FLAG_OUTPUT_UNCHANGED
_HEADER = struct.Struct("<HBBH")
_PAYLOAD = struct.Struct("<IIHHBBBB")


class ResultStatus(IntEnum):
    RECEIVED = 1
    ACCEPTED = 2
    REJECTED = 3
    INTERNAL_ERROR = 4


class ResultStage(IntEnum):
    TRANSPORT = 1
    SCHEMA = 2
    SAFETY = 3
    MOTION = 4
    RECORD = 5
    COMPLETE = 6


class ResultCodecError(ValueError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class TaskResult:
    request_id: int
    timestamp_ms: int
    error_code: int
    record_count: int
    status: int
    stage: int
    safety_state: int
    flags: int


def validate_result(result: TaskResult) -> None:
    integer_values = tuple(result.__dict__.values())
    if not all(isinstance(value, int) and not isinstance(value, bool) for value in integer_values):
        raise ResultCodecError("field_type", "result fields must be integers")
    if not 0 <= result.request_id <= 0xFFFFFFFF:
        raise ResultCodecError("request_id", "request_id is outside uint32")
    if result.request_id == 0 and result.stage != int(ResultStage.TRANSPORT):
        raise ResultCodecError("request_id", "request_id zero is transport-stage only")
    if not 0 <= result.timestamp_ms <= 0xFFFFFFFF:
        raise ResultCodecError("timestamp", "timestamp_ms is outside uint32")
    if not 0 <= result.error_code <= 0xFFFF or not 0 <= result.record_count <= 0xFFFF:
        raise ResultCodecError("field_range", "error_code or record_count is outside uint16")
    if result.status not in tuple(int(item) for item in ResultStatus):
        raise ResultCodecError("status", "unknown result status")
    if result.stage not in tuple(int(item) for item in ResultStage):
        raise ResultCodecError("stage", "unknown result stage")
    if not 0 <= result.safety_state <= 0xFF:
        raise ResultCodecError("safety_state", "safety_state is outside uint8")
    if result.flags & ~ALLOWED_FLAGS:
        raise ResultCodecError("flags", "unknown result flags")
    if result.status == int(ResultStatus.ACCEPTED) and (
        result.stage != int(ResultStage.COMPLETE) or result.error_code != 0
    ):
        raise ResultCodecError("semantics", "accepted result must be complete with no error")
    if result.status == int(ResultStatus.REJECTED) and result.error_code == 0:
        raise ResultCodecError("semantics", "rejected result requires an error code")


def encode_result(result: TaskResult) -> bytes:
    validate_result(result)
    body = _HEADER.pack(MAGIC, VERSION, MESSAGE_TYPE, PAYLOAD_LENGTH) + _PAYLOAD.pack(
        result.request_id,
        result.timestamp_ms,
        result.error_code,
        result.record_count,
        result.status,
        result.stage,
        result.safety_state,
        result.flags,
    )
    return body + struct.pack("<H", crc16_ccitt(body))


def decode_result(frame: bytes) -> TaskResult:
    if not isinstance(frame, bytes) or len(frame) != FRAME_LENGTH:
        raise ResultCodecError("length", "result frame must be exactly 24 bytes")
    magic, version, message_type, payload_length = _HEADER.unpack_from(frame)
    if magic != MAGIC:
        raise ResultCodecError("magic", "invalid result magic")
    if version != VERSION:
        raise ResultCodecError("version", "unsupported result version")
    if message_type != MESSAGE_TYPE:
        raise ResultCodecError("type", "unsupported result message type")
    if payload_length != PAYLOAD_LENGTH:
        raise ResultCodecError("payload_length", "invalid result payload length")
    if struct.unpack_from("<H", frame, FRAME_LENGTH - 2)[0] != crc16_ccitt(frame[:-2]):
        raise ResultCodecError("crc", "result CRC mismatch")
    result = TaskResult(*_PAYLOAD.unpack_from(frame, _HEADER.size))
    validate_result(result)
    return result
