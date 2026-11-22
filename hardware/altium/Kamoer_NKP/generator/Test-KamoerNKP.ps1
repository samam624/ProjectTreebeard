[CmdletBinding()]
param(
    [string]$IntLib,
    [string]$AltiumExe = 'C:\Program Files\Altium\AD25\X2.exe',
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$generatorDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $IntLib) {
    $IntLib = Join-Path (Split-Path -Parent $generatorDir) 'Kamoer_NKP.IntLib'
}
$scriptId = [Guid]::NewGuid().ToString('N')
$scriptUnit = "VerifyKamoerNKP_$scriptId"
$scriptRunDir = Join-Path ([System.IO.Path]::GetTempPath()) ("treebeard-nkp-verifier-$scriptId")
New-Item -ItemType Directory -Force -Path $scriptRunDir | Out-Null
$scriptSource = Join-Path $scriptRunDir "$scriptUnit.pas"
$project = Join-Path $scriptRunDir "$scriptUnit.PrjScr"
Copy-Item -LiteralPath (Join-Path $generatorDir 'VerifyKamoerNKP.pas') -Destination $scriptSource
$projectText = (Get-Content -LiteralPath (Join-Path $generatorDir 'VerifyKamoerNKP.PrjScr') -Raw).Replace('VerifyKamoerNKP.pas', "$scriptUnit.pas")
[System.IO.File]::WriteAllText($project, $projectText, (New-Object System.Text.UTF8Encoding $false))

$bridgeDir = 'C:\Users\Public\treebeard_altium_nkp'
$requestFile = Join-Path $bridgeDir 'verify_request.txt'
$responseFile = Join-Path $bridgeDir 'verify_response.json'
New-Item -ItemType Directory -Force -Path $bridgeDir | Out-Null
$intLibPath = (Resolve-Path -LiteralPath $IntLib).Path
$pcbLibPath = [IO.Path]::ChangeExtension($intLibPath, '.PcbLib')
$schLibPath = [IO.Path]::ChangeExtension($intLibPath, '.SchLib')
foreach ($f in @($pcbLibPath, $schLibPath)) {
    if (-not (Test-Path -LiteralPath $f)) { throw "Source library not found next to the IntLib: $f" }
}
[System.IO.File]::WriteAllText($requestFile, "$intLibPath`r`n$pcbLibPath`r`n$schLibPath", (New-Object System.Text.UTF8Encoding $false))
if (Test-Path -LiteralPath $responseFile) { Remove-Item -LiteralPath $responseFile -Force }

$launcher = Join-Path ([System.IO.Path]::GetTempPath()) 'treebeard_nkp_altium_verify.bat'
$command = '"{0}" -RScriptingSystem:RunScript(ProjectName="{1}"^|ProcName="{2}>Run")' -f $AltiumExe, $project, $scriptUnit
[System.IO.File]::WriteAllText($launcher, $command, [System.Text.Encoding]::ASCII)
Start-Process -FilePath $launcher -WindowStyle Hidden

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while (-not (Test-Path -LiteralPath $responseFile) -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
}
if (-not (Test-Path -LiteralPath $responseFile)) {
    throw "Timed out after $TimeoutSeconds seconds waiting for Altium verification."
}
$result = Get-Content -LiteralPath $responseFile -Raw | ConvertFrom-Json
# Footprint links, read from the SchLib the IntLib is compiled from ('*' = default).
# IntegratedLibraryManager.ModelCount is printed but not trusted (session cache).
$expectedImpls = 'KAMOER_NKP_LPLATE_FLAT,KAMOER_NKP_STRAIGHT_PANEL,KAMOER_NKP_VERTICAL_TABS*'
if ($result.sch_impls -ne $expectedImpls) {
    throw "Symbol footprint links are wrong: got '$($result.sch_impls)', expected '$expectedImpls'"
}
# Expected pads/holes, typed from the Kamoer catalogue NKP drawings (mm), NOT
# from the generator, so a typo there cannot verify itself:
#   straight plate 2-dia3.2 at 48.5; L plate 4-dia4.3 at 48 and 37;
#   motor leads 1.3 mm holes at 5.08 mm pitch; one board cutout (straight only).
#   Vertical (2026-11-18): motor tabs 22.5 apart (scaled off the drawing;
#   caliper-check), so tab radius 11.25 about the motor axis. Each tab sits in
#   a ROUTED arc slot, 1.0 wide, centreline +/-40 deg about its nominal angle
#   (pin 1 at 180, pin 2 at 0; +/-30 deg rotation + the 3.2 tab), round ends.
#   Slot at 0 deg: x max = 11.25 + 0.5 = 11.75; end-cap centre
#   (11.25 cos40, 11.25 sin40) = (8.618, 7.231), so x min = 8.12, |y| max = 7.73.
#   Copper: PADS only, so all of it carries the pin's net (copper regions came
#   out No Net on 11-18). Per pin per layer: band pads every 3 deg over +/-48 =
#   33 positions x 2 bands + 2 bridge pads = 68. No copper regions; one
#   mask-opening region per tab on EACH side (per-pad openings left 0.046 mm
#   slivers). Copper 0.32 off the slot (board rule 0.3); bridge flush at 48 deg.
#   Mask region per side hugs the copper outline 0.05 mm outside it. 3 plated 0.6 mm stitch holes per tab on
#   R 12.75 at the nominal angle and +/-25 deg. dia 13 boss hole = one cutout.
$p = 'KAMOER_NKP_STRAIGHT_PANEL'; $l = 'KAMOER_NKP_LPLATE_FLAT'; $v = 'KAMOER_NKP_VERTICAL_TABS'
$expectedPads = @(
    "$v||-24.25|0.00|3.20|N", "$v||24.25|0.00|3.20|N",
    "$v|1|-12.75|0.00|0.60|P", "$v|1|-11.56|5.39|0.60|P", "$v|1|-11.56|-5.39|0.60|P",
    "$v|2|12.75|0.00|0.60|P", "$v|2|11.56|5.39|0.60|P", "$v|2|11.56|-5.39|0.60|P",
    "$v|cutouts|3", "$v|cutout|-6.50|-6.50|6.50|6.50",
    "$v|cutout|-11.75|-7.73|-8.12|7.73", "$v|cutout|8.12|-7.73|11.75|7.73",
    "$v|regions|T0|B0|TM2|BM2|O0", "$v|smd|1T68|1B68|2T68|2B68|O0",
    "$p|smd|1T0|1B0|2T0|2B0|O0", "$l|smd|1T0|1B0|2T0|2B0|O0",
    "$p||-24.25|0.00|3.20|N", "$p||24.25|0.00|3.20|N",
    "$p|1|-2.54|-25.50|1.30|P", "$p|2|2.54|-25.50|1.30|P", "$p|cutouts|1",
    "$p|cutout|-15.50|-15.50|15.50|15.50", "$p|regions|T0|B0|TM0|BM0|O0",
    "$l||-24.00|0.00|4.30|N", "$l||-18.50|0.00|4.30|N",
    "$l||18.50|0.00|4.30|N", "$l||24.00|0.00|4.30|N",
    "$l|1|-2.54|42.50|1.30|P", "$l|2|2.54|42.50|1.30|P", "$l|cutouts|0",
    "$l|regions|T0|B0|TM0|BM0|O0"
)
$got = @($result.pad_report -split ';')
$diff = Compare-Object ($got | Sort-Object) ($expectedPads | Sort-Object)
if ($diff) {
    $diff | Format-Table -AutoSize | Out-Host
    throw "Footprint pad/hole check failed (<= only in library, => only expected)."
}
Write-Host 'NKP symbol links all 3 footprints (VERTICAL_TABS default); every pad, hole, cutout extent and copper/mask region count matches.' -ForegroundColor Green
$result
