"""Bounded, non-executable test-plan drafts tied to a device capability profile."""

from __future__ import annotations

from dataclasses import asdict, dataclass
import json
import re

from capability import DeviceProfile


SCHEMA_VERSION = 1
STATUS = "draft_not_executable"
MAX_STEPS = 16
MAX_ATTEMPTS = 16
MAX_RECORDS = 64
TEST_IDS = frozenset({"feedback_readonly", "zero_output", "unarmed_rejection",
                      "limited_motion", "deadline_stop"})
_ID = re.compile(r"[a-z][a-z0-9_]{2,63}\Z")
_SHA = re.compile(r"[0-9A-Fa-f]{64}\Z")


class DraftError(ValueError):
    """A draft cannot be safely interpreted against the selected profile."""


@dataclass(frozen=True)
class DraftStep:
    step_id: str
    test_id: str
    mode_id: str | None
    command_value: int | None
    duration_ms: int
    attempts: int


@dataclass(frozen=True)
class DraftPlan:
    schema_version: int
    status: str
    plan_id: str
    profile_id: str
    profile_sha256: str
    purpose: str
    steps: tuple[DraftStep, ...]

    @property
    def reserved_records_upper_bound(self) -> int:
        return 2 * sum(step.attempts for step in self.steps)


def _pairs(items: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in items:
        if key in result:
            raise DraftError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _object(value: object, fields: set[str], label: str) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != fields:
        raise DraftError(f"{label}: required fields {sorted(fields)}")
    return value


def _id(value: object, label: str) -> str:
    if not isinstance(value, str) or _ID.fullmatch(value) is None:
        raise DraftError(f"{label}: invalid identifier")
    return value


def _int(value: object, label: str, minimum: int, maximum: int) -> int:
    if type(value) is not int or not minimum <= value <= maximum:
        raise DraftError(f"{label}: integer {minimum}..{maximum} required")
    return value


def parse_draft(raw: bytes, profile: DeviceProfile, profile_sha256: str) -> DraftPlan:
    if not isinstance(profile_sha256, str) or _SHA.fullmatch(profile_sha256) is None:
        raise DraftError("selected profile SHA-256 is invalid")
    try:
        value = json.loads(raw.decode("utf-8-sig"), object_pairs_hook=_pairs)
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise DraftError(f"invalid draft JSON: {exc}") from exc
    row = _object(value, {"schema_version", "status", "plan_id", "profile_id", "profile_sha256",
                          "purpose", "steps"}, "plan")
    if row["schema_version"] != SCHEMA_VERSION or type(row["schema_version"]) is not int:
        raise DraftError("unsupported plan schema_version")
    if row["status"] != STATUS:
        raise DraftError("plan must remain a non-executable draft")
    plan_id = _id(row["plan_id"], "plan_id")
    if _id(row["profile_id"], "profile_id") != profile.profile_id:
        raise DraftError("profile_id differs from selected capability profile")
    digest = row["profile_sha256"]
    if not isinstance(digest, str) or _SHA.fullmatch(digest) is None:
        raise DraftError("profile_sha256: 64 hex digits required")
    if digest.upper() != profile_sha256.upper():
        raise DraftError("profile_sha256 differs from selected capability file")
    purpose = row["purpose"]
    if not isinstance(purpose, str) or not purpose.strip() or purpose != purpose.strip() or len(purpose) > 200:
        raise DraftError("purpose: 1..200 trimmed characters required")
    values = row["steps"]
    if not isinstance(values, list) or not 1 <= len(values) <= MAX_STEPS:
        raise DraftError(f"steps: 1..{MAX_STEPS} required")
    modes = {mode.mode_id: mode for mode in profile.control_modes}
    steps: list[DraftStep] = []
    for index, value in enumerate(values, 1):
        item = _object(value, {"step_id", "test_id", "mode_id", "command_value",
                               "duration_ms", "attempts"}, f"step {index}")
        step_id = _id(item["step_id"], "step_id")
        test_id = item["test_id"]
        if not isinstance(test_id, str) or test_id not in TEST_IDS:
            raise DraftError(f"step {index}: unsupported test_id")
        attempts = _int(item["attempts"], "attempts", 1, MAX_ATTEMPTS)
        if test_id == "feedback_readonly":
            if (item["mode_id"] is not None or item["command_value"] is not None
                    or item["duration_ms"] != 0 or type(item["duration_ms"]) is not int):
                raise DraftError(f"step {index}: readonly feedback has no command or duration")
            step = DraftStep(step_id, test_id, None, None, 0, attempts)
        else:
            mode_id = _id(item["mode_id"], "mode_id")
            mode = modes.get(mode_id)
            if mode is None:
                raise DraftError(f"step {index}: unknown profile control mode")
            command = _int(item["command_value"], "command_value",
                           mode.min_command, mode.max_command)
            duration = _int(item["duration_ms"], "duration_ms", 1, mode.max_duration_ms)
            if test_id in {"zero_output", "unarmed_rejection"} and command != 0:
                raise DraftError(f"step {index}: this test requires zero command")
            if test_id in {"limited_motion", "deadline_stop"} and command == 0:
                raise DraftError(f"step {index}: motion test requires nonzero command")
            step = DraftStep(step_id, test_id, mode_id, command, duration, attempts)
        steps.append(step)
    if len({step.step_id for step in steps}) != len(steps):
        raise DraftError("duplicate step_id")
    plan = DraftPlan(SCHEMA_VERSION, STATUS, plan_id, profile.profile_id, digest.upper(), purpose,
                     tuple(steps))
    if plan.reserved_records_upper_bound > MAX_RECORDS:
        raise DraftError("worst-case record reservation exceeds 64-entry RAM budget")
    return plan


def draft_json(plan: DraftPlan) -> str:
    return json.dumps(asdict(plan), ensure_ascii=False, indent=2) + "\n"
