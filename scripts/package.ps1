#Requires -Version 7.0
<#
.SYNOPSIS
    Builds dist\CoopAndreas-Setup (+ .zip) for a friend's PC: the installer, our CoopAndreas build,
    CoopTunnel, the official xdelta3 and the GPL source of our CoopAndreas changes.
.DESCRIPTION
    Run scripts\build.ps1 and scripts\publish-tunnel.ps1 first, and commit everything under CoopAndreas\:
    SOURCE\coopandreas-changes.patch is the diff from the latest subtree import to HEAD.
.PARAMETER IncludePatches
    Also bundles .tools\patches\Patches_v2.zip (~880 MB) next to Install.ps1, so the installer doesn't need
    the download server. Without it Install.ps1 downloads the archive; either way it checks the pinned SHA-256.
.PARAMETER TestPlan
    Optional test checklist (relative to the repository root or absolute). Included as TEST-PLAN.md.
    manifest.json identifies the source commit, version and every payload file by SHA-256; the zip gets
    a .sha256 sidecar. Use PowerShell 7 for portable zip entry names.
#>
param([switch]$IncludePatches, [string]$TestPlan)

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$repo  = Join-Path $root 'CoopAndreas'
$build = Join-Path $repo 'build\windows\x86\release'
$out   = Join-Path $root 'dist\CoopAndreas-Setup'
$tools = Join-Path $root '.tools'
if ($TestPlan) {
    if (-not [IO.Path]::IsPathRooted($TestPlan)) { $TestPlan = Join-Path $root $TestPlan }
    if (-not (Test-Path -LiteralPath $TestPlan -PathType Leaf)) { throw "Test plan not found: $TestPlan" }
}
if (-not (Test-Path -LiteralPath (Join-Path $tools 'xdelta3\xdelta3.exe'))) {
    # In a linked git worktree (agents) the git-ignored .tools exists only in the main checkout
    $commonDir = git -C $root rev-parse --path-format=absolute --git-common-dir
    if ($LASTEXITCODE -eq 0) { $tools = Join-Path (Split-Path -Parent $commonDir) '.tools' }
}

foreach ($f in 'CoopAndreasSA.dll', 'proxy.dll', 'server.exe') {
    if (-not (Test-Path -LiteralPath (Join-Path $build $f))) { throw "$f is missing in $build; run scripts\build.ps1 first" }
}
if (-not (Test-Path -LiteralPath (Join-Path $root 'dist\CoopTunnel\CoopTunnel.exe'))) { throw 'dist\CoopTunnel is missing; run scripts\publish-tunnel.ps1 first' }
if (-not (Test-Path -LiteralPath (Join-Path $tools 'xdelta3\xdelta3.exe'))) { throw "xdelta3.exe is missing in $tools\xdelta3" }

