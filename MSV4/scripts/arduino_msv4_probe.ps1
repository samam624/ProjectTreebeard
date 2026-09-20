param(
  [ValidateSet("compile", "doctor")]
  [string]$Action = "compile",
  [string]$Sketch = "arduino/MSV4_Probe_Bringup_BLE",
  # Alphanumeric tag appended to "MSV4-BUILD:" in the image (DIS firmware revision + `info`).
  [string]$BuildTag,
  # Written into the signed DFU init packet. The MSV3 bootloader (1.0.2+) keeps a monotonic
  # application-version floor, so every later image for the same board must use a larger number.
  # 0x00040000 + n keeps MSV4 bring-up images in their own range, clear of MSV3's 655xx numbers.
  [uint32]$ApplicationVersion = 262145,
  # Also produce a signed BLE DFU package (needs the MSV3 signing key and DFU tools).
  [switch]$Package,
  # Required with -Package. The package is signed with the MSV3 key and tagged with MSV3's device
  # type 0x3356, so every MSV3 field bootloader would also accept it -- and this LFXO image hangs an
  # MSV3 board until its crystal check escapes to DFU. Passing this switch records that you are
  # building for a bench MSV4 board, until MSV4 has its own device type.
  [switch]$AcceptSharedMsv3DeviceType,
  [string]$SigningKey,
  [string]$Python,
  [string]$CliPath = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
)

# MSV4 has a populated 32.768 kHz crystal and NO USB. Both facts invert MSV3 rules, so this script
# shares nothing mutable with MSV3/scripts/arduino_msv3_probe.ps1:
# - The LFCLK source comes from the TRACKED variant MSV4/arduino/variants/msv4_e73 (USE_LFXO),
#   selected with build.variant.path. The installed core's pca10056 variant -- which the MSV3
#   script rewrites to USE_LFRC -- is never read or written here.
# - There is no upload action. MSV4 is programmed only over SWD (scripts/swd_flash_hex.py, with its
#   contact gate) or BLE DFU.
#
# The application linker script IS shared, read-only, from MSV3/linker: it reserves the last eight
# bytes of RAM (0x2003FFF8) that MSV3 bootloader 1.0.3+ also reserves, and the MSV4 bring-up
# sketch keeps its crystal-failure boot counter there.

$ErrorActionPreference = "Stop"
$Fqbn = "adafruit:nrf52:pca10056"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$msv4Root = (Resolve-Path (Join-Path $scriptDir "..")).Path
$repoRoot = (Resolve-Path (Join-Path $msv4Root "..")).Path
$msv3Root = Join-Path $repoRoot "MSV3"
$sketchPath = if ([IO.Path]::IsPathRooted($Sketch)) { $Sketch } else { Join-Path $msv4Root $Sketch }
$sketchName = Split-Path -Leaf $sketchPath
$variantPath = Join-Path $msv4Root "arduino\variants\msv4_e73"
$linkerPath = Join-Path $msv3Root "linker\msv3_nrf52840_s140_v6.ld"
# The sketch #includes ResetSafeBLEDfu.cpp by a path relative to <buildPath>\sketch, so this
# directory depth is part of the contract. Do not move it without updating the sketch.
$buildPath = Join-Path $msv4Root ".arduino-build\msv4-bringup"
$outDir = Join-Path $msv4Root ".arduino-build\msv4-out"

function Assert-Msv4Variant {
  foreach ($f in @("variant.h", "variant.cpp")) {
    if (!(Test-Path -LiteralPath (Join-Path $variantPath $f))) {
      throw "Tracked MSV4 variant file missing: $(Join-Path $variantPath $f)"
    }
  }
  $h = [IO.File]::ReadAllText((Join-Path $variantPath "variant.h"))
  $lfxo = [regex]::Matches($h, '(?m)^\s*#\s*define\s+USE_LFXO\b').Count
  $lfrc = [regex]::Matches($h, '(?m)^\s*#\s*define\s+USE_LFRC\b').Count
  $marker = [regex]::Matches($h, '(?m)^\s*#\s*define\s+MSV4_VARIANT\s+1\b').Count
  if ($lfxo -ne 1 -or $lfrc -ne 0 -or $marker -ne 1) {
    throw "MSV4 variant must define MSV4_VARIANT 1 and USE_LFXO, and not USE_LFRC (found LFXO=$lfxo LFRC=$lfrc MARKER=$marker): $variantPath"
  }
  Write-Host "MSV4 variant: OK (USE_LFXO, tracked at $variantPath)"
}

