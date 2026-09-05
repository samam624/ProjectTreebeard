param(
  [ValidateSet("compile", "upload", "monitor", "board-list", "doctor")]
  [string]$Action = "compile",
  [string]$Port,
  [string]$Sketch = "arduino/MSV3_Probe_Bringup_Test",
  [string]$CliPath = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
  [string]$ArduinoDataDir = (Join-Path $env:LOCALAPPDATA "Arduino15")
)

$ErrorActionPreference = "Stop"

$Fqbn = "adafruit:nrf52:pca10056"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
$sketchPath = if ([IO.Path]::IsPathRooted($Sketch)) { $Sketch } else { Join-Path $repoRoot $Sketch }
$buildPath = Join-Path $repoRoot ".arduino-build\msv3-probe"

function Invoke-ArduinoCli {
  param([string[]]$Arguments)

  & $CliPath @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "arduino-cli failed with exit code $LASTEXITCODE"
  }
}

function Get-AdafruitNrf52CorePath {
  $platformRoot = Join-Path $ArduinoDataDir "packages\adafruit\hardware\nrf52"
  if (!(Test-Path -LiteralPath $platformRoot)) {
    throw "Adafruit nRF52 core is not installed under '$platformRoot'. Install adafruit:nrf52, then rerun this script."
  }

  $candidates = @(
    Get-ChildItem -LiteralPath $platformRoot -Directory |
      Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "variants\pca10056\variant.h") }
  )
  if ($candidates.Count -eq 0) {
    throw "No installed Adafruit nRF52 core contains variants\pca10056\variant.h under '$platformRoot'."
  }

  # arduino-cli uses the newest installed platform version when the FQBN has no
  # version suffix. Arduino package versions are numeric for this platform.
  return ($candidates | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).FullName
}

function Ensure-Msv3LfRcVariant {
  param([string]$CorePath)

  $variantPath = Join-Path $CorePath "variants\pca10056\variant.h"
  $original = [IO.File]::ReadAllText($variantPath)
  $activeLfxo = [regex]::Matches($original, '(?m)^\s*#\s*define\s+USE_LFXO\b').Count
  $activeLfrc = [regex]::Matches($original, '(?m)^\s*#\s*define\s+USE_LFRC\b').Count

  if ($activeLfxo -eq 0 -and $activeLfrc -eq 1) {
    Write-Host "MSV3 LFCLK guard: OK (USE_LFRC active in $variantPath)"
    return
  }

  if ($activeLfxo -ne 1 -or $activeLfrc -ne 0) {
    throw "Refusing to guess while repairing '$variantPath': expected exactly one active USE_LFXO and no active USE_LFRC, but found USE_LFXO=$activeLfxo USE_LFRC=$activeLfrc. See docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md."
  }

  $updated = [regex]::Replace(
    $original,
    '(?m)^(\s*)#\s*define\s+USE_LFXO\b([^\r\n]*)',
    '$1// #define USE_LFXO$2',
    1
  )
  $updated = [regex]::Replace(
    $updated,
    '(?m)^(\s*)//\s*#?\s*define\s+USE_LFRC\b([^\r\n]*)',
    '$1#define USE_LFRC$2',
    1
  )

  $newActiveLfxo = [regex]::Matches($updated, '(?m)^\s*#\s*define\s+USE_LFXO\b').Count
  $newActiveLfrc = [regex]::Matches($updated, '(?m)^\s*#\s*define\s+USE_LFRC\b').Count
  if ($newActiveLfxo -ne 0 -or $newActiveLfrc -ne 1) {
    throw "Could not safely convert '$variantPath' to USE_LFRC. The installed core layout may have changed."
  }

  [IO.File]::WriteAllText($variantPath, $updated, [Text.UTF8Encoding]::new($false))
  Write-Host "MSV3 LFCLK guard: repaired installed core to USE_LFRC: $variantPath"
  Write-Host "This repair is intentionally repeated by the repo script after Arduino core installs/upgrades."
}

