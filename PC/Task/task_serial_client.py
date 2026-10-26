"""Safe one-shot serial task exchange for fixed P4/P3-C profiles.

The command constructs a constrained TaskDescriptor and uses task_transport to
produce the fixed 30-byte frame. It cannot accept arbitrary bytes, CAN IDs or
register addresses. Dry-run is the default; real serial output requires both
--send and an explicit Windows COM port.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
import re
import time
from typing import Callable, Protocol, Sequence

from task_codec import (
    Action,
    Permission,
    TARGET_C620_ID,
    TaskDescriptor,
    TimeoutPolicy,
)
from task_result import (
    FRAME_LENGTH as RESULT_FRAME_LENGTH,
    ResultCodecError,
    ResultStage,
    ResultStatus,
    TaskResult,
    decode_result,
)
from task_stop_result import (
    FRAME_LENGTH as STOP_FRAME_LENGTH,
    MotionStopResult,
    StopCodecError,
    decode_stop,
)
from task_transport import FRAME_LENGTH, crc16_ccitt, encode_task_frame

BAUD_RATE = 115200
SERIAL_TIMEOUT_S = 1.0
RESULT_TIMEOUT_S = 1.0
STOP_TIMEOUT_S = 3.0
TEST_CASE_VALID = "valid"
TEST_CASE_SCHEMA_PERMISSION = "schema-permission"
TEST_CASE_CRC_ERROR = "crc-error"
TEST_CASE_PARTIAL_TIMEOUT = "partial-timeout"
TEST_CASE_FIRST_MOTION = "first-motion"
TEST_CASE_MOTION_STEP_200 = "motion-step-200"
TEST_CASE_MOTION_STEP_1000 = "motion-step-1000"
TEST_CASE_MOTION_RUN_1000_1S = "motion-run-1000-1s"
TEST_CASES = (
    TEST_CASE_VALID,
    TEST_CASE_SCHEMA_PERMISSION,
    TEST_CASE_CRC_ERROR,
    TEST_CASE_PARTIAL_TIMEOUT,
    TEST_CASE_FIRST_MOTION,
    TEST_CASE_MOTION_STEP_200,
    TEST_CASE_MOTION_STEP_1000,
        TEST_CASE_MOTION_RUN_1000_1S,
)
CSV_FIELDS = (
    "host_timestamp_utc",
    "port",
    "test_case",
    "sent_request_id",
    "sent_byte_count",
    "result_request_id",
    "mcu_timestamp_ms",
    "status",
    "stage",
    "error_code",
    "record_count",
    "safety_state",
    "flags",
    "task_frame_hex",
    "result_frame_hex",
)
STOP_CSV_FIELDS = (
    "schema_version", "host_timestamp_utc", "port", "test_case", "request_id",
    "mcu_accept_timestamp_ms", "mcu_stop_timestamp_ms",
    "command_amplitude_raw", "stop_reason", "safety_state", "link_state",
    "safety_fault_code", "feedback_speed_rpm", "feedback_current_raw",
    "feedback_encoder_raw", "feedback_temperature_c",
    "observation_abs_travel_raw", "observation_peak_abs_speed_rpm",
    "observation_sample_count", "task_frame_hex", "accept_frame_hex",
    "stop_frame_hex",
)
MOTION_TEST_CASES = (
    TEST_CASE_FIRST_MOTION, TEST_CASE_MOTION_STEP_200,
    TEST_CASE_MOTION_STEP_1000, TEST_CASE_MOTION_RUN_1000_1S,
)
_WINDOWS_COM_PATTERN = re.compile(r"COM([1-9][0-9]*)", re.IGNORECASE)


class SerialPort(Protocol):
    def write(self, data: bytes) -> int: ...

    def read(self, size: int) -> bytes: ...

    def flush(self) -> None: ...

    def close(self) -> None: ...


SerialFactory = Callable[..., SerialPort]


class SerialClientError(RuntimeError):
    """Serial preparation or one-shot write failure with a stable code."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class SendReceipt:
    port: str
    request_id: int
    byte_count: int
    frame_hex: str


