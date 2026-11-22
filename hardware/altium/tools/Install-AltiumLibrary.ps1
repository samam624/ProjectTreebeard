[CmdletBinding()]
param(
    # The .IntLib to put into the running Altium's Components panel (Installed libraries).
    [Parameter(Mandatory = $true)][string]$IntLib,
    [string]$AltiumExe = 'C:\Program Files\Altium\AD25\X2.exe',
    [int]$TimeoutSeconds = 120
)

# Installs one integrated library into Altium, replacing any installed copy with the same file
# name, and closes leftover %TEMP%\treebeard-* script projects that generator runs leave open.
# This is how parts made for the maintainer reach the Components panel. Do not have a generator
# call CreateIntegratedLibrary(..., True) instead: that installs every temp build (2026-11-11).

$ErrorActionPreference = 'Stop'
$toolDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$intLibPath = (Resolve-Path -LiteralPath $IntLib).Path
if ([IO.Path]::GetExtension($intLibPath) -ne '.IntLib') { throw "Not an .IntLib: $intLibPath" }
if (-not (Test-Path -LiteralPath $AltiumExe)) { throw "Altium not found: $AltiumExe" }

$id = [Guid]::NewGuid().ToString('N')
$unit = "InstallAltiumLibrary_$id"
$runDir = Join-Path ([IO.Path]::GetTempPath()) "treebeard-install-$id"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
Copy-Item -LiteralPath (Join-Path $toolDir 'InstallAltiumLibrary.pas') -Destination (Join-Path $runDir "$unit.pas")
$prj = Join-Path $runDir "$unit.PrjScr"
$prjText = "[Design]`r`nVersion=1.0`r`nHierarchyMode=0`r`nOutputPath=`r`nUserID=0xFFFFFFFF`r`n`r`n[Document1]`r`nDocumentPath=$unit.pas`r`n"
[IO.File]::WriteAllText($prj, $prjText, (New-Object Text.UTF8Encoding $false))

$bridge = 'C:\Users\Public\treebeard_altium_install'
New-Item -ItemType Directory -Force -Path $bridge | Out-Null
$request = Join-Path $bridge 'request.txt'
$response = Join-Path $bridge 'response.txt'
if (Test-Path -LiteralPath $response) { Remove-Item -LiteralPath $response -Force }
[IO.File]::WriteAllText($request, $intLibPath, (New-Object Text.UTF8Encoding $false))

$bat = Join-Path $runDir 'launch.bat'
$cmd = '"{0}" -RScriptingSystem:RunScript(ProjectName="{1}"^|ProcName="{2}>Run")' -f $AltiumExe, $prj, $unit
[IO.File]::WriteAllText($bat, $cmd, [Text.Encoding]::ASCII)
Start-Process -FilePath $bat -WindowStyle Hidden

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while (-not (Test-Path -LiteralPath $response) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
if (-not (Test-Path -LiteralPath $response)) { throw "Timed out waiting for Altium. Check it for a dialog." }
$lines = Get-Content -LiteralPath $response
$lines | Out-Host
if (-not ($lines -contains "INSTALLED $intLibPath")) { throw "Altium did not report installing $intLibPath" }
$after = $lines[([Array]::IndexOf($lines, '--- NOW INSTALLED') + 1)..($lines.Count - 1)]
if (-not ($after -contains $intLibPath)) { throw "$intLibPath is not in Altium's installed list afterwards" }
Write-Host "Installed in the Components panel: $intLibPath" -ForegroundColor Green
# Verifier temp folders from earlier steps of the same build, plus this installer's own folder.
Get-ChildItem -LiteralPath ([IO.Path]::GetTempPath()) -Directory -Filter 'treebeard-*' -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^treebeard-(install|[a-z0-9]+-verifier|[a-z0-9]+-generator)-[0-9a-f]{32}$' } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue }
