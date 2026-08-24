param(
  [ValidateSet("compile", "upload", "monitor", "board-list")]
  [string]$Action = "upload",
  [string]$Port = "COM13",
  [string]$Sketch = "",
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

$Fqbn = "esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB,DebugLevel=none,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default"
$BuildPath = Join-Path (Resolve-Path ".").Path ".arduino-build\sd-test"

if ($Action -in @("compile", "upload")) {
  # The old default, SD_test/SD_Test, never existed in Git and no board-specific SD
  # pin map is documented here. Fail closed instead of compiling or flashing an
  # unrelated example with guessed wiring.
  if ([string]::IsNullOrWhiteSpace($Sketch) -or !(Test-Path $Sketch)) {
    throw "No repository SD test sketch exists. Pass -Sketch with an existing board-specific SD test path."
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
