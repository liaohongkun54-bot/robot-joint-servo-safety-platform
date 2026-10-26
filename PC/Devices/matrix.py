"""Print a read-only, artifact-checked compatibility evidence matrix."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

from capability import CapabilityError, DeviceProfile, load_catalog


REPO_ROOT = Path(__file__).resolve().parents[2]
PROFILE_DIR = Path(__file__).resolve().parent / "profiles"


def render_matrix(profiles: tuple[DeviceProfile, ...]) -> str:
    lines = [
        "# 设备能力与兼容证据矩阵（只读）",
        "",
        "本目录只展示声明和证据，不授权输出；未识别设备仅可离线审查。"
        "历史板测不等于当前固件、目标工装或商业验收。",
        "",
        "| 配置 | 驱动/电机 | 接口与适配 | 驱动固件 | 证据层级 | 当前输出许可 |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for profile in profiles:
        scopes = ", ".join(sorted({claim.scope for claim in profile.test_claims})) or "none"
        adapter = profile.adapter_id or "none"
        lines.append(
            f"| {profile.profile_id} | {profile.vendor} {profile.drive_model} / "
            f"{profile.motor_model} | {profile.protocol_family}; {adapter} | "
            f"{profile.drive_firmware} | {scopes} | 不由本目录授权 |"
        )
    for profile in profiles:
        lines.extend(("", f"## {profile.profile_id}", "",
                      f"- 物理接口：{profile.physical_interface}",
                      f"- 停止语义：{profile.stop_semantics}",
                      f"- 故障语义：{profile.fault_semantics}",
                      "- 控制范围仅为该配置的描述，MCU 仍须按当前固件独立验证："))
        lines.extend(f"  - {mode.mode_id}: {mode.min_command}..{mode.max_command} "
                     f"{mode.command_unit}; max {mode.max_duration_ms} ms"
                     for mode in profile.control_modes)
        if not profile.control_modes:
            lines.append("  - 未声明控制模式")
        lines.append("- 反馈编码字段：")
        lines.extend(f"  - {field.field_id}: {field.value_min}..{field.value_max} {field.unit}"
                     for field in profile.feedback_fields)
        if not profile.feedback_fields:
            lines.append("  - 未声明反馈字段")
        lines.append("- 硬件条件：")
        lines.extend(f"  - {condition}" for condition in profile.hardware_conditions)
        if not profile.hardware_conditions:
            lines.append("  - 未记录；禁止据此推断硬件匹配")
        lines.extend(("- 分版本测试证据（下列路径已核对 SHA-256，仅证明文件完整性）：", ""))
        lines.extend(("  | 测试项 | 层级 | 版本 | 观察与限制 | 证据文件 |", "  | --- | --- | --- | --- | --- |"))
        for claim in profile.test_claims:
            sources = ", ".join(f"`{item.path}`" for item in claim.evidence)
            lines.append(f"  | {claim.test_id} | {claim.scope} | {claim.version_label} | "
                         f"{claim.finding} | {sources} |")
        if not profile.test_claims:
            lines.append("  | 无 | 无 | 无 | 无 | 无 |")
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="只读设备能力/历史证据矩阵")
    parser.add_argument("--profiles", type=Path, default=PROFILE_DIR)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    args = parser.parse_args(argv)
    try:
        profiles = load_catalog(args.profiles, args.repo_root)
    except CapabilityError as exc:
        print(f"capability_error: {exc}", file=sys.stderr)
        return 2
    print(render_matrix(profiles), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
