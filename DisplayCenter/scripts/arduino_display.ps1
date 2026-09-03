param(
  [ValidateSet("compile", "upload", "monitor", "board-list")]
  [string]$Action = "upload",
  [string]$Port = "COM13",
  [string]$Sketch = "DisplayCenter/arduino/Treebeard_ESP32_Display_Runtime",
  [string]$CliPath = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
  [switch]$ForceWifiSetup,
  # Diagnostic builds only. Production stays "none". "error" is enough to surface the esp32
  # core's own log_e lines - notably ssl_client.cpp's `socket error on fd %d, errno: %d`, which
  # carries the real SO_ERROR that NetworkClientSecure otherwise discards when it returns a
  # bare -1 (see DisplayCenter/docs/RECEIVER_CLOUD_UPLOAD_CONNECTION_REFUSED_2026-08-31.md).
  # Builds to its own build path so a diagnostic image never reuses production objects.
  [ValidateSet("none", "error", "warn", "info", "debug", "verbose")]
  [string]$CoreDebugLevel = "none"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $CliPath)) {
  throw "arduino-cli.exe not found at $CliPath"
}

function Invoke-ArduinoCli {
  param([string[]]$Arguments)
  & $CliPath @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "arduino-cli failed with exit code $LASTEXITCODE"
  }
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SubsystemRoot = (Resolve-Path (Join-Path $ScriptDir "..")).Path
$RepoRoot = (Resolve-Path (Join-Path $SubsystemRoot "..")).Path

function Resolve-ProjectPath {
  param([string]$Path)

  $Candidates = if ([IO.Path]::IsPathRooted($Path)) {
    @($Path)
  } else {
    @(
      (Join-Path $RepoRoot $Path),
      (Join-Path $SubsystemRoot $Path),
      (Join-Path (Get-Location).Path $Path)
    )
  }

  foreach ($Candidate in $Candidates) {
    if (Test-Path -LiteralPath $Candidate) {
      return (Resolve-Path -LiteralPath $Candidate).Path
    }
  }

  throw "Display sketch not found at '$Path' (checked the repository root, DisplayCenter folder, and current directory)"
}

function Invoke-ReceiverRegressionChecks {
  $NodeCommand = Get-Command node -ErrorAction SilentlyContinue
  if ($null -eq $NodeCommand) {
    throw "node is required to run DisplayCenter receiver regression checks before compile/upload"
  }

  # Keep every dependency-free firmware contract in the compile/upload gate. Running only
  # verify_receiver_cloud_recovery.mjs let display geometry, moisture, battery, and MSV3
  # reference regressions compile and flash even though dedicated checks already existed.
  $RegressionScripts = @(
    "verify_receiver_cloud_recovery.mjs",
    "verify_display_card_layout.mjs",
    "verify_moisture_alert_thresholds.mjs",
    "verify_receiver_moisture_filter.mjs",
    "verify_battery_curve_sync.mjs",
    "verify_msv3_reference_compensation.mjs"
  )

  foreach ($RegressionName in $RegressionScripts) {
    $RegressionScript = Join-Path $ScriptDir $RegressionName
    if (!(Test-Path -LiteralPath $RegressionScript)) {
      throw "Receiver regression check not found at '$RegressionScript'"
    }

    & $NodeCommand.Source $RegressionScript
    if ($LASTEXITCODE -ne 0) {
      throw "Receiver regression check '$RegressionName' failed; refusing to compile or flash"
    }
  }
}

# 2026-09-02: PSRAM disabled -> opi. This board is an ESP32-S3-WROOM-1U-N16R8 (user-built,
# confirmed): 16MB flash + 8MB OCTAL PSRAM. PSRAM had never been enabled, so every allocation
# competed for internal RAM only - live telemetry caught minFreeHeapEverBytes at 6,368 bytes
# under load, with one cloud upload measured consuming ~45KB (104,880 -> 59,688). Large
# allocations (ArduinoJson documents, Strings, history payload) can now spill to PSRAM,
# leaving internal RAM for what genuinely needs it. "opi" NOT "enabled": the R8 part is octal;
# the QSPI setting will not boot on this module.
# Verified safe for stored data: this changes the memory map only. FlashSize is deliberately
# left at 4M - raising it to 16M would rewrite the partition table and relocate the LittleFS
# partition, orphaning /cloud.json, /wifi.json, /ui.json and /history-outbox-v1.bin.
# EraseFlash=none does NOT protect against that. Do not change FlashSize without a migration.
# Display uses GPIO 10-16, buzzer 38, LED 2 - no conflict with octal PSRAM's GPIO 33-37.
$Fqbn = "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=4M,PartitionScheme=huge_app,DebugLevel=$CoreDebugLevel,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default"
$BuildArgs = @()
$BuildName = if ($ForceWifiSetup) { "display-force-wifi-setup" } else { "display-normal" }
if ($CoreDebugLevel -ne "none") { $BuildName = "$BuildName-debug-$CoreDebugLevel" }
$BuildPath = Join-Path $RepoRoot ".arduino-build\$BuildName"

if ($Action -in @("compile", "upload")) {
  $Sketch = Resolve-ProjectPath $Sketch
  Invoke-ReceiverRegressionChecks
  New-Item -ItemType Directory -Force -Path $BuildPath | Out-Null
}

if ($ForceWifiSetup) {
  $BuildArgs += @("--build-property", "compiler.cpp.extra_flags=-DTREEBEARD_FORCE_WIFI_SETUP=1")
}

switch ($Action) {
  "compile" {
    Invoke-ArduinoCli (@("compile", "--fqbn", $Fqbn, "--build-path", $BuildPath) + $BuildArgs + @($Sketch))
  }
  "upload" {
    Invoke-ArduinoCli (@("compile", "--fqbn", $Fqbn, "--build-path", $BuildPath) + $BuildArgs + @($Sketch))
    Invoke-ArduinoCli @("upload", "-p", $Port, "--fqbn", $Fqbn, "--input-dir", $BuildPath, $Sketch)
  }
  "monitor" {
    Invoke-ArduinoCli @("monitor", "-p", $Port, "-c", "baudrate=115200")
  }
  "board-list" {
    Invoke-ArduinoCli @("board", "list")
  }
}