function Assert-SharedLinker {
  if (!(Test-Path -LiteralPath $linkerPath)) { throw "Shared linker script missing: $linkerPath" }
  $t = [IO.File]::ReadAllText($linkerPath)
  foreach ($s in @("ORIGIN = 0x2003FFF8, LENGTH = 0x08", ".msv3_boot_guard_retained (NOLOAD)")) {
    if (!$t.Contains($s)) { throw "Shared linker script is missing sentinel '$s': $linkerPath" }
  }
  Write-Host "Shared linker: OK (0x2003FFF8 reserved)"
}

if (!(Test-Path -LiteralPath $CliPath)) {
  throw "arduino-cli.exe not found at '$CliPath'. Pass -CliPath."
}
Assert-Msv4Variant
Assert-SharedLinker
if ($Action -eq "doctor") { Write-Host "MSV4 toolchain check passed."; exit 0 }

if (!(Test-Path -LiteralPath $sketchPath)) { throw "Sketch not found: '$sketchPath'" }
if ($BuildTag -and $BuildTag -notmatch '^[A-Za-z0-9._-]+$') {
  throw "-BuildTag must be alphanumeric with . _ - only (the sketch stringifies it)."
}

if ($Package -and -not $AcceptSharedMsv3DeviceType) {
  throw @"
-Package would sign an MSV4 image with MSV3's device type 0x3356, which every MSV3 field bootloader
accepts. Until MSV4 has its own device type, add -AcceptSharedMsv3DeviceType to confirm this package
is only for a bench MSV4 board, and never hand it to scripts/ble_ota_update.py --probe <MSV3 probe>.
"@
}

New-Item -ItemType Directory -Force -Path $buildPath, $outDir | Out-Null

$defines = @("-DMSV4_APP_VERSION=$ApplicationVersion")
if ($BuildTag) { $defines += "-DMSV4_BUILD_TAG=$BuildTag" }
$defineFlags = $defines -join " "

# Forward slashes: these strings pass through arduino-cli property expansion into gcc/ld arguments.
$variantArg = $variantPath -replace '\\', '/'
$linkerArg = $linkerPath -replace '\\', '/'

$logPath = Join-Path $outDir "$sketchName.compile.log"
$compileArgs = @(
  "compile", "--clean", "--verbose",
  "--fqbn", $Fqbn,
  "--build-path", $buildPath,
  "--build-property", "build.variant.path=$variantArg",
  "--build-property", "build.ldscript=$linkerArg",
  "--build-property", "compiler.c.extra_flags=$defineFlags",
  "--build-property", "compiler.cpp.extra_flags=$defineFlags",
  $sketchPath
)
# Windows PowerShell 5.1 turns every native stderr line into an error record, and with
# $ErrorActionPreference = "Stop" the first compiler warning would abort a good build. Judge the
# compile by its exit code instead.
$ErrorActionPreference = "Continue"
& $CliPath @compileArgs 2>&1 | ForEach-Object { "$_" } | Set-Content -LiteralPath $logPath -Encoding utf8
$compileExit = $LASTEXITCODE
$ErrorActionPreference = "Stop"
if ($compileExit -ne 0) {
  Get-Content -LiteralPath $logPath -Tail 40
  throw "arduino-cli compile failed (exit $compileExit). Full log: $logPath"
}

