<#
.SYNOPSIS
    CoopAndreas setup: copies the Steam GTA San Andreas, downgrades the copy to 1.0 US and installs
    CoopAndreas, CoopTunnel and the shortcuts.
.DESCRIPTION
    The Steam install is never changed: the game is copied to -GameDir. The patches come from
    gtasa-open-downgrader's Patches_v2.zip (pinned SHA-256) and are applied with the official xdelta3;
    every file is checked against the MD5 in the patch manifest. If -GameDir already holds 1.0 US, the game
    is not copied again: only the mod files are updated, and files that older packages created under
    other names are removed. Runs on Windows PowerShell 5.1.
.PARAMETER GameDir
    Folder for the 1.0 US copy (default C:\Games\GTASA_Coop). It must be empty or already hold 1.0 US.
.PARAMETER SteamGameDir
    The Steam GTA San Andreas folder, if the setup can't find it.
.PARAMETER PatchesZip
    A Patches_v2.zip to use instead of the download. One next to this script is used too. Either is
    checked against the pinned SHA-256.
.PARAMETER NoDesktopShortcuts
    Don't put shortcuts on the desktop.
#>
param(
    [string]$GameDir = 'C:\Games\GTASA_Coop',
    [string]$SteamGameDir = '',
    [string]$PatchesZip = '',
    [switch]$NoDesktopShortcuts
)

$ErrorActionPreference = 'Stop'
$PatchesUrl = 'https://files.xxanqw.me/s/6jhSHGdr8N'
$PatchesSha256 = 'F7DF42723D2FE123ADEC5B3E1D3459E52810B6D2566F51EBEE0373944388265B'
$Us10Md5 = '170b3a9108687b26da2d8901c6948a18'
$SupportedExe = @{
    '5bfd4dd83989a8264de4b8e771f237fd' = 'Steam (NewSteam R2)'
    'd9cb35c898d3298ca904a63e10ee18d7' = 'Steam (NewSteam R2, German)'
    '170b3a9108687b26da2d8901c6948a18' = '1.0 US'
}
$Pkg = $PSScriptRoot
$GameDir = [IO.Path]::GetFullPath($GameDir)

function Say([string]$Text, [string]$Color = 'Gray') { Write-Host $Text -ForegroundColor $Color }
function Step([string]$Text) { Write-Host ''; Write-Host "==> $Text" -ForegroundColor Cyan }
function Get-Md5([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm MD5).Hash.ToLower() }

function Get-GameExe([string]$Dir) {
    foreach ($name in 'gta_sa.exe', 'gta-sa.exe') {
        $path = Join-Path $Dir $name
        if (Test-Path -LiteralPath $path) { return $path }
    }
    return $null
}

