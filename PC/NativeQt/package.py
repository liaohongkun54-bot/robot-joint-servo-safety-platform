"""Developer-only packaging. Never opens serial or starts a hardware task.
Requires an already-built executable and Qt/MinGW SDK; runtime needs neither.
All release/output directories must be new. No cleanup or in-place overwrite.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import zipfile


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--qt', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    args = parser.parse_args()
    native = Path(__file__).resolve().parent
    repo = native.parent.parent
    output = args.output.resolve()
    exe = args.build.resolve() / 'MotorTestPlatformNative.exe'
    if not exe.is_file():
        raise SystemExit('Build the application before packaging')
    archive = Path(str(output) + '.zip')
    if output.exists() or archive.exists():
        raise SystemExit('Use a new release directory; existing files are preserved')
    output.mkdir(parents=True)
    shutil.copy2(exe, output / exe.name)
    env = os.environ.copy()
    env['PATH'] = str(args.compiler / 'bin') + ';' + str(args.qt / 'bin') + ';' + env['PATH']
    deployed = subprocess.run([str(args.qt / 'bin/windeployqt.exe'), '--release', '--compiler-runtime', '--no-translations', '--dir', str(output), str(output / exe.name)], env=env, capture_output=True, timeout=120)
    (output / 'docs').mkdir(exist_ok=True)
    (output / 'docs/deploy.log').write_bytes(deployed.stdout + b'\n' + deployed.stderr)
    if deployed.returncode:
        raise SystemExit('windeployqt failed; preserved partial release for inspection')
    shutil.copy2(native / 'USER_GUIDE.md', output / 'docs/USER_GUIDE.md')
    shutil.copytree(native / 'plans', output / 'plans')
    profile = repo / 'PC/Devices/profiles/c620_m3508_lab_v1.json'
    (output / 'profiles').mkdir(exist_ok=True)
    shutil.copy2(profile, output / 'profiles' / profile.name)
    (output / 'examples').mkdir()
    shutil.copy2(native / 'tests/synthetic-feedback.jsonl', output / 'examples/synthetic-feedback.jsonl')
    shutil.copytree(native, output / 'source/PC/NativeQt', ignore=shutil.ignore_patterns('__pycache__', '*.pyc', '.git', 'build'))
    source_profile = output / 'source/PC/Devices/profiles'
    source_profile.mkdir(parents=True)
    shutil.copy2(profile, source_profile / profile.name)
    licenses = output / 'licenses'
    licenses.mkdir()
    shutil.copytree(native / 'third_party/qt-licenses', licenses / 'Qt')
    shutil.copy2(native / 'third_party/qcustomplot/GPL.txt', licenses / 'QCustomPlot-GPL.txt')
    for name in ['gcc', 'winpthreads', 'mingw-w64']:
        shutil.copytree(args.compiler / 'licenses' / name, licenses / name)
    notice = ('本包使用Qt 6.8.3动态库、QCustomPlot 2.1.1及MinGW运行库。\n'
              '对应许可文本位于本目录；QCustomPlot上游源码及本应用对应源代码位于source。\n'
              '本地研发交付不构成商业闭源分发授权；对外分发需按实际许可安排。\n'
              'Qt来源：https://download.qt.io/official_releases/qt/6.8/6.8.3/\n'
              'QCustomPlot来源：https://www.qcustomplot.com/release/2.1.1/QCustomPlot-source.tar.gz\n'
              'MinGW运行库说明：https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html\n')
    (licenses / 'NOTICE.txt').write_text(notice, encoding='utf-8')
    (output / '开始使用.txt').write_text('机器人关节伺服驱动与安全控制平台 1.0.0\n\n双击MotorTestPlatformNative.exe。完整保留本目录文件。\n软件内按F1打开使用说明书。\n启动不会连接端口或运行任务；正常测试结束保持串口连接。\n\n主机/离线部署验证通过，当前C++版本受影响的真实平台回归待现场完成。\n当前控制为受约束电流raw，不是速度/位置闭环；详情见说明书第1节。\n', encoding='utf-8')
    # Start in an environment with no SDK/compiler/Python paths or plugin overrides.
    offline = os.environ.copy()
    for key in ['QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QTDIR', 'QML_IMPORT_PATH']:
        offline.pop(key, None)
    offline['PATH'] = os.environ['SystemRoot'] + '\\System32;' + os.environ['SystemRoot']
    verify = output / 'verification'
    verify.mkdir()
    cases = []
    for page, size in [('monitor', '1280,900'), ('tasks', '1000,760'), ('history', '1280,900'), ('maintenance', '860,680')]:
        stem = page + '-' + size.replace(',', 'x')
        report = verify / (stem + '.json')
        run = subprocess.run([str(output / exe.name), '--replay', str(output / 'examples/synthetic-feedback.jsonl'), '--self-test', '--page', page, '--size', size, '--report', str(report), '--screenshot', str(verify / (stem + '.png'))], cwd=verify, env=offline, capture_output=True, timeout=25)
        (verify / (stem + '.log')).write_bytes(run.stdout + run.stderr)
        if run.returncode or not report.exists():
            raise SystemExit(f'Offline startup failed: {stem}, exit {run.returncode}')
        result = json.loads(report.read_text(encoding='utf-8'))
        assert result['status'] == 'passed' and result['serial_opened'] is False
        cases.append({'page': page, 'size': size, 'serial_opened': False, 'status': 'passed'})
    manifest = {'product': 'MotorTestPlatform', 'version': '1.0.0', 'implementation': 'C++17 Qt Widgets QCustomPlot', 'scope': 'Host tests and SDK-free offline deployment; native field regression pending', 'startup_cases': cases,
                'files': {str(p.relative_to(output)).replace('\\', '/'): sha(p) for p in sorted(output.rglob('*')) if p.is_file()}}
    (output / 'release-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in sorted(output.rglob('*')):
            if p.is_file():
                z.write(p, Path(output.name) / p.relative_to(output))
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
    print(json.dumps({'directory': str(output), 'zip': str(archive), 'zip_sha256': sha(archive), 'exe_sha256': sha(output / exe.name), 'files': len(manifest['files']) + 1, 'sdk_free_startup': cases}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
