param(
  [ValidateSet("compile", "upload", "monitor", "board-list", "doctor")]
  [string]$Action = "compile",
  [string]$Port,
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
  [ValidateRange(0, 255)]
  [int]$WakeDiscardConversions = 2,
  [ValidateSet(10, 33)]
  [int]$FdcDeglitchMhz = 10,
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
if ($SensorId -ge 0) {
  $fdcMuxConfig = if ($FdcDeglitchMhz -eq 10) { "0x020D" } else { "0x020F" }
  $defines += "-DMSV3_FDC_SETTLECOUNT_CH0=$FdcSettleCount"
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
  Write-Host "  FDC: SETTLECOUNT=$FdcSettleCount, discarded conversions=$WakeDiscardConversions, deglitch=${FdcDeglitchMhz}MHz"
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
  if ([string]::IsNullOrWhiteSpace($Port)) {
    throw "Compile passed, but upload needs the current port. Rerun with -Action upload -Port COMx."
  }
  Invoke-ArduinoCli @(
    "upload", "-p", $Port, "--fqbn", $Fqbn,
    "--input-dir", $buildPath, $sketchPath
  )
}