function Get-SteamPath {
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if ($steam) { return ($steam -replace '/', '\') }
    return $null
}

function Find-SteamGame {
    $steam = Get-SteamPath
    if (-not $steam) { throw "Steam not found. Give the game folder yourself: Install.bat -SteamGameDir `"...\Grand Theft Auto San Andreas`"" }
    $libraries = @($steam)
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $vdf) {
        $libraries += [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"') |
            ForEach-Object { $_.Groups[1].Value -replace '\\\\', '\' }
    }
    foreach ($lib in ($libraries | Select-Object -Unique)) {
        $dir = Join-Path $lib 'steamapps\common\Grand Theft Auto San Andreas'
        if (Get-GameExe $dir) { return $dir }
    }
    throw "GTA San Andreas not found in the Steam libraries. Is it installed in Steam? If it is, give its folder: Install.bat -SteamGameDir `"...\Grand Theft Auto San Andreas`""
}

# Steam display name of the most recent login, cleaned for CoopAndreas (24 chars, no quotes or spaces)
function Get-Nickname {
    $nick = ''
    $steam = Get-SteamPath
    if ($steam) {
        $users = Join-Path $steam 'config\loginusers.vdf'
        if (Test-Path -LiteralPath $users) {
            foreach ($m in [regex]::Matches((Get-Content -LiteralPath $users -Raw -Encoding UTF8), '"\d{17}"\s*\{([^}]*)\}')) {
                $block = $m.Groups[1].Value
                if ($block -match '"MostRecent"\s+"1"' -and $block -match '"PersonaName"\s+"([^"]*)"') { $nick = $Matches[1] }
            }
        }
    }
    if (-not $nick) { $nick = $env:USERNAME }
    $nick = $nick -replace '[^A-Za-z0-9_.-]', ''
    if (-not $nick) { $nick = 'Player' }
    if ($nick.Length -gt 24) { $nick = $nick.Substring(0, 24) }
    return $nick
}

function Get-Patches {
    $candidates = @($PatchesZip, (Join-Path $Pkg 'Patches_v2.zip'), (Join-Path $env:TEMP 'CoopAndreas_Patches_v2.zip')) | Where-Object { $_ }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            Say "Checking the patch archive: $candidate"
            if ((Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash -eq $PatchesSha256) { return $candidate }
            Say '  Wrong SHA-256; this file is not used.' 'Yellow'
        }
    }
    $target = Join-Path $env:TEMP 'CoopAndreas_Patches_v2.zip'
    Say "Downloading the patches (~880 MB): $PatchesUrl"
    & curl.exe -L --fail --retry 3 -o $target $PatchesUrl
    if ($LASTEXITCODE -ne 0) { throw "Download failed (curl $LASTEXITCODE). Check your internet connection and run Install.bat again." }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $PatchesSha256) {
        throw 'The downloaded patch archive is not the expected file (wrong SHA-256). Setup stopped.'
    }
    return $target
}

# path of a manifest entry in the game folder; the exe may exist under either name
function Resolve-GamePath([string]$Dir, [string]$Rel) {
    $path = Join-Path $Dir ($Rel -replace '/', '\')
    if (($Rel -in @('gta_sa.exe', 'gta-sa.exe')) -and -not (Test-Path -LiteralPath $path)) {
        $other = Get-GameExe $Dir
        if ($other) { return $other }
    }
    return $path
}

# same steps as gtasa-open-downgrader's PatchWorker: check every file first, then patch, then verify
function Invoke-Downgrade([string]$Dir, [string]$PatchDir, [string]$Xdelta) {
    $manifest = Get-Content -LiteralPath (Join-Path $PatchDir 'manifest.json') -Raw | ConvertFrom-Json
    $plan = @()
    foreach ($entry in $manifest.files) {
        $path = Resolve-GamePath $Dir $entry.path
        $have = $null
        if (Test-Path -LiteralPath $path) { $have = Get-Md5 $path }
        if ($have -eq $entry.target_hash) { continue }
        if ($entry.action -eq 'copy') {
            $payloadName = 'gta_sa.exe'
            if ($entry.payload) { $payloadName = $entry.payload }
            $payload = Join-Path $PatchDir $payloadName
            if ((Get-Md5 $payload) -ne $entry.target_hash) { throw "$payloadName in the patch archive is not the expected file." }
            $plan += , @($entry, $path, $payload)
        }
        else {
            $source = $entry.sources | Where-Object { $_.hash -eq $have } | Select-Object -First 1
            if (-not $source) { throw "$($entry.path) is an unexpected version (MD5 $have). No file was changed." }
            $patch = Join-Path $PatchDir ($source.patch -replace '/', '\')
            if (-not (Test-Path -LiteralPath $patch)) { throw "Patch file missing: $($source.patch)" }
            $plan += , @($entry, $path, $patch)
        }
    }
    Say "Check passed; files to change: $($plan.Count)."

    foreach ($item in $plan) {
        $entry, $path, $from = $item
        $tmp = "$path.tmp"
        if ($entry.action -eq 'copy') {
            Copy-Item -LiteralPath $from -Destination $tmp -Force
        }
        else {
            & $Xdelta -d -f -s $path $from $tmp
            if ($LASTEXITCODE -ne 0) { throw "xdelta3 failed: $($entry.path)" }
        }
        if ((Get-Md5 $tmp) -ne $entry.target_hash) {
            Remove-Item -LiteralPath $tmp -Force
            throw "Wrong result after patching: $($entry.path)"
        }
        Move-Item -LiteralPath $tmp -Destination $path -Force
        Say "  1.0: $($entry.path)"
    }

    # like the downgrader, both executable names end up as the 1.0 exe
    $exe = Get-GameExe $Dir
    foreach ($name in 'gta_sa.exe', 'gta-sa.exe') {
        $other = Join-Path $Dir $name
        if ($other -ne $exe) { Copy-Item -LiteralPath $exe -Destination $other -Force }
    }
    foreach ($entry in $manifest.files) {
        if ((Get-Md5 (Resolve-GamePath $Dir $entry.path)) -ne $entry.target_hash) { throw "Final check failed: $($entry.path)" }
    }
}

function New-Shortcut([string]$Path, [string]$Target, [string]$Arguments, [string]$WorkingDir) {
    $link = (New-Object -ComObject WScript.Shell).CreateShortcut($Path)
    $link.TargetPath = $Target
    $link.Arguments = $Arguments
    $link.WorkingDirectory = $WorkingDir
    $link.Save()
}

# Earlier packages created these files under other names; this package installs them under new ones.
# Only these exact paths are removed.
function Remove-LegacyFiles([string]$Dir, [string]$TunnelDir) {
    $oldJoin = Join-Path $TunnelDir '3-Oyuna-baglan.bat'
    $paths = @(
        (Join-Path $Dir 'CoopAndreas (tani).lnk'),
        (Join-Path $TunnelDir '1-Host-ac.bat'),
        (Join-Path $TunnelDir '2-Baglanti-testi.bat'),
        $oldJoin,
        (Join-Path $TunnelDir 'KULLANIM.txt')
    )
    # the name earlier packages gave the desktop shortcut that is now "CoopAndreas Tunnel - Join"
    $oldLink = Join-Path ([Environment]::GetFolderPath('Desktop')) ('CoopAndreas T{0}nel - Ba{1}lan.lnk' -f [char]0x00FC, [char]0x011F)
    if (Test-Path -LiteralPath $oldLink -PathType Leaf) {
        try {
            # left alone when it starts the old launcher of another install that still has it
            $target = (New-Object -ComObject WScript.Shell).CreateShortcut($oldLink).TargetPath
            if (-not $target -or $target -eq $oldJoin -or -not (Test-Path -LiteralPath $target)) { $paths += $oldLink }
        }
        catch { Say "Could not read the old desktop shortcut $oldLink ($($_.Exception.Message))" 'Yellow' }
    }
    foreach ($path in $paths) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        try {
            Remove-Item -LiteralPath $path -Force
            Say "Removed old file: $path"
        }
        catch { Say "Could not remove the old file $path ($($_.Exception.Message))" 'Yellow' }
    }
}

try {
    Say 'CoopAndreas setup' 'White'
    $required = 'files\CoopAndreasSA.dll', 'files\eax.dll', 'files\CoopAndreas\main.scm', 'files\CoopAndreas\script.img',
                'files\server\server.exe', 'tools\xdelta3.exe', 'CoopTunnel\CoopTunnel.exe', 'CoopTunnel\steam_api64.dll'
    foreach ($file in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $Pkg $file))) { throw "The package is incomplete ($file is missing). Extract the whole zip into a folder and run Install.bat from there." }
    }
    if (Get-Process -Name 'gta_sa', 'gta-sa' -ErrorAction SilentlyContinue) { throw 'GTA San Andreas is running. Close the game and run Install.bat again.' }

    $exe = Get-GameExe $GameDir
    if ($exe -and (Get-Md5 $exe) -eq $Us10Md5) {
        Step "$GameDir already has GTA SA 1.0 US: updating the mod files only."
    }
    else {
        if ((Test-Path -LiteralPath $GameDir) -and (Get-ChildItem -LiteralPath $GameDir -Force | Select-Object -First 1)) {
            throw "$GameDir is not empty and has no 1.0 US install. Empty it or choose another folder: Install.bat -GameDir D:\GTASA_Coop"
        }
        Step 'Looking for the Steam install'
        $source = $SteamGameDir
        if (-not $source) { $source = Find-SteamGame }
        $sourceExe = Get-GameExe $source
        if (-not $sourceExe) { throw "No gta-sa.exe in $source." }
        $sourceMd5 = Get-Md5 $sourceExe
        if (-not $SupportedExe.ContainsKey($sourceMd5)) {
            throw "This GTA SA version is not supported (gta-sa.exe MD5 $sourceMd5). The patches are for the current Steam version (NewSteam R2)."
        }
        Say "Found: $source ($($SupportedExe[$sourceMd5]))"

        $free = (Get-PSDrive -Name $GameDir.Substring(0, 1)).Free
        if ($free -lt 7GB) { throw ('Drive {0} needs at least 7 GB free space ({1:N1} GB now).' -f $GameDir.Substring(0, 2), ($free / 1GB)) }

        Step "Copying the game to $GameDir (the Steam install is not changed)"
        robocopy $source $GameDir /E /COPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /MT:8 | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "Copy failed (robocopy $LASTEXITCODE)." }
        Say 'Copied.'

        if ($sourceMd5 -ne $Us10Md5) {
            Step 'Preparing the patch archive'
            $zip = Get-Patches
            $extract = Join-Path $env:TEMP 'CoopAndreas_Patches'
            if (Test-Path -LiteralPath $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            [IO.Compression.ZipFile]::ExtractToDirectory($zip, $extract)
            $manifest = Get-ChildItem -LiteralPath $extract -Recurse -Filter 'manifest.json' | Select-Object -First 1
            if (-not $manifest) { throw 'No manifest.json in the patch archive.' }

            Step 'Downgrading to 1.0 US (this can take a few minutes)'
            Invoke-Downgrade $GameDir $manifest.DirectoryName (Join-Path $Pkg 'tools\xdelta3.exe')
            Remove-Item -LiteralPath $extract -Recurse -Force
            if ($zip -eq (Join-Path $env:TEMP 'CoopAndreas_Patches_v2.zip')) { Remove-Item -LiteralPath $zip -Force }
            Say 'The game is now GTA SA 1.0 US.' 'Green'
        }
    }

    Step 'Installing CoopAndreas'
    $files = Join-Path $Pkg 'files'
    $eax = Join-Path $GameDir 'eax.dll'
    if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'eax_orig.dll'))) {
        # the proxy takes eax.dll's name and forwards to the original
        Rename-Item -LiteralPath $eax -NewName 'eax_orig.dll'
    }
    Copy-Item -LiteralPath (Join-Path $files 'eax.dll') -Destination $eax -Force
    Copy-Item -LiteralPath (Join-Path $files 'CoopAndreasSA.dll') -Destination $GameDir -Force
    foreach ($sub in 'CoopAndreas', 'server') {
        New-Item -ItemType Directory -Force -Path (Join-Path $GameDir $sub) | Out-Null
        Copy-Item -Path (Join-Path $files "$sub\*") -Destination (Join-Path $GameDir $sub) -Force
    }
    # older builds' Discord Rich Presence registered this URL handler; this build doesn't write it any more
    $discordKey = 'HKCU:\Software\Classes\discord-1324128258672951317'
    if (Test-Path -LiteralPath $discordKey) {
        try {
            Remove-Item -LiteralPath $discordKey -Recurse -Force
            Say "Removed the Discord registry key an older build left behind: $discordKey"
        }
        catch { Say "Could not remove the Discord registry key an older build left behind: $($_.Exception.Message)" 'Yellow' }
    }

    Step 'Installing CoopTunnel'
    $tunnelDir = Join-Path $GameDir 'CoopTunnel'
    New-Item -ItemType Directory -Force -Path $tunnelDir | Out-Null
    Copy-Item -Path (Join-Path $Pkg 'CoopTunnel\*') -Destination $tunnelDir -Force

    Step 'Connection settings'
    $userDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'GTA San Andreas User Files'
    $ini = Join-Path $userDir 'coopandreas.ini'
    if (Test-Path -LiteralPath $ini) {
        Say "Keeping your settings: $ini"
    }
    else {
        $nick = Get-Nickname
        New-Item -ItemType Directory -Force -Path $userDir | Out-Null
        $lines = '[config]', "nickname=$nick", 'ip=127.0.0.1', 'port=6767', 'report-crashlogs=false'
        [IO.File]::WriteAllText($ini, ($lines -join "`r`n") + "`r`n", [Text.Encoding]::ASCII)
        Say "Nickname '$nick', server 127.0.0.1:6767 (you can change them in the game menu)."
    }

    Step 'Shortcuts'
    $gameExe = Join-Path $GameDir 'gta_sa.exe'
    New-Shortcut (Join-Path $GameDir 'CoopAndreas.lnk') $gameExe '--coop' $GameDir
    New-Shortcut (Join-Path $GameDir 'CoopAndreas Server.lnk') (Join-Path $GameDir 'server\server.exe') '' (Join-Path $GameDir 'server')
    # with diagnostic logs (--coopdiag): CoopAndreas_diag.log and the other CoopAndreas_*.log files go into the game folder
    New-Shortcut (Join-Path $GameDir 'CoopAndreas (diagnostics).lnk') $gameExe '--coop --coopdiag' $GameDir
    if (-not $NoDesktopShortcuts) {
        $desktop = [Environment]::GetFolderPath('Desktop')
        New-Shortcut (Join-Path $desktop 'CoopAndreas.lnk') $gameExe '--coop' $GameDir
        New-Shortcut (Join-Path $desktop 'CoopAndreas Tunnel - Join.lnk') (Join-Path $tunnelDir '3-Join.bat') '' $tunnelDir
        Say "Added 'CoopAndreas' and 'CoopAndreas Tunnel - Join' to the desktop."
    }
    Remove-LegacyFiles $GameDir $tunnelDir

    Say ''
    Say 'SETUP COMPLETE' 'Green'
    Say "Game: $GameDir"
    Say 'To play: start "CoopAndreas Tunnel - Join" on the desktop first (keep its window open), then "CoopAndreas".'
    Say 'In the game: Start Game > Connect (127.0.0.1 : 6767 is filled in) > New Game. Old saves cannot be loaded.'
    exit 0
}
catch {
    Say ''
    Say "ERROR: $($_.Exception.Message)" 'Red'
    exit 1
}
