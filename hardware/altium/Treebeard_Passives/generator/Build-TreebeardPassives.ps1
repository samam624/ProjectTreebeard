[CmdletBinding()]
param(
    [string]$AltiumExe = 'C:\Program Files\Altium\AD25\X2.exe',
    [int]$TimeoutSeconds = 240
)

$ErrorActionPreference = 'Stop'

$generatorDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$libraryDir = Split-Path -Parent $generatorDir
$scriptId = [Guid]::NewGuid().ToString('N')
$scriptUnit = "GenerateTreebeardPassives_$scriptId"
$scriptRunDir = Join-Path ([System.IO.Path]::GetTempPath()) ("treebeard-passives-generator-$scriptId")
New-Item -ItemType Directory -Force -Path $scriptRunDir | Out-Null
$scriptSource = Join-Path $scriptRunDir "$scriptUnit.pas"
$project = Join-Path $scriptRunDir "$scriptUnit.PrjScr"
Copy-Item -LiteralPath (Join-Path $generatorDir 'GenerateTreebeardPassives.pas') -Destination $scriptSource
$projectText = (Get-Content -LiteralPath (Join-Path $generatorDir 'GenerateTreebeardPassives.PrjScr') -Raw).Replace('GenerateTreebeardPassives.pas', "$scriptUnit.pas")
[System.IO.File]::WriteAllText($project, $projectText, (New-Object System.Text.UTF8Encoding $false))
$bridgeDir = 'C:\Users\Public\treebeard_altium_passives'
$requestFile = Join-Path $bridgeDir 'generate_request.txt'
$responseFile = Join-Path $bridgeDir 'generate_response.json'
$buildDir = Join-Path ([System.IO.Path]::GetTempPath()) 'treebeard-passives-altium-build'
$pcbLib = Join-Path $buildDir 'Treebeard_Passives.PcbLib'
$schLib = Join-Path $buildDir 'Treebeard_Passives.SchLib'

if (-not (Test-Path -LiteralPath $AltiumExe)) { throw "Altium not found: $AltiumExe" }
if (-not (Test-Path -LiteralPath $project)) { throw "Staged script project not found: $project" }

New-Item -ItemType Directory -Force -Path $bridgeDir | Out-Null
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

foreach ($path in @($requestFile, $responseFile, $pcbLib, $schLib)) {
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
}

[System.IO.File]::WriteAllText(
    $requestFile,
    $buildDir,
    (New-Object System.Text.UTF8Encoding $false)
)

$launcher = Join-Path ([System.IO.Path]::GetTempPath()) 'treebeard_passives_altium_launch.bat'
$command = '"{0}" -RScriptingSystem:RunScript(ProjectName="{1}"^|ProcName="{2}>Run")' -f $AltiumExe, $project, $scriptUnit
[System.IO.File]::WriteAllText($launcher, $command, [System.Text.Encoding]::ASCII)

Write-Host 'Generating the passive libraries with Altium Designer...'
Start-Process -FilePath $launcher -WindowStyle Hidden

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while (-not (Test-Path -LiteralPath $responseFile) -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
}
if (-not (Test-Path -LiteralPath $responseFile)) {
    throw "Timed out after $TimeoutSeconds seconds. Check Altium for a script error dialog."
}

$result = Get-Content -LiteralPath $responseFile -Raw | ConvertFrom-Json
if ($result.status -ne 'ok') { throw "Altium generation failed: $($result.detail)" }

foreach ($path in @($pcbLib, $schLib)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Expected output missing: $path" }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 8 -or $bytes[0] -ne 0xD0 -or $bytes[1] -ne 0xCF) {
        throw "Output is not an Altium OLE compound document: $path"
    }
}

& (Join-Path $generatorDir 'Test-TreebeardPassives.ps1') `
    -PcbLib $pcbLib `
    -SchLib $schLib `
    -AltiumExe $AltiumExe `
    -TimeoutSeconds $TimeoutSeconds | Out-Host

Copy-Item -LiteralPath $pcbLib -Destination (Join-Path $libraryDir 'Treebeard_Passives.PcbLib') -Force
Copy-Item -LiteralPath $schLib -Destination (Join-Path $libraryDir 'Treebeard_Passives.SchLib') -Force

Write-Host "Generated and installed libraries in $libraryDir" -ForegroundColor Green
Get-Item -LiteralPath (Join-Path $libraryDir 'Treebeard_Passives.PcbLib'), (Join-Path $libraryDir 'Treebeard_Passives.SchLib') |
    Select-Object Name, Length, LastWriteTime