@dataclass(frozen=True)
class ExchangeReceipt:
    send: SendReceipt
    test_case: str
    result: TaskResult
    result_frame_hex: str
    stop: MotionStopResult | None = None
    stop_frame_hex: str = ""


def _read_exact(serial_port: SerialPort, length: int, timeout_s: float) -> bytes:
    data = bytearray()
    deadline = time.monotonic() + timeout_s
    while len(data) < length and time.monotonic() < deadline:
        chunk = serial_port.read(length - len(data))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def normalize_com_port(port: str) -> str:
    if not isinstance(port, str):
        raise SerialClientError("invalid_port", "serial port must be text")
    match = _WINDOWS_COM_PATTERN.fullmatch(port.strip())
    if match is None:
        raise SerialClientError("invalid_port", "expected a Windows COM port such as COM7")
    return f"COM{int(match.group(1))}"


def build_zero_task(request_id: int) -> TaskDescriptor:
    """Return the only task shape permitted by the first physical UART test."""

    if isinstance(request_id, bool) or not isinstance(request_id, int):
        raise SerialClientError("invalid_request_id", "request_id must be an integer")
    if not 1 <= request_id <= 0xFFFFFFFF:
        raise SerialClientError(
            "invalid_request_id", "request_id must be in range 1..4294967295"
        )

    return TaskDescriptor(
        schema_version=1,
        request_id=request_id,
        target_node_id=TARGET_C620_ID,
        action=int(Action.CURRENT_TEST),
        amplitude_raw=0,
        amplitude_limit_raw=100,
        duration_ms=100,
        feedback_timeout_ms=100,
        timeout_policy=int(TimeoutPolicy.STOP),
        permission=int(Permission.OPERATOR),
    )


def build_zero_frame(request_id: int) -> bytes:
    return encode_task_frame(build_zero_task(request_id))


def build_first_motion_task(request_id: int) -> TaskDescriptor:
    """Return the approved one-shot ID 1, +100 raw, 100 ms profile."""

    task = build_zero_task(request_id)
    return TaskDescriptor(
        schema_version=task.schema_version,
        request_id=task.request_id,
        target_node_id=task.target_node_id,
        action=task.action,
        amplitude_raw=100,
        amplitude_limit_raw=100,
        duration_ms=100,
        feedback_timeout_ms=100,
        timeout_policy=task.timeout_policy,
        permission=task.permission,
    )


def build_motion_step_200_task(request_id: int) -> TaskDescriptor:
    """Return the fixed ID 1, +200 raw, 100 ms verification step."""

    task = build_zero_task(request_id)
    return TaskDescriptor(
        schema_version=task.schema_version,
        request_id=task.request_id,
        target_node_id=task.target_node_id,
        action=task.action,
        amplitude_raw=200,
        amplitude_limit_raw=200,
        duration_ms=100,
        feedback_timeout_ms=100,
        timeout_policy=task.timeout_policy,
        permission=task.permission,
    )


def build_motion_step_1000_task(request_id: int) -> TaskDescriptor:
    """Return the approved one-shot ID 1, +1000 raw, 100 ms profile."""

    task = build_zero_task(request_id)
    return TaskDescriptor(
        schema_version=task.schema_version,
        request_id=task.request_id,
        target_node_id=task.target_node_id,
        action=task.action,
        amplitude_raw=1000,
        amplitude_limit_raw=1000,
        duration_ms=100,
        feedback_timeout_ms=100,
        timeout_policy=task.timeout_policy,
        permission=task.permission,
    )

def build_motion_run_1000_1s_task(request_id: int) -> TaskDescriptor:
    task = build_zero_task(request_id)
    return TaskDescriptor(
        schema_version=task.schema_version,
        request_id=task.request_id,
        target_node_id=task.target_node_id,
        action=task.action,
        amplitude_raw=1000,
        amplitude_limit_raw=1000,
        duration_ms=1000,
        feedback_timeout_ms=100,
        timeout_policy=task.timeout_policy,
        permission=task.permission,
    )



