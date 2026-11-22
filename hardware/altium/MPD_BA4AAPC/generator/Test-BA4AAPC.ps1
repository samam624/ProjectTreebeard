[CmdletBinding()]
param(
    [string]$IntLib,
    [string]$AltiumExe = 'C:\Program Files\Altium\AD25\X2.exe',
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$generatorDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $IntLib) {
    $IntLib = Join-Path (Split-Path -Parent $generatorDir) 'MPD_BA4AAPC.IntLib'
}
$scriptId = [Guid]::NewGuid().ToString('N')
$scriptUnit = "VerifyBA4AAPC_$scriptId"
$scriptRunDir = Join-Path ([System.IO.Path]::GetTempPath()) ("treebeard-ba4aapc-verifier-$scriptId")
New-Item -ItemType Directory -Force -Path $scriptRunDir | Out-Null
$scriptSource = Join-Path $scriptRunDir "$scriptUnit.pas"
$project = Join-Path $scriptRunDir "$scriptUnit.PrjScr"
Copy-Item -LiteralPath (Join-Path $generatorDir 'VerifyBA4AAPC.pas') -Destination $scriptSource
$projectText = (Get-Content -LiteralPath (Join-Path $generatorDir 'VerifyBA4AAPC.PrjScr') -Raw).Replace('VerifyBA4AAPC.pas', "$scriptUnit.pas")
[System.IO.File]::WriteAllText($project, $projectText, (New-Object System.Text.UTF8Encoding $false))

$bridgeDir = 'C:\Users\Public\treebeard_altium_ba4aapc'
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

$launcher = Join-Path ([System.IO.Path]::GetTempPath()) 'treebeard_ba4aapc_altium_verify.bat'
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
$expectedImpls = 'MPD_BA4AAPC_4AA_PC*'
if ($result.sch_impls -ne $expectedImpls) {
    throw "Symbol footprint link is wrong: got '$($result.sch_impls)', expected '$expectedImpls'"
}
# Expected holes/pads typed from MPD drawing BA4AAPC rev H "PCB LAYOUT", NOT from the generator:
#   mounting holes on the centre line 54.61 apart; pins 26.82 to the right of that line,
#   6.17 (pin 1, +) and 18.87 (pin 2, -) above the lower hole. Holes enlarged on purpose:
#   pins 1.10 (drawing 1.02), mounting 2.40 (drawing 2.03); see GenerateBA4AAPC.pas.
#   Pin y = -27.305+6.17 = -21.135 and -27.305+18.87 = -8.435; the report prints 2 decimals.
$f = 'MPD_BA4AAPC_4AA_PC'
$expectedPads = @(
    "$f||0.00|27.31|2.40|N", "$f||0.00|-27.31|2.40|N",
    "$f|1|26.82|-21.13|1.10|P", "$f|2|26.82|-8.43|1.10|P", "$f|cutouts|0"
)
$got = @($result.pad_report -split ';')
$diff = Compare-Object ($got | Sort-Object) ($expectedPads | Sort-Object)
if ($diff) {
    $diff | Format-Table -AutoSize | Out-Host
    throw "Footprint pad/hole check failed (<= only in library, => only expected)."
}
Write-Host 'BA4AAPC symbol links its footprint; every pad and hole matches MPD rev H.' -ForegroundColor Green
$result
