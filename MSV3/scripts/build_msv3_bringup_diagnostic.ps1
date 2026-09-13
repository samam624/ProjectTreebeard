param(
  [Parameter(Mandatory = $true)]
  [ValidateRange(0, 255)]
  [int]$SensorId,
  [Parameter(Mandatory = $true)]
  [ValidateRange(1, 4294967294)]
  [uint32]$ApplicationVersion,
  [Parameter(Mandatory = $true)]
  [ValidatePattern('^[A-Za-z0-9._-]+$')]
  [string]$BuildTag,
  [string]$BootloaderRelease = "1.0.3",
  [string]$SigningKey,
  [string]$Python
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$msv3Root = (Resolve-Path (Join-Path $scriptDir "..")).Path
$repoRoot = (Resolve-Path (Join-Path $msv3Root "..")).Path
. (Join-Path $scriptDir "msv3_python.ps1")
$pythonExe = Resolve-Msv3Python -Explicit $Python -Msv3Root $msv3Root

$fleetPath = Join-Path $msv3Root "fleet.json"
$fleetProbe = (Get-Content -LiteralPath $fleetPath -Raw | ConvertFrom-Json).probes |
  Where-Object { [int]$_.sensor_id -eq $SensorId } |
  Select-Object -First 1
if ($null -eq $fleetProbe) {
  throw "Sensor $SensorId is not registered in $fleetPath"
}
if ($fleetProbe.bootloader -ne "msv3-$BootloaderRelease" -or !$fleetProbe.ota_capable) {
  throw "Probe $SensorId is not registered OTA-capable on msv3-$BootloaderRelease."
}
if ($ApplicationVersion -le [uint32]$fleetProbe.installed_application_version) {
  throw "Diagnostic version $ApplicationVersion must be above Probe $SensorId's recorded version $($fleetProbe.installed_application_version)."
}

$keyPath = if ([string]::IsNullOrWhiteSpace($SigningKey)) {
  Join-Path $msv3Root ".secrets\msv3-dfu-signing-private.pem"
} elseif ([IO.Path]::IsPathRooted($SigningKey)) {
  $SigningKey
} else {
  Join-Path $repoRoot $SigningKey
}
if (!(Test-Path -LiteralPath $keyPath)) {
  throw "Signing key not found at $keyPath. Restore the established key; never generate a new trust root for a coated probe."
}

& powershell -ExecutionPolicy Bypass -File (Join-Path $scriptDir "arduino_msv3_probe.ps1") -Action doctor
if ($LASTEXITCODE -ne 0) { throw "MSV3 doctor check failed" }

& powershell -ExecutionPolicy Bypass -File (Join-Path $scriptDir "arduino_msv3_probe.ps1") `
  -Action compile -Sketch "arduino/MSV3_Probe_Bringup_Test" -SensorId $SensorId `
  -SerialLogs -BuildTag $BuildTag
if ($LASTEXITCODE -ne 0) { throw "MSV3 bring-up diagnostic compile failed" }

$appHex = Join-Path $msv3Root ".arduino-build\msv3-probe\MSV3_Probe_Bringup_Test.ino.hex"
if (!(Test-Path -LiteralPath $appHex)) { throw "Missing compiled diagnostic application: $appHex" }

$nrfutil = (& (Join-Path $scriptDir "prepare_msv3_dfu_tools.ps1") -Python $pythonExe | Select-Object -Last 1)
$env:PYTHONPATH = Join-Path $msv3Root ".arduino-build\dfu-pydeps"
$releaseDir = Join-Path $msv3Root "bootloader\generated\$BootloaderRelease\diagnostics"
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
$packageName = "msv3-$BootloaderRelease-sensor$('{0:d3}' -f $SensorId)-v$ApplicationVersion-bringup-$BuildTag-signed-ota.zip"
$packagePath = Join-Path $releaseDir $packageName
if (Test-Path -LiteralPath $packagePath) {
  throw "Refusing to overwrite the existing diagnostic artifact: $packagePath. Use a new application version."
}

& $nrfutil dfu genpkg --dev-type 0x3356 --dev-revision 52840 `
  --application-version $ApplicationVersion --sd-req 0xB6 `
  --application $appHex --key-file $keyPath $packagePath
if ($LASTEXITCODE -ne 0) {
  Remove-Item -LiteralPath $packagePath -ErrorAction SilentlyContinue
  throw "Signed diagnostic package generation failed"
}

& $pythonExe (Join-Path $scriptDir "verify_msv3_dfu_package.py") $packagePath `
  --device-type 0x3356 --application-version $ApplicationVersion --key $keyPath
if ($LASTEXITCODE -ne 0) {
  Remove-Item -LiteralPath $packagePath -ErrorAction SilentlyContinue
  throw "Signed diagnostic package verification failed"
}

$packageHash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host ""
Write-Host "Signed MSV3 bring-up diagnostic package: $packagePath"
Write-Host "  sensor=$SensorId app_version=$ApplicationVersion build=$BuildTag guard2=off"
Write-Host "  sha256=$packageHash"
Write-Host "  Recovery: with USB power connected, type 'dfu' in the 115200-baud console, then"
Write-Host "  install a signed production package at this version or higher via BLE recovery."
