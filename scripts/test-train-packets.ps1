<#
.SYNOPSIS
    Builds real train, mission mirror blips and mission object packet tests with the vendored SDK, then exchanges
    client/server wire fixtures.
.DESCRIPTION
    Run from Windows PowerShell 5.1 or newer: powershell -File scripts/test-train-packets.ps1
    Requires the MSVC x86 C++ tools used by scripts/build.ps1. All outputs stay in ignored build/.
    No game, ENet, SDK library, or .tools dependency is loaded or modified.
#>
param([string]$VcVarsAll = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $VcVarsAll) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -eq 0 -and $installation) {
            $VcVarsAll = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
        }
    }
}
if (-not (Test-Path -LiteralPath $VcVarsAll -PathType Leaf)) {
    throw 'MSVC x86 tools not found. Pass -VcVarsAll with the absolute path to vcvarsall.bat.'
}

$build = Join-Path $root 'CoopAndreas\build\tests\train-packets'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$includeDirs = @(
    'CoopAndreas\third_party',
    'CoopAndreas\third_party\plugin-sdk\shared',
    'CoopAndreas\third_party\plugin-sdk\shared\game',
    'CoopAndreas\third_party\plugin-sdk\shared\dxsdk',
    'CoopAndreas\third_party\plugin-sdk\plugin_sa',
    'CoopAndreas\third_party\plugin-sdk\plugin_sa\game_sa'
)
$includes = '/I"' + (Join-Path $root 'CoopAndreas\shared') + '" /external:W0 '
foreach ($includeDir in $includeDirs) {
    $includes += '/external:I"' + (Join-Path $root $includeDir) + '" '
}
foreach ($test in @('train_packet', 'mirror_blips_packet', 'mirror_objects_packet')) {
$source = Join-Path $root ('CoopAndreas\tests\network\' + $test + '_test.cpp')
foreach ($mode in @('debug', 'release')) {
    $flags = '/Od /MTd /D_DEBUG'
    if ($mode -eq 'release') { $flags = '/O2 /MT /DNDEBUG' }
    foreach ($role in @('client', 'server')) {
        $name = $test + '_' + $role + '_' + $mode
        $exe = Join-Path $build ($name + '.exe')
        $obj = Join-Path $build ($name + '.obj')
        $cmd = Join-Path $build ($name + '.cmd')
        $log = Join-Path $build ($name + '.compile.log')
        $command = 'cl /nologo /std:c++17 /EHsc /W4 /WX /DCOOP_' + $role.ToUpperInvariant() +
            ' /DNOMINMAX /DGTASA /DPLUGIN_SGV_10US /DRW /D_USE_MATH_DEFINES /D_CRT_SECURE_NO_WARNINGS ' +
            $flags + ' ' + $includes + '/Fo"' + $obj + '" /Fe"' + $exe + '" "' + $source + '"'
        $batch = "@echo off`r`ncall `"$VcVarsAll`" x64_x86 >nul`r`nif errorlevel 1 exit /b 1`r`n$command`r`nexit /b %errorlevel%`r`n"
        [IO.File]::WriteAllText($cmd, $batch, [Text.Encoding]::ASCII)
        & $env:ComSpec /d /c $cmd *> $log
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $log | Write-Host
            throw "Compilation failed: $name"
        }
        & $exe --write (Join-Path $build ($mode + '-' + $role))
        if ($LASTEXITCODE -ne 0) { throw "Fixture write failed: $name" }
    }
    & (Join-Path $build ($test + '_client_' + $mode + '.exe')) --read (Join-Path $build ($mode + '-server'))
    if ($LASTEXITCODE -ne 0) { throw "S2C decode failed: $test $mode" }
    & (Join-Path $build ($test + '_server_' + $mode + '.exe')) --read (Join-Path $build ($mode + '-client'))
    if ($LASTEXITCODE -ne 0) { throw "C2S decode failed: $test $mode" }
}
}
Write-Host 'All train, mirror blips and mirror objects packet tests passed (Debug and Release, C2S and S2C).'
