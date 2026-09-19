[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PcbLib,
    [Parameter(Mandatory = $true)][string]$SchLib,
    [string]$AltiumExe = 'C:\Program Files\Altium\AD25\X2.exe',
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$generatorDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptId = [Guid]::NewGuid().ToString('N')
$scriptUnit = "VerifyTreebeardPassives_$scriptId"
$scriptRunDir = Join-Path ([System.IO.Path]::GetTempPath()) ("treebeard-passives-verifier-$scriptId")
New-Item -ItemType Directory -Force -Path $scriptRunDir | Out-Null
$scriptSource = Join-Path $scriptRunDir "$scriptUnit.pas"
$project = Join-Path $scriptRunDir "$scriptUnit.PrjScr"
Copy-Item -LiteralPath (Join-Path $generatorDir 'VerifyTreebeardPassives.pas') -Destination $scriptSource
$projectText = (Get-Content -LiteralPath (Join-Path $generatorDir 'VerifyTreebeardPassives.PrjScr') -Raw).Replace('VerifyTreebeardPassives.pas', "$scriptUnit.pas")
[System.IO.File]::WriteAllText($project, $projectText, (New-Object System.Text.UTF8Encoding $false))
$bridgeDir = 'C:\Users\Public\treebeard_altium_passives'
$requestFile = Join-Path $bridgeDir 'verify_request.txt'
$responseFile = Join-Path $bridgeDir 'verify_response.json'

# Altium returns an already-open document when the same path is requested again,
# even if that file has since been replaced on disk. Verify uniquely named copies
# so every build is checked from its newly written bytes.
$verificationPcbLib = Join-Path $scriptRunDir "Treebeard_Passives_$scriptId.PcbLib"
$verificationSchLib = Join-Path $scriptRunDir "Treebeard_Passives_$scriptId.SchLib"
Copy-Item -LiteralPath (Resolve-Path -LiteralPath $PcbLib).Path -Destination $verificationPcbLib
Copy-Item -LiteralPath (Resolve-Path -LiteralPath $SchLib).Path -Destination $verificationSchLib
$paths = @($verificationPcbLib, $verificationSchLib)
New-Item -ItemType Directory -Force -Path $bridgeDir | Out-Null
[System.IO.File]::WriteAllLines($requestFile, [string[]]$paths, (New-Object System.Text.UTF8Encoding $false))
if (Test-Path -LiteralPath $responseFile) { Remove-Item -LiteralPath $responseFile -Force }

$launcher = Join-Path ([System.IO.Path]::GetTempPath()) 'treebeard_passives_altium_verify.bat'
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

$results = Get-Content -LiteralPath $responseFile -Raw | ConvertFrom-Json
if ($results.Count -ne 2 -or @($results | Where-Object { -not $_.opened }).Count) {
    throw 'Altium did not reopen both generated libraries.'
}

$pcb = $results | Where-Object kind -eq 'PCBLIB'
$sch = $results | Where-Object kind -eq 'SCHLIB'
$expectedPcb = @('TB_R_0805_2012M', 'TB_C_0805_2012M')
$expectedSch = @(
    'R_0805_1PCT_0P125W_150V',
    'C_0805_X5R_16V_10PCT',
    'C_0805_X5R_25V_10PCT',
    'C_0805_X7R_50V_10PCT',
    'C_0805_C0G_50V_5PCT'
)

$gotPcb = @($pcb.components.name)
$gotSch = @($sch.components.name)
if (@($expectedPcb | Where-Object { $gotPcb -notcontains $_ }).Count -or $gotPcb.Count -ne $expectedPcb.Count) {
    throw "Unexpected PcbLib components: $($gotPcb -join ', ')"
}
if (@($expectedSch | Where-Object { $gotSch -notcontains $_ }).Count -or $gotSch.Count -ne $expectedSch.Count) {
    throw "Unexpected SchLib components: $($gotSch -join ', ')"
}

foreach ($component in @($pcb.components)) {
    if ($component.pads -ne 2 -or $component.tracks -ne 10 -or $component.bodies -ne 1) {
        throw "Bad footprint primitive counts for $($component.name): pads=$($component.pads), tracks=$($component.tracks), bodies=$($component.bodies)"
    }
}
foreach ($component in @($sch.components)) {
    $minimumParameters = if ($component.name -like 'R_*') { 15 } else { 14 }
    if ($component.pins -ne 2 -or $component.implementations -ne 1 -or $component.parameters -lt $minimumParameters) {
        throw "Bad symbol primitive counts for $($component.name): pins=$($component.pins), parameters=$($component.parameters), implementations=$($component.implementations)"
    }
    $expectedLines = if ($component.name -like 'R_*') { 8 } else { 2 }
    if ($component.lines -ne $expectedLines -or $component.rectangles -ne 0) {
        throw "Bad symbol geometry for $($component.name): lines=$($component.lines), rectangles=$($component.rectangles)"
    }
    $requiredValueParameter = if ($component.name -like 'R_*') { 'Resistance' } else { 'Capacitance' }
    if ($component.comment -notlike "*$requiredValueParameter*") {
        throw "Generated Comment for $($component.name) does not reference $requiredValueParameter`: $($component.comment)"
    }
    # 2026-09-19: a unitless resistance ("470", "10M") fuzzy-matched to nothing or to a milliohm
    # shunt on LCSC. The resistor Comment must carry an explicit ASCII unit.
    if ($component.name -like 'R_*' -and $component.comment -notlike "*' ohm *") {
        throw "Resistor Comment for $($component.name) lacks the ' ohm ' unit: $($component.comment)"
    }
}

Write-Host 'Altium reopened both libraries and resolved every expected passive, footprint link, and primitive count.' -ForegroundColor Green
$results