function Assert-DuplicatedSketchSources {
  # ResetSafeBLEDfu.{h,cpp} exists twice, once in MSV3_BLE_OTA_Test and once in
  # MSV3_Moisture_BLE_Sleep, because the Arduino builder only compiles files that live inside the
  # sketch folder. Until 2026-09-04 the only thing keeping the two copies in step was a comment
  # asking people to remember. That code is the buttonless-jump write-authorize path -- the exact
  # place a SUCCESS reply must set update=1, which took a full day of hardware debugging to find
  # once. Silent drift there would resurrect it in whichever sketch was not edited, and the proof
  # sketch is what any future investigation would be run against.
  $duplicated = @("ResetSafeBLEDfu.h", "ResetSafeBLEDfu.cpp")
  $folders = @(
    (Join-Path $repoRoot "arduino\MSV3_Moisture_BLE_Sleep"),
    (Join-Path $repoRoot "arduino\MSV3_BLE_OTA_Test")
  )

  foreach ($file in $duplicated) {
    $paths = $folders | ForEach-Object { Join-Path $_ $file }
    $present = @($paths | Where-Object { Test-Path -LiteralPath $_ })
    if ($present.Count -eq 0) { continue }
    if ($present.Count -ne $paths.Count) {
      $missing = $paths | Where-Object { !(Test-Path -LiteralPath $_) }
      throw "$file exists in one sketch but not the other; missing: $($missing -join ', '). These copies must stay identical."
    }
    $hashes = $present | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
    if (($hashes | Select-Object -Unique).Count -ne 1) {
      throw @"
$file has drifted between the two sketch folders:
  $($present -join "`n  ")
They are deliberate duplicates -- the Arduino builder only compiles files inside the sketch folder
-- and must stay byte-identical. Copy whichever is correct over the other, then rebuild.
"@
    }
  }
  Write-Host "MSV3 duplicated-source check: OK ($($duplicated -join ', ') identical across both sketches)"
}

if (!(Test-Path -LiteralPath $CliPath)) {
  throw "arduino-cli.exe not found at '$CliPath'. Pass -CliPath if Arduino IDE is installed elsewhere."
}

if ($Action -eq "board-list") {
  Invoke-ArduinoCli @("board", "list")
  exit 0
}

if ($Action -eq "monitor") {
  if ([string]::IsNullOrWhiteSpace($Port)) {
    throw "Pass the current port explicitly, for example: -Action monitor -Port COM11"
  }
  Invoke-ArduinoCli @("monitor", "-p", $Port, "-c", "baudrate=115200")
  exit 0
}

$corePath = Get-AdafruitNrf52CorePath
Ensure-Msv3LfRcVariant -CorePath $corePath
Assert-DuplicatedSketchSources

if ($Action -eq "doctor") {
  Write-Host "MSV3 toolchain check passed. Core: $corePath"
  exit 0
}

if (!(Test-Path -LiteralPath $sketchPath)) {
  throw "Sketch not found: '$sketchPath'"
}
New-Item -ItemType Directory -Force -Path $buildPath | Out-Null

$compileArgs = @(
  "compile",
  "--clean",
  "--fqbn", $Fqbn,
  "--build-path", $buildPath,
  "--build-property", "compiler.c.extra_flags=-DCONFIG_NFCT_PINS_AS_GPIOS",
  "--build-property", "compiler.cpp.extra_flags=-DCONFIG_NFCT_PINS_AS_GPIOS",
  $sketchPath
)
Invoke-ArduinoCli $compileArgs

if ($Action -eq "upload") {
  if ([string]::IsNullOrWhiteSpace($Port)) {
    throw "Compile passed, but upload needs the current port. Rerun with -Action upload -Port COMx."
  }
  Invoke-ArduinoCli @(
    "upload", "-p", $Port, "--fqbn", $Fqbn,
    "--input-dir", $buildPath, $sketchPath
  )
}
