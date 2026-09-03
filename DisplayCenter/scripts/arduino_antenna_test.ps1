param(
  [ValidateSet("compile", "upload", "monitor", "board-list")]
  [string]$Action = "upload",
  [string]$Port,
  [string]$Sketch = "DisplayCenter/arduino/Treebeard_Antenna_Test",
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

  throw "Antenna test sketch not found at '$Path' (checked the repository root, DisplayCenter folder, and current directory)"
}

# Same ESP32-S3-WROOM-1U-N16R8 hardware options as scripts/arduino_display.ps1 except that this
# small diagnostic intentionally leaves PSRAM disabled because it does not allocate receiver UI,
# history, or TLS state.
$Fqbn = "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=4M,PartitionScheme=huge_app,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default"
$BuildPath = Join-Path $RepoRoot ".arduino-build\antenna-test"

if ($Action -in @("compile", "upload")) {
  $Sketch = Resolve-ProjectPath $Sketch
  New-Item -ItemType Directory -Force -Path $BuildPath | Out-Null
}

if ($Action -in @("upload", "monitor") -and [string]::IsNullOrEmpty($Port)) {
  throw "This is the new custom PCB, not the devkit - its COM port isn't known in advance. Run '-Action board-list' first, then pass '-Port COMx' explicitly."
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