def build_test_frame(request_id: int, test_case: str) -> bytes:
    """Build one fixed link-test or approved first-motion profile."""

    if test_case == TEST_CASE_FIRST_MOTION:
        return encode_task_frame(build_first_motion_task(request_id))
    if test_case == TEST_CASE_MOTION_STEP_200:
        return encode_task_frame(build_motion_step_200_task(request_id))
    if test_case == TEST_CASE_MOTION_STEP_1000:
        return encode_task_frame(build_motion_step_1000_task(request_id))
    if test_case == TEST_CASE_MOTION_RUN_1000_1S:
        return encode_task_frame(build_motion_run_1000_1s_task(request_id))
    frame = bytearray(build_zero_frame(request_id))
    if test_case == TEST_CASE_VALID:
        return bytes(frame)
    if test_case == TEST_CASE_SCHEMA_PERMISSION:
        frame[27] = 0
        frame[-2:] = crc16_ccitt(bytes(frame[:-2])).to_bytes(2, "little")
        return bytes(frame)
    if test_case == TEST_CASE_CRC_ERROR:
        frame[-1] ^= 0x01
        return bytes(frame)
    if test_case == TEST_CASE_PARTIAL_TIMEOUT:
        return bytes(frame[:10])
    raise SerialClientError("invalid_test_case", f"unknown test case: {test_case}")


def _pyserial_factory(**settings: object) -> SerialPort:
    try:
        import serial
    except ImportError as exc:
        raise SerialClientError(
            "missing_pyserial",
            "pyserial is required only for --send; install it after confirmation",
        ) from exc
    return serial.Serial(**settings)


def list_com_ports() -> list[str]:
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise SerialClientError(
            "missing_pyserial",
            "pyserial is required to enumerate COM ports",
        ) from exc
    return sorted(port.device for port in list_ports.comports())


def send_zero_task_once(
    port: str,
    request_id: int,
    serial_factory: SerialFactory | None = None,
) -> SendReceipt:
    """Open, write exactly one frame, flush and close; never retry silently."""

    normalized_port = normalize_com_port(port)
    frame = build_zero_frame(request_id)
    if len(frame) != FRAME_LENGTH:
        raise SerialClientError("frame_length", "encoded task frame is not 30 bytes")

    factory = serial_factory or _pyserial_factory
    serial_port: SerialPort | None = None
    try:
        serial_port = factory(
            port=normalized_port,
            baudrate=BAUD_RATE,
            bytesize=8,
            parity="N",
            stopbits=1,
            timeout=SERIAL_TIMEOUT_S,
            write_timeout=SERIAL_TIMEOUT_S,
        )
        written = serial_port.write(frame)
        if written != len(frame):
            raise SerialClientError(
                "short_write",
                f"serial write accepted {written} of {len(frame)} bytes",
            )
        serial_port.flush()
    except SerialClientError:
        raise
    except Exception as exc:
        raise SerialClientError("write_failed", f"serial write failed: {exc}") from exc
    finally:
        if serial_port is not None:
            serial_port.close()

    return SendReceipt(
        port=normalized_port,
        request_id=request_id,
        byte_count=len(frame),
        frame_hex=frame.hex(" "),
    )


