"""Read-only controller self-report; UID/version are not authentication."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import date
import struct

from task_transport import crc16_ccitt

QUERY_TYPE = REPLY_TYPE = 6
CONTRACT_REVISION = 1
QUERY_LENGTH = 30
REPLY_LENGTH = 48
REPLY_PAYLOAD_LENGTH = 40
FEATURE_INFO = 4
_QUERY = struct.Struct("<HBBHIIH12s")
_REPLY = struct.Struct("<HBBH6IHH3I")


@dataclass(frozen=True)
class DeviceInfo:
    request_id: int
    nonce: int
    timestamp_ms: int
    uid_word0: int
    uid_word1: int
    uid_word2: int
    feature_mask: int
    firmware_version: int
    build_date: int
    build_time: int

    @property
    def uid(self) -> str:
        return f"{self.uid_word0:08X}{self.uid_word1:08X}{self.uid_word2:08X}"

    @property
    def version(self) -> str:
        return f"{self.firmware_version >> 16}.{(self.firmware_version >> 8) & 255}.{self.firmware_version & 255}"


def _uint(value: int, bits: int) -> None:
    if type(value) is not int or not 0 <= value < 1 << bits:
        raise ValueError(f"expected uint{bits}")


def build_info_query(request_id: int, nonce: int) -> bytes:
    _uint(request_id, 32)
    _uint(nonce, 32)
    if not request_id:
        raise ValueError("request_id must be nonzero")
    body = _QUERY.pack(0x544B, 1, QUERY_TYPE, 22, request_id, nonce,
                       CONTRACT_REVISION, bytes(12))
    return body + struct.pack("<H", crc16_ccitt(body))


def decode_info_query(frame: bytes) -> tuple[int, int]:
    if not isinstance(frame, bytes) or len(frame) != QUERY_LENGTH:
        raise ValueError("info query length")
    magic, version, kind, payload, request, nonce, revision, reserved = _QUERY.unpack_from(frame)
    if (magic, version, kind, payload, revision, reserved) != (
            0x544B, 1, QUERY_TYPE, 22, CONTRACT_REVISION, bytes(12)):
        raise ValueError("info query header or reserved bytes")
    if not request or struct.unpack_from("<H", frame, 28)[0] != crc16_ccitt(frame[:28]):
        raise ValueError("info query request or CRC")
    return request, nonce


def _validate(value: DeviceInfo) -> None:
    for name in ("request_id", "nonce", "timestamp_ms", "uid_word0", "uid_word1",
                 "uid_word2", "firmware_version", "build_date", "build_time"):
        _uint(getattr(value, name), 32)
    _uint(value.feature_mask, 16)
    if not value.request_id or not value.feature_mask & FEATURE_INFO or not value.firmware_version:
        raise ValueError("info request, features or version")
    year, month, day = value.build_date // 10000, value.build_date // 100 % 100, value.build_date % 100
    if year < 1900:
        raise ValueError("info build year")
    date(year, month, day)
    if value.build_time // 10000 > 23 or value.build_time // 100 % 100 > 59 or value.build_time % 100 > 59:
        raise ValueError("info build time")


def encode_info_reply(value: DeviceInfo) -> bytes:
    _validate(value)
    body = _REPLY.pack(0x5254, 1, REPLY_TYPE, REPLY_PAYLOAD_LENGTH,
                       value.request_id, value.nonce, value.timestamp_ms,
                       value.uid_word0, value.uid_word1, value.uid_word2,
                       CONTRACT_REVISION, value.feature_mask, value.firmware_version,
                       value.build_date, value.build_time)
    return body + struct.pack("<H", crc16_ccitt(body))


def decode_info_reply(frame: bytes) -> DeviceInfo:
    if not isinstance(frame, bytes) or len(frame) != REPLY_LENGTH:
        raise ValueError("info reply length")
    fields = _REPLY.unpack_from(frame)
    if fields[:4] != (0x5254, 1, REPLY_TYPE, REPLY_PAYLOAD_LENGTH) or fields[10] != CONTRACT_REVISION:
        raise ValueError("info reply header or revision")
    if struct.unpack_from("<H", frame, 46)[0] != crc16_ccitt(frame[:46]):
        raise ValueError("info reply CRC")
    value = DeviceInfo(*fields[4:10], *fields[11:])
    _validate(value)
    return value
