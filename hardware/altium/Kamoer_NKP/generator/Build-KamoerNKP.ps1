[CmdletBinding()]
param(
    [string]$AltiumExe = 'C:\Program Files\Altium\AD25\X2.exe',
    [int]$TimeoutSeconds = 240
)

$ErrorActionPreference = 'Stop'

$generatorDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$libraryDir = Split-Path -Parent $generatorDir
$scriptId = [Guid]::NewGuid().ToString('N')
$scriptUnit = "GenerateKamoerNKP_$scriptId"
$scriptRunDir = Join-Path ([System.IO.Path]::GetTempPath()) ("treebeard-nkp-generator-$scriptId")
New-Item -ItemType Directory -Force -Path $scriptRunDir | Out-Null
$scriptSource = Join-Path $scriptRunDir "$scriptUnit.pas"
$project = Join-Path $scriptRunDir "$scriptUnit.PrjScr"
Copy-Item -LiteralPath (Join-Path $generatorDir 'GenerateKamoerNKP.pas') -Destination $scriptSource
$projectText = (Get-Content -LiteralPath (Join-Path $generatorDir 'GenerateKamoerNKP.PrjScr') -Raw).Replace('GenerateKamoerNKP.pas', "$scriptUnit.pas")
[System.IO.File]::WriteAllText($project, $projectText, (New-Object System.Text.UTF8Encoding $false))

$bridgeDir = 'C:\Users\Public\treebeard_altium_nkp'
$requestFile = Join-Path $bridgeDir 'generate_request.txt'
$responseFile = Join-Path $bridgeDir 'generate_response.json'
$buildDir = Join-Path ([System.IO.Path]::GetTempPath()) ("treebeard-nkp-altium-build-$scriptId")
$baseName = 'Kamoer_NKP'
$outputs = @('.IntLib', '.SchLib', '.PcbLib', '.LibPkg') | ForEach-Object { Join-Path $buildDir ($baseName + $_) }

if (-not (Test-Path -LiteralPath $AltiumExe)) { throw "Altium not found: $AltiumExe" }
New-Item -ItemType Directory -Force -Path $bridgeDir | Out-Null
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
foreach ($path in @($requestFile, $responseFile) + $outputs) {
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
}
[System.IO.File]::WriteAllText($requestFile, $buildDir, (New-Object System.Text.UTF8Encoding $false))

$launcher = Join-Path ([System.IO.Path]::GetTempPath()) 'treebeard_nkp_altium_launch.bat'
$command = '"{0}" -RScriptingSystem:RunScript(ProjectName="{1}"^|ProcName="{2}>Run")' -f $AltiumExe, $project, $scriptUnit
[System.IO.File]::WriteAllText($launcher, $command, [System.Text.Encoding]::ASCII)

Write-Host 'Generating and compiling the Kamoer NKP pump Altium library...'
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

foreach ($path in $outputs) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Expected output missing: $path" }
}
foreach ($path in $outputs | Where-Object { [IO.Path]::GetExtension($_) -in '.IntLib', '.SchLib', '.PcbLib' }) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 8 -or $bytes[0] -ne 0xD0 -or $bytes[1] -ne 0xCF) {
        throw "Output is not an Altium OLE compound document: $path"
    }
}

foreach ($path in $outputs) {
    Copy-Item -LiteralPath $path -Destination (Join-Path $libraryDir ([IO.Path]::GetFileName($path))) -Force
}

& (Join-Path $generatorDir 'Test-KamoerNKP.ps1') `
    -IntLib (Join-Path $libraryDir "$baseName.IntLib") `
    -AltiumExe $AltiumExe `
    -TimeoutSeconds $TimeoutSeconds | Out-Host

Write-Host "Library generated and verified in $libraryDir" -ForegroundColor Green

# Put the ONE repo copy into the Components panel (replacing any same-named install), and close
# the generator/verifier script projects this run left open in Altium.
& (Join-Path (Split-Path -Parent (Split-Path -Parent $generatorDir)) 'tools\Install-AltiumLibrary.ps1') `
    -IntLib (Join-Path $libraryDir "$baseName.IntLib") -AltiumExe $AltiumExe | Out-Host

# This run's temp folders; their Altium documents are closed by now.
foreach ($dir in @($scriptRunDir, $buildDir)) {
    if ($dir -and $dir.StartsWith([IO.Path]::GetTempPath()) -and (Test-Path -LiteralPath $dir)) {
        Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
Get-Item -LiteralPath (Join-Path $libraryDir "$baseName.IntLib") | Select-Object FullName, Length, LastWriteTime
