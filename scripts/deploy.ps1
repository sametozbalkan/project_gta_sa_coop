<#
.SYNOPSIS
    Installs the locally built CoopAndreas into the 1.0 US game copy.
.DESCRIPTION
    Refuses to run unless the game folder has a 1.0 US gta_sa.exe. On the first run it renames the
    game's eax.dll to eax_orig.dll, because the proxy takes its name and forwards to the original.
    Then it copies the client DLL, the proxy (as eax.dll), the server (into server\) and the co-op
    main.scm / script.img (into CoopAndreas\), and creates three shortcuts in the game folder:
    "CoopAndreas" (gta_sa.exe --coop; the proxy only loads the mod with that flag),
    "CoopAndreas (diagnostics)" (--coop --coopdiag, as the installer's) and "CoopAndreas Server".
    The launcher isn't used: this build has no serial check. It also removes the diagnostics shortcut
    under its old name and the discord-<appid> URL handler that older builds' Discord Rich Presence
    wrote to HKCU. Supports -WhatIf.
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

function Get-Sha256([string]$Path) {
    # not Get-FileHash: under -WhatIf in Windows PowerShell 5.1 it returns nothing, so every file looked unchanged
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { [BitConverter]::ToString($sha.ComputeHash($stream)) }
    finally { $stream.Dispose(); $sha.Dispose() }
}

function Copy-One([string]$From, [string]$To) {
    # unchanged files are skipped, so a running server.exe doesn't block a client-only redeploy
    if ((Test-Path -LiteralPath $To) -and (Get-Sha256 $From) -eq (Get-Sha256 $To)) {
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
# with diagnostic logs (--coopdiag): CoopAndreas_diag.log and the other CoopAndreas_*.log files go into the game folder
New-Shortcut (Join-Path $GameDir 'CoopAndreas (diagnostics).lnk') $exe '--coop --coopdiag' $GameDir
# the name earlier packages gave the diagnostics shortcut
$oldDiagLink = Join-Path $GameDir 'CoopAndreas (tani).lnk'
if (Test-Path -LiteralPath $oldDiagLink -PathType Leaf) {
    if ($PSCmdlet.ShouldProcess($oldDiagLink, 'Remove the diagnostics shortcut under its old name')) {
        Remove-Item -LiteralPath $oldDiagLink -Force
        "Removed the old diagnostics shortcut: $oldDiagLink"
    }
}

# Story host pinning: "story-host" in server-config.ini names the player who is always the host. When the key is
# missing, the nickname from this PC's coopandreas.ini (the game reads it from its user files folder) goes in, in the
# form the server compares: PLAYER_CONNECTED keeps at most 24 bytes and turns every byte other than a-z, A-Z, 0-9, '_',
# '[' and ']' into '_'. An existing story-host line stays as it is, an empty one too (the first player is the host).
function Get-IniNickname([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    # one character per byte, as GetPrivateProfileString hands the client the ANSI bytes the server then checks
    $text = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($Path))
    if ($text.StartsWith([string][char]0xEF + [char]0xBB + [char]0xBF)) { $text = $text.Substring(3) }
    $section = ''
    foreach ($line in ($text -split "`r?`n")) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.*)\]$') { $section = $Matches[1].Trim(); continue }
        if ($section -eq 'config' -and $trimmed -match '^nickname\s*=(.*)$') {
            $value = $Matches[1].Trim()
            if ($value.Length -ge 2 -and ($value[0] -eq '"' -or $value[0] -eq "'") -and $value[-1] -eq $value[0]) {
                $value = $value.Substring(1, $value.Length - 2)
            }
            return $value
        }
    }
    return $null
}

$serverConfig = Join-Path $serverDir 'server-config.ini'
$userIni = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'GTA San Andreas User Files\coopandreas.ini'
$configLines = @()
if (Test-Path -LiteralPath $serverConfig) { $configLines = @(Get-Content -LiteralPath $serverConfig) }
$storyHostLine = $configLines | Where-Object { $_ -match '^\s*story-host\s*[=:]' } | Select-Object -First 1
if ($storyHostLine) {
    $current = ($storyHostLine -replace '^\s*story-host\s*[=:]', '').Trim()
    if ($current) { "story-host is already set in ${serverConfig}: $current" }
    else { "story-host is empty in ${serverConfig}: the first player to connect is the host" }
}
elseif ($configLines | Where-Object { $_ -match '^\s*\[' }) {
    Write-Warning "$serverConfig has [sections]; add 'story-host = <nickname>' above them by hand"
}
else {
    $nick = Get-IniNickname $userIni
    if (-not $nick) {
        Write-Warning "No nickname in $userIni; story-host not added to $serverConfig"
    }
    else {
        if ($nick.Length -gt 24) { $nick = $nick.Substring(0, 24) }
        $nick = $nick -replace '[^A-Za-z0-9_\[\]]', '_'
        if ($PSCmdlet.ShouldProcess($serverConfig, "Add story-host = $nick")) {
            New-Item -ItemType Directory -Force -Path $serverDir | Out-Null
            $prefix = ''
            if (Test-Path -LiteralPath $serverConfig) {
                $existing = [IO.File]::ReadAllText($serverConfig)
                if ($existing.Length -gt 0 -and -not $existing.EndsWith("`n")) { $prefix = "`r`n" }
            }
            [IO.File]::AppendAllText($serverConfig, "${prefix}story-host = $nick`r`n", [Text.Encoding]::ASCII)
            "Added story-host = $nick to $serverConfig (restart server.exe if it is running)"
        }
    }
}

# Older builds' Discord Rich Presence registered this URL handler; the client no longer does (COOP_NO_DISCORD_RPC)
$discordKey = 'HKCU:\Software\Classes\discord-1324128258672951317'
if (Test-Path -LiteralPath $discordKey) {
    if ($PSCmdlet.ShouldProcess($discordKey, 'Remove the old Discord URL handler')) {
        Remove-Item -LiteralPath $discordKey -Recurse -Force
        "Removed the old Discord URL handler: $discordKey"
    }
}

if ($WhatIfPreference) { 'Dry run: nothing was changed' } else { "Deployed the $Mode build to $GameDir" }
