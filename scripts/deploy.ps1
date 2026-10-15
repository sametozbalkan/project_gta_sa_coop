<#
.SYNOPSIS
    Installs the locally built CoopAndreas into the 1.0 US game copy.
.DESCRIPTION
    Refuses to run unless the game folder has a 1.0 US gta_sa.exe. On the first run it renames the
    game's eax.dll to eax_orig.dll, because the proxy takes its name and forwards to the original.
    Then it copies the client DLL, the proxy (as eax.dll), the server (into server\) and the co-op
    main.scm / script.img (into CoopAndreas\), and creates two shortcuts in the game folder:
    "CoopAndreas" (gta_sa.exe --coop; the proxy only loads the mod with that flag) and
    "CoopAndreas Server". The launcher isn't used: this build has no serial check. Supports -WhatIf.
.PARAMETER Force
    Continue even if gta_sa.exe has a size that is not a known 1.0 US size.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$GameDir = 'C:\Games\GTASA_Coop',
    [ValidateSet('release', 'debug')] [string]$Mode = 'release',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$repo  = Join-Path $root 'CoopAndreas'
$build = Join-Path $repo "build\windows\x86\$Mode"

# gta_sa.exe sizes of 1.0 US: the original/Hoodlum release and the widely used "compact" build.
$knownSizes = @{ 14383616 = '1.0 US'; 5189632 = '1.0 US (compact)' }

$exe = Join-Path $GameDir 'gta_sa.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    if (Test-Path -LiteralPath (Join-Path $GameDir 'gta-sa.exe')) {
        throw "Only the Steam executable (gta-sa.exe) is in $GameDir. Run the 1.0 downgrader on that folder first."
    }
    throw "gta_sa.exe not found in $GameDir"
}
$size = [int](Get-Item -LiteralPath $exe).Length
if ($knownSizes.ContainsKey($size)) {
    "gta_sa.exe: $($knownSizes[$size]) ($size bytes)"
}
elseif ($Force) {
    Write-Warning "gta_sa.exe is $size bytes, not a known 1.0 US size; continuing because of -Force"
}
else {
    throw "gta_sa.exe is $size bytes, not a known 1.0 US size. Re-run with -Force only if you are sure it is 1.0 US."
}

foreach ($f in 'CoopAndreasSA.dll', 'proxy.dll', 'server.exe') {
    if (-not (Test-Path -LiteralPath (Join-Path $build $f))) { throw "$f is missing in $build; run scripts\build.ps1 first" }
}

$eax = Join-Path $GameDir 'eax.dll'
if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'eax_orig.dll'))) {
    if ($PSCmdlet.ShouldProcess($eax, 'Rename to eax_orig.dll')) {
        Rename-Item -LiteralPath $eax -NewName 'eax_orig.dll'
    }
}

function Copy-One([string]$From, [string]$To) {
    # unchanged files are skipped, so a running server.exe doesn't block a client-only redeploy
    if ((Test-Path -LiteralPath $To) -and (Get-FileHash -LiteralPath $From).Hash -eq (Get-FileHash -LiteralPath $To).Hash) {
        return
    }
    if ($PSCmdlet.ShouldProcess($To, "Copy $From")) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $To) | Out-Null
        Copy-Item -LiteralPath $From -Destination $To -Force
    }
}

function New-Shortcut([string]$Path, [string]$Target, [string]$Arguments, [string]$WorkingDir) {
    if ($PSCmdlet.ShouldProcess($Path, "Create shortcut to $Target $Arguments")) {
        $link = (New-Object -ComObject WScript.Shell).CreateShortcut($Path)
        $link.TargetPath = $Target
        $link.Arguments = $Arguments
        $link.WorkingDirectory = $WorkingDir
        $link.Save()
    }
}

$serverDir = Join-Path $GameDir 'server'
Copy-One (Join-Path $build 'CoopAndreasSA.dll') (Join-Path $GameDir 'CoopAndreasSA.dll')
Copy-One (Join-Path $build 'proxy.dll')         $eax
Copy-One (Join-Path $build 'server.exe')        (Join-Path $serverDir 'server.exe')
Copy-One (Join-Path $repo 'scm\main.scm')       (Join-Path $GameDir 'CoopAndreas\main.scm')
Copy-One (Join-Path $repo 'scm\script.img')     (Join-Path $GameDir 'CoopAndreas\script.img')

New-Shortcut (Join-Path $GameDir 'CoopAndreas.lnk') $exe '--coop' $GameDir
New-Shortcut (Join-Path $GameDir 'CoopAndreas Server.lnk') (Join-Path $serverDir 'server.exe') '' $serverDir

if ($WhatIfPreference) { 'Dry run: nothing was changed' } else { "Deployed the $Mode build to $GameDir" }
