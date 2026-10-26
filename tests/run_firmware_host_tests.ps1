param(
    [switch]$SkipArmclang
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$buildRoot = Join-Path $tempRoot ("c_board_p4_tests_" + [guid]::NewGuid().ToString('N'))
$buildRoot = [IO.Path]::GetFullPath($buildRoot)

if (-not $buildRoot.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Temporary build directory escaped the system temp path: $buildRoot"
}

function Invoke-Ctest {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [Parameter(Mandatory)] [string[]]$Includes,
        [Parameter(Mandatory)] [string[]]$Sources
    )

    $output = Join-Path $buildRoot ($Name + '.exe')
    & gcc -std=c99 -Wall -Wextra -Werror @Includes @Sources -o $output
    if ($LASTEXITCODE -ne 0) {
        throw "C compilation failed: $Name"
    }
    & $output
    if ($LASTEXITCODE -ne 0) {
        throw "C test failed: $Name, exit=$LASTEXITCODE"
    }
}

New-Item -ItemType Directory -Path $buildRoot | Out-Null
Push-Location $repoRoot
try {
    $taskIncludes = @('-IModules/Task')
    Invoke-Ctest operator_session @('-IModules/Task', '-IModules/Safety') @(
        'tests/test_operator_session.c',
        'Modules/Safety/operator_session.c',
        'Modules/Task/test_task_schema.c'
    )
    Invoke-Ctest operator_receiver $taskIncludes @(
        'tests/test_operator_receiver.c',
        'Modules/Task/test_task_receiver.c',
        'Modules/Task/operator_wire_codec.c',
        'Modules/Task/test_task_wire_codec.c',
        'Modules/Task/test_task_schema.c',
        'Modules/Task/control_status_wire_codec.c',
        'Modules/Task/telemetry_wire_codec.c',
        'Modules/Task/device_info_wire_codec.c'
    )
    $mockUartIncludes = @('-Itests/mocks', '-IBSP/Uart', '-IModules/Task')
    Invoke-Ctest operator_controller @('-IModules/Task', '-IModules/Safety') @(
        'tests/test_operator_controller.c',
        'Modules/Safety/operator_controller.c',
        'Modules/Safety/operator_session.c',
        'Modules/Task/test_task_schema.c'
    )
    Invoke-Ctest operator_uart $mockUartIncludes @(
        'tests/test_operator_uart.c',
        'tests/mocks/task_uart_platform_mock.c',
        'tests/mocks/device_info_platform_mock.c',
        'BSP/Uart/task_uart_adapter.c',
        'Modules/Task/test_task_receiver.c',
        'Modules/Task/operator_wire_codec.c',
        'Modules/Task/test_task_wire_codec.c',
        'Modules/Task/test_task_schema.c',
        'Modules/Task/control_status_wire_codec.c',
        'Modules/Task/telemetry_wire_codec.c',
        'Modules/Task/device_info_wire_codec.c',
        'Modules/Task/test_task_result_wire_codec.c'
    )
    $fullIncludes = @(
        '-DSTM32F407xx', '-include', 'stdint.h',
        '-Itests/mocks', '-Itests/board', '-ICore/Inc', '-IBSP/Can', '-IBSP/Uart', '-IModules/Task',
        '-IModules/Recorder', '-IModules/Motion', '-IModules/Safety',
        '-IModules/C620', '-IDrivers/STM32F4xx_HAL_Driver/Inc',
        '-IDrivers/CMSIS/Device/ST/STM32F4xx/Include',
        '-IDrivers/CMSIS/Include'
    )

    Invoke-Ctest operator_runtime $fullIncludes @(
        'tests/test_operator_runtime.c',
        'Modules/Safety/operator_runtime.c',
        'Modules/Safety/operator_controller.c',
        'Modules/Safety/operator_session.c',
        'Modules/Safety/safety_state_machine.c',
        'Modules/Task/test_task_runtime.c',
        'Modules/Task/test_task_schema.c',
        'Modules/Recorder/test_record_buffer.c',
        'Modules/Motion/c620_motion_gate.c',
        'Modules/C620/c620_control_output.c',
        'Modules/C620/c620_control_codec.c',
        'Modules/C620/c620_feedback.c'
    )
    & (Join-Path $buildRoot 'operator_runtime.exe') boot-unavailable
    if ($LASTEXITCODE -ne 0) { throw 'Boot-unavailable fail-closed test failed' }
    Invoke-Ctest task_wire $taskIncludes @(
        'tests/test_task_wire_codec.c',
        'Modules/Task/test_task_wire_codec.c',
        'Modules/Task/test_task_schema.c'
    )
    Invoke-Ctest task_receiver $taskIncludes @(
        'tests/test_task_receiver.c',
        'Modules/Task/test_task_receiver.c',
        'Modules/Task/control_status_wire_codec.c',
        'Modules/Task/telemetry_wire_codec.c',
        'Modules/Task/device_info_wire_codec.c',
        'Modules/Task/operator_wire_codec.c',
        'Modules/Task/test_task_wire_codec.c',
        'Modules/Task/test_task_schema.c'
    )
    Invoke-Ctest task_result_wire $taskIncludes @(
        'tests/test_task_result_wire_codec.c',
        'Modules/Task/test_task_result_wire_codec.c'
    )
    Invoke-Ctest control_status_wire $taskIncludes @(
        'tests/test_control_status_wire_codec.c',
        'Modules/Task/control_status_wire_codec.c'
    )
    Invoke-Ctest telemetry_wire $taskIncludes @(
        'tests/test_telemetry_wire_codec.c',
        'Modules/Task/telemetry_wire_codec.c'
    )
    Invoke-Ctest device_info_wire $taskIncludes @(
        'tests/test_device_info_wire_codec.c',
        'Modules/Task/device_info_wire_codec.c',
        'Modules/Task/operator_wire_codec.c',
        'Modules/Task/test_task_receiver.c',
        'Modules/Task/control_status_wire_codec.c',
        'Modules/Task/telemetry_wire_codec.c',
        'Modules/Task/test_task_wire_codec.c',
        'Modules/Task/test_task_schema.c'
    )
    Invoke-Ctest task_uart_adapter $mockUartIncludes @(
        'tests/test_task_uart_adapter.c',
        'tests/mocks/task_uart_platform_mock.c',
        'BSP/Uart/task_uart_adapter.c',
        'tests/mocks/device_info_platform_mock.c',
        'Modules/Task/test_task_receiver.c',
        'Modules/Task/control_status_wire_codec.c',
        'Modules/Task/telemetry_wire_codec.c',
        'Modules/Task/device_info_wire_codec.c',
        'Modules/Task/operator_wire_codec.c',
        'Modules/Task/test_task_wire_codec.c',
        'Modules/Task/test_task_result_wire_codec.c',
        'Modules/Task/test_task_schema.c'
    )
    Invoke-Ctest uart_concurrency $mockUartIncludes @(
        'tests/test_uart_concurrency.c',
        'BSP/Uart/task_uart_adapter.c',
        'tests/mocks/device_info_platform_mock.c',
        'Modules/Task/test_task_receiver.c',
        'Modules/Task/control_status_wire_codec.c',
        'Modules/Task/telemetry_wire_codec.c',
        'Modules/Task/device_info_wire_codec.c',
        'Modules/Task/operator_wire_codec.c',
        'Modules/Task/test_task_wire_codec.c',
        'Modules/Task/test_task_result_wire_codec.c',
        'Modules/Task/test_task_schema.c'
    )
    Invoke-Ctest p4_modules $fullIncludes @(
        'tests/test_p4_modules.c',
        'Modules/Task/test_task_runtime.c',
        'Modules/Task/test_task_schema.c',
        'Modules/Recorder/test_record_buffer.c',
        'Modules/Motion/c620_motion_gate.c',
        'Modules/C620/c620_control_output.c',
        'Modules/C620/c620_control_codec.c',
        'Modules/Safety/safety_state_machine.c',
        'Modules/C620/c620_feedback.c'
    )
    Invoke-Ctest c620_control_codec @('-IModules/C620') @(
        'tests/test_c620_control_codec.c',
        'Modules/C620/c620_control_codec.c'
    )
    Invoke-Ctest c620_control_output @('-IModules/C620') @(
        'tests/test_c620_control_output.c',
        'Modules/C620/c620_control_output.c',
        'Modules/C620/c620_control_codec.c'
    )
    Invoke-Ctest c620_motion_gate $fullIncludes @(
        'tests/test_c620_motion_gate.c',
        'Modules/Motion/c620_motion_gate.c',
        'Modules/C620/c620_control_output.c',
        'Modules/C620/c620_control_codec.c',
        'Modules/Safety/safety_state_machine.c',
        'Modules/C620/c620_feedback.c'
    )
    Invoke-Ctest task_ingress_service $fullIncludes @(
        'tests/test_task_ingress_service.c',
        'tests/mocks/task_uart_platform_mock.c',
        'BSP/Uart/task_uart_adapter.c',
        'tests/mocks/device_info_platform_mock.c',
        'Modules/Task/test_task_ingress_service.c',
        'Modules/Task/test_task_runtime.c',
        'Modules/Task/test_task_receiver.c',
        'Modules/Task/control_status_wire_codec.c',
        'Modules/Task/telemetry_wire_codec.c',
        'Modules/Task/device_info_wire_codec.c',
        'Modules/Task/operator_wire_codec.c',
        'Modules/Task/test_task_wire_codec.c',
        'Modules/Task/test_task_result_wire_codec.c',
        'Modules/Task/test_task_schema.c',
        'Modules/Recorder/test_record_buffer.c',
        'Modules/Motion/c620_motion_gate.c',
        'Modules/C620/c620_control_output.c',
        'Modules/C620/c620_control_codec.c',
        'Modules/Safety/safety_state_machine.c',
        'Modules/C620/c620_feedback.c'
    )
    Invoke-Ctest app_main_task_service $fullIncludes @(
        'tests/test_app_main_task_service.c',
        'Modules/Task/app_main_task_service.c'
    )
    Invoke-Ctest p4_ingress_smoke $fullIncludes @(
        'tests/test_p4_ingress_smoke_host.c',
        'tests/board/p4_ingress_smoke_test.c',
        'Modules/Task/test_task_runtime.c',
        'Modules/Task/test_task_receiver.c',
        'Modules/Task/control_status_wire_codec.c',
        'Modules/Task/telemetry_wire_codec.c',
        'Modules/Task/device_info_wire_codec.c',
        'Modules/Task/operator_wire_codec.c',
        'Modules/Task/test_task_wire_codec.c',
        'Modules/Task/test_task_schema.c',
        'Modules/Recorder/test_record_buffer.c',
        'Modules/Motion/c620_motion_gate.c',
        'Modules/C620/c620_control_output.c',
        'Modules/C620/c620_control_codec.c',
        'Modules/Safety/safety_state_machine.c',
        'Modules/C620/c620_feedback.c'
    )

    if (-not $SkipArmclang) {
        $armclang = 'C:\Keil_v5\ARM\ARMCLANG\bin\armclang.exe'
        if (-not (Test-Path -LiteralPath $armclang)) {
            throw "ArmClang not found: $armclang"
        }
        $armIncludes = @(
            '-ICore/Inc', '-IBSP/Can', '-IBSP/Uart', '-IModules/Task',
            '-IModules/Recorder', '-IModules/Motion', '-IModules/Safety',
            '-IModules/C620', '-IDrivers/STM32F4xx_HAL_Driver/Inc',
            '-IDrivers/STM32F4xx_HAL_Driver/Inc/Legacy',
            '-IDrivers/CMSIS/Device/ST/STM32F4xx/Include',
            '-IDrivers/CMSIS/Include'
        )
        $armArgs = @(
            '--target=arm-arm-none-eabi', '-mcpu=cortex-m4', '-std=c99',
            '-DSTM32F407xx', '-DUSE_HAL_DRIVER'
        ) + $armIncludes + @(
            '-fsyntax-only',
            'BSP/Uart/task_uart_adapter.c',
            'BSP/Uart/device_info_platform_stm32.c',
            'BSP/Uart/task_uart_platform_stm32.c',
            'Modules/Task/app_main_task_service.c',
            'Modules/Task/test_task_ingress_service.c',
            'Modules/Task/test_task_result_wire_codec.c',
            'Modules/Task/control_status_wire_codec.c',
            'Modules/Task/telemetry_wire_codec.c',
            'Modules/Task/device_info_wire_codec.c',
            'Modules/Task/operator_wire_codec.c',
            'Modules/Motion/c620_motion_gate.c',
            'Modules/Safety/safety_state_machine.c',
            'Modules/Safety/operator_session.c',
            'Modules/Safety/operator_controller.c',
            'Modules/Safety/operator_runtime.c',
            'BSP/Uart/operator_platform_stm32.c',
            'Modules/C620/c620_control_output.c',
            'Modules/C620/c620_control_codec.c'
        )
        & $armclang @armArgs
        if ($LASTEXITCODE -ne 0) {
            throw "ArmClang syntax check failed"
        }
        Write-Output 'armclang_p4_syntax=pass'
    }

    Write-Output 'p4_all_host_tests=pass'
}
finally {
    Pop-Location
    if (Test-Path -LiteralPath $buildRoot) {
        $resolvedBuild = (Resolve-Path -LiteralPath $buildRoot).Path
        $safeName = Split-Path -Leaf $resolvedBuild
        if ((-not $resolvedBuild.StartsWith(
                    $tempRoot,
                    [StringComparison]::OrdinalIgnoreCase)) -or
            ($safeName -notlike 'c_board_p4_tests_*')) {
            throw "Refusing to remove unexpected temporary path: $resolvedBuild"
        }
        Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
    }
}
