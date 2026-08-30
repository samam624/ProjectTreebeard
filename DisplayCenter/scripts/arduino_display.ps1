param(
  [ValidateSet("compile", "upload", "monitor", "board-list")]
  [string]$Action = "upload",
  [string]$Port = "COM13",
  [string]$Sketch = "DisplayCenter/arduino/MSV2_ESP32_Display_Runtime",
  [string]$CliPath = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
  [switch]$ForceWifiSetup
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

$Fqbn = "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=4M,PartitionScheme=huge_app,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default"
$BuildArgs = @()
$BuildName = if ($ForceWifiSetup) { "display-force-wifi-setup" } else { "display-normal" }
$BuildPath = Join-Path $RepoRoot ".arduino-build\$BuildName"

if ($Action -in @("compile", "upload")) {
  $Sketch = Resolve-ProjectPath $Sketch
  New-Item -ItemType Directory -Force -Path $BuildPath | Out-Null
}

if ($ForceWifiSetup) {
  $BuildArgs += @("--build-property", "compiler.cpp.extra_flags=-DMSV2_FORCE_WIFI_SETUP=1")
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
