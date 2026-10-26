param(
 [string]$QtRoot=$env:QT_ROOT,
 [string]$CompilerRoot=$env:MINGW_ROOT,
 [string]$CMakeExe='cmake',
 [string]$NinjaExe='ninja',
 [string]$BuildDirectory="$PSScriptRoot/../../build/native-qt",
 [switch]$RunTests
)
$ErrorActionPreference='Stop'
if (-not $QtRoot -or -not $CompilerRoot) { throw 'Specify -QtRoot and -CompilerRoot, or set QT_ROOT and MINGW_ROOT.' }
$env:PATH="$CompilerRoot/bin;$QtRoot/bin;"+$env:PATH
& $CMakeExe -S $PSScriptRoot -B $BuildDirectory -G Ninja "-DCMAKE_MAKE_PROGRAM=$NinjaExe" "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_CXX_COMPILER=$CompilerRoot/bin/g++.exe" -DCMAKE_BUILD_TYPE=Release
if($LASTEXITCODE -ne 0){throw 'CMake configure failed'}
& $CMakeExe --build $BuildDirectory --parallel 4
if($LASTEXITCODE -ne 0){throw 'C++ build failed'}
if($RunTests){
 foreach($suite in @('native_protocol_tests','native_monitor_tests','native_operator_tests','native_plan_tests','native_controller_tests','native_audit_tests','native_serial_tool_tests')){
  & (Join-Path $BuildDirectory "$suite.exe") -o "$(Join-Path $BuildDirectory "$suite-latest.txt"),txt"
  if($LASTEXITCODE -ne 0){throw "Failed: $suite"}; Get-Content -Encoding UTF8 -LiteralPath (Join-Path $BuildDirectory "$suite-latest.txt")
 }
}
