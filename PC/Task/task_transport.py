"""Fixed binary transport envelope for the constrained task descriptor.

The frame carries structured task intent only.  It does not carry CAN IDs,
CAN payload bytes, register addresses, serial-port commands, or transmit
operations.  The MCU must decode and validate the frame again.
"""

from __future__ import annotations

import struct

from task_codec import TaskCodecError, TaskDescriptor, ValidationCode, validate_task

MAGIC = 0x544B
VERSION = 1
MESSAGE_TYPE_TASK = 1
PAYLOAD_LENGTH = 22
FRAME_LENGTH = 30
_HEADER_PAYLOAD = struct.Struct("<HBBH")
_TASK_PAYLOAD = struct.Struct("<HIBBhhIIBB")


class TransportError(ValueError):
    """Rejected transport frame with a stable machine-readable code."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode_task_frame(task: TaskDescriptor) -> bytes:
    if validate_task(task) != ValidationCode.VALID:
        raise TaskCodecError(validate_task(task), "task validation failed")
    header = _HEADER_PAYLOAD.pack(MAGIC, VERSION, MESSAGE_TYPE_TASK, PAYLOAD_LENGTH)
    payload = _TASK_PAYLOAD.pack(
        task.schema_version,
        task.request_id,
        task.target_node_id,
        task.action,
        task.amplitude_raw,
        task.amplitude_limit_raw,
        task.duration_ms,
        task.feedback_timeout_ms,
        task.timeout_policy,
        task.permission,
    )
    body = header + payload
    return body + struct.pack("<H", crc16_ccitt(body))


def decode_task_frame(frame: bytes) -> TaskDescriptor:
    if not isinstance(frame, bytes) or len(frame) != FRAME_LENGTH:
        raise TransportError("length", "task frame must be exactly 30 bytes")
    magic, version, message_type, payload_length = _HEADER_PAYLOAD.unpack_from(frame)
    if magic != MAGIC:
        raise TransportError("magic", "invalid task frame magic")
    if version != VERSION:
        raise TransportError("version", "unsupported task frame version")
    if message_type != MESSAGE_TYPE_TASK:
        raise TransportError("type", "unsupported task frame type")
    if payload_length != PAYLOAD_LENGTH:
        raise TransportError("payload_length", "invalid task payload length")
    expected_crc = crc16_ccitt(frame[:-2])
    (received_crc,) = struct.unpack_from("<H", frame, FRAME_LENGTH - 2)
    if expected_crc != received_crc:
        raise TransportError("crc", "task frame CRC mismatch")
    values = _TASK_PAYLOAD.unpack_from(frame, _HEADER_PAYLOAD.size)
    task = TaskDescriptor(*values)
    code = validate_task(task)
    if code != ValidationCode.VALID:
        raise TransportError("task_schema", f"task validation failed: {code.name.lower()}")
    return task


if __name__ == "__main__":
    sample = TaskDescriptor(1, 1001, 6, 1, 0, 100, 100, 100, 1, 1)
    encoded = encode_task_frame(sample)
    print(encoded.hex(" "))
    print(decode_task_frame(encoded))
