param(
  [int]$Port = 8765,
  [string]$DataDir,
  [switch]$NoBrowser
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$portable = Join-Path $repoRoot 'MSV3\.arduino-build\python-portable\runtime\python.exe'
$app = Join-Path $PSScriptRoot 'msv4_lab.py'

if (Test-Path -LiteralPath $portable) {
  $pythonCommand = $portable
  $pythonPrefix = @()
} elseif (Get-Command py.exe -ErrorAction SilentlyContinue) {
  $pythonCommand = 'py.exe'
  $pythonPrefix = @('-3')
} elseif (Get-Command python.exe -ErrorAction SilentlyContinue) {
  $pythonCommand = 'python.exe'
  $pythonPrefix = @()
} else {
  throw 'Python 3 is required. Install it or copy the project portable Python runtime.'
}

$arguments = @($app, '--port', [string]$Port)
if ($DataDir) { $arguments += @('--data-dir', $DataDir) }
if ($NoBrowser) { $arguments += '--no-browser' }

Write-Host "MSV4 Lab uses $pythonCommand. BLE scanning and connection require the bleak Python package."
& $pythonCommand @pythonPrefix @arguments
exit $LASTEXITCODE
