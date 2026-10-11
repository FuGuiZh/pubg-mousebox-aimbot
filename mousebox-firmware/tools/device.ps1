param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('List', 'Backup', 'Upload', 'Monitor')]
    [string]$Action,
    [string]$RuntimePort = 'COM12',
    [string]$RuntimeVendorId = '046D',
    [string]$RuntimeProductId = 'C54D',
    [string]$RuntimeInterface = '03',
    [string]$BackupName = 'backup-native-v1-20261012',
    [string]$HexPath = '',
    [ValidateRange(1, 60)] [int]$MonitorSeconds = 15
)
. (Join-Path $PSScriptRoot 'toolchain.ps1')
if ($BackupName -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid backup name.' }
foreach ($taskId in @($RuntimeVendorId, $RuntimeProductId)) {
    if ($taskId -notmatch '^[a-fA-F0-9]{4}$') { throw 'USB VID/PID must be four hex digits.' }
}
if ($RuntimeInterface -notmatch '^[a-fA-F0-9]{2}$') { throw 'USB interface must be two hex digits.' }
$taskBackup = Join-Path $taskRoot ('backups\' + $BackupName)

function Assert-RuntimeDevice {
    $taskPattern = '^USB\\VID_' + $RuntimeVendorId + '&PID_' + $RuntimeProductId + '&MI_' + $RuntimeInterface + '\\'
    $taskDevices = @(Get-CimInstance Win32_SerialPort | Where-Object {
        $_.DeviceID -eq $RuntimePort -and $_.PNPDeviceID -match $taskPattern
    })
    if ($taskDevices.Count -ne 1) { throw "The expected $RuntimePort / USB identity is missing. Refusing a different device." }
    return $taskDevices[0]
}

function Enter-Bootloader {
    $taskDevice = Assert-RuntimeDevice
    $taskBootPattern = '^USB\\VID_(2341|2A03)&PID_003[67]\\'
    if (@(Get-CimInstance Win32_SerialPort | Where-Object { $_.PNPDeviceID -match $taskBootPattern }).Count) {
        throw 'An existing bootloader makes the target ambiguous.'
    }
    Write-Host "Resetting only $($taskDevice.DeviceID) into its existing bootloader."
    $taskSerial = [IO.Ports.SerialPort]::new($RuntimePort, 1200)
    try { $taskSerial.DtrEnable = $true; $taskSerial.Open(); $taskSerial.DtrEnable = $false }
    finally { $taskSerial.Dispose() }
    $taskWatch = [Diagnostics.Stopwatch]::StartNew()
    while ($taskWatch.ElapsedMilliseconds -lt 8000) {
        $taskBoot = @(Get-CimInstance Win32_SerialPort | Where-Object { $_.PNPDeviceID -match $taskBootPattern })
        if ($taskBoot.Count -gt 1) { throw 'Multiple bootloaders detected.' }
        if ($taskBoot.Count -eq 1) {
            Write-Host "Bootloader: $($taskBoot[0].DeviceID), $($taskBoot[0].PNPDeviceID)"
            return $taskBoot[0].DeviceID
        }
        Start-Sleep -Milliseconds 150
    }
    throw 'Bootloader not found; no flash operation performed.'
}

if ($Action -eq 'List') {
    Get-CimInstance Win32_SerialPort | Select-Object DeviceID, Name, PNPDeviceID
    return
}
if ($Action -eq 'Monitor') {
    $null = Assert-RuntimeDevice
    $taskSerial = [IO.Ports.SerialPort]::new($RuntimePort, 115200)
    try {
        $taskSerial.DtrEnable = $true; $taskSerial.ReadTimeout = 250; $taskSerial.Open()
        $taskWatch = [Diagnostics.Stopwatch]::StartNew()
        while ($taskWatch.Elapsed.TotalSeconds -lt $MonitorSeconds) {
            try { Write-Output $taskSerial.ReadLine().TrimEnd("`r") }
            catch [TimeoutException] { }
        }
    } finally { $taskSerial.Dispose() }
    return
}
$taskAvrdude = Resolve-Avrdude
if ($Action -eq 'Backup') {
    if (Test-Path -LiteralPath $taskBackup) { throw 'Backup directory already exists; will not overwrite it.' }
    New-Item -ItemType Directory -Path $taskBackup | Out-Null
    Copy-Item -LiteralPath (Join-Path $taskRoot 'firmware\logitech_native_bridge') -Destination (Join-Path $taskBackup 'source') -Recurse
    Copy-Item -LiteralPath (Join-Path $taskRoot 'hardware\logitech-relay') -Destination (Join-Path $taskBackup 'platform') -Recurse
    $taskPort = Enter-Bootloader
    & $taskAvrdude.Exe -C $taskAvrdude.Config -p atmega32u4 -c avr109 -P $taskPort -b 57600 -A -D -n `
        '-U' ('flash:r:' + (Join-Path $taskBackup 'flash-before.bin') + ':r') `
        '-U' ('eeprom:r:' + (Join-Path $taskBackup 'eeprom-before.bin') + ':r') `
        '-U' ('flash:v:' + (Join-Path $taskBackup 'flash-before.bin') + ':r')
    if ($LASTEXITCODE -ne 0) { throw 'Read-only backup failed.' }
    Get-FileHash -LiteralPath (Join-Path $taskBackup 'flash-before.bin'),(Join-Path $taskBackup 'eeprom-before.bin') -Algorithm SHA256
    return
}
$taskBackupFile = Join-Path $taskBackup 'flash-before.bin'
if (!(Test-Path -LiteralPath $taskBackupFile) -or (Get-Item -LiteralPath $taskBackupFile).Length -ne 32768) {
    throw 'A verified 32 KiB pre-upload flash backup is required.'
}
$taskHex = if ($HexPath) { (Resolve-Path -LiteralPath $HexPath).Path } else { Join-Path $taskRoot 'build\native-v2\logitech_native_bridge.ino.hex' }
if (!(Test-Path -LiteralPath $taskHex)) { throw 'Compiled application HEX is missing.' }
$taskPort = Enter-Bootloader
# Application flash only; automatic readback verification remains enabled.
# Never write the full 32 KiB backup into the application flash region.
& $taskAvrdude.Exe -C $taskAvrdude.Config -p atmega32u4 -c avr109 -P $taskPort -b 57600 -D '-U' ('flash:w:' + $taskHex + ':i')
if ($LASTEXITCODE -ne 0) { throw 'Application upload/readback verification failed.' }
