<#
.SYNOPSIS
    CoopAndreas kurulumu: Steam'deki GTA San Andreas'ın bir kopyasını 1.0 US'e düşürür,
    CoopAndreas'ı, CoopTunnel'ı ve kısayolları kurar.
.DESCRIPTION
    Steam kurulumuna dokunulmaz; oyun -GameDir klasörüne kopyalanır. Yamalar gtasa-open-downgrader'ın
    Patches_v2.zip arşivinden gelir (SHA-256 sabit) ve resmi xdelta3 ile uygulanır; her dosya manifest'teki
    MD5 ile doğrulanır. -GameDir'de zaten 1.0 US varsa oyun kopyalanmaz, yalnızca mod dosyaları güncellenir.
    Windows PowerShell 5.1 ile çalışır.
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
    'd9cb35c898d3298ca904a63e10ee18d7' = 'Steam (NewSteam R2, Almanca)'
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
    if (-not $steam) { throw "Steam bulunamadı. Oyun klasörünü elle ver: Kur.bat -SteamGameDir `"...\Grand Theft Auto San Andreas`"" }
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
    throw "Steam kütüphanelerinde GTA San Andreas bulunamadı. Oyun Steam'de kurulu mu? Kuruluysa klasörü elle ver: -SteamGameDir"
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
    if (-not $nick) { $nick = 'Oyuncu' }
    if ($nick.Length -gt 24) { $nick = $nick.Substring(0, 24) }
    return $nick
}

function Get-Patches {
    $candidates = @($PatchesZip, (Join-Path $Pkg 'Patches_v2.zip'), (Join-Path $env:TEMP 'CoopAndreas_Patches_v2.zip')) | Where-Object { $_ }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            Say "Yama arşivi doğrulanıyor: $candidate"
            if ((Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash -eq $PatchesSha256) { return $candidate }
            Say '  SHA-256 tutmadı, bu dosya kullanılmayacak.' 'Yellow'
        }
    }
    $target = Join-Path $env:TEMP 'CoopAndreas_Patches_v2.zip'
    Say "Yamalar indiriliyor (~880 MB): $PatchesUrl"
    & curl.exe -L --fail --retry 3 -o $target $PatchesUrl
    if ($LASTEXITCODE -ne 0) { throw "Yamalar indirilemedi (curl $LASTEXITCODE). İnternet bağlantını kontrol edip Kur.bat'ı tekrar çalıştır." }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $PatchesSha256) {
        throw 'İndirilen yama arşivi beklenen dosya değil (SHA-256 tutmadı); kurulum durduruldu.'
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
            if ((Get-Md5 $payload) -ne $entry.target_hash) { throw "Yama arşivindeki $payloadName beklenen dosya değil." }
            $plan += , @($entry, $path, $payload)
        }
        else {
            $source = $entry.sources | Where-Object { $_.hash -eq $have } | Select-Object -First 1
            if (-not $source) { throw "$($entry.path) beklenmeyen bir sürüm (MD5 $have). Hiçbir dosya değiştirilmedi." }
            $patch = Join-Path $PatchDir ($source.patch -replace '/', '\')
            if (-not (Test-Path -LiteralPath $patch)) { throw "Yama dosyası eksik: $($source.patch)" }
            $plan += , @($entry, $path, $patch)
        }
    }
    Say "Kontrol tamam, $($plan.Count) dosya değiştirilecek."

    foreach ($item in $plan) {
        $entry, $path, $from = $item
        $tmp = "$path.tmp"
        if ($entry.action -eq 'copy') {
            Copy-Item -LiteralPath $from -Destination $tmp -Force
        }
        else {
            & $Xdelta -d -f -s $path $from $tmp
            if ($LASTEXITCODE -ne 0) { throw "xdelta3 hatası: $($entry.path)" }
        }
        if ((Get-Md5 $tmp) -ne $entry.target_hash) {
            Remove-Item -LiteralPath $tmp -Force
            throw "Doğrulama başarısız: $($entry.path)"
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
        if ((Get-Md5 (Resolve-GamePath $Dir $entry.path)) -ne $entry.target_hash) { throw "Son kontrolde tutmayan dosya: $($entry.path)" }
    }
}

function New-Shortcut([string]$Path, [string]$Target, [string]$Arguments, [string]$WorkingDir) {
    $link = (New-Object -ComObject WScript.Shell).CreateShortcut($Path)
    $link.TargetPath = $Target
    $link.Arguments = $Arguments
    $link.WorkingDirectory = $WorkingDir
    $link.Save()
}

try {
    Say 'CoopAndreas kurulumu' 'White'
    $required = 'files\CoopAndreasSA.dll', 'files\eax.dll', 'files\CoopAndreas\main.scm', 'files\CoopAndreas\script.img',
                'files\server\server.exe', 'tools\xdelta3.exe', 'CoopTunnel\CoopTunnel.exe', 'CoopTunnel\steam_api64.dll'
    foreach ($file in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $Pkg $file))) { throw "Paket eksik: $file. Zip'in tamamını bir klasöre çıkarıp oradan çalıştır." }
    }
    if (Get-Process -Name 'gta_sa', 'gta-sa' -ErrorAction SilentlyContinue) { throw 'GTA San Andreas açık. Oyunu kapatıp Kur.bat''ı tekrar çalıştır.' }

    $exe = Get-GameExe $GameDir
    if ($exe -and (Get-Md5 $exe) -eq $Us10Md5) {
        Step "$GameDir zaten GTA SA 1.0 US; oyun kopyalanmayacak, mod dosyaları güncellenecek."
    }
    else {
        if ((Test-Path -LiteralPath $GameDir) -and (Get-ChildItem -LiteralPath $GameDir -Force | Select-Object -First 1)) {
            throw "$GameDir boş değil ve içinde 1.0 US kurulum yok. Klasörü boşalt ya da başka bir klasör seç: Kur.bat -GameDir D:\GTASA_Coop"
        }
        Step 'Steam kurulumu aranıyor'
        $source = $SteamGameDir
        if (-not $source) { $source = Find-SteamGame }
        $sourceExe = Get-GameExe $source
        if (-not $sourceExe) { throw "$source içinde gta-sa.exe yok." }
        $sourceMd5 = Get-Md5 $sourceExe
        if (-not $SupportedExe.ContainsKey($sourceMd5)) {
            throw "Bu GTA SA sürümü desteklenmiyor (gta-sa.exe MD5 $sourceMd5). Yamalar Steam'in güncel (NewSteam R2) sürümü için."
        }
        Say "Bulundu: $source ($($SupportedExe[$sourceMd5]))"

        $free = (Get-PSDrive -Name $GameDir.Substring(0, 1)).Free
        if ($free -lt 7GB) { throw ('{0} sürücüsünde en az 7 GB boş alan gerekiyor (şu an {1:N1} GB).' -f $GameDir.Substring(0, 2), ($free / 1GB)) }

        Step "Oyun kopyalanıyor: $GameDir (Steam kurulumuna dokunulmaz)"
        robocopy $source $GameDir /E /COPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /MT:8 | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "Kopyalama başarısız (robocopy $LASTEXITCODE)." }
        Say 'Kopyalandı.'

        if ($sourceMd5 -ne $Us10Md5) {
            Step 'Yama arşivi hazırlanıyor'
            $zip = Get-Patches
            $extract = Join-Path $env:TEMP 'CoopAndreas_Patches'
            if (Test-Path -LiteralPath $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            [IO.Compression.ZipFile]::ExtractToDirectory($zip, $extract)
            $manifest = Get-ChildItem -LiteralPath $extract -Recurse -Filter 'manifest.json' | Select-Object -First 1
            if (-not $manifest) { throw 'Yama arşivinde manifest.json yok.' }

            Step "1.0 US'e düşürülüyor (birkaç dakika sürebilir)"
            Invoke-Downgrade $GameDir $manifest.DirectoryName (Join-Path $Pkg 'tools\xdelta3.exe')
            Remove-Item -LiteralPath $extract -Recurse -Force
            if ($zip -eq (Join-Path $env:TEMP 'CoopAndreas_Patches_v2.zip')) { Remove-Item -LiteralPath $zip -Force }
            Say 'Oyun artık GTA SA 1.0 US.' 'Green'
        }
    }

    Step 'CoopAndreas kuruluyor'
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
            Say "Eski sürümden kalan Discord kayıt defteri anahtarı silindi: $discordKey"
        }
        catch { Say "Eski sürümden kalan Discord kayıt defteri anahtarı silinemedi: $($_.Exception.Message)" 'Yellow' }
    }

    Step 'CoopTunnel kuruluyor'
    $tunnelDir = Join-Path $GameDir 'CoopTunnel'
    New-Item -ItemType Directory -Force -Path $tunnelDir | Out-Null
    Copy-Item -Path (Join-Path $Pkg 'CoopTunnel\*') -Destination $tunnelDir -Force

    Step 'Bağlantı ayarları'
    $userDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'GTA San Andreas User Files'
    $ini = Join-Path $userDir 'coopandreas.ini'
    if (Test-Path -LiteralPath $ini) {
        Say "Mevcut ayar korunuyor: $ini"
    }
    else {
        $nick = Get-Nickname
        New-Item -ItemType Directory -Force -Path $userDir | Out-Null
        $lines = '[config]', "nickname=$nick", 'ip=127.0.0.1', 'port=6767', 'report-crashlogs=false'
        [IO.File]::WriteAllText($ini, ($lines -join "`r`n") + "`r`n", [Text.Encoding]::ASCII)
        Say "Takma ad '$nick', sunucu 127.0.0.1:6767 olarak ayarlandı (oyun menüsünden değiştirilebilir)."
    }

    Step 'Kısayollar'
    $gameExe = Join-Path $GameDir 'gta_sa.exe'
    New-Shortcut (Join-Path $GameDir 'CoopAndreas.lnk') $gameExe '--coop' $GameDir
    New-Shortcut (Join-Path $GameDir 'CoopAndreas Server.lnk') (Join-Path $GameDir 'server\server.exe') '' (Join-Path $GameDir 'server')
    if (-not $NoDesktopShortcuts) {
        $desktop = [Environment]::GetFolderPath('Desktop')
        New-Shortcut (Join-Path $desktop 'CoopAndreas.lnk') $gameExe '--coop' $GameDir
        New-Shortcut (Join-Path $desktop 'CoopAndreas Tünel - Bağlan.lnk') (Join-Path $tunnelDir '3-Oyuna-baglan.bat') '' $tunnelDir
        Say "Masaüstüne 'CoopAndreas' ve 'CoopAndreas Tünel - Bağlan' eklendi."
    }

    Say ''
    Say 'KURULUM TAMAM' 'Green'
    Say "Oyun: $GameDir"
    Say 'Oynamak için: önce masaüstündeki "CoopAndreas Tünel - Bağlan" (pencere açık kalsın), sonra "CoopAndreas".'
    Say 'Oyunda: Start Game > Connect (127.0.0.1 : 6767 hazır) > New Game. Eski kayıtlar yüklenemez.'
    exit 0
}
catch {
    Say ''
    Say "HATA: $($_.Exception.Message)" 'Red'
    exit 1
}
