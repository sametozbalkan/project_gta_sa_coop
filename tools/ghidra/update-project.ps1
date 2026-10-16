<#
.SYNOPSIS
    Creates (once) and updates the persistent Ghidra project of gta_sa.exe 1.0 US that ReVa opens.
.DESCRIPTION
    When the project is missing, imports the exe (read only) and runs Ghidra's auto-analysis, about
    4 minutes. Then writes the gta-reversed and plugin-sdk names (gta_names.py) and applies them
    (ApplyGtaNames.java, about 15 s). Close every ReVa session on the project first: Ghidra lets one
    process open a project. Ghidra refuses a project path with a folder that starts with '.', so the
    project folder is passed by its 8.3 short name.
.PARAMETER GhidraDir
    Ghidra 12.1.x install folder; defaults to $env:GHIDRA_INSTALL_DIR.
.PARAMETER ProjectDir
    Folder holding gta_sa.gpr; defaults to .tools\ghidra-projects of the main checkout.
.PARAMETER Exe
    The 1.0 US executable to import when the project is created.
#>
param(
    [string]$GhidraDir = $env:GHIDRA_INSTALL_DIR,
    [string]$ProjectDir,
    [string]$Exe = 'C:\Games\GTASA_Coop\gta_sa.exe'
)

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$tools = Join-Path $root '.tools'
if (-not (Test-Path -LiteralPath (Join-Path $tools 'gta-reversed'))) {
    # In a linked git worktree (agents) the git-ignored .tools exists only in the main checkout
    $commonDir = git -C $root rev-parse --path-format=absolute --git-common-dir
    if ($LASTEXITCODE -eq 0) { $tools = Join-Path (Split-Path -Parent $commonDir) '.tools' }
}
$headless = Join-Path "$GhidraDir" 'support\analyzeHeadless.bat'
if (-not $GhidraDir -or -not (Test-Path -LiteralPath $headless)) { throw 'Ghidra not found: pass -GhidraDir or set GHIDRA_INSTALL_DIR' }
if (-not $ProjectDir) { $ProjectDir = Join-Path $tools 'ghidra-projects' }
$settings = Join-Path $tools 'ghidra-settings'
New-Item -ItemType Directory -Force -Path $ProjectDir, $settings | Out-Null

$shortDir = (New-Object -ComObject Scripting.FileSystemObject).GetFolder($ProjectDir).ShortPath
if ($shortDir -match '\\\.') { throw "Ghidra cannot open a project under $ProjectDir and the volume has no 8.3 name for it" }

$env:GHIDRA_HEADLESS_MAXMEM = '8G'
$env:GHIDRA_HEADLESS_JAVA_OPTIONS = "-Dapplication.settingsdir=$settings"

if (-not (Test-Path -LiteralPath (Join-Path $ProjectDir 'gta_sa.gpr'))) {
    & $headless $shortDir gta_sa -import $Exe
    if ($LASTEXITCODE) { throw "import and analysis failed (exit $LASTEXITCODE)" }
}

$tsv = Join-Path $ProjectDir 'gta_sa_names.tsv'
& python -I (Join-Path $PSScriptRoot 'gta_names.py') (Join-Path $tools 'gta-reversed') (Join-Path $root 'CoopAndreas\third_party\plugin-sdk') $tsv $Exe
if ($LASTEXITCODE) { throw "gta_names.py failed (exit $LASTEXITCODE)" }

& $headless $shortDir gta_sa -process gta_sa.exe -noanalysis -scriptPath $PSScriptRoot -postScript ApplyGtaNames.java $tsv
if ($LASTEXITCODE) { throw "ApplyGtaNames.java failed (exit $LASTEXITCODE)" }
