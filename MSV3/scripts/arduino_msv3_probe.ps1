param(
  [ValidateSet("compile", "upload", "monitor", "board-list", "doctor")]
  [string]$Action = "compile",
  # For -Action upload this is the APPLICATION port (PID 0x80DA). The DFU port (PID 0x00DA) is a
  # DIFFERENT COM number on Windows and is discovered automatically from the same chip serial.
  [string]$Port,
  # -Action upload only. The version written into the SIGNED init packet, which the bootloader
  # checks against its on-device anti-rollback floor. It must be >= the floor or the image is
  # refused -- see MSV3/fleet.json installed_application_version for what is currently installed.
  [uint32]$ApplicationVersion = 0,
  # -Action upload only. Build and sign the package but do NOT touch the board.
  [switch]$PackageOnly,
  [string]$Sketch = "arduino/MSV3_Probe_Bringup_Test",
  # REQUIRED for the production sketch. See Assert-ProductionBuildParameters below: an image whose
  # identity came from an uncommitted edit is not reproducible, and probe 4's deployed firmware is
  # already an example of exactly that.
  [int]$SensorId = -1,
  # Bench-only. Turns on USB CDC logging and the boot banner. Never used for a fleet image.
  [switch]$SerialLogs,
  # Guard 2, the boot-attempt counter that escapes into BLE DFU after BOOT_GUARD_LIMIT boots that
  # never reach healthy operation. A build flag rather than an edited constant, for the same reason
  # SENSOR_ID is: committed source must always describe a deployable image.
  [switch]$BootGuard,
  # Builds an image that DELIBERATELY HANGS before advertising -- the unrecoverable state guard 2
  # exists to rescue. Bench board only, SWD attached only. See the checks below.
  [switch]$HangTest,
  # Alphanumeric tag appended to "MSV3-BUILD:" in the image. Passed as a token and stringified by
  # the sketch, so no quoting has to survive PowerShell -> arduino-cli -> the compiler.
  [string]$BuildTag,
  # Reproducible FDC settling-test parameters. Defaults are the committed production values.
  [ValidateRange(1, 65535)]
  [int]$FdcSettleCount = 100,
  [ValidateRange(0, 31)]
  [int]$FdcIdriveCode = 16,
  [ValidateRange(0, 255)]
  [int]$WakeDiscardConversions = 2,
  [ValidateSet(10, 33)]
  [int]$FdcDeglitchMhz = 10,
  # 2026-09-18 inverted-hybrid experiment: measure on mux code 2 only (an external sensor wired
  # across the tank at the C17 pads). Probe-specific, needs -SensorId and a -BuildTag so DIS names
  # the experiment. See MSV3_HYBRID_V12_TANK in the production sketch.
  [switch]$HybridV12Tank,
  # 2 = MSV3 electrodes disconnected; 0 = MSV3 electrode connected as padding (hold it in AIR).
  [ValidateSet(0, 2)]
  [int]$HybridMuxCode = 2,
  # Only consulted for -BootGuard/-HangTest, which must ask MSV3/fleet.json whether the target
  # can survive the image being built. An ordinary build never needs an interpreter.
  [string]$Python,
  [string]$CliPath = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
  [string]$ArduinoDataDir = (Join-Path $env:LOCALAPPDATA "Arduino15")
)

$ErrorActionPreference = "Stop"

$Fqbn = "adafruit:nrf52:pca10056"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
# NOTE: $repoRoot is historically the MSV3 SUBSYSTEM root (MSV3/), not the repository root -- every
# path below resolves relative to it. scripts/msv3_fleet.py lives one level further up, so the
# dangerous-mode gate needs the real repository root as well.
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
$actualRepoRoot = (Resolve-Path (Join-Path $repoRoot "..")).Path
$sketchPath = if ([IO.Path]::IsPathRooted($Sketch)) { $Sketch } else { Join-Path $repoRoot $Sketch }
$buildPath = Join-Path $repoRoot ".arduino-build\msv3-probe"
$msv3LinkerName = "msv3_nrf52840_s140_v6.ld"
$trackedMsv3Linker = Join-Path $repoRoot "linker\$msv3LinkerName"

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