# The manifest names HEAD and SOURCE is made from commits; all packaged sources must match that commit.
$dirty = git -C $root status --porcelain -- CoopAndreas tunnel installer scripts docs
if ($LASTEXITCODE) { throw "git status failed (exit $LASTEXITCODE)" }
if ($dirty) { throw "Package sources have uncommitted changes; commit them first:`n$($dirty -join "`n")" }
# GPL-3.0 source of the shipped build: the latest subtree import is a squash commit holding the upstream tree
# of its git-subtree-split commit, so the diff from that tree to HEAD:CoopAndreas is all of our changes
$import = @(git -C $root log -1 --no-show-signature --format='%H%n%B' --grep='^git-subtree-dir: CoopAndreas/*$')
if ($LASTEXITCODE -or -not $import) { throw 'no git subtree import of CoopAndreas found in the history' }
$squash = $import[0]
$split = [regex]::Match(($import -join "`n"), '(?m)^git-subtree-split: ([0-9a-f]{40})$').Groups[1].Value
if (-not $split -or ($import -match '^git-subtree-mainline:')) { throw "$squash is not a git subtree --squash import" }
$head = git -C $root rev-parse HEAD

$patches = Join-Path $tools 'patches\Patches_v2.zip'
if ($IncludePatches) {
    if (-not (Test-Path -LiteralPath $patches)) { throw "$patches is missing" }
    $installer = [IO.File]::ReadAllText((Join-Path $root 'installer\Install.ps1'))
    $pinned = [regex]::Match($installer, '\$PatchesSha256\s*=\s*''([0-9A-Fa-f]{64})''').Groups[1].Value
    if (-not $pinned) { throw 'no $PatchesSha256 found in installer\Install.ps1' }
    if ((Get-FileHash -LiteralPath $patches -Algorithm SHA256).Hash -ne $pinned) { throw "$patches doesn't match the SHA-256 pinned in Install.ps1" }
}

if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
foreach ($dir in 'files\CoopAndreas', 'files\server', 'tools', 'CoopTunnel', 'SOURCE\CoopTunnel') {
    New-Item -ItemType Directory -Force -Path (Join-Path $out $dir) | Out-Null
}

# Windows PowerShell 5.1 reads a .ps1 without BOM as ANSI, which would garble any non-ASCII text
$utf8Bom = New-Object System.Text.UTF8Encoding $true
foreach ($name in 'Install.ps1', 'README.txt') {
    $text = [IO.File]::ReadAllText((Join-Path $root "installer\$name")) -replace "`r?`n", "`r`n"
    [IO.File]::WriteAllText((Join-Path $out $name), $text, $utf8Bom)
}
$bat = [IO.File]::ReadAllText((Join-Path $root 'installer\Install.bat')) -replace "`r?`n", "`r`n"
[IO.File]::WriteAllText((Join-Path $out 'Install.bat'), $bat, [Text.Encoding]::ASCII)
if ($TestPlan) {
    $text = [IO.File]::ReadAllText($TestPlan) -replace "`r?`n", "`r`n"
    [IO.File]::WriteAllText((Join-Path $out 'TEST-PLAN.md'), $text, $utf8Bom)
}

Copy-Item -LiteralPath (Join-Path $build 'CoopAndreasSA.dll') -Destination (Join-Path $out 'files')
Copy-Item -LiteralPath (Join-Path $build 'proxy.dll') -Destination (Join-Path $out 'files\eax.dll')
Copy-Item -LiteralPath (Join-Path $build 'server.exe') -Destination (Join-Path $out 'files\server')
Copy-Item -LiteralPath (Join-Path $repo 'scm\main.scm'), (Join-Path $repo 'scm\script.img') -Destination (Join-Path $out 'files\CoopAndreas')
Copy-Item -Path (Join-Path $tools 'xdelta3\*') -Destination (Join-Path $out 'tools')
Copy-Item -Path (Join-Path $root 'dist\CoopTunnel\*') -Destination (Join-Path $out 'CoopTunnel')
# Install.ps1 takes a Patches_v2.zip lying next to it (after its own SHA-256 check) instead of downloading one
if ($IncludePatches) { Copy-Item -LiteralPath $patches -Destination $out }

# diff-tree is the plumbing form of git diff: textconv, external diff and prefix settings can't change the patch
$source = Join-Path $out 'SOURCE'
$patch = Join-Path $source 'coopandreas-changes.patch'
git -C $root diff-tree -r -p --binary --output=$patch "$squash^{tree}" HEAD:CoopAndreas
if ($LASTEXITCODE) { throw "git diff-tree failed (exit $LASTEXITCODE)" }
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination (Join-Path $source 'LICENSE-CoopAndreas.txt')
Copy-Item -LiteralPath (Join-Path $root 'tunnel\Program.cs'), (Join-Path $root 'tunnel\CoopTunnel.csproj') -Destination (Join-Path $source 'CoopTunnel')
$note = @"
Source of the CoopAndreas build in this package (GPL-3.0)

Upstream     : https://github.com/Tornamic/CoopAndreas
Base commit  : $split
Changes      : coopandreas-changes.patch (in a CoopAndreas checkout of the base commit: git apply coopandreas-changes.patch)
License      : LICENSE-CoopAndreas.txt
Full source  : https://github.com/sametozbalkan/project_gta_sa_coop (commit $head)

CoopTunnel   : CoopTunnel\Program.cs, CoopTunnel\CoopTunnel.csproj
xdelta3      : https://github.com/jmacd/xdelta-gpl/releases/tag/v3.1.0 (tools\SOURCE.txt)
"@
[IO.File]::WriteAllText((Join-Path $source 'README.txt'), ($note -replace "`r?`n", "`r`n"), $utf8Bom)

$version = [regex]::Match([IO.File]::ReadAllText((Join-Path $repo 'shared\config.h')),
    '(?m)^#define COOPANDREAS_VERSION "([^"]+)"').Groups[1].Value
if (-not $version) { throw 'COOPANDREAS_VERSION is missing from shared\config.h' }
$manifest = [ordered]@{
    formatVersion = 1
    version = $version
    sourceCommit = $head
    testPlan = $(if ($TestPlan) { 'TEST-PLAN.md' } else { $null })
    files = @(Get-ChildItem -LiteralPath $out -Recurse -File | Sort-Object FullName | ForEach-Object {
        [ordered]@{
            path = $_.FullName.Substring($out.Length + 1).Replace('\', '/')
            size = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })
}
[IO.File]::WriteAllText((Join-Path $out 'manifest.json'),
    ($manifest | ConvertTo-Json -Depth 4) + "`n", [Text.UTF8Encoding]::new($false))

$zip = "$out.zip"
Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip -Force
$zipHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$zip.sha256", "$zipHash  $([IO.Path]::GetFileName($zip))`n", [Text.Encoding]::ASCII)
Get-ChildItem -LiteralPath $out -Recurse -File | ForEach-Object { '{0,-45} {1,10:N0}' -f $_.FullName.Substring($out.Length + 1), $_.Length }
'zip: {0} ({1:N1} MB)' -f $zip, ((Get-Item -LiteralPath $zip).Length / 1MB)
"SHA-256: $zipHash"
