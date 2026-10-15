<#
.SYNOPSIS
    Builds dist\CoopAndreas-Kurulum (+ .zip) for a friend's PC: the installer, our CoopAndreas build,
    CoopTunnel, the official xdelta3 and the GPL source of our CoopAndreas changes.
.DESCRIPTION
    Run scripts\build.ps1 and publish the tunnel (see CLAUDE.md) first. The patches archive itself is not
    bundled; Kur.ps1 downloads it and checks its SHA-256.
#>
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$repo  = Join-Path $root 'CoopAndreas'
$build = Join-Path $repo 'build\windows\x86\release'
$out   = Join-Path $root 'dist\CoopAndreas-Kurulum'

foreach ($f in 'CoopAndreasSA.dll', 'proxy.dll', 'server.exe') {
    if (-not (Test-Path -LiteralPath (Join-Path $build $f))) { throw "$f is missing in $build; run scripts\build.ps1 first" }
}
if (-not (Test-Path -LiteralPath (Join-Path $root 'dist\CoopTunnel\CoopTunnel.exe'))) { throw 'dist\CoopTunnel is missing; publish the tunnel first' }
if (-not (Test-Path -LiteralPath (Join-Path $root '.tools\xdelta3\xdelta3.exe'))) { throw '.tools\xdelta3\xdelta3.exe is missing' }

if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
foreach ($dir in 'files\CoopAndreas', 'files\server', 'tools', 'CoopTunnel', 'KAYNAK\CoopTunnel') {
    New-Item -ItemType Directory -Force -Path (Join-Path $out $dir) | Out-Null
}

# Windows PowerShell 5.1 reads a .ps1 without BOM as ANSI, which garbles the Turkish text
$utf8Bom = New-Object System.Text.UTF8Encoding $true
foreach ($name in 'Kur.ps1', 'OKU-BENI.txt') {
    $text = [IO.File]::ReadAllText((Join-Path $root "installer\$name")) -replace "`r?`n", "`r`n"
    [IO.File]::WriteAllText((Join-Path $out $name), $text, $utf8Bom)
}
$bat = [IO.File]::ReadAllText((Join-Path $root 'installer\Kur.bat')) -replace "`r?`n", "`r`n"
[IO.File]::WriteAllText((Join-Path $out 'Kur.bat'), $bat, [Text.Encoding]::ASCII)

Copy-Item -LiteralPath (Join-Path $build 'CoopAndreasSA.dll') -Destination (Join-Path $out 'files')
Copy-Item -LiteralPath (Join-Path $build 'proxy.dll') -Destination (Join-Path $out 'files\eax.dll')
Copy-Item -LiteralPath (Join-Path $build 'server.exe') -Destination (Join-Path $out 'files\server')
Copy-Item -LiteralPath (Join-Path $repo 'scm\main.scm'), (Join-Path $repo 'scm\script.img') -Destination (Join-Path $out 'files\CoopAndreas')
Copy-Item -Path (Join-Path $root '.tools\xdelta3\*') -Destination (Join-Path $out 'tools')
Copy-Item -Path (Join-Path $root 'dist\CoopTunnel\*') -Destination (Join-Path $out 'CoopTunnel')

# GPL-3.0 source of the shipped build: upstream commit + our edits, new files included
# (diffed against /dev/null so the repo's index isn't touched)
$source = Join-Path $out 'KAYNAK'
$base = (git -C $repo rev-parse HEAD).Trim()
$parts = @()
$tracked = Join-Path $env:TEMP 'coop-tracked.patch'
git -C $repo diff HEAD --output=$tracked
$parts += $tracked
$i = 0
foreach ($new in (git -C $repo ls-files --others --exclude-standard)) {
    $part = Join-Path $env:TEMP "coop-new-$i.patch"
    git -C $repo diff --no-index --output=$part -- /dev/null $new
    $parts += $part
    $i++
}
$patchBytes = foreach ($part in $parts) { [IO.File]::ReadAllBytes($part) }
[IO.File]::WriteAllBytes((Join-Path $source 'coopandreas-ozel.patch'), [byte[]]$patchBytes)
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination (Join-Path $source 'LICENSE-CoopAndreas.txt')
Copy-Item -LiteralPath (Join-Path $root 'tunnel\Program.cs'), (Join-Path $root 'tunnel\CoopTunnel.csproj') -Destination (Join-Path $source 'CoopTunnel')
$note = @"
Bu paketteki CoopAndreas derlemesinin kaynağı (GPL-3.0)

Üst proje : https://github.com/Tornamic/CoopAndreas
Temel commit: $base
Değişiklikler: coopandreas-ozel.patch  (repo kökünde: git apply coopandreas-ozel.patch)
Lisans    : LICENSE-CoopAndreas.txt

CoopTunnel kaynağı: CoopTunnel\Program.cs, CoopTunnel\CoopTunnel.csproj
xdelta3   : https://github.com/jmacd/xdelta-gpl/releases/tag/v3.1.0 (tools\SOURCE.txt)
"@
[IO.File]::WriteAllText((Join-Path $source 'OKU.txt'), ($note -replace "`r?`n", "`r`n"), $utf8Bom)

$zip = "$out.zip"
Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip -Force
Get-ChildItem -LiteralPath $out -Recurse -File | ForEach-Object { '{0,-45} {1,10:N0}' -f $_.FullName.Substring($out.Length + 1), $_.Length }
'zip: {0} ({1:N1} MB)' -f $zip, ((Get-Item -LiteralPath $zip).Length / 1MB)
exit 0  # git diff --no-index leaves exit code 1 behind when it finds differences
