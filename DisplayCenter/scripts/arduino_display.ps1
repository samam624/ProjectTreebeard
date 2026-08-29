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

$Fqbn = "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=4M,PartitionScheme=huge_app,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default"
$BuildArgs = @()
$BuildName = if ($ForceWifiSetup) { "display-force-wifi-setup" } else { "display-normal" }
$BuildPath = Join-Path (Resolve-Path ".").Path ".arduino-build\$BuildName"

New-Item -ItemType Directory -Force -Path $BuildPath | Out-Null

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
