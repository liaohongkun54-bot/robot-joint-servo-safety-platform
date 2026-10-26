"""Version 1, 42-byte read-only MCU motion-stop result frame."""

from __future__ import annotations

from dataclasses import dataclass
import struct

from task_transport import crc16_ccitt

MAGIC = 0x5254
VERSION = 1
MESSAGE_TYPE = 3
PAYLOAD_LENGTH = 34
FRAME_LENGTH = 42
_HEADER = struct.Struct("<HBBH")
_PAYLOAD = struct.Struct("<IIhhhHBBBBIIHI")


class StopCodecError(ValueError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class MotionStopResult:
    request_id: int
    timestamp_ms: int
    command_amplitude_raw: int
    feedback_speed_rpm: int
    feedback_current_raw: int
    feedback_encoder_raw: int
    feedback_temperature_c: int
    stop_reason: int
    safety_state: int
    link_state: int
    safety_fault_code: int
    observation_abs_travel_raw: int
    observation_peak_abs_speed_rpm: int
    observation_sample_count: int


def validate_stop(stop: MotionStopResult) -> None:
    values = tuple(stop.__dict__.values())
    if not all(isinstance(value, int) and not isinstance(value, bool) for value in values):
        raise StopCodecError("field_type", "stop fields must be integers")
    ranges = (
        (stop.request_id, 1, 0xFFFFFFFF),
        (stop.timestamp_ms, 0, 0xFFFFFFFF),
        (stop.command_amplitude_raw, -32768, 32767),
        (stop.feedback_speed_rpm, -32768, 32767),
        (stop.feedback_current_raw, -32768, 32767),
        (stop.feedback_encoder_raw, 0, 0xFFFF),
        (stop.feedback_temperature_c, 0, 0xFF),
        (stop.stop_reason, 1, 5),
        (stop.safety_state, 0, 0xFF),
        (stop.link_state, 0, 0xFF),
        (stop.safety_fault_code, 0, 0xFFFFFFFF),
        (stop.observation_abs_travel_raw, 0, 0xFFFFFFFF),
        (stop.observation_peak_abs_speed_rpm, 0, 0xFFFF),
        (stop.observation_sample_count, 0, 0xFFFFFFFF),
    )
    if any(not low <= value <= high for value, low, high in ranges):
        raise StopCodecError("field_range", "stop field outside wire range")


def encode_stop(stop: MotionStopResult) -> bytes:
    validate_stop(stop)
    body = _HEADER.pack(MAGIC, VERSION, MESSAGE_TYPE, PAYLOAD_LENGTH) + _PAYLOAD.pack(
        *stop.__dict__.values()
    )
    return body + struct.pack("<H", crc16_ccitt(body))


def decode_stop(frame: bytes) -> MotionStopResult:
    if not isinstance(frame, bytes) or len(frame) != FRAME_LENGTH:
        raise StopCodecError("length", "stop frame must be exactly 42 bytes")
    magic, version, message_type, payload_length = _HEADER.unpack_from(frame)
    if magic != MAGIC:
        raise StopCodecError("magic", "invalid stop magic")
    if version != VERSION:
        raise StopCodecError("version", "unsupported stop version")
    if message_type != MESSAGE_TYPE:
        raise StopCodecError("type", "unsupported stop message type")
    if payload_length != PAYLOAD_LENGTH:
        raise StopCodecError("payload_length", "invalid stop payload length")
    if struct.unpack_from("<H", frame, FRAME_LENGTH - 2)[0] != crc16_ccitt(frame[:-2]):
        raise StopCodecError("crc", "stop CRC mismatch")
    stop = MotionStopResult(*_PAYLOAD.unpack_from(frame, _HEADER.size))
    validate_stop(stop)
    return stop
