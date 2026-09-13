# Shared Python resolution for the MSV3 PowerShell wrappers.
#
# build_msv3_release.ps1 and arduino_msv3_probe.ps1 both need an interpreter to reach
# scripts/msv3_fleet.py, which is the SINGLE reader of MSV3/fleet.json. Two copies of this
# resolution would be two places for "which python" to drift apart, and the one that drifted
# would be the one enforcing a safety gate.
#
# Dot-source this file, then call Resolve-Msv3Python.

function Resolve-Msv3Python {
  param(
    # An explicit interpreter from the caller's -Python parameter, if it has one.
    [string]$Explicit,
    # MSV3/ -- the portable interpreter the DFU tooling installs lives under its .arduino-build.
    [Parameter(Mandatory = $true)][string]$Msv3Root
  )

  if (![string]::IsNullOrWhiteSpace($Explicit)) { return $Explicit }
  $portable = Join-Path $Msv3Root ".arduino-build\python-portable\runtime\python.exe"
  if (Test-Path -LiteralPath $portable) { return $portable }
  return "python"
}

# Prove the interpreter actually runs before a caller relies on it for a safety decision.
#
# `python` on Windows is frequently the App Execution Alias stub, which exits 9009 and opens the
# Store rather than running anything. A gate that treats "could not run the checker" as "the check
# passed" is worse than no gate, so this returns $false and callers must fail closed on it.
function Test-Msv3Python {
  param([Parameter(Mandatory = $true)][string]$PythonExe)

  try {
    $null = & $PythonExe -c "import sys; sys.exit(0)" 2>&1
    return ($LASTEXITCODE -eq 0)
  } catch {
    return $false
  }
}
