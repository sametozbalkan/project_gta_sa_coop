<#
.SYNOPSIS
    Publishes CoopTunnel as a self-contained single exe into dist\CoopTunnel (+ .zip), together with the
    .bat launchers and USAGE.txt from tunnel\package. scripts\package.ps1 takes it from there.
#>
$ErrorActionPreference = 'Stop'
$root   = Split-Path -Parent $PSScriptRoot
$tunnel = Join-Path $root 'tunnel'
$out    = Join-Path $root 'dist\CoopTunnel'
$zip    = "$out.zip"

if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) { throw 'dotnet not found; CoopTunnel needs the .NET 10 SDK' }
# no usage data from the SDK (no third-party ties, see CLAUDE.md)
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'

foreach ($old in $out, $zip) {
    if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Recurse -Force }
}
Push-Location $tunnel
try {
    & dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:EnableCompressionInSingleFile=true -p:DebugType=none -o $out
    if ($LASTEXITCODE) { throw "dotnet publish failed (exit $LASTEXITCODE)" }
}
finally { Pop-Location }
# Install.ps1 refuses a package without these two
foreach ($f in 'CoopTunnel.exe', 'steam_api64.dll') {
    if (-not (Test-Path -LiteralPath (Join-Path $out $f))) { throw "$f is missing in $out after the publish" }
}

# cmd.exe misreads batch files with LF line endings; the BOM keeps any non-ASCII text readable in Notepad
$utf8Bom = New-Object System.Text.UTF8Encoding $true
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $tunnel 'package') -File) {
    $target = Join-Path $out $file.Name
    switch ($file.Extension) {
        '.bat'  { [IO.File]::WriteAllText($target, ([IO.File]::ReadAllText($file.FullName) -replace "`r?`n", "`r`n"), [Text.Encoding]::ASCII) }
        '.txt'  { [IO.File]::WriteAllText($target, ([IO.File]::ReadAllText($file.FullName) -replace "`r?`n", "`r`n"), $utf8Bom) }
        default { Copy-Item -LiteralPath $file.FullName -Destination $target }
    }
}

Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip -Force
Get-ChildItem -LiteralPath $out -File | ForEach-Object { '{0,-25} {1,12:N0}' -f $_.Name, $_.Length }
'zip: {0} ({1:N1} MB)' -f $zip, ((Get-Item -LiteralPath $zip).Length / 1MB)
