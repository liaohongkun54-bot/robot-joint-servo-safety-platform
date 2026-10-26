"""Strict, read-only device capability and evidence catalog.

Profiles describe candidate hardware and historical evidence. Loading a profile
never grants MCU output permission and contains no device I/O or frame encoder.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath
import re


SCHEMA_VERSION = 1
PROTOCOLS = frozenset({"vendor_can", "canopen_cia402", "other"})
EVIDENCE_SCOPES = frozenset({"protocol_vector", "historical_board", "current_host", "current_board", "site"})
TEST_IDS = frozenset({
    "feedback_readonly", "zero_output", "unarmed_rejection",
    "limited_motion", "deadline_stop", "fault_response",
})
PROFILE_FIELDS = frozenset({
    "schema_version", "profile_id", "vendor", "drive_model", "motor_model",
    "drive_firmware", "protocol_family", "physical_interface", "adapter_id",
    "control_modes", "feedback_fields", "fault_semantics", "stop_semantics",
    "hardware_conditions", "test_claims",
})
_ID_PATTERN = re.compile(r"[a-z][a-z0-9_]{2,63}\Z")
_SHA_PATTERN = re.compile(r"[0-9a-fA-F]{64}\Z")


class CapabilityError(ValueError):
    """A profile or its cited artifact fails the catalog contract."""


@dataclass(frozen=True)
class ControlMode:
    mode_id: str
    command_unit: str
    min_command: int
    max_command: int
    max_duration_ms: int


@dataclass(frozen=True)
class FeedbackField:
    field_id: str
    unit: str
    value_min: int
    value_max: int


@dataclass(frozen=True)
class EvidenceRef:
    path: str
    sha256: str


@dataclass(frozen=True)
class TestClaim:
    test_id: str
    scope: str
    version_label: str
    finding: str
    evidence: tuple[EvidenceRef, ...]


@dataclass(frozen=True)
class DeviceProfile:
    profile_id: str
    vendor: str
    drive_model: str
    motor_model: str
    drive_firmware: str
    protocol_family: str
    physical_interface: str
    adapter_id: str | None
    control_modes: tuple[ControlMode, ...]
    feedback_fields: tuple[FeedbackField, ...]
    fault_semantics: str
    stop_semantics: str
    hardware_conditions: tuple[str, ...]
    test_claims: tuple[TestClaim, ...]


def _object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    value: dict[str, object] = {}
    for key, item in pairs:
        if key in value:
            raise CapabilityError(f"duplicate JSON key: {key}")
        value[key] = item
    return value


def _fields(value: object, expected: frozenset[str], label: str) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != expected:
        raise CapabilityError(f"{label}: required fields {sorted(expected)}; no extras")
    return value


def _text(value: object, label: str) -> str:
    if not isinstance(value, str) or not value or value != value.strip():
        raise CapabilityError(f"{label}: nonempty trimmed string required")
    return value


def _identifier(value: object, label: str) -> str:
    result = _text(value, label)
    if _ID_PATTERN.fullmatch(result) is None:
        raise CapabilityError(f"{label}: invalid identifier")
    return result


def _integer(value: object, label: str) -> int:
    if type(value) is not int:
        raise CapabilityError(f"{label}: integer required")
    return value


def _list(value: object, label: str) -> list[object]:
    if not isinstance(value, list):
        raise CapabilityError(f"{label}: array required")
    return value


def _mode(value: object) -> ControlMode:
    row = _fields(value, frozenset({"mode_id", "command_unit", "min_command",
                                    "max_command", "max_duration_ms"}), "control_mode")
    minimum = _integer(row["min_command"], "min_command")
    maximum = _integer(row["max_command"], "max_command")
    duration = _integer(row["max_duration_ms"], "max_duration_ms")
    if minimum > maximum or duration <= 0:
        raise CapabilityError("control_mode: invalid range or duration")
    return ControlMode(_identifier(row["mode_id"], "mode_id"),
                       _text(row["command_unit"], "command_unit"), minimum,
                       maximum, duration)


def _feedback(value: object) -> FeedbackField:
    row = _fields(value, frozenset({"field_id", "unit", "value_min", "value_max"}),
                  "feedback_field")
    minimum = _integer(row["value_min"], "value_min")
    maximum = _integer(row["value_max"], "value_max")
    if minimum > maximum:
        raise CapabilityError("feedback_field: invalid value range")
    return FeedbackField(_identifier(row["field_id"], "field_id"),
                         _text(row["unit"], "unit"), minimum, maximum)


def _evidence(value: object) -> EvidenceRef:
    row = _fields(value, frozenset({"path", "sha256"}), "evidence")
    path = _text(row["path"], "evidence.path")
    relative = PurePosixPath(path)
    if (relative.is_absolute() or ".." in relative.parts or "." in relative.parts
            or "\\" in path or ":" in path or path != relative.as_posix()):
        raise CapabilityError("evidence.path: repository-relative POSIX path required")
    digest = _text(row["sha256"], "evidence.sha256")
    if _SHA_PATTERN.fullmatch(digest) is None:
        raise CapabilityError("evidence.sha256: 64 hex digits required")
    return EvidenceRef(path, digest.upper())


def _claim(value: object) -> TestClaim:
    row = _fields(value, frozenset({"test_id", "scope", "version_label", "finding", "evidence"}),
                  "test_claim")
    test_id = _text(row["test_id"], "test_id")
    scope = _text(row["scope"], "scope")
    if test_id not in TEST_IDS or scope not in EVIDENCE_SCOPES:
        raise CapabilityError("test_claim: unsupported test or evidence scope")
    evidence = tuple(_evidence(item) for item in _list(row["evidence"], "evidence"))
    if not evidence:
        raise CapabilityError("test_claim: at least one raw artifact required")
    if len({item.path for item in evidence}) != len(evidence):
        raise CapabilityError("test_claim: duplicate artifact")
    return TestClaim(test_id, scope, _text(row["version_label"], "version_label"),
                     _text(row["finding"], "finding"), evidence)


def parse_profile(payload: str) -> DeviceProfile:
    """Reject unknown fields, duplicate keys, ambiguous types and unsupported versions."""
    try:
        value = json.loads(payload, object_pairs_hook=_object)
    except (TypeError, json.JSONDecodeError) as exc:
        raise CapabilityError(f"invalid profile JSON: {exc}") from exc
    row = _fields(value, PROFILE_FIELDS, "profile")
    if _integer(row["schema_version"], "schema_version") != SCHEMA_VERSION:
        raise CapabilityError("unsupported profile schema_version")
    protocol = _text(row["protocol_family"], "protocol_family")
    if protocol not in PROTOCOLS:
        raise CapabilityError("unsupported protocol_family")
    adapter = row["adapter_id"]
    if adapter is not None:
        adapter = _identifier(adapter, "adapter_id")
    modes = tuple(_mode(item) for item in _list(row["control_modes"], "control_modes"))
    feedback = tuple(_feedback(item) for item in _list(row["feedback_fields"],
                                                      "feedback_fields"))
    claims = tuple(_claim(item) for item in _list(row["test_claims"], "test_claims"))
    for label, identifiers in (("mode_id", [x.mode_id for x in modes]),
                               ("field_id", [x.field_id for x in feedback]),
                               ("test_id/scope/version", [(x.test_id, x.scope, x.version_label)
                                                          for x in claims])):
        if len(set(identifiers)) != len(identifiers):
            raise CapabilityError(f"duplicate {label}")
    conditions = tuple(_text(item, "hardware_condition") for item in
                       _list(row["hardware_conditions"], "hardware_conditions"))
    return DeviceProfile(
        profile_id=_identifier(row["profile_id"], "profile_id"),
        vendor=_text(row["vendor"], "vendor"),
        drive_model=_text(row["drive_model"], "drive_model"),
        motor_model=_text(row["motor_model"], "motor_model"),
        drive_firmware=_text(row["drive_firmware"], "drive_firmware"),
        protocol_family=protocol,
        physical_interface=_text(row["physical_interface"], "physical_interface"),
        adapter_id=adapter,
        control_modes=modes,
        feedback_fields=feedback,
        fault_semantics=_text(row["fault_semantics"], "fault_semantics"),
        stop_semantics=_text(row["stop_semantics"], "stop_semantics"),
        hardware_conditions=conditions,
        test_claims=claims,
    )


def load_profile(path: Path) -> DeviceProfile:
    try:
        return parse_profile(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError) as exc:
        raise CapabilityError(f"cannot read profile {path}: {exc}") from exc


def verify_artifacts(profile: DeviceProfile, repo_root: Path) -> None:
    """Verify cited bytes. This checks integrity, not experimental conclusions."""
    root = repo_root.resolve()
    for claim in profile.test_claims:
        for reference in claim.evidence:
            path = (root / reference.path).resolve()
            if not path.is_relative_to(root) or not path.is_file():
                raise CapabilityError(f"missing or external evidence: {reference.path}")
            try:
                digest = hashlib.sha256(path.read_bytes()).hexdigest().upper()
            except OSError as exc:
                raise CapabilityError(f"cannot read evidence: {reference.path}: {exc}") from exc
            if digest != reference.sha256:
                raise CapabilityError(f"evidence SHA-256 mismatch: {reference.path}")


def load_catalog(profile_dir: Path, repo_root: Path) -> tuple[DeviceProfile, ...]:
    profiles = tuple(load_profile(path) for path in sorted(profile_dir.glob("*.json")))
    if not profiles:
        raise CapabilityError("no device profiles found")
    if len({profile.profile_id for profile in profiles}) != len(profiles):
        raise CapabilityError("duplicate profile_id")
    for profile in profiles:
        verify_artifacts(profile, repo_root)
    return profiles
