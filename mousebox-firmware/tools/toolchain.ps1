$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

function Resolve-ArduinoCli {
    param([string]$ExplicitPath = '')
    if ($ExplicitPath) { return (Resolve-Path -LiteralPath $ExplicitPath).Path }
    $taskCommand = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($taskCommand) { return $taskCommand.Source }
    $taskCandidates = @(
        'D:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe',
        (Join-Path $env:ProgramFiles 'Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe')
    )
    foreach ($taskCandidate in $taskCandidates) {
        if (Test-Path -LiteralPath $taskCandidate) { return $taskCandidate }
    }
    throw 'Arduino CLI not found. Supply -ArduinoCliPath explicitly.'
}

function Resolve-Avrdude {
    $taskData = if ($env:ARDUINO_DIRECTORIES_DATA) { $env:ARDUINO_DIRECTORIES_DATA } else { Join-Path $env:LOCALAPPDATA 'Arduino15' }
    $taskBase = Join-Path $taskData 'packages\arduino\tools\avrdude'
    $taskVersions = @(Get-ChildItem -LiteralPath $taskBase -Directory | Sort-Object Name -Descending)
    foreach ($taskVersion in $taskVersions) {
        $taskExe = Join-Path $taskVersion.FullName 'bin\avrdude.exe'
        $taskConfig = Join-Path $taskVersion.FullName 'etc\avrdude.conf'
        if ((Test-Path -LiteralPath $taskExe) -and (Test-Path -LiteralPath $taskConfig)) {
            return @{ Exe = $taskExe; Config = $taskConfig }
        }
    }
    throw 'AVRDUDE not found in the installed Arduino AVR platform.'
}