def exchange_zero_task_once(
    port: str,
    request_id: int,
    test_case: str = TEST_CASE_VALID,
    serial_factory: SerialFactory | None = None,
    result_timeout_s: float = RESULT_TIMEOUT_S,
    wait_stop: bool = False,
    stop_timeout_s: float = STOP_TIMEOUT_S,
) -> ExchangeReceipt:
    """Send one fixed profile; optionally await its same-request stop event."""

    normalized_port = normalize_com_port(port)
    frame = build_test_frame(request_id, test_case)
    if result_timeout_s <= 0:
        raise SerialClientError("invalid_timeout", "result timeout must be positive")
    if wait_stop and (test_case not in MOTION_TEST_CASES or stop_timeout_s <= 0):
        raise SerialClientError(
            "invalid_stop_request", "--wait-stop requires a motion profile and positive timeout"
        )

    factory = serial_factory or _pyserial_factory
    serial_port: SerialPort | None = None
    result_frame = b""
    stop_frame = b""
    stop: MotionStopResult | None = None
    try:
        serial_port = factory(
            port=normalized_port,
            baudrate=BAUD_RATE,
            bytesize=8,
            parity="N",
            stopbits=1,
            timeout=min(SERIAL_TIMEOUT_S, result_timeout_s, stop_timeout_s)
            if wait_stop else min(SERIAL_TIMEOUT_S, result_timeout_s),
            write_timeout=SERIAL_TIMEOUT_S,
        )
        written = serial_port.write(frame)
        if written != len(frame):
            raise SerialClientError(
                "short_write", f"serial write accepted {written} of {len(frame)} bytes"
            )
        serial_port.flush()

        result_frame = _read_exact(serial_port, RESULT_FRAME_LENGTH, result_timeout_s)
        if len(result_frame) != RESULT_FRAME_LENGTH:
            raise SerialClientError(
                "result_timeout",
                f"received {len(result_frame)} of {RESULT_FRAME_LENGTH} result bytes",
            )
        try:
            result = decode_result(result_frame)
        except ResultCodecError as exc:
            raise SerialClientError(
                "result_decode", f"result decode failed: {exc.code}"
            ) from exc
        if wait_stop and result.status == int(ResultStatus.ACCEPTED):
            if result.request_id != request_id:
                raise SerialClientError("request_mismatch", "accept request_id mismatch")
            stop_frame = _read_exact(serial_port, STOP_FRAME_LENGTH, stop_timeout_s)
            if len(stop_frame) != STOP_FRAME_LENGTH:
                raise SerialClientError(
                    "stop_timeout",
                    f"received {len(stop_frame)} of {STOP_FRAME_LENGTH} stop bytes",
                )
            try:
                stop = decode_stop(stop_frame)
            except StopCodecError as exc:
                raise SerialClientError(
                    "stop_decode", f"stop decode failed: {exc.code}"
                ) from exc
            if stop.request_id != request_id:
                raise SerialClientError(
                    "stop_request_mismatch",
                    f"sent request_id={request_id}, stop request_id={stop.request_id}",
                )
            if stop.command_amplitude_raw != int.from_bytes(
                frame[14:16], "little", signed=True
            ):
                raise SerialClientError(
                    "stop_target_mismatch", "stop command differs from sent task"
                )
    except SerialClientError:
        raise
    except Exception as exc:
        raise SerialClientError("exchange_failed", f"serial exchange failed: {exc}") from exc
    finally:
        if serial_port is not None:
            serial_port.close()

    if test_case in (
        TEST_CASE_VALID,
        TEST_CASE_FIRST_MOTION,
        TEST_CASE_MOTION_STEP_200,
        TEST_CASE_MOTION_STEP_1000,
                TEST_CASE_MOTION_RUN_1000_1S,
    ) and (
        result.request_id != request_id
    ):
        raise SerialClientError(
            "request_mismatch",
            f"sent request_id={request_id}, received request_id={result.request_id}",
        )
    if test_case == TEST_CASE_SCHEMA_PERMISSION:
        if result.request_id != request_id or result.stage != int(ResultStage.SCHEMA):
            raise SerialClientError(
                "stage_mismatch", "schema fault must return the matching request at schema stage"
            )
    elif test_case in (TEST_CASE_CRC_ERROR, TEST_CASE_PARTIAL_TIMEOUT) and (
        result.stage != int(ResultStage.TRANSPORT)
    ):
        raise SerialClientError(
            "stage_mismatch", "transport fault must stop at transport stage"
        )

    send_receipt = SendReceipt(
        port=normalized_port,
        request_id=request_id,
        byte_count=len(frame),
        frame_hex=frame.hex(" "),
    )
    return ExchangeReceipt(
        send=send_receipt,
        test_case=test_case,
        result=result,
        result_frame_hex=result_frame.hex(" "),
        stop=stop,
        stop_frame_hex=stop_frame.hex(" "),
    )


