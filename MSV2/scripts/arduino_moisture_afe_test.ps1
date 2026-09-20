param(
  [ValidateSet("compile", "upload", "monitor", "board-list")]
  [string]$Action = "upload",
  [string]$Port = "COM12",
  # Which rig this image is for. 3 = the original bench rig reading the v1.2 module's own pad,
  # 9 = the hybrid rig carrying an MSV3 probe end instead. Two boards run this sketch in parallel,
  # so the identity is compiled in rather than hand-edited -- and each ID gets its own build
  # directory below, so an upload can never push one rig's image to the other from a stale cache.
  [ValidateRange(1, 16)]
  [int]$SensorId = 3,
  [string]$Sketch = "MSV2/arduino/MSV2_Moisture_AFE_Test",
  [string]$CliPath = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
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

  throw "Moisture AFE sketch not found at '$Path' (checked the repository root, MSV2 folder, and current directory)"
}

# ESP32-S3 devkit (native USB, VID_303A PID_4001) bought separately for this AFE
# comparison bench test - not one of the other known MSV2 boards.
$Fqbn = "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=4M,PartitionScheme=huge_app,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default"
# Per-identity build directory. `upload` below reuses --input-dir, so a shared path would let a
# compile for sensor 3 be uploaded to the sensor 9 board (or the reverse) whenever the two flashes
# happen back to back -- the same stale-artifact hazard MSV3 hit with its probe packages.
$BuildPath = Join-Path $RepoRoot ".arduino-build\moisture-afe-test-sensor$SensorId"

# Compiled in, so the running image and the identity on the air cannot disagree.
$ExtraFlags = "compiler.cpp.extra_flags=-DAFE_SENSOR_ID=$SensorId"

if ($Action -in @("compile", "upload")) {
  $Sketch = Resolve-ProjectPath $Sketch
  New-Item -ItemType Directory -Force -Path $BuildPath | Out-Null
}

switch ($Action) {
  "compile" {
    Write-Host "Building AFE rig image for sensor $SensorId"
    Invoke-ArduinoCli @("compile", "--fqbn", $Fqbn, "--build-property", $ExtraFlags, "--build-path", $BuildPath, $Sketch)
  }
  "upload" {
    Write-Host "Building AFE rig image for sensor $SensorId -> $Port"
    Invoke-ArduinoCli @("compile", "--fqbn", $Fqbn, "--build-property", $ExtraFlags, "--build-path", $BuildPath, $Sketch)
    Invoke-ArduinoCli @("upload", "-p", $Port, "--fqbn", $Fqbn, "--input-dir", $BuildPath, $Sketch)
  }
  "monitor" {
    Invoke-ArduinoCli @("monitor", "-p", $Port, "-c", "baudrate=115200")
  }
  "board-list" {
    Invoke-ArduinoCli @("board", "list")
  }
}
