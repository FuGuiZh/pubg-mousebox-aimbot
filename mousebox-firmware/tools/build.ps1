param([string]$ArduinoCliPath = '')
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$taskCli = Resolve-ArduinoCli $ArduinoCliPath
$taskPrevious = [Environment]::GetEnvironmentVariable('ARDUINO_DIRECTORIES_USER', 'Process')
try {
    # A process-only override: global Arduino configuration stays untouched.
    [Environment]::SetEnvironmentVariable('ARDUINO_DIRECTORIES_USER', $taskRoot, 'Process')
    & $taskCli compile --fqbn logitech-relay:avr:receiver `
        --build-path (Join-Path $taskRoot 'build\native-v2') `
        (Join-Path $taskRoot 'firmware\logitech_native_bridge')
    if ($LASTEXITCODE -ne 0) { throw 'Firmware compilation failed.' }
} finally {
    [Environment]::SetEnvironmentVariable('ARDUINO_DIRECTORIES_USER', $taskPrevious, 'Process')
}