def append_exchange_csv(path: str | Path, receipt: ExchangeReceipt) -> Path:
    """Append one verified exchange; never truncate an existing evidence file."""

    csv_path = Path(path).expanduser().resolve()
    try:
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        write_header = not csv_path.exists() or csv_path.stat().st_size == 0
        if not write_header:
            with csv_path.open("r", encoding="utf-8", newline="") as stream:
                existing_header = next(csv.reader(stream), [])
            if tuple(existing_header) != CSV_FIELDS:
                raise SerialClientError(
                    "csv_schema", "existing CSV header does not match the P4 schema"
                )

        result = receipt.result
        row = {
            "host_timestamp_utc": datetime.now(timezone.utc).isoformat(),
            "port": receipt.send.port,
            "test_case": receipt.test_case,
            "sent_request_id": receipt.send.request_id,
            "sent_byte_count": receipt.send.byte_count,
            "result_request_id": result.request_id,
            "mcu_timestamp_ms": result.timestamp_ms,
            "status": ResultStatus(result.status).name.lower(),
            "stage": ResultStage(result.stage).name.lower(),
            "error_code": f"0x{result.error_code:04X}",
            "record_count": result.record_count,
            "safety_state": result.safety_state,
            "flags": f"0x{result.flags:02X}",
            "task_frame_hex": receipt.send.frame_hex,
            "result_frame_hex": receipt.result_frame_hex,
        }
        with csv_path.open("a", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS)
            if write_header:
                writer.writeheader()
            writer.writerow(row)
    except SerialClientError:
        raise
    except OSError as exc:
        raise SerialClientError("csv_write", f"CSV append failed: {exc}") from exc
    return csv_path


