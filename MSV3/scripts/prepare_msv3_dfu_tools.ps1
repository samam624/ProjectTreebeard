param(
  [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$msv3Root = (Resolve-Path (Join-Path $scriptDir "..")).Path
$deps = Join-Path $msv3Root ".arduino-build\dfu-pydeps"
$nrfutil = Join-Path $deps "bin\adafruit-nrfutil.exe"

if (!(Test-Path -LiteralPath $nrfutil)) {
  New-Item -ItemType Directory -Force -Path $deps | Out-Null
  & $Python -m pip install --target $deps "adafruit-nrfutil==0.5.3.post16" "ecdsa==0.19.2"
  if ($LASTEXITCODE -ne 0) { throw "Could not install pinned DFU tooling" }
}

if (!(Test-Path -LiteralPath (Join-Path $deps "intelhex\__init__.py"))) {
  & $Python -m pip install --target $deps "intelhex==2.3.0"
  if ($LASTEXITCODE -ne 0) { throw "Could not install pinned Intel HEX tooling" }
}

# Adafruit 0.5.3.post16 is the format-compatible tool for this legacy bootloader, but its
# signing.py still contains three Python-2 bytes/text operations. Patch only the exact known
# expressions and fail closed if the installed source has an unfamiliar shape.
$signingPy = Join-Path $deps "nordicsemi\dfu\signing.py"
if (!(Test-Path -LiteralPath $signingPy)) { throw "Missing $signingPy" }
$text = [IO.File]::ReadAllText($signingPy)
$replacements = @(
  @('with open(filename, "w") as sk_file:', 'with open(filename, "wb") as sk_file:'),
  @('with open(filename, "r") as sk_file:', 'with open(filename, "rb") as sk_file:'),
  @('        sk_hex = "".join(c.encode(''hex'') for c in self.sk.to_string())', ''),
  @('binascii.hexlify(vk.to_string())', 'binascii.hexlify(vk.to_string()).decode("ascii")')
)
foreach ($pair in $replacements) {
  $old = $pair[0]
  $new = $pair[1]
  if ($text.Contains($old)) {
    $text = $text.Replace($old, $new)
  } elseif (!$text.Contains($new) -and $new -ne '') {
    throw "Pinned nrfutil compatibility patch no longer matches: $old"
  }
}
while ($text.Contains('.decode("ascii").decode("ascii")')) {
  $text = $text.Replace('.decode("ascii").decode("ascii")', '.decode("ascii")')
}
[IO.File]::WriteAllText($signingPy, $text, [Text.UTF8Encoding]::new($false))

$manifestPy = Join-Path $deps "nordicsemi\dfu\manifest.py"
$manifestText = [IO.File]::ReadAllText($manifestPy)
$manifestText = $manifestText.Replace(
  'binascii.hexlify(field)',
  'binascii.hexlify(field).decode("ascii")'
)
while ($manifestText.Contains('.decode("ascii").decode("ascii")')) {
  $manifestText = $manifestText.Replace('.decode("ascii").decode("ascii")', '.decode("ascii")')
}
[IO.File]::WriteAllText($manifestPy, $manifestText, [Text.UTF8Encoding]::new($false))

$env:PYTHONPATH = $deps
& $nrfutil version
if ($LASTEXITCODE -ne 0) { throw "Pinned adafruit-nrfutil did not start" }
Write-Output $nrfutil
