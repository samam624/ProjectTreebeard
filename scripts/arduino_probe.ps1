param(
  [ValidateSet("compile", "upload", "monitor", "board-list")]
  [string]$Action = "upload",
  [string]$Port = "COM5",
  [string]$Sketch = "arduino/MSV2_Moisture_BLE_Sleep",
  [string]$CliPath = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
  [string]$Fqbn = "Seeeduino:nrf52:xiaonRF52840Sense"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $CliPath)) {
  throw "arduino-cli.exe not found at $CliPath"
}

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