def append_stop_csv(path: str | Path, receipt: ExchangeReceipt) -> Path:
    """Append a CRC-checked, same-request final stop; never infer one from an ACK."""

    stop = receipt.stop
    if stop is None or receipt.result.status != int(ResultStatus.ACCEPTED):
        raise SerialClientError("no_stop", "no verified accepted motion stop to record")
    if stop.request_id != receipt.send.request_id:
        raise SerialClientError("stop_request_mismatch", "stop request_id mismatch")
    csv_path = Path(path).expanduser().resolve()
    try:
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        write_header = not csv_path.exists() or csv_path.stat().st_size == 0
        if not write_header:
            with csv_path.open("r", encoding="utf-8", newline="") as stream:
                if tuple(next(csv.reader(stream), [])) != STOP_CSV_FIELDS:
                    raise SerialClientError(
                        "csv_schema", "existing CSV header does not match stop schema v1"
                    )
        row = {
            "schema_version": 1,
            "host_timestamp_utc": datetime.now(timezone.utc).isoformat(),
            "port": receipt.send.port,
            "test_case": receipt.test_case,
            "request_id": stop.request_id,
            "mcu_accept_timestamp_ms": receipt.result.timestamp_ms,
            "mcu_stop_timestamp_ms": stop.timestamp_ms,
            "command_amplitude_raw": stop.command_amplitude_raw,
            "stop_reason": stop.stop_reason,
            "safety_state": stop.safety_state,
            "link_state": stop.link_state,
            "safety_fault_code": f"0x{stop.safety_fault_code:08X}",
            "feedback_speed_rpm": stop.feedback_speed_rpm,
            "feedback_current_raw": stop.feedback_current_raw,
            "feedback_encoder_raw": stop.feedback_encoder_raw,
            "feedback_temperature_c": stop.feedback_temperature_c,
            "observation_abs_travel_raw": stop.observation_abs_travel_raw,
            "observation_peak_abs_speed_rpm": stop.observation_peak_abs_speed_rpm,
            "observation_sample_count": stop.observation_sample_count,
            "task_frame_hex": receipt.send.frame_hex,
            "accept_frame_hex": receipt.result_frame_hex,
            "stop_frame_hex": receipt.stop_frame_hex,
        }
        with csv_path.open("a", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=STOP_CSV_FIELDS)
            if write_header:
                writer.writeheader()
            writer.writerow(row)
    except SerialClientError:
        raise
    except OSError as exc:
        raise SerialClientError("csv_write", f"stop CSV append failed: {exc}") from exc
    return csv_path


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Preview or send one fixed constrained task frame."
    )
    parser.add_argument("--request-id", type=int, default=3001)
    parser.add_argument("--port", help="explicit Windows COM port, for example COM7")
    parser.add_argument("--send", action="store_true", help="perform one real serial write")
    parser.add_argument(
        "--wait-result",
        action="store_true",
        help="keep the port open and decode one 24-byte MCU result",
    )
    parser.add_argument(
        "--test-case",
        choices=TEST_CASES,
        default=TEST_CASE_VALID,
        help="fixed safe profile; fault profiles never reach runtime",
    )
    parser.add_argument("--csv", help="append a verified exchange to this CSV path")
    parser.add_argument("--wait-stop", action="store_true", help="wait for the same-request 42-byte motion stop")
    parser.add_argument("--stop-csv", help="append a verified final stop to a separate schema-v1 CSV")
    parser.add_argument("--list-ports", action="store_true", help="list available COM ports")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    try:
        if args.list_ports:
            ports = list_com_ports()
            print("\n".join(ports) if ports else "no_com_ports")
            return 0

        frame = build_test_frame(args.request_id, args.test_case)
        if not args.send:
            if args.wait_result or args.csv or args.wait_stop or args.stop_csv:
                raise SerialClientError(
                    "send_required", "result or CSV options require --send"
                )
            print(
                f"dry_run test_case={args.test_case} "
                f"request_id={args.request_id} bytes={len(frame)}"
            )
            print(frame.hex(" "))
            return 0

        if args.port is None:
            raise SerialClientError("port_required", "--port is required with --send")
        if args.test_case != TEST_CASE_VALID and not args.wait_result:
            raise SerialClientError(
                "wait_required", "fault test cases require --wait-result"
            )
        if args.csv and not args.wait_result:
            raise SerialClientError("wait_required", "--csv requires --wait-result")
        if (args.wait_stop or args.stop_csv) and not args.wait_result:
            raise SerialClientError("wait_required", "stop options require --wait-result")
        if args.stop_csv and not args.wait_stop:
            raise SerialClientError("wait_required", "--stop-csv requires --wait-stop")
        if args.csv and args.stop_csv and (
            Path(args.csv).expanduser().resolve() ==
            Path(args.stop_csv).expanduser().resolve()
        ):
            raise SerialClientError(
                "csv_path_conflict", "accept and stop CSV paths must differ"
            )

        if args.wait_result:
            exchange = exchange_zero_task_once(
                args.port,
                args.request_id,
                args.test_case,
                wait_stop=args.wait_stop,
            )
            result = exchange.result
            print(
                f"sent port={exchange.send.port} test_case={exchange.test_case} "
                f"request_id={exchange.send.request_id} "
                f"bytes={exchange.send.byte_count}"
            )
            print(exchange.send.frame_hex)
            print(
                f"result request_id={result.request_id} "
                f"status={ResultStatus(result.status).name.lower()} "
                f"stage={ResultStage(result.stage).name.lower()} "
                f"error_code=0x{result.error_code:04X} "
                f"record_count={result.record_count} "
                f"safety_state={result.safety_state} "
                f"flags=0x{result.flags:02X} bytes={RESULT_FRAME_LENGTH}"
            )
            print(exchange.result_frame_hex)
            if args.csv:
                csv_path = append_exchange_csv(args.csv, exchange)
                print(f"csv_appended path={csv_path}")
            if args.wait_stop:
                if exchange.stop is None:
                    print("stop_skipped reason=task_not_accepted")
                else:
                    stop = exchange.stop
                    print(
                        f"stop request_id={stop.request_id} mcu_tick_ms={stop.timestamp_ms} "
                        f"reason={stop.stop_reason} feedback_speed_rpm={stop.feedback_speed_rpm} "
                        f"fault=0x{stop.safety_fault_code:08X} bytes={STOP_FRAME_LENGTH}"
                    )
                    print(exchange.stop_frame_hex)
                    if args.stop_csv:
                        csv_path = append_stop_csv(args.stop_csv, exchange)
                        print(f"stop_csv_appended path={csv_path}")
            return 0

        receipt = send_zero_task_once(args.port, args.request_id)
        print(
            f"sent port={receipt.port} request_id={receipt.request_id} "
            f"bytes={receipt.byte_count}"
        )
        print(receipt.frame_hex)
        return 0
    except SerialClientError as exc:
        print(f"error code={exc.code} message={exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
