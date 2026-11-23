param([string]$PythonPath)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if (-not $PythonPath) { $PythonPath = Join-Path $taskRoot '.venv\Scripts\python.exe' }
$taskTemp = Join-Path $taskRoot 'tmp'
New-Item -ItemType Directory -Path $taskTemp -Force | Out-Null
$env:TEMP = $taskTemp
$env:TMP = $taskTemp
$main = Join-Path $taskRoot 'firmware\ESP_SolarYou\main'
& gcc -std=c11 -Wall -Wextra -Wconversion -Werror -I $main (Join-Path $main 'ble_protocol.c') (Join-Path $taskRoot 'tests\ble_host_sim.c') -lm -o (Join-Path $taskTemp 'ble_host_sim.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $taskTemp 'ble_host_sim.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& gcc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -I (Join-Path $taskRoot 'tests\esp_stubs') -I $main (Join-Path $main 'ble_protocol.c') (Join-Path $taskRoot 'tests\ble_adapter_sim.c') -lm -o (Join-Path $taskTemp 'ble_adapter_sim.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $taskTemp 'ble_adapter_sim.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
foreach ($mode in @('motoroff', 'control')) {
    $bridgeFlags = @()
    if ($mode -eq 'control') { $bridgeFlags += '-DCONFIG_SOLARYOU_RUNTIME_FULL_CONTROL=1' }
    $bridgeExe = Join-Path $taskTemp ("ble_app_" + $mode + "_sim.exe")
    & gcc -std=c11 -Wall -Wextra -Wconversion -Werror @bridgeFlags -I (Join-Path $taskRoot 'tests\bridge_stubs') -I $main -I (Join-Path $taskRoot 'firmware\ESP_SolarYou\components\sensor_service\include') (Join-Path $main 'ble_protocol.c') (Join-Path $taskRoot 'tests\ble_app_sim.c') -lm -o $bridgeExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $bridgeExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $startupExe = Join-Path $taskTemp ("app_startup_" + $mode + "_sim.exe")
    & gcc -std=c11 -Wall -Wextra -Wconversion -Werror @bridgeFlags -I (Join-Path $taskRoot 'tests\startup_stubs') -I $main -I (Join-Path $taskRoot 'firmware\ESP_SolarYou\components\sensor_service\include') -I (Join-Path $taskRoot 'firmware\ESP_SolarYou\components\system_state\include') -I (Join-Path $taskRoot 'firmware\ESP_SolarYou\components\motor_control\include') -I (Join-Path $taskRoot 'firmware\ESP_SolarYou\components\pid_stabilization\include') (Join-Path $taskRoot 'firmware\ESP_SolarYou\components\system_state\system_state.c') (Join-Path $taskRoot 'tests\app_startup_sim.c') -o $startupExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $startupExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
$powerExe = Join-Path $taskTemp 'power_io_sim.exe'
& g++ -std=c++11 -Wall -Wextra -Werror -DCONFIG_IDF_TARGET_ESP32S3=1 -I (Join-Path $taskRoot 'tests\power_stubs') -I (Join-Path $taskRoot 'firmware\ESP_SolarYou\components\adafruit_ina219\vendor') -I (Join-Path $taskRoot 'firmware\ESP_SolarYou\components\sensor_service\include') -I (Join-Path $taskRoot 'firmware\ESP_SolarYou\components\board_config\include') (Join-Path $taskRoot 'tests\power_io_sim.cpp') (Join-Path $taskRoot 'firmware\ESP_SolarYou\components\adafruit_ina219\vendor\Adafruit_INA219.cpp') (Join-Path $taskRoot 'firmware\ESP_SolarYou\components\sensor_service\power_sensor.cpp') (Join-Path $taskRoot 'firmware\ESP_SolarYou\components\sensor_service\sensor_status.cpp') (Join-Path $taskRoot 'firmware\ESP_SolarYou\components\sensor_service\sensor_math.cpp') -o $powerExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $powerExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$dllPath = Join-Path $taskTemp 'ble_protocol.dll'
if (-not (Test-Path -LiteralPath $dllPath) -or (Get-Item -LiteralPath $dllPath).LastWriteTime -lt (Get-Item -LiteralPath (Join-Path $main 'ble_protocol.c')).LastWriteTime) {
    & gcc -std=c11 -Wall -Wextra -Wconversion -Werror -shared '-Wl,--export-all-symbols' (Join-Path $main 'ble_protocol.c') -o $dllPath
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& $PythonPath -m unittest discover -s (Join-Path $taskRoot 'dashboard\dashboard\tests') -v
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& node --check (Join-Path $taskRoot 'dashboard\dashboard\static\js\dashboard.js')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& node --check (Join-Path $taskRoot 'dashboard\dashboard\static\js\telemetry-rate.js')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& node (Join-Path $taskRoot 'tools\test_rate_dashboard.cjs')
exit $LASTEXITCODE
