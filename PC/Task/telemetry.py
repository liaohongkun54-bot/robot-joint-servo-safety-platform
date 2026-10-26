"""Read-only KT/TR v1 type 5 C620 feedback snapshot wire contract."""

from __future__ import annotations

from dataclasses import dataclass
import struct

from task_transport import crc16_ccitt

QUERY_TYPE = REPLY_TYPE = 5
REVISION = 1
SOURCE_C620_CAN1 = 1
C620_FEEDBACK_STD_ID = 0x201
QUERY_LENGTH = 30
REPLY_LENGTH = 42
REPLY_PAYLOAD_LENGTH = 34
_QUERY = struct.Struct("<HBBHIIH12s")
_REPLY = struct.Struct("<HBBHIIIIIHhhBBBBHH")


@dataclass(frozen=True)
class TelemetrySample:
    request_id: int
    nonce: int
    sample_tick_ms: int
    last_rx_tick_ms: int
    valid_frame_count: int
    encoder_raw: int
    speed_rpm: int
    current_raw: int
    temperature_c: int
    link_state: int
    has_valid_feedback: int
    source_id: int
    feedback_std_id: int

    @property
    def fresh(self) -> bool:
        age = (self.sample_tick_ms - self.last_rx_tick_ms) & 0xFFFFFFFF
        return self.has_valid_feedback == 1 and self.link_state == 1 and age <= 100


def _uint(value: int, bits: int, name: str) -> None:
    if type(value) is not int or not 0 <= value < 1 << bits:
        raise ValueError(f"{name} must be uint{bits}")


def _int16(value: int, name: str) -> None:
    if type(value) is not int or not -32768 <= value <= 32767:
        raise ValueError(f"{name} must be int16")


def build_telemetry_query(request_id: int, nonce: int) -> bytes:
    _uint(request_id, 32, "request_id")
    _uint(nonce, 32, "nonce")
    if request_id == 0:
        raise ValueError("request_id must be nonzero")
    body = _QUERY.pack(0x544B, 1, QUERY_TYPE, 22, request_id, nonce,
                       REVISION, bytes(12))
    return body + struct.pack("<H", crc16_ccitt(body))


def decode_telemetry_query(frame: bytes) -> tuple[int, int]:
    if not isinstance(frame, bytes) or len(frame) != QUERY_LENGTH:
        raise ValueError("telemetry query length")
    magic, version, kind, payload, request_id, nonce, revision, reserved = _QUERY.unpack_from(frame)
    if (magic, version, kind, payload, revision, reserved) != (
            0x544B, 1, QUERY_TYPE, 22, REVISION, bytes(12)):
        raise ValueError("telemetry query header or fields")
    if request_id == 0 or struct.unpack_from("<H", frame, 28)[0] != crc16_ccitt(frame[:28]):
        raise ValueError("telemetry query request_id or CRC")
    return request_id, nonce


def _validate(sample: TelemetrySample) -> None:
    for name in ("request_id", "nonce", "sample_tick_ms", "last_rx_tick_ms",
                 "valid_frame_count"):
        _uint(getattr(sample, name), 32, name)
    _uint(sample.encoder_raw, 16, "encoder_raw")
    _int16(sample.speed_rpm, "speed_rpm")
    _int16(sample.current_raw, "current_raw")
    _uint(sample.temperature_c, 8, "temperature_c")
    if (sample.request_id == 0 or sample.encoder_raw > 8191
            or sample.link_state not in (0, 1, 2)
            or sample.has_valid_feedback not in (0, 1)
            or sample.source_id != SOURCE_C620_CAN1
            or sample.feedback_std_id != C620_FEEDBACK_STD_ID):
        raise ValueError("unsupported telemetry fields")


def encode_telemetry_reply(sample: TelemetrySample) -> bytes:
    _validate(sample)
    body = _REPLY.pack(
        0x5254, 1, REPLY_TYPE, REPLY_PAYLOAD_LENGTH,
        sample.request_id, sample.nonce, sample.sample_tick_ms,
        sample.last_rx_tick_ms, sample.valid_frame_count,
        sample.encoder_raw, sample.speed_rpm, sample.current_raw,
        sample.temperature_c, sample.link_state, sample.has_valid_feedback,
        sample.source_id, sample.feedback_std_id, 0,
    )
    return body + struct.pack("<H", crc16_ccitt(body))


def decode_telemetry_reply(frame: bytes) -> TelemetrySample:
    if not isinstance(frame, bytes) or len(frame) != REPLY_LENGTH:
        raise ValueError("telemetry reply length")
    fields = _REPLY.unpack_from(frame)
    if fields[:4] != (0x5254, 1, REPLY_TYPE, REPLY_PAYLOAD_LENGTH):
        raise ValueError("telemetry reply header")
    if fields[17] != 0 or struct.unpack_from("<H", frame, 40)[0] != crc16_ccitt(frame[:40]):
        raise ValueError("telemetry reply reserved bytes or CRC")
    sample = TelemetrySample(*fields[4:17])
    _validate(sample)
    return sample
