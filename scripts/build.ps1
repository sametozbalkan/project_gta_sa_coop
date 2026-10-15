<#
.SYNOPSIS
    Builds every CoopAndreas target (client, server, proxy, launcher) with the bundled xmake.
.PARAMETER Mode
    release (default) or debug.
#>
param([ValidateSet('release', 'debug')] [string]$Mode = 'release')

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$tools = Join-Path $root '.tools'
if (-not (Test-Path -LiteralPath (Join-Path $tools 'xmake\xmake.exe'))) {
    # In a linked git worktree (agents) the git-ignored .tools exists only in the main checkout
    $commonDir = git -C $root rev-parse --path-format=absolute --git-common-dir
    if ($LASTEXITCODE -eq 0) { $tools = Join-Path (Split-Path -Parent $commonDir) '.tools' }
}
$xmake = Join-Path $tools 'xmake\xmake.exe'
if (-not (Test-Path -LiteralPath $xmake)) { throw "xmake not found at $xmake; see CLAUDE.md (.tools)" }
$repo  = Join-Path $root 'CoopAndreas'

$env:XMAKE_COLORTERM = 'nocolor'
# xmake.lua drops the client DLL straight into GTA_SA_DIR when it is set; deploy.ps1 does the copying instead.
Remove-Item Env:GTA_SA_DIR -ErrorAction SilentlyContinue

Push-Location $repo
try {
    & $xmake f -y -p windows -a x86 -m $Mode
    if ($LASTEXITCODE) { throw "xmake configure failed (exit $LASTEXITCODE)" }
    & $xmake build -y -a
    if ($LASTEXITCODE) { throw "xmake build failed (exit $LASTEXITCODE)" }
}
finally { Pop-Location }

Get-ChildItem (Join-Path $repo "build\windows\x86\$Mode") -File |
    Where-Object Extension -In '.dll', '.exe', '.manifest' |
    Format-Table Name, Length, LastWriteTime -AutoSize
