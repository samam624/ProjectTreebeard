param(
  [ValidateSet("compile", "upload", "monitor", "board-list")]
  [string]$Action = "upload",
  [string]$Port = "COM12",
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
$BuildPath = Join-Path $RepoRoot ".arduino-build\moisture-afe-test"

if ($Action -in @("compile", "upload")) {
  $Sketch = Resolve-ProjectPath $Sketch
  New-Item -ItemType Directory -Force -Path $BuildPath | Out-Null
}

switch ($Action) {
  "compile" {
    Invoke-ArduinoCli @("compile", "--fqbn", $Fqbn, "--build-path", $BuildPath, $Sketch)
  }
  "upload" {
    Invoke-ArduinoCli @("compile", "--fqbn", $Fqbn, "--build-path", $BuildPath, $Sketch)
    Invoke-ArduinoCli @("upload", "-p", $Port, "--fqbn", $Fqbn, "--input-dir", $BuildPath, $Sketch)
  }
  "monitor" {
    Invoke-ArduinoCli @("monitor", "-p", $Port, "-c", "baudrate=115200")
  }
  "board-list" {
    Invoke-ArduinoCli @("board", "list")
  }
}
