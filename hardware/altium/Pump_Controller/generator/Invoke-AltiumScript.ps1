[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Source,
    [string]$Request = '',
    [string]$OutputDir,
    [int]$TimeoutSeconds = 90
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $OutputDir) { $OutputDir = Join-Path $root 'staging' }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
$run = Join-Path ([IO.Path]::GetTempPath()) ('treebeard-pump-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
$unit = 'Pump_' + [guid]::NewGuid().ToString('N')
$requestFile = Join-Path $run 'request.txt'
$responseFile = Join-Path $run 'response.json'
$sourceFile = Join-Path $run ($unit + '.pas')
$projectFile = Join-Path $run ($unit + '.PrjScr')
$utf8 = New-Object System.Text.UTF8Encoding $false
[IO.File]::WriteAllText($requestFile, $Request, $utf8)
$code = (Get-Content -LiteralPath $Source -Raw).Replace('__REQUEST__', $requestFile).Replace('__RESPONSE__', $responseFile).Replace('__OUTPUT__', $OutputDir + '\')
[IO.File]::WriteAllText($sourceFile, $code, $utf8)
[IO.File]::WriteAllText($projectFile, "[Design]`r`nVersion=1.0`r`n[Document1]`r`nDocumentPath=$unit.pas`r`n", $utf8)
$launcher = Join-Path $run 'launch.bat'
$command = '"C:\Program Files\Altium\AD25\X2.exe" -RScriptingSystem:RunScript(ProjectName="{0}"^|ProcName="{1}>Run")' -f $projectFile, $unit
[IO.File]::WriteAllText($launcher, $command, [Text.Encoding]::ASCII)
Start-Process -FilePath $launcher -WindowStyle Hidden
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while (-not (Test-Path -LiteralPath $responseFile) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
if (-not (Test-Path -LiteralPath $responseFile)) { throw "Altium timed out. Run directory: $run" }
$result = Get-Content -LiteralPath $responseFile -Raw
$result | ConvertFrom-Json | Out-Null
$report = Join-Path $OutputDir (([IO.Path]::GetFileNameWithoutExtension($Source)) + '-report.json')
[IO.File]::WriteAllText($report, $result, $utf8)
$result
