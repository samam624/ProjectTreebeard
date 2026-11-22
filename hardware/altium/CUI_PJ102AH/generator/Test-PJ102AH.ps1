[CmdletBinding()]
param(
    [string]$IntLib,
    [string]$AltiumExe = 'C:\Program Files\Altium\AD25\X2.exe',
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$generatorDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $IntLib) {
    $IntLib = Join-Path (Split-Path -Parent $generatorDir) 'CUI_PJ102AH.IntLib'
}
$scriptId = [Guid]::NewGuid().ToString('N')
$scriptUnit = "VerifyPJ102AH_$scriptId"
$scriptRunDir = Join-Path ([System.IO.Path]::GetTempPath()) ("treebeard-pj102ah-verifier-$scriptId")
New-Item -ItemType Directory -Force -Path $scriptRunDir | Out-Null
$scriptSource = Join-Path $scriptRunDir "$scriptUnit.pas"
$project = Join-Path $scriptRunDir "$scriptUnit.PrjScr"
Copy-Item -LiteralPath (Join-Path $generatorDir 'VerifyPJ102AH.pas') -Destination $scriptSource
$projectText = (Get-Content -LiteralPath (Join-Path $generatorDir 'VerifyPJ102AH.PrjScr') -Raw).Replace('VerifyPJ102AH.pas', "$scriptUnit.pas")
[System.IO.File]::WriteAllText($project, $projectText, (New-Object System.Text.UTF8Encoding $false))

$bridgeDir = 'C:\Users\Public\treebeard_altium_pj102ah'
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

$launcher = Join-Path ([System.IO.Path]::GetTempPath()) 'treebeard_pj102ah_altium_verify.bat'
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
# Footprint link, read from the SchLib the IntLib is compiled from ('*' = default).
$expectedImpls = 'CUI_PJ-102AH_DC_JACK_RA*'
if ($result.sch_impls -ne $expectedImpls) {
    throw "Symbol footprint link is wrong: got '$($result.sch_impls)', expected '$expectedImpls'"
}
# Expected holes typed from CUI datasheet PJ-102AH 10/30/2019 "PCB LAYOUT TOP VIEW", NOT from the
# generator: pins 1 and 2 on the jack axis 3.0 + 3.0 = 6.0 apart, pin 3 4.7 to the left of that
# axis (mating face up) at the midpoint. Origin = pin 1, mating face toward +Y. All three holes
# plated, round 1.60 (CUI recommends 1.00 x 1.60; JLCPCB plated slots need length >= 2 x width).
$f = 'CUI_PJ-102AH_DC_JACK_RA'
$expectedPads = @(
    "$f|1|0.00|0.00|1.60|P", "$f|2|0.00|6.00|1.60|P", "$f|3|-4.70|3.00|1.60|P", "$f|cutouts|0"
)
$got = @($result.pad_report -split ';')
$diff = Compare-Object ($got | Sort-Object) ($expectedPads | Sort-Object)
if ($diff) {
    $diff | Format-Table -AutoSize | Out-Host
    throw "Footprint pad/hole check failed (<= only in library, => only expected)."
}
Write-Host 'PJ-102AH symbol links its footprint; every hole matches the CUI 10/30/2019 PCB layout.' -ForegroundColor Green
$result