function Ensure-Msv3BootGuardLinker {
  param([string]$CorePath)

  if (!(Test-Path -LiteralPath $trackedMsv3Linker)) {
    throw "Tracked MSV3 linker script is missing: $trackedMsv3Linker"
  }

  $tracked = [IO.File]::ReadAllText($trackedMsv3Linker)
  $required = @(
    "MSV3_BOOT_GUARD_RETAINED",
    "ORIGIN = 0x2003FFF8, LENGTH = 0x08",
    ".msv3_boot_guard_retained (NOLOAD)",
    "SIZEOF(.msv3_boot_guard_retained) == LENGTH(MSV3_BOOT_GUARD_RETAINED)"
  )
  foreach ($sentinel in $required) {
    if (!$tracked.Contains($sentinel)) {
      throw "Tracked MSV3 linker script is missing required sentinel '$sentinel': $trackedMsv3Linker"
    }
  }

  $installed = Join-Path $CorePath "cores\nRF5\linker\$msv3LinkerName"
  $installedText = if (Test-Path -LiteralPath $installed) {
    [IO.File]::ReadAllText($installed)
  } else {
    ""
  }
  if ($installedText -ne $tracked) {
    [IO.File]::WriteAllText($installed, $tracked, [Text.UTF8Encoding]::new($false))
    Write-Host "MSV3 boot-guard linker: installed tracked script at $installed"
  }

  $verified = [IO.File]::ReadAllText($installed)
  if ($verified -ne $tracked) {
    throw "Installed MSV3 linker script does not byte-match the tracked source: $installed"
  }
  Write-Host "MSV3 boot-guard linker: OK (8-byte NOLOAD state reserved at 0x2003FFF8)"
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

function Remove-CCommentsAndStrings {
  # Blanks out comments AND string/char literals in one left-to-right pass.
  #
  # Ordering matters and a regex pipeline gets it wrong. Measured 2026-09-06 against the naive
  # "strip /*...*/ then strip //...$" version this replaces:
  #
  #   const char *u = "http://example"; uint32_t r = NRF_POWER->RESETREAS;
  #
  # The // inside the string literal ate the rest of the line, so a REAL violation on that line
  # became invisible -- a false negative in a check whose whole job is to have no false negatives.
  # A single pass that knows it is inside a string cannot be fooled that way.
  #
  # Replaces content with spaces rather than deleting it so byte offsets stay usable if this ever
  # needs to report a line number.
  param([string]$Text)

  $out = [System.Text.StringBuilder]::new($Text.Length)
  $i = 0
  $n = $Text.Length
  while ($i -lt $n) {
    $c = $Text[$i]
    $next = if ($i + 1 -lt $n) { $Text[$i + 1] } else { [char]0 }

    if ($c -eq '/' -and $next -eq '/') {
      while ($i -lt $n -and $Text[$i] -ne "`n") { [void]$out.Append(' '); $i++ }
    }
    elseif ($c -eq '/' -and $next -eq '*') {
      [void]$out.Append('  '); $i += 2
      while ($i -lt $n -and -not ($Text[$i] -eq '*' -and ($i + 1 -lt $n) -and $Text[$i + 1] -eq '/')) {
        [void]$out.Append($(if ($Text[$i] -eq "`n") { "`n" } else { ' ' })); $i++
      }
      if ($i -lt $n) { [void]$out.Append('  '); $i += 2 }
    }
    elseif ($c -eq '"' -or $c -eq "'") {
      $quote = $c
      [void]$out.Append(' '); $i++
      while ($i -lt $n -and $Text[$i] -ne $quote) {
        if ($Text[$i] -eq '\' -and $i + 1 -lt $n) { [void]$out.Append('  '); $i += 2; continue }
        [void]$out.Append($(if ($Text[$i] -eq "`n") { "`n" } else { ' ' })); $i++
      }
      if ($i -lt $n) { [void]$out.Append(' '); $i++ }
    }
    else {
      [void]$out.Append($c); $i++
    }
  }
  return $out.ToString()
}

function Assert-NoRawResetReasRead {
  # Reading NRF_POWER->RESETREAS directly always yields 0. The Adafruit core's init() runs before
  # setup(), reads the register, write-1-to-clears it, and saves the value behind
  # readResetReason() (cores/nRF5/wiring.c). The bootloader does NOT consume it -- its own
  # src/main.c says so -- but that wrong conclusion was on record from 2026-09-04 to 2026-09-06
  # precisely because the direct read looked like hardware behaviour rather than a framework
  # interaction.
  #
  # This is the third time this project has reached past a framework-mediated register: the first
  # guard-2 build wrote NRF_POWER->GPREGRET2 from loop() with the SoftDevice up and reset every
  # ~76 s, and the DRDY/ERROR_CONFIG family is the same shape. A wrong reset cause is quieter than
  # either -- nothing crashes, the value is simply always 0 -- so it needs a build-time check
  # rather than a comment.
  #
  # Deliberately narrow: this bans the RESETREAS *read*, not NRF_POWER as a whole. bootGuardCheck()
  # legitimately writes GPREGRET directly to request DFU because it runs before Bluefruit.begin().
  $sketchFiles = Get-ChildItem -LiteralPath (Join-Path $repoRoot "arduino") -Recurse -File -Include *.ino, *.cpp, *.h -ErrorAction SilentlyContinue
  $offenders = @()
  foreach ($f in $sketchFiles) {
    $text = Get-Content -LiteralPath $f.FullName -Raw
    $stripped = Remove-CCommentsAndStrings -Text $text
    foreach ($m in [regex]::Matches($stripped, 'NRF_POWER\s*->\s*RESETREAS')) {
      $offenders += "$($f.FullName)"
      break
    }
  }
  if ($offenders.Count -gt 0) {
    throw @"
Direct NRF_POWER->RESETREAS access found in:
  $($offenders -join "`n  ")
That register is already read and cleared by the Adafruit core's init() before setup() runs, so a
direct read returns 0 on every boot and a direct write clears nothing that is still set. Use the
core accessor instead:
    uint32_t reason = readResetReason();   // wiring.h, via Arduino.h
See docs/KNOWN_ISSUES.md and MSV3/docs/MSV3_UNREACHABLE_PROBE_GUARDS_2026-09-04.md.
"@
  }
  Write-Host "MSV3 reset-cause check: OK (no direct NRF_POWER->RESETREAS access)"
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
Ensure-Msv3BootGuardLinker -CorePath $corePath
Assert-DuplicatedSketchSources
Assert-NoRawResetReasRead

if ($Action -eq "doctor") {
  Write-Host "MSV3 toolchain check passed. Core: $corePath"
  exit 0
}

if (!(Test-Path -LiteralPath $sketchPath)) {
  throw "Sketch not found: '$sketchPath'"
}
New-Item -ItemType Directory -Force -Path $buildPath | Out-Null

# Identity, logging and build tag are BUILD PARAMETERS, not lines to edit in the sketch.
#
# The production sketch refuses to compile without MSV3_SENSOR_ID (#error). This function is why
# that refusal is helpful rather than merely strict: it turns a missing -SensorId into a sentence
# saying what to pass, instead of a preprocessor error partway down a build log.
function Assert-ProductionBuildParameters {
  $isProduction = (Split-Path -Leaf $sketchPath) -eq "MSV3_Moisture_BLE_Sleep"
  $isBringup = (Split-Path -Leaf $sketchPath) -eq "MSV3_Probe_Bringup_Test"
  if (!$isProduction -and !$isBringup) {
    if ($SensorId -ge 0) {
      throw "-SensorId applies only to MSV3_Moisture_BLE_Sleep or MSV3_Probe_Bringup_Test; '$Sketch' has no SENSOR_ID."
    }
    return
  }
  # Untargeted bring-up builds retain their historical id-99/test-name behaviour. Supplying an
  # identity opts into the signed fleet-diagnostic contract used for a coated probe.
  if ($isBringup -and $SensorId -lt 0) { return }
  if ($SensorId -lt 0) {
    throw @"
-SensorId is required when building MSV3_Moisture_BLE_Sleep.

  -SensorId 99   the USB-recoverable bench board
  -SensorId 4    the deployed field probe

MSV3/fleet.json is the registry, and ble_ota_update.py refuses a package whose identity
disagrees with the probe it is aimed at.

This used to be a constant edited in place, which made every deployed image depend on an
uncommitted working tree -- probe 4's running firmware cannot be rebuilt from this repository
for exactly that reason. Passing it here makes (commit, sensor id) reproduce an image.
"@
  }
  if ($SensorId -gt 255) { throw "-SensorId must be 0-255; it travels as one advertisement byte." }
  if ($BuildTag -and $BuildTag -notmatch '^[A-Za-z0-9._-]+$') {
    throw "-BuildTag must be alphanumeric with . _ - only (the sketch stringifies it)."
  }
}

Assert-ProductionBuildParameters

function Assert-TargetedOtaWindowContract {
  # A sketch carrying a fleet SENSOR_ID can be signed and installed on a coated probe. Once that
  # happens, USB/SWD may be physically unavailable, so every such image must periodically expose
  # the same reset-safe OTA handoff as production. Probe 6's 2026-09-13 bring-up image lacked it
  # and required a USB-data reconnection solely to restore production firmware.
  if ($SensorId -lt 0) { return }

  $inoFiles = @(Get-ChildItem -LiteralPath $sketchPath -File -Filter *.ino)
  if ($inoFiles.Count -ne 1) {
    throw "A targeted MSV3 sketch must contain exactly one .ino file for OTA-contract inspection; found $($inoFiles.Count) in '$sketchPath'."
  }
  $source = Remove-CCommentsAndStrings -Text (Get-Content -LiteralPath $inoFiles[0].FullName -Raw)
  $required = [ordered]@{
    "ResetSafeBLEDfu service" = 'ResetSafeBLEDfu\s+bledfu\s*;'
    "Device Information Service" = 'BLEDis\s+bledis\s*;'
    "periodic advertising cadence" = '(?:ADVERTISE_INTERVAL_MS|OTA_ADV_INTERVAL_MS)'
    "bounded OTA listen window" = 'OTA_ADV_LISTEN_MS'
    "DFU service registration" = 'bledfu\.begin\s*\(\s*\)'
    "DIS registration" = 'bledis\.begin\s*\(\s*\)'
    "battery/USB authorization gate" = 'bledfu\.setDfuAllowed\s*\('
    "deferred reset check" = 'bledfu\.dfuResetPending\s*\('
    "reset-safe handoff" = 'bledfu\.completeDfuReset\s*\('
  }
  $missing = @()
  foreach ($entry in $required.GetEnumerator()) {
    if ($source -notmatch $entry.Value) { $missing += $entry.Key }
  }
  if ($missing.Count -gt 0) {
    throw @"
Targeted MSV3 build REFUSED: '$($inoFiles[0].Name)' carries sensor ID $SensorId but is missing the
periodic OTA recovery contract:
  $($missing -join "`n  ")

Any image that can reach a coated probe must open a bounded, power-gated ResetSafeBLEDfu window
periodically and publish its exact build through DIS. Use an untargeted bench build (omit
-SensorId) only while wired recovery is genuinely available.
"@
  }
  Write-Host "MSV3 targeted OTA-window check: OK (ResetSafeBLEDfu + DIS + periodic bounded window)"
}

Assert-TargetedOtaWindowContract

$defines = @(
  "-DCONFIG_NFCT_PINS_AS_GPIOS",
  # The production sketch fails closed without this define. It is paired with the custom
  # build.ldscript below; defining one without the other still fails the linker's address/size
  # assertions, while bypassing this wrapper fails at preprocessing.
  "-DMSV3_BOOT_GUARD_RETAINED_LINKER=1"
)
if ($SensorId -ge 0) { $defines += "-DMSV3_SENSOR_ID=$SensorId" }
if ($SerialLogs) { $defines += "-DMSV3_ENABLE_SERIAL_LOGS=1" }
if ($BootGuard) { $defines += "-DMSV3_ENABLE_BOOT_FAILURE_GUARD=1" }
if ($HangTest) { $defines += "-DMSV3_HANG_BEFORE_ADVERTISE=1" }
if ($BuildTag) { $defines += "-DMSV3_BUILD_TAG=$BuildTag" }
if ($PSBoundParameters.ContainsKey('HybridMuxCode') -and -not $HybridV12Tank) {
  throw "-HybridMuxCode only applies with -HybridV12Tank; on its own it would be silently ignored."
}
if ($HybridV12Tank) {
  if ($SensorId -lt 0 -or -not $BuildTag) {
    throw "-HybridV12Tank needs -SensorId and a -BuildTag: it is a probe-specific experiment image, and DIS must say which one is running."
  }
  $defines += "-DMSV3_HYBRID_V12_TANK=1"
  $defines += "-DMSV3_HYBRID_MUX_CODE=$HybridMuxCode"
}
if ($SensorId -ge 0) {
  $fdcMuxConfig = if ($FdcDeglitchMhz -eq 10) { "0x020D" } else { "0x020F" }
  $defines += "-DMSV3_FDC_SETTLECOUNT_CH0=$FdcSettleCount"
  $defines += "-DMSV3_FDC_IDRIVE_CODE=$FdcIdriveCode"
  $defines += "-DMSV3_WAKE_DISCARD_CONVERSIONS=$WakeDiscardConversions"
  $defines += "-DMSV3_FDC_MUX_CONFIG=$fdcMuxConfig"
}
$defineFlags = $defines -join " "

# A hang-before-advertise image is the exact unrecoverable state guard 2 exists to rescue: signed,
# valid, boots, and never opens a radio window. It is only safe on a board with SWD attached, and
# it must never be built for a probe that is about to be sealed under conformal coating.
if ($HangTest) {
  if (-not $BootGuard) {
    throw "-HangTest without -BootGuard hangs the board with no escape armed. On any probe without SWD attached that is a brick. Pass both, or neither."
  }
  if (-not $SerialLogs) {
    throw "-HangTest without -SerialLogs produces a board that hangs silently and tells you nothing. The reset-cause banner IS the evidence this test exists to collect."
  }
}

# THE DANGEROUS-MODE GATE.
#
# This used to be `if ($SensorId -ne 99) { throw }`. The bench board WAS 99; on 2026-09-08 it was
# reassigned as production identity 8 and sensor 99 left MSV3/fleet.json entirely. The constant
# was not updated, so -HangTest became buildable for exactly one id that the registry rejects --
# and unbuildable for every board that exists. A hardcoded identity is what rotted; asking the
# registry is the fix.
#
# Deliberately scoped to the two dangerous modes. An ordinary build must not acquire a Python
# dependency, and does not: nothing below runs unless -BootGuard or -HangTest was passed.
if ($BootGuard -or $HangTest) {
  # No identity means no gate, and "no gate" must not be reachable by simply omitting a
  # parameter. -BootGuard/-HangTest describe an image destined for a specific board, and which
  # board it is decides whether the image is safe at all -- so the identity is required here even
  # though an ordinary bring-up build may go without one.
  if ($SensorId -lt 0) {
    throw @"
-BootGuard/-HangTest require -SensorId. Both flags are only safe for particular probes -- Guard 2
needs a bootloader that reserves 0x2003FFF8, and a hang image needs pads that are still reachable
-- and neither question can be answered without knowing which board this image is for.
"@
  }

  . (Join-Path $scriptDir "msv3_python.ps1")
  $gatePython = Resolve-Msv3Python -Explicit $Python -Msv3Root $repoRoot

  # Fail CLOSED. "The checker would not run" must never read as "the check passed" -- on Windows
  # a bare `python` is often the Store alias stub, which exits without running anything.
  if (-not (Test-Msv3Python -PythonExe $gatePython)) {
    throw @"
-BootGuard/-HangTest need Python to ask MSV3/fleet.json whether this is safe for sensor $SensorId,
and '$gatePython' would not run. Refusing to build a dangerous image on an unchecked assumption.
Pass -Python <path-to-python.exe>.
"@
  }

  $gateArgs = @((Join-Path $actualRepoRoot "scripts\msv3_fleet.py"), "gate", "--sensor-id", $SensorId)
  if ($BootGuard) { $gateArgs += "--boot-guard" }
  if ($HangTest) { $gateArgs += "--hang-test" }
  & $gatePython @gateArgs
  if ($LASTEXITCODE -ne 0) {
    throw "MSV3/fleet.json refuses this build for sensor $SensorId (see the REFUSED line above)."
  }
}

if ($HangTest) {
  Write-Host "MSV3 HANG TEST BUILD -- this image deliberately hangs before advertising."
  Write-Host "  Only run it with SWD attached. Guard 2 should escape into BLE DFU after 5 failed boots."
}

if ($SensorId -ge 0) {
  $tagNote = if ($BuildTag) { "build tag $BuildTag" } else { "default build tag" }
  $logNote = if ($SerialLogs) { "SERIAL LOGS ON (bench build)" } else { "serial logs off (fleet build)" }
  $guardNote = if ($BootGuard) { "boot guard ON" } else { "boot guard off" }
  Write-Host "MSV3 build parameters: sensor $SensorId, $tagNote, $logNote, $guardNote"
  Write-Host "  FDC: IDRIVE=$FdcIdriveCode, SETTLECOUNT=$FdcSettleCount, discarded conversions=$WakeDiscardConversions, deglitch=${FdcDeglitchMhz}MHz"
}

$compileArgs = @(
  "compile",
  "--clean",
  "--fqbn", $Fqbn,
  "--build-path", $buildPath,
  "--build-property", "build.ldscript=$msv3LinkerName",
  "--build-property", "compiler.c.extra_flags=$defineFlags",
  "--build-property", "compiler.cpp.extra_flags=$defineFlags",
  $sketchPath
)
Invoke-ArduinoCli $compileArgs

# This is the final proof that Arduino actually selected the tracked linker script. Merely copying
# it into the installed core is not enough: a renamed boards.txt property or CLI behaviour change
# could silently select the stock script and put the retained word back under the bootloader stack.
if ((Split-Path -Leaf $sketchPath) -eq "MSV3_Moisture_BLE_Sleep") {
  $mapPath = Join-Path $buildPath "MSV3_Moisture_BLE_Sleep.ino.map"
  if (!(Test-Path -LiteralPath $mapPath)) {
    throw "Production build succeeded but its linker map is missing: $mapPath"
  }
  $mapText = [IO.File]::ReadAllText($mapPath)
  if ($mapText -notmatch '(?m)^\.msv3_boot_guard_retained\s+0x2003fff8\s+0x8\b') {
    throw @"
MSV3 boot-guard retained state is not exactly 8 bytes at 0x2003FFF8 in:
  $mapPath
Refusing this image. The bootloader/application retention ABI was not linked.
"@
  }
  Write-Host "MSV3 boot-guard map check: OK (.msv3_boot_guard_retained = 0x2003FFF8, 8 bytes)"
}

if ($Action -eq "upload") {
  # MSV3_SIGNED_UPLOAD
  #
  # WHY THIS DOES NOT CALL `arduino-cli upload`.
  #
  # The Adafruit core's upload recipe runs `adafruit-nrfutil dfu genpkg --dev-type 0x0052
  # --application ...` and pushes the result. That package is UNSIGNED and carries STOCK ADAFRUIT'S
  # device type. The MSV3 bootloader is built with SIGNED_FW and binds to device type 0x3356, so it
  # is required to reject exactly that image -- and because single-bank DFU erases the application
  # BEFORE the init packet is authenticated, a rejected upload costs the probe its running
  # firmware and needs a BLE recovery.
  #
  # Measured on probe 8, 2026-09-21: two Arduino IDE uploads, which use that same recipe, each
  # erased the application and reset the board. dfu_init_prevalidate() refused the image and
  # APP_ERROR_CHECK rebooted; the retained trace showed reset_tag 0 (no MSV3 reset site) with the
  # application page erased. The transport was fine -- zero frames dropped. The PACKAGE was wrong.
  #
  # So this signs the compiled sketch the same way build_msv3_release.ps1 signs a release, proves
  # the package is well-formed BEFORE the radio or the cable touches the board, and only then
  # uploads. Never "fix" this by relaxing the device type or dropping the signature: those checks
  # are what make OTA to a coated probe safe at all.

  if ($ApplicationVersion -eq 0) {
    throw @"
-Action upload needs -ApplicationVersion.

It is written into the signed init packet and checked against the bootloader's on-device
anti-rollback floor, which refuses anything BELOW the installed version. Look up the target's
installed_application_version in MSV3/fleet.json and pass that or higher.
"@
  }

  . (Join-Path $scriptDir "msv3_python.ps1")
  $upPython = Resolve-Msv3Python -Explicit $Python -Msv3Root $repoRoot
  if (-not (Test-Msv3Python -PythonExe $upPython)) {
    throw "-Action upload needs a working Python for signing and for the DFU client. Pass -Python <path>."
  }

  $keyPath = Join-Path $repoRoot ".secrets\msv3-dfu-signing-private.pem"
  if (-not (Test-Path -LiteralPath $keyPath)) {
    throw @"
No signing key at:
  $keyPath
Without it this cannot produce an image the bootloader will accept. An unsigned upload does not
fail safely -- it erases the application first and is refused afterwards.
"@
  }

  $appHex = Join-Path $buildPath ((Split-Path $sketchPath -Leaf) + ".ino.hex")
  if (-not (Test-Path -LiteralPath $appHex)) {
    throw "Compiled hex not found at $appHex. Run -Action compile first."
  }

  $nrfutil = Join-Path $repoRoot ".arduino-build\dfu-pydeps\nordicsemi"
  $pkgDir  = Join-Path $buildPath "signed"
  $null = New-Item -ItemType Directory -Force -Path $pkgDir
  $signedPkg = Join-Path $pkgDir ("msv3-sketch-v$ApplicationVersion-signed.zip")
  if (Test-Path -LiteralPath $signedPkg) { Remove-Item -LiteralPath $signedPkg -Force }

  # Identical parameters to a release build. 0x3356 is the MSV3 device type; 0xB6 is S140 6.1.1.
  & $upPython -m nordicsemi dfu genpkg --dev-type 0x3356 --dev-revision 52840 `
    --application-version $ApplicationVersion --sd-req 0xB6 `
    --application $appHex --key-file $keyPath $signedPkg
  if ($LASTEXITCODE -ne 0) { throw "Signing the sketch package failed." }
  Write-Host "MSV3 signed package: $signedPkg"

  # PROVE IT BEFORE TOUCHING THE BOARD. ble_ota_update.py --preflight-only is laptop-only -- it
  # stops before the radio -- and it checks exactly what the bootloader will: package integrity,
  # the signature against the key compiled into the target's bootloader, device type, the
  # ANTI-ROLLBACK FLOOR, and boot-guard pairing. A version below the floor is refused here rather
  # than after the probe's application has been erased.
  if ($SensorId -lt 0) {
    throw "-Action upload needs -SensorId so the package can be checked against MSV3/fleet.json before anything is sent."
  }
  & $upPython (Join-Path $actualRepoRoot "scripts\ble_ota_update.py") $signedPkg --probe $SensorId --preflight-only
  if ($LASTEXITCODE -ne 0) {
    throw @"
Preflight refused this package, so NOTHING was sent to the board and the probe is untouched.
That is the intended outcome: single-bank DFU erases before it authenticates, so a package the
bootloader would reject must never reach a probe.
"@
  }

  if ($PackageOnly) {
    Write-Host "MSV3 -PackageOnly: stopping before the cable. Package is at $signedPkg"
    return
  }

  if ([string]::IsNullOrWhiteSpace($Port)) {
    throw "Upload needs the APPLICATION port. Rerun with -Action upload -Port COMx -ApplicationVersion N."
  }

  # The DFU port is a different COM number. Both are children of the same USB composite device,
  # whose serial is the chip's FICR DEVICEID -- match on the PARENT, never on the MI_00 child,
  # whose instance id is a Windows-generated hash. See
  # MSV3_COM_PORT_PROBE_IDENTIFICATION_PITFALLS_2026-09-11.md.
  $appDev = Get-PnpDevice -Class Ports -ErrorAction SilentlyContinue |
            Where-Object { $_.FriendlyName -match "\($Port\)" } | Select-Object -First 1
  if ($null -eq $appDev) { throw "No device found for $Port." }
  $chipSerial = ((Get-PnpDeviceProperty -InstanceId $appDev.InstanceId -KeyName 'DEVPKEY_Device_Parent' `
                   -ErrorAction SilentlyContinue).Data -split '\\')[-1]
  if ([string]::IsNullOrWhiteSpace($chipSerial)) { throw "Could not read the chip serial behind $Port." }
  Write-Host "MSV3 target chip serial: $chipSerial"

  # If -Port is ALREADY the DFU port, there is nothing to touch. This is the common case on this
  # hardware: the production application is duty-cycled and only enumerates USB CDC while awake, so
  # a probe is often reachable over USB only once it is already sitting in the bootloader -- after a
  # double-tap reset, after a BLE-commanded jump, or after a failed attempt left it there.
  if ($appDev.InstanceId -like "*PID_00DA*") {
    Write-Host "MSV3 $Port is already the DFU port -- skipping the touch."
    $dfuPort = $Port
  }
  else {

  Write-Host "MSV3 1200-baud touch on $Port to enter DFU ..."
  try {
    $touch = New-Object System.IO.Ports.SerialPort $Port, 1200
    $touch.DtrEnable = $false
    $touch.Open(); Start-Sleep -Milliseconds 200; $touch.Close()
  } catch { Write-Host "  (port dropped during touch, which is normal)" }

  $dfuPort = $null
  foreach ($i in 1..60) {
    Start-Sleep -Milliseconds 500
    $cand = Get-PnpDevice -Class Ports -ErrorAction SilentlyContinue | Where-Object {
      $_.Status -eq "OK" -and $_.InstanceId -like "*PID_00DA*" -and
      ((Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_Parent' `
         -ErrorAction SilentlyContinue).Data -like "*$chipSerial*")
    } | Select-Object -First 1
    if ($cand -and $cand.FriendlyName -match "\((COM\d+)\)") { $dfuPort = $Matches[1]; break }
  }
  if (-not $dfuPort) {
    throw @"
The DFU port for chip $chipSerial never appeared after the touch.
The application may have been asleep -- this is a duty-cycled sensor and USB CDC only enumerates
while it is awake. Nothing was sent, so the probe is untouched. Try again during a wake window.
"@
  }
  Write-Host "MSV3 DFU port: $dfuPort (application was on $Port)"
  Start-Sleep -Milliseconds 1500
  }

  & $upPython -m nordicsemi dfu serial -pkg $signedPkg -p $dfuPort -b 115200 -sb -t 0
  if ($LASTEXITCODE -ne 0) {
    throw @"
The wired DFU did not complete. The probe's application is very likely ERASED -- single-bank DFU
erases before it authenticates -- so recover it over BLE before anything else:

  MSV3\.arduino-build\python-portable\runtime\python.exe scripts\ble_ota_update.py <signed zip> --probe N

Then read the bootloader's account of what happened:

  MSV3\.arduino-build\python-portable\runtime\python.exe MSV3\scripts\msv3_read_dfu_trace.py --probe N
"@
  }
  Write-Host "MSV3 wired upload complete. Confirm the probe is actually running it:"
  Write-Host "  scripts\msv3_fleet_scan.py --probe N --require-progress"
}
