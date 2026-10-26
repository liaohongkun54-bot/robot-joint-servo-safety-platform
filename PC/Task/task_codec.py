"""Versioned, constrained PC-side task JSON codec.

This module converts a structured task intent to/from canonical JSON. It does
not contain CAN identifiers, CAN payload bytes, register addresses, serial
ports, or any transmit operation. The STM32 must validate the decoded task a
second time before any future execution layer is considered.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from enum import IntEnum
import json
from typing import Any, Mapping

SCHEMA_VERSION = 1
TARGET_C620_ID = 1
MAX_AMPLITUDE_RAW = 16384
MAX_DURATION_MS = 1000
MIN_FEEDBACK_TIMEOUT_MS = 100
MAX_FEEDBACK_TIMEOUT_MS = 1000


class Action(IntEnum):
    CURRENT_TEST = 1


class TimeoutPolicy(IntEnum):
    STOP = 1


class Permission(IntEnum):
    OPERATOR = 1


class ValidationCode(IntEnum):
    VALID = 0
    NULL_ARGUMENT = 1
    SCHEMA_VERSION = 2
    REQUEST_ID = 3
    TARGET_NODE = 4
    ACTION = 5
    AMPLITUDE_RANGE = 6
    AMPLITUDE_LIMIT = 7
    DURATION = 8
    FEEDBACK_TIMEOUT = 9
    TIMEOUT_POLICY = 10
    PERMISSION = 11
    JSON_FORMAT = 12
    UNKNOWN_FIELD = 13
    FIELD_TYPE = 14


class TaskCodecError(ValueError):
    """Rejected task with a stable machine-readable validation code."""

    def __init__(self, code: ValidationCode, message: str) -> None:
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class TaskDescriptor:
    schema_version: int
    request_id: int
    target_node_id: int
    action: int
    amplitude_raw: int
    amplitude_limit_raw: int
    duration_ms: int
    feedback_timeout_ms: int
    timeout_policy: int
    permission: int


_TASK_FIELDS = frozenset(TaskDescriptor.__dataclass_fields__)


def _is_int(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def validate_task(task: TaskDescriptor | None) -> ValidationCode:
    """Validate a descriptor using the same limits as the MCU Schema module."""

    if task is None:
        return ValidationCode.NULL_ARGUMENT

    integer_fields = (
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
    if not all(_is_int(value) for value in integer_fields):
        return ValidationCode.FIELD_TYPE
    if task.schema_version != SCHEMA_VERSION:
        return ValidationCode.SCHEMA_VERSION
    if task.request_id == 0:
        return ValidationCode.REQUEST_ID
    if task.target_node_id != TARGET_C620_ID:
        return ValidationCode.TARGET_NODE
    if task.action != int(Action.CURRENT_TEST):
        return ValidationCode.ACTION
    if not 1 <= task.amplitude_limit_raw <= MAX_AMPLITUDE_RAW:
        return ValidationCode.AMPLITUDE_LIMIT
    if not -MAX_AMPLITUDE_RAW <= task.amplitude_raw <= MAX_AMPLITUDE_RAW:
        return ValidationCode.AMPLITUDE_RANGE
    if not -task.amplitude_limit_raw <= task.amplitude_raw <= task.amplitude_limit_raw:
        return ValidationCode.AMPLITUDE_LIMIT
    if not 1 <= task.duration_ms <= MAX_DURATION_MS:
        return ValidationCode.DURATION
    if not MIN_FEEDBACK_TIMEOUT_MS <= task.feedback_timeout_ms <= MAX_FEEDBACK_TIMEOUT_MS:
        return ValidationCode.FEEDBACK_TIMEOUT
    if task.timeout_policy != int(TimeoutPolicy.STOP):
        return ValidationCode.TIMEOUT_POLICY
    if task.permission != int(Permission.OPERATOR):
        return ValidationCode.PERMISSION
    return ValidationCode.VALID


def _raise_if_invalid(task: TaskDescriptor | None) -> None:
    code = validate_task(task)
    if code != ValidationCode.VALID:
        raise TaskCodecError(code, f"task validation failed: {code.name.lower()}")


def encode_task(task: TaskDescriptor) -> str:
    """Return canonical JSON with stable key order and no extra fields."""

    _raise_if_invalid(task)
    return json.dumps(asdict(task), ensure_ascii=True, sort_keys=True, separators=(",", ":"))


def _mapping_to_task(value: object) -> TaskDescriptor:
    if not isinstance(value, Mapping):
        raise TaskCodecError(ValidationCode.JSON_FORMAT, "task JSON must be an object")
    keys = set(value)
    unknown = keys - _TASK_FIELDS
    if unknown:
        names = ",".join(sorted(str(name) for name in unknown))
        raise TaskCodecError(ValidationCode.UNKNOWN_FIELD, f"unknown task field(s): {names}")
    missing = _TASK_FIELDS - keys
    if missing:
        names = ",".join(sorted(missing))
        raise TaskCodecError(ValidationCode.JSON_FORMAT, f"missing task field(s): {names}")
    task = TaskDescriptor(**{field: value[field] for field in _TASK_FIELDS})
    _raise_if_invalid(task)
    return task


def decode_task(payload: str) -> TaskDescriptor:
    """Decode task JSON and reject unknown or unsafe fields."""

    try:
        value: Any = json.loads(payload)
    except (TypeError, json.JSONDecodeError) as exc:
        raise TaskCodecError(ValidationCode.JSON_FORMAT, "invalid task JSON") from exc
    return _mapping_to_task(value)


def validation_error_text(code: ValidationCode) -> str:
    """Return a stable lowercase diagnostic token."""

    return code.name.lower()
