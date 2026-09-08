param(
  [string]$ReleaseVersion = "1.0.1",
  # 65538 is the first version built from source where DEVICE_NAME is derived from
  # SENSOR_ID. scripts/msv3_fleet.py treats anything below it as a package whose advertised
  # name cannot be trusted to equal its sensor ID, and ble_ota_update.py refuses those
  # without --allow-legacy-package. Do not lower this default.
  [uint32]$ApplicationVersion = 65538,
  [string]$SigningKey,
  [string]$Python,
  [switch]$GenerateSigningKey,
  [switch]$SkipApplicationCompile,
  # Which probe this release's APPLICATION package is for. Required unless the application build
  # is skipped: since 2026-09-05 the sketch takes its identity as a build parameter, so a release
  # is per-probe and the package name says which. The bootloader half is identity-free and shared.
  [int]$SensorId = -1,
  [string]$BuildTag
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$msv3Root = (Resolve-Path (Join-Path $scriptDir "..")).Path
$repoRoot = (Resolve-Path (Join-Path $msv3Root "..")).Path
$cacheRoot = Join-Path $msv3Root ".arduino-build"
$source = Join-Path $cacheRoot "Adafruit_nRF52_Bootloader"
$boardSource = Join-Path $msv3Root "bootloader\msv3_e73"
$boardDest = Join-Path $source "src\boards\msv3_e73"
$portablePython = Join-Path $cacheRoot "python-portable\runtime\python.exe"
$pythonExe = if (![string]::IsNullOrWhiteSpace($Python)) {
  $Python
} elseif (Test-Path -LiteralPath $portablePython) {
  $portablePython
} else {
  "python"
}
$keyPath = if ([string]::IsNullOrWhiteSpace($SigningKey)) {
  Join-Path $msv3Root ".secrets\msv3-dfu-signing-private.pem"
} elseif ([IO.Path]::IsPathRooted($SigningKey)) {
  $SigningKey
} else {
  Join-Path $repoRoot $SigningKey
}

if ($ReleaseVersion -notmatch '^\d+\.\d+\.\d+$') {
  throw "ReleaseVersion must be semantic numeric form, for example 1.0.0"
}
if ($ApplicationVersion -eq 0 -or $ApplicationVersion -eq [uint32]::MaxValue) {
  throw "ApplicationVersion must be explicit and cannot be 0 or 0xFFFFFFFF"
}

$nrfutil = (& (Join-Path $scriptDir "prepare_msv3_dfu_tools.ps1") -Python $pythonExe | Select-Object -Last 1)
$deps = Join-Path $cacheRoot "dfu-pydeps"
$env:PYTHONPATH = $deps

if (!(Test-Path -LiteralPath $keyPath)) {
  if (!$GenerateSigningKey) {
    throw "Signing key not found at $keyPath. Restore the existing key, or explicitly pass -GenerateSigningKey when establishing a new SWD-installed trust root."
  }
  & $pythonExe (Join-Path $scriptDir "generate_msv3_dfu_key.py") $keyPath
  if ($LASTEXITCODE -ne 0) { throw "MSV3 signing-key generation failed" }
}

$vkCode = (& $nrfutil keys --show-vk code $keyPath | Out-String)
if ($LASTEXITCODE -ne 0) { throw "Could not derive verification key" }
$qxMatch = [regex]::Match($vkCode, 'Qx\[\]\s*=\s*\{\s*(.*?)\s*\}', 'Singleline')
$qyMatch = [regex]::Match($vkCode, 'Qy\[\]\s*=\s*\{\s*(.*?)\s*\}', 'Singleline')
if (!$qxMatch.Success -or !$qyMatch.Success) { throw "Unexpected verification-key output" }
$qx = $qxMatch.Groups[1].Value -replace '\s+', ''
$qy = $qyMatch.Groups[1].Value -replace '\s+', ''

if (!(Test-Path -LiteralPath $source)) {
  New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
  & git clone --branch 0.11.0 --recurse-submodules `
    https://github.com/adafruit/Adafruit_nRF52_Bootloader.git $source
  if ($LASTEXITCODE -ne 0) { throw "Could not clone pinned Adafruit bootloader source" }
}

$requiredSource = Join-Path $source "lib\softdevice\s140_nrf52_6.1.1\s140_nrf52_6.1.1_softdevice.hex"
if (!(Test-Path -LiteralPath $requiredSource)) {
  throw "Pinned bootloader checkout is incomplete; missing $requiredSource"
}

# Return the pinned checkout to its stock state before applying anything. Every source change
# below is reapplied from tracked files on each run, so a rebuild is deterministic no matter what
# a previous run (or a hand edit) left behind. The whole clone lives under the gitignored
# .arduino-build/, so nothing in it is part of this repository and nothing in it may be the only
# copy of a change -- see the repository CLAUDE.md rule about edits outside the tree.
if (Test-Path -LiteralPath $boardDest) { Remove-Item -LiteralPath $boardDest -Recurse -Force }

& git -C $source checkout -- lib src
if ($LASTEXITCODE -ne 0) { throw "Could not reset the pinned bootloader checkout to its stock state" }

# Submodules need their own reset. `git checkout -- lib` restores the superproject's gitlinks but
# does NOT clean a submodule's working tree, so a modified file inside lib/tinyusb, lib/nrfx,
# lib/uf2 or lib/tinycrypt survives it. Measured 2026-09-06 by deliberately dirtying
# lib/tinyusb/src/tusb.c: the superproject files were restored and the submodule stayed modified.
# TinyUSB is compiled into the bootloader, so that would silently change the one binary that can
# never be re-flashed, and the sentinel grep below would not notice.
& git -C $source submodule foreach --quiet --recursive 'git checkout -- .'
if ($LASTEXITCODE -ne 0) { throw "Could not reset the pinned bootloader checkout's submodules" }

# Then refuse to build from anything still dirty, rather than trusting the two resets above to
# have been exhaustive. Fail closed: an unexplained modification here ends up inside an immutable
# bootloader, and no downstream check would attribute it.
$residual = @(& git -C $source status --porcelain -- lib src |
              Where-Object { $_ -and ($_ -notmatch 'src/boards/msv3_e73') })
if ($residual.Count -gt 0) {
  throw @"
The pinned bootloader checkout is still modified after resetting it:
  $($residual -join "`n  ")
Refusing to build. Every MSV3 source change is reapplied from tracked files on each run, so
anything left here is unexplained and would end up inside a bootloader that cannot be re-flashed
once a probe is coated. Investigate, or delete $source and let this script re-clone it.
"@
}

# MSV3 source patches, applied in release order. 1.0.0 and 1.0.1 take none: both are installed on
# real hardware right now and must stay byte-reproducible from this script.
#
# 1.0.2 adds on-device anti-rollback. Measured on probe 99 2026-09-05, stock 0.11.0 installs an
# OLDER validly signed image without complaint, because dfu_init.c ships an explicit empty template
# where the version check belongs. That matters for a coated probe: rolling back past guard 1
# restores the defect where a sensor fault closes the radio.
$patches = @()
if ([version]$ReleaseVersion -ge [version]"1.0.2") {
  $patches += @{
    File = Join-Path $msv3Root "bootloader\patches\msv3-1.0.2-anti-rollback.patch"
    # Grepped for after the patch applies. A patch that silently fails to apply would produce a
    # bootloader that looks right, installs, and quietly accepts rollbacks forever on hardware
    # whose SWD pads are under coating. Absence must fail the build, not the field.
    Sentinel = "MSV3_VERSION_FLOOR_MAGIC"
    SentinelIn = "lib\sdk11\components\libraries\bootloader_dfu\bootloader_types.h"
  }
}

foreach ($p in $patches) {
  if (!(Test-Path -LiteralPath $p.File)) {
    throw "Required bootloader patch is missing: $($p.File)"
  }
  & git -C $source apply --whitespace=nowarn $p.File
  if ($LASTEXITCODE -ne 0) { throw "Failed to apply bootloader patch $($p.File)" }

  $sentinelPath = Join-Path $source $p.SentinelIn
  if (!(Select-String -LiteralPath $sentinelPath -SimpleMatch -Pattern $p.Sentinel -Quiet)) {
    throw "Patch $([IO.Path]::GetFileName($p.File)) reported success but its sentinel '$($p.Sentinel)' is absent from $sentinelPath. Refusing to build a bootloader whose provenance cannot be confirmed."
  }
  Write-Host "MSV3 bootloader patch applied and verified: $([IO.Path]::GetFileName($p.File))"
}

if (Test-Path -LiteralPath $boardDest) { Remove-Item -LiteralPath $boardDest -Recurse -Force }
Copy-Item -LiteralPath $boardSource -Destination $boardDest -Recurse

# Use an MSV3-only legacy DFU device type. The stock image accepts generic 0x0052 packages;
# leaving that value in a sealed product would permit a correctly signed but wrong-family image.
$dfuInit = Join-Path $source "src\dfu_init.c"
$dfuText = [IO.File]::ReadAllText($dfuInit)
$stockDeviceType = '#define ADAFRUIT_DEVICE_TYPE         0x0052'
$msv3DeviceType = '#define ADAFRUIT_DEVICE_TYPE         0x3356'
if ($dfuText.Contains($stockDeviceType)) {
  $dfuText = $dfuText.Replace($stockDeviceType, $msv3DeviceType)
  [IO.File]::WriteAllText($dfuInit, $dfuText, [Text.UTF8Encoding]::new($false))
} elseif (!$dfuText.Contains($msv3DeviceType)) {
  throw "Refusing to guess while setting the MSV3 DFU device type"
}

$make = Join-Path $env:LOCALAPPDATA `
  "Microsoft\WinGet\Packages\ezwinports.make_Microsoft.Winget.Source_8wekyb3d8bbwe\bin\make.exe"
if (!(Test-Path -LiteralPath $make)) {
  throw "GNU Make 4.4.1 is required. Install WinGet package ezwinports.make."
}
$gcc = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA "Arduino15\packages\adafruit\tools\arm-none-eabi-gcc") `
  -Filter "arm-none-eabi-gcc.exe" -Recurse | Select-Object -First 1
if (!$gcc) { throw "Arduino's arm-none-eabi-gcc toolchain was not found" }
$crossCompile = ((Split-Path -Parent $gcc.FullName) -replace '\\', '/') + '/arm-none-eabi-'

$bootVersion = "0.11.0-msv3.$ReleaseVersion"
$makeArgs = @(
  "BOARD=msv3_e73",
  "GIT_VERSION=$bootVersion",
  # GNU sed is not part of the minimal Windows make package. These are the exact values that
  # upstream's sed expression would derive from the pinned bootloader tag 0.11.0.
  "_VER_MAJ=0",
  "_VER_MIN=11",
  "_VER_PAT=0",
  "_VER3=0 11 0",
  "CROSS_COMPILE=$crossCompile",
  "NRFUTIL=$($nrfutil -replace '\\', '/')",
  "SIGNED_FW=1",
  "SIGNED_FW_QX=$qx",
  "SIGNED_FW_QY=$qy",
  # The 1.0.0 dual-bank build accepted a complete BLE transfer but failed to leave a valid
  # application in bank 0 on the real E73 board. Adafruit's default single-bank path has been
  # proven on this hardware and DEFAULT_TO_OTA_DFU still guarantees recovery after an invalid app.
  "DUALBANK_FW=0",
  "DEFAULT_TO_OTA_DFU=1",
  "all"
)
$priorBuild = Join-Path $source "_build\build-msv3_e73"
if (Test-Path -LiteralPath $priorBuild) {
  Remove-Item -LiteralPath $priorBuild -Recurse -Force
}
Push-Location $source
try {
  & $make @makeArgs
  if ($LASTEXITCODE -ne 0) { throw "MSV3 bootloader build failed" }
} finally {
  Pop-Location
}

$buildDir = Join-Path $source "_build\build-msv3_e73"
$bootOnly = Join-Path $buildDir "msv3_e73_bootloader-$bootVersion.hex"
$bootMerged = Join-Path $buildDir "msv3_e73_bootloader-$($bootVersion)_s140_6.1.1.hex"
if (!(Test-Path -LiteralPath $bootOnly) -or !(Test-Path -LiteralPath $bootMerged)) {
  throw "Expected bootloader outputs were not created"
}

if (!$SkipApplicationCompile) {
  if ($SensorId -lt 0) {
    throw "-SensorId is required to build a release application (the sketch takes its identity as a build parameter). Pass -SkipApplicationCompile to build only the bootloader."
  }
  & powershell -ExecutionPolicy Bypass -File (Join-Path $scriptDir "arduino_msv3_probe.ps1") `
    -Action doctor
  if ($LASTEXITCODE -ne 0) { throw "MSV3 doctor check failed" }
  $probeArgs = @("-Action", "compile", "-Sketch", "arduino/MSV3_Moisture_BLE_Sleep",
                 "-SensorId", $SensorId)
  if ($BuildTag) { $probeArgs += @("-BuildTag", $BuildTag) }
  & powershell -ExecutionPolicy Bypass -File (Join-Path $scriptDir "arduino_msv3_probe.ps1") @probeArgs
  if ($LASTEXITCODE -ne 0) { throw "MSV3 production application build failed" }
}

# -SkipApplicationCompile NEVER PRODUCES AN APPLICATION PACKAGE. It builds the bootloader only.
#
# It used to package whatever hex was sitting in the shared build cache. Demonstrated 2026-09-06:
# `-ReleaseVersion 1.0.1 -ApplicationVersion 65537 -SkipApplicationCompile` emitted a correctly
# signed package NAMED and VERSIONED 1.0.1/65537 whose image was actually MSV3-MOISTURE-004 build
# 2026-09-06-s4v50. Nothing downstream would have questioned it: valid signature, right device
# type, only the identity disagreed with the filename.
#
# An identity check against -SensorId was the first fix, and it is still in place below -- but it
# is not sufficient on its own, because it cannot see SAME-SENSOR staleness. A cached sensor-004
# image built from older source, repackaged at a new version, presents a correct identity AND a
# correct signed version while being the wrong code. That is precisely how probe 4's running
# firmware became unreproducible in the first place, so the workflow that allows it is removed
# rather than guarded.
#
# If reusing a prebuilt application is ever wanted, add an explicit -ApplicationHex parameter with
# a required expected hash. Do not restore implicit consumption of shared cache state.
if ($SkipApplicationCompile) {
  Write-Host ""
  Write-Host "-SkipApplicationCompile: bootloader only. No application or migration package will be"
  Write-Host "produced, because the only image available would be unattributable build-cache state."
  $releaseDirBootOnly = Join-Path $msv3Root "bootloader\generated\$ReleaseVersion"
  New-Item -ItemType Directory -Force -Path $releaseDirBootOnly | Out-Null
  $releaseBootOnly = Join-Path $releaseDirBootOnly "msv3-e73-recovery-signed-s140-6.1.1.hex"
  Copy-Item -LiteralPath $bootMerged -Destination $releaseBootOnly -Force
  $bootOnlyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $releaseBootOnly).Hash.ToLowerInvariant()
  Write-Host ""
  Write-Host "MSV3 bootloader built: $releaseBootOnly"
  Write-Host "  sha256 $bootOnlyHash"
  Write-Host "Release manifest NOT rewritten -- it describes a full release and this was not one."
  exit 0
}

$appHex = Join-Path $cacheRoot "msv3-probe\MSV3_Moisture_BLE_Sleep.ino.hex"
if (!(Test-Path -LiteralPath $appHex)) { throw "Missing production application: $appHex" }

$releaseDir = Join-Path $msv3Root "bootloader\generated\$ReleaseVersion"
$canonicalDir = Join-Path $msv3Root "bootloader\generated"
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
$releaseBoot = Join-Path $releaseDir "msv3-e73-recovery-signed-s140-6.1.1.hex"
# From 1.0.2 the APPLICATION VERSION is part of the package name. Anti-rollback testing needs
# N-1, N and N+1 for the same probe to exist side by side, and before this they differed only by
# a field inside the signed init packet -- so each build silently overwrote the last, and the file
# you reached for could not be identified from its name. Names below 1.0.2 are left exactly as
# they were: those packages are archived artifacts and their names appear in the field records.
$otaName = if ($SensorId -lt 0) {
  "msv3-$ReleaseVersion-signed-ota.zip"
} elseif ([version]$ReleaseVersion -ge [version]"1.0.2") {
  "msv3-$ReleaseVersion-sensor$('{0:d3}' -f $SensorId)-v$ApplicationVersion-signed-ota.zip"
} else {
  "msv3-$ReleaseVersion-sensor$('{0:d3}' -f $SensorId)-signed-ota.zip"
}
$signedOta = Join-Path $releaseDir $otaName
# The migration package carries SoftDevice + bootloader + APPLICATION, so it embeds a sensor
# identity exactly like the OTA package does, and from 1.0.2 its name says which. Before this it
# did not: every build overwrote one file whose identity you could only discover by unzipping it.
# Flashing the wrong one gives two probes the same sensor id and silently mixes two plants' data
# in the receiver -- which has already happened once on this project, with identity 005.
$initialInstallName = if ($SensorId -ge 0 -and [version]$ReleaseVersion -ge [version]"1.0.2") {
  "msv3-$ReleaseVersion-sensor$('{0:d3}' -f $SensorId)-initial-install-from-stock.zip"
} else {
  "msv3-$ReleaseVersion-initial-install-from-stock.zip"
}
$initialInstall = Join-Path $releaseDir $initialInstallName
Copy-Item -LiteralPath $bootMerged -Destination $releaseBoot -Force

& $nrfutil dfu genpkg --dev-type 0x3356 --dev-revision 52840 `
  --application-version $ApplicationVersion --sd-req 0xB6 `
  --application $appHex --key-file $keyPath $signedOta
if ($LASTEXITCODE -ne 0) { throw "Signed OTA package generation failed" }

# One-time bridge: the installed generic stock loader requires 0x0052 and CRC16 init data.
& $nrfutil dfu genpkg --dev-type 0x0052 --dev-revision 52840 `
  --application-version $ApplicationVersion --sd-req 0xB6 `
  --application $appHex --bootloader $bootOnly --softdevice $requiredSource $initialInstall
if ($LASTEXITCODE -ne 0) { throw "Initial stock-to-MSV3 package generation failed" }

& $pythonExe (Join-Path $scriptDir "verify_msv3_dfu_package.py") $signedOta `
  --device-type 0x3356 --application-version $ApplicationVersion --key $keyPath
if ($LASTEXITCODE -ne 0) { throw "Signed OTA package verification failed" }

# Confirm the package that was just SIGNED actually carries the identity this run asked for.
#
# -SkipApplicationCompile packages whatever is sitting in the shared build cache. Demonstrated
# 2026-09-06 while regression-testing this script: a `-ReleaseVersion 1.0.1 -ApplicationVersion
# 65537 -SkipApplicationCompile` run produced a package NAMED and VERSIONED 1.0.1/65537 whose image
# was really MSV3-MOISTURE-004 build 2026-09-06-s4v50. It was correctly signed, so nothing
# downstream would have questioned it -- and a mislabelled signed package is exactly what the fleet
# registry exists to prevent. Once a probe is coated the only remedy for a wrong install is another
# OTA, so this is checked at the point of signing, not at the point of flashing.
if ($SensorId -ge 0) {
  & $pythonExe -c @"
import sys
from pathlib import Path
sys.path.insert(0, r'$repoRoot\scripts')
import msv3_fleet
pkg = msv3_fleet.read_package(Path(sys.argv[1]))
want = int(sys.argv[2])
if pkg.sensor_id != want:
    sys.exit('IDENTITY MISMATCH: package carries MSV3-MOISTURE-%03d but -SensorId %d was requested.\n'
             'The build cache is from a different build. Drop -SkipApplicationCompile, or pass the\n'
             '-SensorId matching the image you intend to ship.' % (pkg.sensor_id, want))
print('MSV3 package identity check: OK (MSV3-MOISTURE-%03d, version %d)'
      % (pkg.sensor_id, pkg.application_version))
"@ $signedOta $SensorId
  if ($LASTEXITCODE -ne 0) {
    # Delete the offending artifacts before throwing. Detecting a mislabelled signed package and
    # then leaving it on disk is barely better than not detecting it: it is correctly signed, its
    # filename asserts an identity it does not have, and the next person to reach into this
    # directory has no reason to distrust it. Found the hard way -- the first version of this
    # check reported the mismatch and left the package sitting there.
    foreach ($stale in @($signedOta, $initialInstall)) {
      if (Test-Path -LiteralPath $stale) {
        Remove-Item -LiteralPath $stale -Force
        Write-Host "Removed mislabelled package: $([IO.Path]::GetFileName($stale))"
      }
    }
    throw "Signed package identity does not match -SensorId $SensorId (mislabelled artifacts deleted)"
  }
}
& $pythonExe (Join-Path $scriptDir "verify_msv3_dfu_package.py") $initialInstall `
  --device-type 0x0052 --application-version $ApplicationVersion
if ($LASTEXITCODE -ne 0) { throw "Initial migration package verification failed" }

$canonicalBoot = Join-Path $canonicalDir "msv3-e73-recovery-signed-s140-6.1.1.hex"
Copy-Item -LiteralPath $releaseBoot -Destination $canonicalBoot -Force
$bootHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $canonicalBoot).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$canonicalBoot.sha256", "$bootHash  $([IO.Path]::GetFileName($canonicalBoot))`n", [Text.UTF8Encoding]::new($false))

$vkHex = (& $nrfutil keys --show-vk hex $keyPath | Out-String).Trim()
$files = @($releaseBoot, $signedOta, $initialInstall, $appHex) | ForEach-Object {
  [ordered]@{
    name = [IO.Path]::GetFileName($_)
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_).Hash.ToLowerInvariant()
    bytes = (Get-Item -LiteralPath $_).Length
  }
}
$manifest = [ordered]@{
  release = $ReleaseVersion
  application_version = $ApplicationVersion
  device_type = "0x3356"
  softdevice_requirement = "0x00B6 (S140 6.1.1)"
  bootloader = [ordered]@{
    upstream = "Adafruit_nRF52_Bootloader 0.11.0"
    dual_bank = $false
    default_to_ble_recovery = $true
    signed_dfu_required = $true
    uf2_enabled = $false
  }
  public_verification_key = $vkHex
  files = $files
}
$manifestPath = Join-Path $releaseDir "release-manifest.json"
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 6) + "`n", [Text.UTF8Encoding]::new($false))

Write-Host "MSV3 release ready: $releaseDir"
Write-Host "  Future OTA: $signedOta"
Write-Host "  One-time stock migration: $initialInstall"
Write-Warning "Back up $keyPath offline before coating a production board."
