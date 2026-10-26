"""Read-only captured-CAN adapters. No CAN device handle or transmit method."""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Protocol

from capability import DeviceProfile


class FeedbackFrameError(ValueError):
    """A frame targeting this adapter has an invalid envelope."""


@dataclass(frozen=True)
class CapturedCanFrame:
    arbitration_id: int
    data: bytes
    is_extended: bool = False
    is_remote: bool = False


@dataclass(frozen=True)
class FeedbackSample:
    adapter_id: str
    encoder_raw: int
    speed_rpm: int
    current_raw: int
    temperature_c: int
    reserved_byte: int
    encoder_nominal_range: bool


class ReadOnlyAdapter(Protocol):
    adapter_id: str

    def decode(self, frame: CapturedCanFrame) -> FeedbackSample | None: ...


class C620Id1ReadOnlyAdapter:
    """Match Modules/C620/c620_feedback.c for fixed node ID 1 only."""

    adapter_id = "c620_id1_v1"
    feedback_id = 0x201

    def decode(self, frame: CapturedCanFrame) -> FeedbackSample | None:
        if type(frame.arbitration_id) is not int or not 0 <= frame.arbitration_id <= 0x1FFFFFFF:
            raise FeedbackFrameError("invalid CAN arbitration ID")
        if frame.arbitration_id != self.feedback_id:
            return None
        if frame.is_extended or frame.is_remote or not isinstance(frame.data, bytes) or len(frame.data) != 8:
            raise FeedbackFrameError("C620 ID 1 requires a standard data frame with DLC 8")
        encoder, speed, current, temperature, reserved = struct.unpack(">HhhBB", frame.data)
        return FeedbackSample(self.adapter_id, encoder, speed, current,
                              temperature, reserved, encoder <= 8191)


def read_only_adapter_for(profile: DeviceProfile) -> ReadOnlyAdapter | None:
    """Unknown or nonmatching profiles have no decoder, and no output path."""
    if (profile.profile_id == "c620_m3508_lab_v1"
            and profile.protocol_family == "vendor_can"
            and profile.adapter_id == "c620_id1_v1"):
        return C620Id1ReadOnlyAdapter()
    return None
