[CmdletBinding()]
param(
    [string]$IntLib,
    [string]$AltiumExe = 'C:\Program Files\Altium\AD25\X2.exe',
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$generatorDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $IntLib) {
    $IntLib = Join-Path (Split-Path -Parent $generatorDir) 'SC-32S_C97604.IntLib'
}
$scriptId = [Guid]::NewGuid().ToString('N')
$scriptUnit = "VerifySC32SC97604_$scriptId"
$scriptRunDir = Join-Path ([System.IO.Path]::GetTempPath()) ("treebeard-sc32s-verifier-$scriptId")
New-Item -ItemType Directory -Force -Path $scriptRunDir | Out-Null
$scriptSource = Join-Path $scriptRunDir "$scriptUnit.pas"
$project = Join-Path $scriptRunDir "$scriptUnit.PrjScr"
Copy-Item -LiteralPath (Join-Path $generatorDir 'VerifySC32SC97604.pas') -Destination $scriptSource
$projectText = (Get-Content -LiteralPath (Join-Path $generatorDir 'VerifySC32SC97604.PrjScr') -Raw).Replace('VerifySC32SC97604.pas', "$scriptUnit.pas")
[System.IO.File]::WriteAllText($project, $projectText, (New-Object System.Text.UTF8Encoding $false))

$bridgeDir = 'C:\Users\Public\treebeard_altium_sc32s'
$requestFile = Join-Path $bridgeDir 'verify_request.txt'
$responseFile = Join-Path $bridgeDir 'verify_response.json'
New-Item -ItemType Directory -Force -Path $bridgeDir | Out-Null
[System.IO.File]::WriteAllText($requestFile, (Resolve-Path -LiteralPath $IntLib).Path, (New-Object System.Text.UTF8Encoding $false))
if (Test-Path -LiteralPath $responseFile) { Remove-Item -LiteralPath $responseFile -Force }

$launcher = Join-Path ([System.IO.Path]::GetTempPath()) 'treebeard_sc32s_altium_verify.bat'
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
if ($result.model_count -ne 1 -or $result.model_name -ne 'SC-32S_SMD3215_2P') {
    throw "Compiled model link is invalid: count=$($result.model_count), name=$($result.model_name)"
}
Write-Host 'Altium resolved the SC-32S symbol and its embedded SC-32S_SMD3215_2P footprint.' -ForegroundColor Green
$result
