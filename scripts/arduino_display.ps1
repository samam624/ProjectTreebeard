param(
  [ValidateSet("compile", "upload", "monitor", "board-list")]
  [string]$Action = "upload",
  [string]$Port = "COM13",
  [string]$Sketch = "arduino/MSV2_ESP32_Display_Runtime",
  [string]$CliPath = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $CliPath)) {
  throw "arduino-cli.exe not found at $CliPath"
}

$Fqbn = "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=4M,PartitionScheme=huge_app,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default"

switch ($Action) {
  "compile" {
    & $CliPath compile --fqbn $Fqbn $Sketch
  }
  "upload" {
    & $CliPath upload -p $Port --fqbn $Fqbn $Sketch
  }
  "monitor" {
    & $CliPath monitor -p $Port -c baudrate=115200
  }
  "board-list" {
    & $CliPath board list
  }
}
