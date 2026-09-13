[CmdletBinding()]
param(
  [ValidateSet("prepare", "verify", "flash", "all")]
  [string]$Action = "prepare",
  [string]$SensorIds = "6,7",
  [uint32]$ApplicationVersion = 65556,
  [string]$ReleaseVersion = "1.0.3",
  [string]$BuildTagPrefix = "2026-09-08-prod",
  [string]$Port = "COM17",
  [int]$FlashSensorId = -1
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$msv3Root = (Resolve-Path (Join-Path $scriptDir "..")).Path
$repoRoot = (Resolve-Path (Join-Path $msv3Root "..")).Path
$python = Join-Path $msv3Root ".arduino-build\python-portable\runtime\python.exe"
$buildScript = Join-Path $scriptDir "build_msv3_release.ps1"
$doctorScript = Join-Path $scriptDir "arduino_msv3_probe.ps1"
$keyVerifier = Join-Path $scriptDir "verify_msv3_signing_key.py"
$packageVerifier = Join-Path $scriptDir "verify_msv3_dfu_package.py"
$swdFlasher = Join-Path $repoRoot "scripts\swd_flash_hex.py"
$releaseDir = Join-Path $msv3Root "bootloader\generated\$ReleaseVersion"
$batchManifest = Join-Path $releaseDir "raw-probes-v$ApplicationVersion-manifest.json"
$sensorIdList = @($SensorIds -split ',' | ForEach-Object {
  $value = 0
  if (![int]::TryParse($_.Trim(), [ref]$value)) {
    throw "Invalid sensor ID in -SensorIds '$SensorIds'. Use comma-separated integers, for example 6,7."
  }
  $value
})

function Assert-LastExitCode([string]$Description) {
  if ($LASTEXITCODE -ne 0) {
    throw "$Description failed with exit code $LASTEXITCODE"
  }
}

function Get-Sha256([string]$Path) {
  return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Assert-Inputs {
  if (!(Test-Path -LiteralPath $python)) {
    throw "Portable Python is missing at $python. Run build_msv3_release.ps1 once to prepare the DFU tools."
  }
  if ($sensorIdList.Count -ne 2) {
    throw "This guarded batch is for exactly two raw probes; pass two values to -SensorIds."
  }
  if (($sensorIdList | Select-Object -Unique).Count -ne 2) {
    throw "-SensorIds contains a duplicate. Each physical probe must receive a unique identity."
  }
  foreach ($sensorId in $sensorIdList) {
    if ($sensorId -lt 0 -or $sensorId -gt 255 -or $sensorId -eq 99) {
      throw "Sensor ID $sensorId is invalid for a production probe (allowed 0-255; 99 is the bench board)."
    }
  }
  if ($FlashSensorId -ge 0 -and $FlashSensorId -notin $sensorIdList) {
    throw "-FlashSensorId $FlashSensorId is not in the prepared batch ($($sensorIdList -join ', '))."
  }
  if ($ApplicationVersion -eq 0 -or $ApplicationVersion -eq [uint32]::MaxValue) {
    throw "ApplicationVersion must be explicit and cannot be 0 or 0xFFFFFFFF."
  }
  if ($BuildTagPrefix -notmatch '^[0-9A-Za-z._-]+$') {
    throw "BuildTagPrefix may contain only letters, digits, dot, underscore, and hyphen."
  }
}

function Prepare-Artifacts {
  Write-Host "Preparing production packages for sensor IDs $($sensorIdList -join ', ')"
  Write-Host "  bootloader $ReleaseVersion, application version $ApplicationVersion"
  Write-Host "  Guard 2 ON; serial logs OFF; hang test OFF"

  $packages = @()
  foreach ($sensorId in $sensorIdList) {
    $buildTag = "$BuildTagPrefix$sensorId"
    Write-Host ""
    Write-Host "Building probe $sensorId as MSV3-BUILD:$buildTag ..."
    & powershell -ExecutionPolicy Bypass -File $buildScript `
      -ReleaseVersion $ReleaseVersion `
      -ApplicationVersion $ApplicationVersion `
      -SensorId $sensorId `
      -BuildTag $buildTag `
      -BootGuard
    Assert-LastExitCode "Probe $sensorId release build"

    $packageName = "msv3-$ReleaseVersion-sensor$('{0:d3}' -f $sensorId)-v$ApplicationVersion-signed-ota.zip"
    $packagePath = Join-Path $releaseDir $packageName
    if (!(Test-Path -LiteralPath $packagePath)) {
      throw "Build completed without the expected package: $packagePath"
    }
    & $python $packageVerifier $packagePath `
      --device-type 0x3356 --application-version $ApplicationVersion `
      --key (Join-Path $msv3Root ".secrets\msv3-dfu-signing-private.pem")
    Assert-LastExitCode "Probe $sensorId signed-package verification"

    $packages += [ordered]@{
      sensor_id = $sensorId
      build_id = "MSV3-BUILD:$buildTag"
      name = $packageName
      sha256 = Get-Sha256 $packagePath
      bytes = (Get-Item -LiteralPath $packagePath).Length
    }
  }

  $bootloaderPath = Join-Path $releaseDir "msv3-e73-recovery-signed-s140-6.1.1.hex"
  if (!(Test-Path -LiteralPath $bootloaderPath)) {
    throw "Bootloader artifact is missing: $bootloaderPath"
  }
  $releaseManifest = Get-Content -LiteralPath (Join-Path $releaseDir "release-manifest.json") -Raw |
    ConvertFrom-Json
  $manifestBootloader = $releaseManifest.files |
    Where-Object { $_.name -eq "msv3-e73-recovery-signed-s140-6.1.1.hex" } |
    Select-Object -First 1
  $bootloaderHash = Get-Sha256 $bootloaderPath
  if ($null -eq $manifestBootloader -or $manifestBootloader.sha256 -ne $bootloaderHash) {
    throw "Bootloader hash does not match release-manifest.json. Refusing to prepare a raw-board batch."
  }

  $manifest = [ordered]@{
    created_at = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ssK")
    release = $ReleaseVersion
    application_version = $ApplicationVersion
    device_type = "0x3356"
    bootloader = [ordered]@{
      name = [IO.Path]::GetFileName($bootloaderPath)
      sha256 = $bootloaderHash
      bytes = (Get-Item -LiteralPath $bootloaderPath).Length
    }
    packages = $packages
  }
  [IO.File]::WriteAllText(
    $batchManifest,
    ($manifest | ConvertTo-Json -Depth 6) + "`n",
    [Text.UTF8Encoding]::new($false)
  )
  Write-Host ""
  Write-Host "Prepared and archived: $batchManifest"
}

function Verify-Artifacts {
  if (!(Test-Path -LiteralPath $batchManifest)) {
    throw "Prepared batch manifest not found: $batchManifest. Run -Action prepare first."
  }
  $manifest = Get-Content -LiteralPath $batchManifest -Raw | ConvertFrom-Json
  if ($manifest.release -ne $ReleaseVersion -or
      [uint32]$manifest.application_version -ne $ApplicationVersion) {
    throw "Prepared manifest release/version does not match this invocation."
  }

  $wantedIds = @($sensorIdList | Sort-Object)
  $manifestIds = @($manifest.packages | ForEach-Object { [int]$_.sensor_id } | Sort-Object)
  if (($wantedIds -join ',') -ne ($manifestIds -join ',')) {
    throw "Prepared manifest sensor IDs ($($manifestIds -join ', ')) do not match requested IDs ($($wantedIds -join ', '))."
  }

  $bootloaderPath = Join-Path $releaseDir $manifest.bootloader.name
  if (!(Test-Path -LiteralPath $bootloaderPath) -or
      (Get-Sha256 $bootloaderPath) -ne $manifest.bootloader.sha256) {
    throw "Prepared bootloader is missing or its SHA-256 changed. Refusing to flash."
  }
  foreach ($package in $manifest.packages) {
    $packagePath = Join-Path $releaseDir $package.name
    if (!(Test-Path -LiteralPath $packagePath) -or
        (Get-Sha256 $packagePath) -ne $package.sha256) {
      throw "Prepared package $($package.name) is missing or its SHA-256 changed."
    }
    & $python $packageVerifier $packagePath `
      --device-type 0x3356 --application-version $ApplicationVersion `
      --key (Join-Path $msv3Root ".secrets\msv3-dfu-signing-private.pem")
    Assert-LastExitCode "Probe $($package.sensor_id) signed-package verification"
  }

  & $python $keyVerifier --release $ReleaseVersion
  Assert-LastExitCode "Signing-key verification"
  Write-Host "Prepared raw-probe batch verification: PASS"
}

function Flash-RawBoards {
  $flashIds = @(if ($FlashSensorId -ge 0) { $FlashSensorId } else { $sensorIdList })
  Write-Warning "DESTRUCTIVE: this action mass-erases and programs $($flashIds.Count) physical target(s)."
  Write-Host "Power and connect only ONE raw probe at a time."
  Write-Host "The ESP32-S3 adapter must be $Port; Probe power may be USB, but do not connect SWD VCC."
  Write-Host "Every target operation retains the mandatory 3-second continuous DPIDR contact gate."

  & powershell -ExecutionPolicy Bypass -File $doctorScript -Action doctor
  Assert-LastExitCode "MSV3 toolchain doctor"

  foreach ($sensorId in $flashIds) {
    Write-Host ""
    Write-Host "============================================================"
    Write-Host "RAW BOARD FOR FUTURE SENSOR $sensorId"
    Write-Host "============================================================"
    $answer = Read-Host "Connect/power ONLY that raw board. Type FLASH-$sensorId to arm its mass erase"
    if ($answer -cne "FLASH-$sensorId") {
      throw "Confirmation did not match FLASH-$sensorId; nothing was sent to this target."
    }

    # Do not replace this with direct serial commands. swd_flash_hex.py owns the contact gate,
    # CTRL-AP ERASEALL, post-erase blank check, release-manifest hash check, segmented writes,
    # and read-back verification.
    & $python $swdFlasher --port $Port --msv3 $ReleaseVersion --no-reset
    Assert-LastExitCode "Raw board $sensorId SWD provisioning"

    Write-Host ""
    Write-Host "Probe $sensorId bootloader/SoftDevice write verified."
    Write-Host "Release the pogo pins, then unplug/replug that probe's USB power."
    Write-Host "It should advertise as AdaDFU because no application is installed yet."
    Read-Host "Record its AdaDFU address, disconnect this board, then press Enter for the next board"
  }

  Write-Host ""
  Write-Host "Selected raw-board flash operation completed. Do not confuse recorded addresses."
  Write-Host "Next: add each address to MSV3/fleet.json, install its matching signed package over BLE,"
  Write-Host "verify DIS + advancing wakes, then read back the settings page over gated SWD."
  foreach ($sensorId in $sensorIdList) {
    $packageName = "msv3-$ReleaseVersion-sensor$('{0:d3}' -f $sensorId)-v$ApplicationVersion-signed-ota.zip"
    Write-Host "  probe $sensorId -> $packageName"
  }
}

Assert-Inputs
if ($Action -in @("prepare", "all")) { Prepare-Artifacts }
if ($Action -in @("verify", "flash", "all")) { Verify-Artifacts }
if ($Action -in @("flash", "all")) { Flash-RawBoards }
