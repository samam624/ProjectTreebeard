param(
  [ValidateSet("compile", "upload", "monitor", "board-list")]
  [string]$Action = "upload",
  [string]$Port = "COM12",
  [string]$Sketch = "arduino/MSV2_Moisture_AFE_Test",
  [string]$CliPath = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $CliPath)) {
  throw "arduino-cli.exe not found at $CliPath"
}

# ESP32-S3 devkit (native USB, VID_303A PID_4001) bought separately for this AFE
# comparison bench test - not one of the other known MSV2 boards.
$Fqbn = "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=4M,PartitionScheme=huge_app,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default"
$BuildPath = Join-Path (Resolve-Path ".").Path ".arduino-build\moisture-afe-test"

New-Item -ItemType Directory -Force -Path $BuildPath | Out-Null

switch ($Action) {
  "compile" {
    & $CliPath compile --fqbn $Fqbn --build-path $BuildPath $Sketch
  }
  "upload" {
    & $CliPath compile --fqbn $Fqbn --build-path $BuildPath $Sketch
    & $CliPath upload -p $Port --fqbn $Fqbn --input-dir $BuildPath $Sketch
  }
  "monitor" {
    & $CliPath monitor -p $Port -c baudrate=115200
  }
  "board-list" {
    & $CliPath board list
  }
}
