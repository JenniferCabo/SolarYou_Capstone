param([string]$DashboardPath, [string]$PythonPath = 'python')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$main = Join-Path $taskRoot 'ESP_SolarYou/main'
$tests = Join-Path $taskRoot 'tests'
$taskBin = Join-Path $tests 'bin'
New-Item -ItemType Directory -Path $taskBin -Force | Out-Null
$env:TEMP = $taskBin
$env:TMP = $taskBin

function Invoke-HostCheck {
    param([string]$Name, [string[]]$CompilerArguments)
    $executable = Join-Path $taskBin ($Name + '.exe')
    & gcc @CompilerArguments -o $executable
    if ($LASTEXITCODE -ne 0) { throw "$Name failed to compile" }
    & $executable
    if ($LASTEXITCODE -ne 0) { throw "$Name failed" }
}

$strict = @('-std=c11', '-Wall', '-Wextra', '-Wconversion', '-Werror', '-I', $main)
$protocol = Join-Path $main 'ble_protocol.c'
Invoke-HostCheck 'protocol' ($strict + @($protocol, (Join-Path $tests 'ble_host_sim.c'), '-lm'))
Invoke-HostCheck 'adapter' ($strict + @('-Wno-misleading-indentation', '-I', (Join-Path $tests 'esp_stubs'), $protocol, (Join-Path $tests 'ble_adapter_sim.c'), '-lm'))
Invoke-HostCheck 'adapter-esp-only' ($strict + @('-DCONFIG_SOLARYOU_RUNTIME_ESP_ONLY_TEST=1', '-Wno-misleading-indentation', '-I', (Join-Path $tests 'esp_stubs'), $protocol, (Join-Path $tests 'ble_adapter_sim.c'), '-lm'))

foreach ($control in @(0, 1)) {
    $modeFlag = "-DCONFIG_SOLARYOU_RUNTIME_FULL_CONTROL=$control"
    Invoke-HostCheck "bridge-control-$control" ($strict + @($modeFlag, '-I', (Join-Path $tests 'bridge_stubs'), $protocol, (Join-Path $tests 'ble_app_sim.c'), '-lm'))
    foreach ($bluetooth in @(0, 1)) {
        $includes = @('-I', (Join-Path $tests 'startup_stubs'))
        foreach ($component in @('sensor_service', 'system_state', 'motor_control', 'pid_stabilization')) {
            $includes += @('-I', (Join-Path $taskRoot "ESP_SolarYou/components/$component/include"))
        }
        Invoke-HostCheck "application-control-$control-ble-$bluetooth" ($strict + $includes + @(
            $modeFlag, "-DCONFIG_SOLARYOU_BLE_TELEMETRY=$bluetooth", $protocol,
            (Join-Path $taskRoot 'ESP_SolarYou/components/system_state/system_state.c'),
            (Join-Path $tests 'application_ble_sim.c'), '-lm'))
    }
}
$espOnlyArguments = $strict + @(
    '-DCONFIG_SOLARYOU_RUNTIME_ESP_ONLY_TEST=1', '-DCONFIG_SOLARYOU_BLE_TELEMETRY=1',
    '-I', (Join-Path $tests 'startup_stubs'),
    '-I', (Join-Path $taskRoot 'ESP_SolarYou/components/sensor_service/include'),
    '-I', (Join-Path $taskRoot 'ESP_SolarYou/components/system_state/include'),
    '-I', (Join-Path $taskRoot 'ESP_SolarYou/components/motor_control/include'),
    '-I', (Join-Path $taskRoot 'ESP_SolarYou/components/pid_stabilization/include'),
    $protocol, (Join-Path $tests 'esp_only_sim.c'), '-lm')
Invoke-HostCheck 'esp-only' $espOnlyArguments
if ($DashboardPath) {
    $library = Join-Path $taskBin 'ble_protocol.dll'
    & gcc @strict -shared '-Wl,--export-all-symbols' $protocol -o $library
    if ($LASTEXITCODE -ne 0) { throw 'Protocol interoperability library failed to compile' }
    & $PythonPath (Join-Path $tests 'test_dashboard_interop.py') $DashboardPath $library
    if ($LASTEXITCODE -ne 0) { throw 'Dashboard interoperability checks failed' }
}
Write-Output 'All BLE host checks passed.'