# Compiling is not proof the tracked variant was used; the sketch's #error on MSV4_VARIANT is one
# check, and the verbose log is the other. variant.cpp must have been compiled from OUR folder and
# no command may reference the installed pca10056 variant.
$log = [IO.File]::ReadAllText($logPath)
# arduino-cli writes the include path with / and the variant.cpp source path with \ (doubled in
# the log), so match the folder with any run of either separator.
$sep = '[/\\]+'
$variantPattern = (($variantArg -split '/') | ForEach-Object { [regex]::Escape($_) }) -join $sep
if ($log -notmatch ('-I' + $variantPattern)) {
  throw "Verbose log shows no -I include of $variantArg. build.variant.path was not honoured. Log: $logPath"
}
if ($log -notmatch ($variantPattern + $sep + 'variant\.cpp')) {
  throw "Verbose log does not show variant.cpp compiled from $variantArg. build.variant.path was not honoured. Log: $logPath"
}
if ($log -match 'variants[\\/]pca10056') {
  throw "Verbose log references the installed pca10056 variant; refusing the image. Log: $logPath"
}
Write-Host "Variant check: OK (variant.cpp compiled from the tracked MSV4 folder, no pca10056 reference)"

$mapPath = Join-Path $buildPath "$sketchName.ino.map"
if (!(Test-Path -LiteralPath $mapPath)) { throw "Link map missing: $mapPath" }
if ([IO.File]::ReadAllText($mapPath) -notmatch '(?m)^\.msv3_boot_guard_retained\s+0x2003fff8\s+0x8\b') {
  throw "Retained record is not 8 bytes at 0x2003FFF8 in $mapPath. The shared linker script was not used."
}
Write-Host "Map check: OK (.msv3_boot_guard_retained = 0x2003FFF8, 8 bytes)"

$hexSrc = Join-Path $buildPath "$sketchName.ino.hex"
if (!(Test-Path -LiteralPath $hexSrc)) { throw "Build produced no hex: $hexSrc" }
$tagPart = if ($BuildTag) { "-$BuildTag" } else { "" }
$hexOut = Join-Path $outDir "$sketchName-v$ApplicationVersion$tagPart.hex"
Copy-Item -LiteralPath $hexSrc -Destination $hexOut -Force
Write-Host "Application hex: $hexOut"

if ($Package) {
  . (Join-Path $msv3Root "scripts\msv3_python.ps1")
  $pythonExe = Resolve-Msv3Python -Explicit $Python -Msv3Root $msv3Root
  if (-not (Test-Msv3Python -PythonExe $pythonExe)) { throw "Python '$pythonExe' would not run. Pass -Python." }
  $nrfutil = (& (Join-Path $msv3Root "scripts\prepare_msv3_dfu_tools.ps1") -Python $pythonExe | Select-Object -Last 1)
  $keyPath = if ($SigningKey) { $SigningKey } else { Join-Path $msv3Root ".secrets\msv3-dfu-signing-private.pem" }
  if (!(Test-Path -LiteralPath $keyPath)) { throw "Signing key not found: $keyPath" }

  # device type 0x3356 = what MSV3 bootloader 1.0.x accepts. MSV4 bring-up runs on that bootloader
  # for now; MSV4 needs its OWN device type before any fleet use, or an MSV3 bootloader will accept
  # an LFXO image. The sketch's crystal check escapes such a mis-flash into BLE DFU, but that is a
  # recovery path, not a substitute for the bootloader refusing the package.
  $zipOut = Join-Path $outDir "$sketchName-v$ApplicationVersion$tagPart-signed-ota.zip"
  & $nrfutil dfu genpkg --dev-type 0x3356 --dev-revision 52840 `
    --application-version $ApplicationVersion --sd-req 0xB6 `
    --application $hexOut --key-file $keyPath $zipOut
  if ($LASTEXITCODE -ne 0) { throw "nrfutil genpkg failed (exit $LASTEXITCODE)" }
  Write-Host "Signed DFU package: $zipOut"
}
