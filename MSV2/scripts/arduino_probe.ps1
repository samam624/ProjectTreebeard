param(
  [ValidateSet("compile", "upload", "monitor", "board-list")]
  [string]$Action = "upload",
  [string]$Port = "COM5",
  [string]$Sketch = "MSV2/arduino/MSV2_Moisture_BLE_Sleep",
  [string]$CliPath = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
  [string]$Fqbn = "Seeeduino:nrf52:xiaonRF52840Sense"
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

$SketchName = Split-Path $Sketch -Leaf
$BuildPath = Join-Path (Resolve-Path ".").Path ".arduino-build\probe-$SketchName"

if ($Action -in @("compile", "upload")) {
  if (!(Test-Path $Sketch)) {
    throw "Probe sketch not found at '$Sketch'"
  }
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
