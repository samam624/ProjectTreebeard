param(
  [string]$ReleaseVersion = "1.0.1",
  [uint32]$ApplicationVersion = 65537,
  [string]$SigningKey,
  [string]$Python,
  [switch]$GenerateSigningKey,
  [switch]$SkipApplicationCompile
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$msv3Root = (Resolve-Path (Join-Path $scriptDir "..")).Path
$repoRoot = (Resolve-Path (Join-Path $msv3Root "..")).Path
$cacheRoot = Join-Path $msv3Root ".arduino-build"
$source = Join-Path $cacheRoot "Adafruit_nRF52_Bootloader"
$boardSource = Join-Path $msv3Root "bootloader\msv3_e73"
$boardDest = Join-Path $source "src\boards\msv3_e73"
$portablePython = Join-Path $cacheRoot "python-portable\runtime\python.exe"
$pythonExe = if (![string]::IsNullOrWhiteSpace($Python)) {
  $Python
} elseif (Test-Path -LiteralPath $portablePython) {
  $portablePython
} else {
  "python"
}
$keyPath = if ([string]::IsNullOrWhiteSpace($SigningKey)) {
  Join-Path $msv3Root ".secrets\msv3-dfu-signing-private.pem"
} elseif ([IO.Path]::IsPathRooted($SigningKey)) {
  $SigningKey
} else {
  Join-Path $repoRoot $SigningKey
}

if ($ReleaseVersion -notmatch '^\d+\.\d+\.\d+$') {
  throw "ReleaseVersion must be semantic numeric form, for example 1.0.0"
}
if ($ApplicationVersion -eq 0 -or $ApplicationVersion -eq [uint32]::MaxValue) {
  throw "ApplicationVersion must be explicit and cannot be 0 or 0xFFFFFFFF"
}

$nrfutil = (& (Join-Path $scriptDir "prepare_msv3_dfu_tools.ps1") -Python $pythonExe | Select-Object -Last 1)
$deps = Join-Path $cacheRoot "dfu-pydeps"
$env:PYTHONPATH = $deps

if (!(Test-Path -LiteralPath $keyPath)) {
  if (!$GenerateSigningKey) {
    throw "Signing key not found at $keyPath. Restore the existing key, or explicitly pass -GenerateSigningKey when establishing a new SWD-installed trust root."
  }
  & $pythonExe (Join-Path $scriptDir "generate_msv3_dfu_key.py") $keyPath
  if ($LASTEXITCODE -ne 0) { throw "MSV3 signing-key generation failed" }
}

$vkCode = (& $nrfutil keys --show-vk code $keyPath | Out-String)
if ($LASTEXITCODE -ne 0) { throw "Could not derive verification key" }
$qxMatch = [regex]::Match($vkCode, 'Qx\[\]\s*=\s*\{\s*(.*?)\s*\}', 'Singleline')
$qyMatch = [regex]::Match($vkCode, 'Qy\[\]\s*=\s*\{\s*(.*?)\s*\}', 'Singleline')
if (!$qxMatch.Success -or !$qyMatch.Success) { throw "Unexpected verification-key output" }
$qx = $qxMatch.Groups[1].Value -replace '\s+', ''
$qy = $qyMatch.Groups[1].Value -replace '\s+', ''

if (!(Test-Path -LiteralPath $source)) {
  New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
  & git clone --branch 0.11.0 --recurse-submodules `
    https://github.com/adafruit/Adafruit_nRF52_Bootloader.git $source
  if ($LASTEXITCODE -ne 0) { throw "Could not clone pinned Adafruit bootloader source" }
}

$requiredSource = Join-Path $source "lib\softdevice\s140_nrf52_6.1.1\s140_nrf52_6.1.1_softdevice.hex"
if (!(Test-Path -LiteralPath $requiredSource)) {
  throw "Pinned bootloader checkout is incomplete; missing $requiredSource"
}

if (Test-Path -LiteralPath $boardDest) { Remove-Item -LiteralPath $boardDest -Recurse -Force }
Copy-Item -LiteralPath $boardSource -Destination $boardDest -Recurse

# Use an MSV3-only legacy DFU device type. The stock image accepts generic 0x0052 packages;
# leaving that value in a sealed product would permit a correctly signed but wrong-family image.
$dfuInit = Join-Path $source "src\dfu_init.c"
$dfuText = [IO.File]::ReadAllText($dfuInit)
$stockDeviceType = '#define ADAFRUIT_DEVICE_TYPE         0x0052'
$msv3DeviceType = '#define ADAFRUIT_DEVICE_TYPE         0x3356'
if ($dfuText.Contains($stockDeviceType)) {
  $dfuText = $dfuText.Replace($stockDeviceType, $msv3DeviceType)
  [IO.File]::WriteAllText($dfuInit, $dfuText, [Text.UTF8Encoding]::new($false))
} elseif (!$dfuText.Contains($msv3DeviceType)) {
  throw "Refusing to guess while setting the MSV3 DFU device type"
}

$make = Join-Path $env:LOCALAPPDATA `
  "Microsoft\WinGet\Packages\ezwinports.make_Microsoft.Winget.Source_8wekyb3d8bbwe\bin\make.exe"
if (!(Test-Path -LiteralPath $make)) {
  throw "GNU Make 4.4.1 is required. Install WinGet package ezwinports.make."
}
$gcc = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA "Arduino15\packages\adafruit\tools\arm-none-eabi-gcc") `
  -Filter "arm-none-eabi-gcc.exe" -Recurse | Select-Object -First 1
if (!$gcc) { throw "Arduino's arm-none-eabi-gcc toolchain was not found" }
$crossCompile = ((Split-Path -Parent $gcc.FullName) -replace '\\', '/') + '/arm-none-eabi-'

$bootVersion = "0.11.0-msv3.$ReleaseVersion"
$makeArgs = @(
  "BOARD=msv3_e73",
  "GIT_VERSION=$bootVersion",
  # GNU sed is not part of the minimal Windows make package. These are the exact values that
  # upstream's sed expression would derive from the pinned bootloader tag 0.11.0.
  "_VER_MAJ=0",
  "_VER_MIN=11",
  "_VER_PAT=0",
  "_VER3=0 11 0",
  "CROSS_COMPILE=$crossCompile",
  "NRFUTIL=$($nrfutil -replace '\\', '/')",
  "SIGNED_FW=1",
  "SIGNED_FW_QX=$qx",
  "SIGNED_FW_QY=$qy",
  # The 1.0.0 dual-bank build accepted a complete BLE transfer but failed to leave a valid
  # application in bank 0 on the real E73 board. Adafruit's default single-bank path has been
  # proven on this hardware and DEFAULT_TO_OTA_DFU still guarantees recovery after an invalid app.
  "DUALBANK_FW=0",
  "DEFAULT_TO_OTA_DFU=1",
  "all"
)
$priorBuild = Join-Path $source "_build\build-msv3_e73"
if (Test-Path -LiteralPath $priorBuild) {
  Remove-Item -LiteralPath $priorBuild -Recurse -Force
}
Push-Location $source
try {
  & $make @makeArgs
  if ($LASTEXITCODE -ne 0) { throw "MSV3 bootloader build failed" }
} finally {
  Pop-Location
}

$buildDir = Join-Path $source "_build\build-msv3_e73"
$bootOnly = Join-Path $buildDir "msv3_e73_bootloader-$bootVersion.hex"
$bootMerged = Join-Path $buildDir "msv3_e73_bootloader-$($bootVersion)_s140_6.1.1.hex"
if (!(Test-Path -LiteralPath $bootOnly) -or !(Test-Path -LiteralPath $bootMerged)) {
  throw "Expected bootloader outputs were not created"
}

if (!$SkipApplicationCompile) {
  & powershell -ExecutionPolicy Bypass -File (Join-Path $scriptDir "arduino_msv3_probe.ps1") `
    -Action doctor
  if ($LASTEXITCODE -ne 0) { throw "MSV3 doctor check failed" }
  & powershell -ExecutionPolicy Bypass -File (Join-Path $scriptDir "arduino_msv3_probe.ps1") `
    -Action compile -Sketch "arduino/MSV3_Moisture_BLE_Sleep"
  if ($LASTEXITCODE -ne 0) { throw "MSV3 production application build failed" }
}

$appHex = Join-Path $cacheRoot "msv3-probe\MSV3_Moisture_BLE_Sleep.ino.hex"
if (!(Test-Path -LiteralPath $appHex)) { throw "Missing production application: $appHex" }

$releaseDir = Join-Path $msv3Root "bootloader\generated\$ReleaseVersion"
$canonicalDir = Join-Path $msv3Root "bootloader\generated"
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
$releaseBoot = Join-Path $releaseDir "msv3-e73-recovery-signed-s140-6.1.1.hex"
$signedOta = Join-Path $releaseDir "msv3-$ReleaseVersion-signed-ota.zip"
$initialInstall = Join-Path $releaseDir "msv3-$ReleaseVersion-initial-install-from-stock.zip"
Copy-Item -LiteralPath $bootMerged -Destination $releaseBoot -Force

& $nrfutil dfu genpkg --dev-type 0x3356 --dev-revision 52840 `
  --application-version $ApplicationVersion --sd-req 0xB6 `
  --application $appHex --key-file $keyPath $signedOta
if ($LASTEXITCODE -ne 0) { throw "Signed OTA package generation failed" }

# One-time bridge: the installed generic stock loader requires 0x0052 and CRC16 init data.
& $nrfutil dfu genpkg --dev-type 0x0052 --dev-revision 52840 `
  --application-version $ApplicationVersion --sd-req 0xB6 `
  --application $appHex --bootloader $bootOnly --softdevice $requiredSource $initialInstall
if ($LASTEXITCODE -ne 0) { throw "Initial stock-to-MSV3 package generation failed" }

& $pythonExe (Join-Path $scriptDir "verify_msv3_dfu_package.py") $signedOta `
  --device-type 0x3356 --application-version $ApplicationVersion --key $keyPath
if ($LASTEXITCODE -ne 0) { throw "Signed OTA package verification failed" }
& $pythonExe (Join-Path $scriptDir "verify_msv3_dfu_package.py") $initialInstall `
  --device-type 0x0052 --application-version $ApplicationVersion
if ($LASTEXITCODE -ne 0) { throw "Initial migration package verification failed" }

$canonicalBoot = Join-Path $canonicalDir "msv3-e73-recovery-signed-s140-6.1.1.hex"
Copy-Item -LiteralPath $releaseBoot -Destination $canonicalBoot -Force
$bootHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $canonicalBoot).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$canonicalBoot.sha256", "$bootHash  $([IO.Path]::GetFileName($canonicalBoot))`n", [Text.UTF8Encoding]::new($false))

$vkHex = (& $nrfutil keys --show-vk hex $keyPath | Out-String).Trim()
$files = @($releaseBoot, $signedOta, $initialInstall, $appHex) | ForEach-Object {
  [ordered]@{
    name = [IO.Path]::GetFileName($_)
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_).Hash.ToLowerInvariant()
    bytes = (Get-Item -LiteralPath $_).Length
  }
}
$manifest = [ordered]@{
  release = $ReleaseVersion
  application_version = $ApplicationVersion
  device_type = "0x3356"
  softdevice_requirement = "0x00B6 (S140 6.1.1)"
  bootloader = [ordered]@{
    upstream = "Adafruit_nRF52_Bootloader 0.11.0"
    dual_bank = $false
    default_to_ble_recovery = $true
    signed_dfu_required = $true
    uf2_enabled = $false
  }
  public_verification_key = $vkHex
  files = $files
}
$manifestPath = Join-Path $releaseDir "release-manifest.json"
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 6) + "`n", [Text.UTF8Encoding]::new($false))

Write-Host "MSV3 release ready: $releaseDir"
Write-Host "  Future OTA: $signedOta"
Write-Host "  One-time stock migration: $initialInstall"
Write-Warning "Back up $keyPath offline before coating a production board."
