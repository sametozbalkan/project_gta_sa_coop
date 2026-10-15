<#
.SYNOPSIS
    Compiles the co-op mission scripts (CoopAndreas\scm\main.txt) into main.scm + script.img with Sanny Builder 4.
.DESCRIPTION
    Runs .tools\SannyBuilder4\sanny.exe --compile in the sa_sbl_coopandreas mode. The result goes to
    CoopAndreas\scm of the checkout this script is in, or to -OutDir. From a linked git worktree (agents)
    it uses the main checkout's .tools, like build.ps1. A compile takes 10-15 s.

    Sanny Builder resolves model names such as #GREENWOO from the IDE files of -GameDir, which it only
    reads. The script keeps these keys in .tools\SannyBuilder4\data\settings.ini: the game folder of the
    mode, update checks off (no network, no self-update that would change the compiler) and the Report and
    IMG-warning dialogs off. It never writes into a game folder: an -OutDir inside one is refused, and
    --compile does not use the mode's copy-directory (<game>\data\script; only the GUI's "Compile + Copy"
    does).

    Sanny Builder runs on a desktop of its own, so no window of it can reach the screen or take the focus
    from a running game; a dialog that stays open there or a run longer than -TimeoutSec gets the process
    killed, and only the sanny.exe this script started is touched. Sanny Builder allows one instance (a
    second one hands its command line to the first), so the script refuses to start while any sanny.exe
    runs and serializes its own runs.

    Sanny Builder's CLI exits with 0 and still writes main.scm/script.img when the source has errors; it
    only records the last error in .tools\SannyBuilder4\compile.log. So the compile goes to a temporary
    folder first and the result is copied to the destination only when all of these hold:
      - no compile.log was written during the run;
      - the scripts fit the game's script space (printed: MAIN = header + main script, and the largest
        mission against the 1.0 limits, the most local variables a mission uses, the largest external
        script in script.img);
      - saves stay loadable: compared with -CompareTo, the parts of main.scm a save depends on are
        unchanged: the global variable chunk, the used-object names, the external script list (names and
        order) and the MAIN code. Mission code, mission offsets and the external scripts' code and sizes may
        change. See docs/mission-porting.md (saves).
.PARAMETER OutDir
    Folder for main.scm and script.img (created if missing). Default: CoopAndreas\scm of this checkout.
.PARAMETER GameDir
    GTA SA 1.0 US folder whose data\*.ide files Sanny Builder reads. Default: C:\Games\GTASA_Coop.
.PARAMETER CompareTo
    The main.scm the players' saves were made with. Default: CoopAndreas/scm/main.scm in the checkout's
    HEAD commit (read with git, so an earlier compile into the checkout doesn't change the baseline).
.PARAMETER SkipSaveCheck
    Skip the save check. Only for a save break the architect has decided on.
.PARAMETER TimeoutSec
    Kill Sanny Builder and fail after this many seconds. Default: 180.
.EXAMPLE
    scripts\compile-scm.ps1 -OutDir $env:TEMP\scm-check
    Compiles into a scratch folder and leaves the committed main.scm / script.img alone.
#>
param(
    [string]$OutDir,
    [string]$GameDir = 'C:\Games\GTASA_Coop',
    [string]$CompareTo,
    [switch]$SkipSaveCheck,
    [ValidateRange(10, 3600)] [int]$TimeoutSec = 180
)

$ErrorActionPreference = 'Stop'
$root   = Split-Path -Parent $PSScriptRoot
$scm    = Join-Path $root 'CoopAndreas\scm'
$source = Join-Path $scm 'main.txt'
$mode   = 'sa_sbl_coopandreas'
if (-not (Test-Path -LiteralPath $source)) { throw "$source not found" }
if (-not $OutDir) { $OutDir = $scm }
$OutDir  = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutDir).TrimEnd('\')
$GameDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($GameDir).TrimEnd('\')
if ($CompareTo) {
    $CompareTo = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CompareTo)
    if (-not (Test-Path -LiteralPath $CompareTo -PathType Leaf)) { throw "-CompareTo $CompareTo not found" }
}

# Script space of gta_sa.exe 1.0: plugin-sdk CTheScripts.h (ScriptSpace[200000], MissionBlock[69000],
# LocalVariablesForCurrentMission[1024]); gta-reversed TheScripts.h (MAIN_SCRIPT_SIZE, MISSION_SCRIPT_SIZE).
# External scripts get a buffer of their own size (gta-reversed StreamedScripts.cpp), so they have no cap.
$mainLimit    = 200000
$missionLimit = 69000
$localsLimit  = 1024

# --- never write into a game folder -------------------------------------------------------------
function Get-GameFolder([string]$Path) {
    # the GTA SA folder (one with gta_sa.exe or the Steam gta-sa.exe) that contains $Path, if any
    for ($dir = $Path; $dir; $dir = Split-Path -Parent $dir) {
        foreach ($exe in 'gta_sa.exe', 'gta-sa.exe') {
            if (Test-Path -LiteralPath (Join-Path $dir $exe)) { return $dir }
        }
    }
}
$gameOfOut = Get-GameFolder $OutDir
if ("$OutDir\".StartsWith("$GameDir\", [StringComparison]::OrdinalIgnoreCase) -or $gameOfOut) {
    throw "-OutDir $OutDir is inside a game folder ($(if ($gameOfOut) { $gameOfOut } else { $GameDir })). Compile into the checkout or a scratch folder; deploy.ps1 installs the scripts."
}

# Sanny Builder reads these through the mode's mode.xml (@game:\data\default.dat lists the first three IDEs)
$gameFiles = 'data\default.dat', 'data\default.ide', 'data\vehicles.ide', 'data\peds.ide', 'data\maps\veh_mods\veh_mods.ide', 'text\american.gxt'
$missing = @($gameFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $GameDir $_)) })
if ($missing) {
    throw "$GameDir lacks $($missing -join ', '). Point -GameDir at a GTA SA 1.0 US folder; without the IDE files Sanny Builder can't resolve model names."
}

# --- Sanny Builder ------------------------------------------------------------------------------
$tools = Join-Path $root '.tools'
if (-not (Test-Path -LiteralPath (Join-Path $tools 'SannyBuilder4\sanny.exe'))) {
    # In a linked git worktree (agents) the git-ignored .tools exists only in the main checkout
    $commonDir = git -C $root rev-parse --path-format=absolute --git-common-dir
    if ($LASTEXITCODE -eq 0) { $tools = Join-Path (Split-Path -Parent $commonDir) '.tools' }
}
$sb    = Join-Path $tools 'SannyBuilder4'
$sanny = Join-Path $sb 'sanny.exe'
if (-not (Test-Path -LiteralPath $sanny)) { throw "Sanny Builder not found at $sanny; see CLAUDE.md (.tools)" }

# The mode in .tools is a copy of CoopAndreas\sdk; a stale copy would compile the co-op opcodes wrongly.
$modeSdk  = Join-Path $root "CoopAndreas\sdk\Sanny Builder 4\data\$mode"
$modeTool = Join-Path $sb "data\$mode"
$stale = @(Get-ChildItem -LiteralPath $modeSdk -Recurse -File | Where-Object {
    $copy = Join-Path $modeTool $_.FullName.Substring($modeSdk.Length + 1)
    -not (Test-Path -LiteralPath $copy) -or
        (Get-FileHash -LiteralPath $copy).Hash -ne (Get-FileHash -LiteralPath $_.FullName).Hash
})
if ($stale) {
    throw "The $mode mode in $modeTool differs from $modeSdk ($($stale.Name -join ', ')). Copy the sdk folder over it, then rerun."
}

function Set-IniValue([Collections.Generic.List[string]]$Lines, [string]$Section, [string]$Key, [string]$Value) {
    $start = -1
    for ($i = 0; $i -lt $Lines.Count; $i++) { if ($Lines[$i].Trim() -eq "[$Section]") { $start = $i; break } }
    if ($start -lt 0) { $Lines.Add("[$Section]"); $Lines.Add("$Key=$Value"); return }
    $end = $Lines.Count
    for ($i = $start + 1; $i -lt $Lines.Count; $i++) { if ($Lines[$i].TrimStart().StartsWith('[')) { $end = $i; break } }
    for ($i = $start + 1; $i -lt $end; $i++) {
        $eq = $Lines[$i].IndexOf('=')
        if ($eq -gt 0 -and $Lines[$i].Substring(0, $eq).Trim() -eq $Key) { $Lines[$i] = "$Key=$Value"; return }
    }
    $Lines.Insert($end, "$Key=$Value")
}

function Get-ScmBudget([string]$Path, [string]$Source) {
    # SA main.scm header (gta-reversed SCMChunks.hpp): chunks that start with a jump (02 00 01 <offset of the
    # next one>) and an index byte. Chunk 2: MAIN size, largest mission size, mission count (u16), exclusive
    # count (u16), most mission locals, mission offsets. Chunk 3: largest external script, count, entries
    # {name[20], offset, size}.
    $b = [IO.File]::ReadAllBytes($Path)
    $seg = @(0)
    for ($i = 0; $i -lt 3; $i++) {
        if ($b[$seg[-1]] -ne 2 -or $b[$seg[-1] + 1] -ne 0 -or $b[$seg[-1] + 2] -ne 1) { throw "$Path has no SA script header" }
        $seg += [BitConverter]::ToInt32($b, $seg[-1] + 3)
    }
    $m = $seg[2] + 8
    $offsets = @(for ($i = 0; $i -lt [BitConverter]::ToUInt16($b, $m + 8); $i++) { [BitConverter]::ToInt32($b, $m + 16 + 4 * $i) })
    $codeEnd = $b.Length
    if ($b.Length -gt 12 -and [Text.Encoding]::ASCII.GetString($b, $b.Length - 8, 8) -eq "__SBFTR`0") {
        $codeEnd = [BitConverter]::ToInt32($b, $b.Length - 12)   # Sanny Builder's extra info block starts here
    }
    $starts = @($offsets | Sort-Object -Unique) + $codeEnd
    $sizeAt = @{}
    for ($i = 0; $i -lt $starts.Count - 1; $i++) { $sizeAt[$starts[$i]] = $starts[$i + 1] - $starts[$i] }
    $largest = -1
    $largestSize = 0
    for ($i = 0; $i -lt $offsets.Count; $i++) {
        if ($sizeAt[$offsets[$i]] -gt $largestSize) { $largest = $i; $largestSize = $sizeAt[$offsets[$i]] }
    }
    $label = Select-String -LiteralPath $Source -Pattern "^DEFINE MISSION $largest AT @(\S+)" |
        ForEach-Object { $_.Matches[0].Groups[1].Value } | Select-Object -First 1
    $s = $seg[3] + 8
    $largestScript = [BitConverter]::ToInt32($b, $s)
    $scriptName = ''
    for ($i = 0; $i -lt [BitConverter]::ToInt32($b, $s + 4); $i++) {
        $e = $s + 8 + 28 * $i
        if ([BitConverter]::ToInt32($b, $e + 24) -eq $largestScript) {
            $scriptName = [Text.Encoding]::ASCII.GetString($b, $e, 20).Split([char]0)[0]
            break
        }
    }
    [pscustomobject]@{
        Main = [BitConverter]::ToInt32($b, $m); Mission = [BitConverter]::ToInt32($b, $m + 4)
        MissionName = "$label (mission $largest)"; Locals = [BitConverter]::ToInt32($b, $m + 12)
        Script = $largestScript; ScriptName = $scriptName
    }
}

function Save-CommittedFile([string]$RepoPath, [string]$Destination) {
    # git show writes the blob's raw bytes; read them as a stream (PowerShell pipes would mangle binary data)
    $psi = New-Object System.Diagnostics.ProcessStartInfo 'git'
    $psi.Arguments = "-C `"$root`" show `"HEAD:$RepoPath`""
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $git = [Diagnostics.Process]::Start($psi)
    $file = [IO.File]::Create($Destination)
    try { $git.StandardOutput.BaseStream.CopyTo($file) } finally { $file.Dispose() }
    $err = $git.StandardError.ReadToEnd()
    $git.WaitForExit()
    if ($git.ExitCode -ne 0) { throw "git show HEAD:$RepoPath failed: $err Pass -CompareTo <main.scm the saves were made with>." }
}

# C# 5 helpers (Add-Type): a process on a desktop of its own, and the save check.
$helperCode = @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

// Starts a program on a desktop of its own: none of its windows, dialogs included, can reach the user's
// screen or take the focus from the game.
public sealed class CompileScmHiddenProcess : IDisposable
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO
    {
        public int cb; public string lpReserved, lpDesktop, lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
    delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateDesktop(string name, IntPtr device, IntPtr devmode, int flags, int access, IntPtr sa);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32.dll")] static extern bool EnumDesktopWindows(IntPtr desktop, EnumProc proc, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc proc, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] static extern int GetWindowThreadProcessId(IntPtr hwnd, out int pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int max);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd, StringBuilder text, int max);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool CreateProcess(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, int flags,
                                     IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll")] static extern int WaitForSingleObject(IntPtr handle, int ms);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr process, out int code);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr process, int code);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);

    IntPtr desktop, process;
    public readonly int Id;

    public CompileScmHiddenProcess(string exe, string args, string cwd)
    {
        string name = "compile-scm-" + Guid.NewGuid().ToString("N").Substring(0, 8);
        desktop = CreateDesktop(name, IntPtr.Zero, IntPtr.Zero, 0, 0x10000000, IntPtr.Zero);   // GENERIC_ALL
        if (desktop == IntPtr.Zero) throw new Win32Exception();
        var si = new STARTUPINFO();
        si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
        si.lpDesktop = name;
        PROCESS_INFORMATION pi;
        if (!CreateProcess(exe, new StringBuilder("\"" + exe + "\" " + args), IntPtr.Zero, IntPtr.Zero, false, 0,
                           IntPtr.Zero, cwd, ref si, out pi))
        {
            var error = new Win32Exception();
            CloseDesktop(desktop);
            throw error;
        }
        CloseHandle(pi.hThread);
        process = pi.hProcess;
        Id = pi.dwProcessId;
    }

    public bool WaitForExit(int ms) { return WaitForSingleObject(process, ms) == 0; }
    public int ExitCode { get { int code; GetExitCodeProcess(process, out code); return code; } }
    public void Kill() { TerminateProcess(process, 1); WaitForSingleObject(process, 10000); }

    // Visible dialogs of the process (Windows message boxes, VCL forms other than the hidden application
    // window; not the input-indicator windows Windows adds to GUI processes), each with its controls' texts
    public string[] Windows()
    {
        var found = new List<string>();
        EnumDesktopWindows(desktop, delegate(IntPtr hwnd, IntPtr unused)
        {
            int owner;
            GetWindowThreadProcessId(hwnd, out owner);
            string cls = ClassOf(hwnd);
            bool dialog = cls == "#32770" || (cls.StartsWith("T") && cls != "TApplication");
            if (owner == Id && dialog && IsWindowVisible(hwnd))
            {
                var line = new StringBuilder(cls + " \"" + TextOf(hwnd) + "\"");
                EnumChildWindows(hwnd, delegate(IntPtr child, IntPtr unused2)
                {
                    string text = TextOf(child);
                    if (text.Length > 0) line.Append(" / " + text);
                    return true;
                }, IntPtr.Zero);
                found.Add(line.ToString());
            }
            return true;
        }, IntPtr.Zero);
        return found.ToArray();
    }

    static string TextOf(IntPtr hwnd) { var s = new StringBuilder(512); GetWindowText(hwnd, s, s.Capacity); return s.ToString(); }
    static string ClassOf(IntPtr hwnd) { var s = new StringBuilder(256); GetClassName(hwnd, s, s.Capacity); return s.ToString(); }

    public void Dispose()
    {
        if (process != IntPtr.Zero) { CloseHandle(process); process = IntPtr.Zero; }
        if (desktop != IntPtr.Zero) { CloseDesktop(desktop); desktop = IntPtr.Zero; }
    }
}

// What a save made with one main.scm needs from the next one. gta-reversed CTheScripts::Save/Load
// (0x5D4C40/0x5D4FD0): the global variable chunk is written and read back as raw bytes; the running scripts
// that are not external are saved with their IPs and gosub stack as offsets into the script space, so the
// MAIN code may not move or change; the brain table (tScriptForBrains) names external scripts by their
// index in the list. The mission table is saved but ignored on load, and external scripts are not saved,
// so mission code, mission offsets and the external scripts' sizes and code may change.
// Chunk layout: gta-reversed SCMChunks.hpp.
public static class CompileScmSaveCheck
{
    static int I32(byte[] b, int at) { return BitConverter.ToInt32(b, at); }

    static string Name(byte[] b, int at, int max)
    {
        int n = 0;
        while (n < max && b[at + n] != 0) n++;
        return Encoding.ASCII.GetString(b, at, n);
    }

    // starts of the six header chunks; [6] is where the MAIN code begins
    static int[] Chunks(byte[] b)
    {
        var at = new int[7];
        for (int i = 0; i < 6; i++)
        {
            if (at[i] < 0 || at[i] + 8 > b.Length || b[at[i]] != 2 || b[at[i] + 1] != 0 || b[at[i] + 2] != 1) return null;
            at[i + 1] = I32(b, at[i] + 3);
        }
        return at;
    }

    // A byte after the NUL of an 8-byte string literal (type 09) where one build has zeros and the other
    // junk: upstream's Sanny Builder 4.1.0 left stale bytes there, 4.2.0 writes zeros. The game reads such
    // a string only up to the NUL.
    static bool IsStringPadding(byte[] a, byte[] b, int i)
    {
        for (int p = i - 1; p >= Math.Max(0, i - 8); p--)
        {
            if (a[p] != 9 || b[p] != 9) continue;
            int nul = -1;
            for (int q = p + 1; q < i; q++) if (a[q] == 0 && b[q] == 0) { nul = q; break; }
            if (nul < 0) continue;
            bool same = true;
            for (int q = p + 1; q < nul; q++) if (a[q] != b[q]) { same = false; break; }
            if (!same) continue;
            bool aZero = true, bZero = true;
            for (int q = nul; q <= p + 8 && q < a.Length && q < b.Length; q++)
            {
                if (a[q] != 0) aZero = false;
                if (b[q] != 0) bZero = false;
            }
            if (aZero || bZero) return true;
        }
        return false;
    }

    // name of the script (script_name, opcode 03A4) whose code contains offset at
    static string ScriptAt(byte[] b, int at, int start)
    {
        for (int p = Math.Min(at, b.Length - 11); p >= start; p--)
            if (b[p] == 0xA4 && b[p + 1] == 0x03 && b[p + 2] == 0x09) return Name(b, p + 3, 8);
        return "?";
    }

    public static List<string> Compare(byte[] old, byte[] cur, out int padding)
    {
        padding = 0;
        var problems = new List<string>();
        int[] a = Chunks(old), b = Chunks(cur);
        if (a == null || b == null) { problems.Add("not a GTA SA main.scm header"); return problems; }

        if (a[1] != b[1])
            problems.Add(string.Format("global variable space: {0} bytes before, {1} now (globals added or removed)", a[1] - 8, b[1] - 8));

        int objA = I32(old, a[1] + 8), objB = I32(cur, b[1] + 8);   // name 0 is the compiler's signature
        var namesA = new List<string>(); var namesB = new List<string>();
        for (int i = 1; i < objA; i++) namesA.Add(Name(old, a[1] + 12 + 24 * i, 24));
        for (int i = 1; i < objB; i++) namesB.Add(Name(cur, b[1] + 12 + 24 * i, 24));
        if (string.Join(",", namesA) != string.Join(",", namesB))
        {
            var added = namesB.FindAll(delegate(string n) { return !namesA.Contains(n); });
            var gone = namesA.FindAll(delegate(string n) { return !namesB.Contains(n); });
            problems.Add(string.Format("used objects: {0} before, {1} now; added: {2}; removed: {3}", objA, objB,
                added.Count > 0 ? string.Join(" ", added.ToArray()) : "none", gone.Count > 0 ? string.Join(" ", gone.ToArray()) : "none"));
        }

        int mainA = I32(old, a[2] + 8), mainB = I32(cur, b[2] + 8);
        int missA = BitConverter.ToUInt16(old, a[2] + 16), missB = BitConverter.ToUInt16(cur, b[2] + 16);
        if (missA != missB) problems.Add(string.Format("mission count: {0} before, {1} now", missA, missB));
        if (mainA != mainB)
            problems.Add(string.Format("MAIN size: {0} bytes before, {1} now (code added to or removed from MAIN or the scripts included before the first mission)", mainA, mainB));

        int extA = I32(old, a[3] + 12), extB = I32(cur, b[3] + 12);
        var extNamesA = new List<string>(); var extNamesB = new List<string>();
        for (int i = 0; i < extA; i++) extNamesA.Add(Name(old, a[3] + 16 + 28 * i, 20));
        for (int i = 0; i < extB; i++) extNamesB.Add(Name(cur, b[3] + 16 + 28 * i, 20));
        if (string.Join(",", extNamesA) != string.Join(",", extNamesB))
            problems.Add(string.Format("external script list (script.img): {0} before, {1} now; names or order differ", extA, extB));

        for (int i = 1; i <= 6; i++)
            if (a[i] != b[i])
            {
                if (i > 1) problems.Add("header layout differs (chunk " + i + " moved)");
                return problems;   // everything after it moved; a byte compare would only repeat that
            }

        // Byte compare of [0, MAIN size): everything except the fields that may change
        int end = Math.Min(mainA, mainB);
        var skip = new bool[end];
        Action<int, int> mask = delegate(int from, int count) { for (int i = from; i < from + count && i < end; i++) skip[i] = true; };
        mask(a[1] + 12, 24 * objA);                         // object names, compared as strings above
        mask(a[2] + 12, 4);                                 // largest mission size
        mask(a[2] + 20, 4 + 4 * missA);                     // most mission locals, mission offsets
        mask(a[3] + 8, 4);                                  // largest external script
        mask(a[3] + 16, 28 * extA);                         // external script names, offsets, sizes
        int differences = 0;
        for (int i = 0; i < end; i++)
        {
            if (skip[i] || old[i] == cur[i]) continue;
            if (IsStringPadding(old, cur, i)) { padding++; continue; }
            if (++differences <= 3)
            {
                string where = i < a[1] ? "in the global variable chunk" : i < a[6] ? "in the header" : "in the MAIN code, script " + ScriptAt(cur, i, a[6]);
                problems.Add(string.Format("byte 0x{0:X} differs {1}", i, where));
            }
        }
        if (differences > 3) problems.Add(string.Format("{0} differing bytes before the first mission in all", differences));
        return problems;
    }
}
'@
if (-not ('CompileScmSaveCheck' -as [type])) { Add-Type -TypeDefinition $helperCode }

# --- run ----------------------------------------------------------------------------------------
$mutex = New-Object System.Threading.Mutex($false, 'Local\gtasa-coop-compile-scm')
$owned = $false
$proc  = $null
$stage = Join-Path ([IO.Path]::GetTempPath()) ('compile-scm-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
try {
    try { $owned = $mutex.WaitOne([TimeSpan]::FromSeconds($TimeoutSec)) }
    catch [System.Threading.AbandonedMutexException] { $owned = $true }
    if (-not $owned) { throw "Another compile-scm.ps1 run did not finish within $TimeoutSec s" }

    $running = @(Get-Process -Name sanny -ErrorAction SilentlyContinue)
    if ($running) {
        throw "Sanny Builder is already running (PID $($running.Id -join ', ')). Close it first: a second sanny.exe hands its command line to the open one instead of compiling."
    }

    # settings.ini is ANSI (Delphi TIniFile); Sanny Builder fills in its other defaults when it exits
    $ini = Join-Path $sb 'data\settings.ini'
    $lines = New-Object 'System.Collections.Generic.List[string]'
    if (Test-Path -LiteralPath $ini) { $lines.AddRange([IO.File]::ReadAllLines($ini, [Text.Encoding]::Default)) }
    $before = $lines -join "`n"
    Set-IniValue $lines 'Main' 'AutoCheckForUpdates' '0'
    Set-IniValue $lines 'Main' 'AutoInstallUpdates' '0'
    Set-IniValue $lines 'Main' 'Editor::AutoUpdateSBL' '0'
    Set-IniValue $lines 'Main' 'Editor::ShowReport' '0'          # modal "Report" after a decompile
    Set-IniValue $lines 'Main' 'Compiler::ShowIMGWarning' '0'
    Set-IniValue $lines 'Formats' 'N3' '$dir\script.img'          # script.img next to the output main.scm
    Set-IniValue $lines $mode 'GamePath' $GameDir
    if (($lines -join "`n") -ne $before) { [IO.File]::WriteAllLines($ini, $lines, [Text.Encoding]::Default) }

    New-Item -ItemType Directory -Path $stage | Out-Null
    if (-not $SkipSaveCheck -and -not $CompareTo) {
        $CompareTo = Join-Path $stage 'HEAD-main.scm'
        Save-CommittedFile 'CoopAndreas/scm/main.scm' $CompareTo
        $baseline = 'the main.scm committed in HEAD'
    }
    else { $baseline = $CompareTo }

    $log = Join-Path $sb 'compile.log'   # rewritten on every error, untouched on success
    $logBefore = if (Test-Path -LiteralPath $log) { (Get-Item -LiteralPath $log).LastWriteTimeUtc.Ticks } else { 0 }
    $guardFiles = 'data\script\main.scm', 'data\script\script.img' | ForEach-Object { Join-Path $GameDir $_ }
    $guardBefore = $guardFiles | ForEach-Object { if (Test-Path -LiteralPath $_) { (Get-Item -LiteralPath $_).LastWriteTimeUtc.Ticks } else { 0 } }

    # -o options apply to this run only. AddExtraInfo/StringCase change the output (upstream's main.scm
    # carries the extra info block); the checks only decide what is an error.
    $argList = @(
        '--no-splash', '--mode', $mode,
        '-o', 'Compiler::AddExtraInfo', '1', '-o', 'Compiler::StringCase', '0',
        '-o', 'Compiler::CheckLocalVariables', '1', '-o', 'Compiler::StrictIfValidation', '0',
        '-o', 'Editor::ShowReport', '0', '-o', 'Compiler::ShowIMGWarning', '0',
        '--compile', "`"$source`"", "`"$(Join-Path $stage 'main.scm')`""
    )
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $proc = [CompileScmHiddenProcess]::new($sanny, ($argList -join ' '), $sb)
    $dialog = ''
    $dialogSince = -1
    while (-not $proc.WaitForExit(250)) {
        $windows = $proc.Windows()
        if ($windows.Count -eq 0) { $dialogSince = -1 }
        elseif ($dialogSince -lt 0) { $dialogSince = $watch.Elapsed.TotalSeconds }
        elseif ($watch.Elapsed.TotalSeconds - $dialogSince -ge 2) { $dialog = $windows -join '; '; break }
        if ($watch.Elapsed.TotalSeconds -ge $TimeoutSec) { break }
    }
    if (-not $proc.WaitForExit(0)) {
        $proc.Kill()
        if ($dialog) {
            throw "Sanny Builder stopped at a window nobody can see (it runs on a desktop of its own): $dialog. Killed PID $($proc.Id); compile in the Sanny Builder GUI to see it."
        }
        throw "Sanny Builder did not finish within $TimeoutSec s; killed PID $($proc.Id)."
    }
    $seconds = $watch.Elapsed.TotalSeconds

    $guardAfter = $guardFiles | ForEach-Object { if (Test-Path -LiteralPath $_) { (Get-Item -LiteralPath $_).LastWriteTimeUtc.Ticks } else { 0 } }
    if (($guardAfter -join ',') -ne ($guardBefore -join ',')) {
        throw "$GameDir\data\script changed during the compile. Restore the vanilla main.scm/script.img there (from the Steam copy downgraded the same way)."
    }
    $logAfter = if (Test-Path -LiteralPath $log) { (Get-Item -LiteralPath $log).LastWriteTimeUtc.Ticks } else { 0 }
    if ($logAfter -ne $logBefore) {
        $text = (Get-Content -LiteralPath $log) -join [Environment]::NewLine
        throw "Sanny Builder reported an error ($log keeps only the last one; fix it and rerun):$([Environment]::NewLine)$text"
    }
    if ($proc.ExitCode -ne 0) { throw "Sanny Builder exited with code $($proc.ExitCode)" }
    foreach ($name in 'main.scm', 'script.img') {
        $file = Join-Path $stage $name
        if (-not (Test-Path -LiteralPath $file) -or (Get-Item -LiteralPath $file).Length -eq 0) {
            throw "Sanny Builder wrote no $name (it exited after $([int]$seconds) s without an error in compile.log)"
        }
    }

    $budget = Get-ScmBudget (Join-Path $stage 'main.scm') $source
    'Compiled {0} in {1:n1} s' -f $source, $seconds
    'MAIN (header + main script) {0,6} of {1} bytes, {2} left' -f $budget.Main, $mainLimit, ($mainLimit - $budget.Main)
    'largest mission             {0,6} of {1} bytes, {2} left: {3}' -f $budget.Mission, $missionLimit, ($missionLimit - $budget.Mission), $budget.MissionName
    'most mission locals         {0,6} of {1}' -f $budget.Locals, $localsLimit
    'largest external script     {0,6} bytes: {1}' -f $budget.Script, $budget.ScriptName
    if ($budget.Main -gt $mainLimit -or $budget.Mission -gt $missionLimit -or $budget.Locals -gt $localsLimit) {
        throw 'The scripts exceed the game''s script space (see above); nothing was copied. See docs/mission-porting.md (size budgets).'
    }

    if ($SkipSaveCheck) { 'Save check skipped (-SkipSaveCheck): saves made with the previous main.scm may not load.' }
    else {
        $padding = 0
        $problems = [CompileScmSaveCheck]::Compare([IO.File]::ReadAllBytes($CompareTo), [IO.File]::ReadAllBytes((Join-Path $stage 'main.scm')), [ref]$padding)
        if ($problems.Count) {
            throw ("Saves made with $baseline would not load with this build; nothing was copied:" +
                [Environment]::NewLine + '  - ' + ($problems -join ([Environment]::NewLine + '  - ')) + [Environment]::NewLine +
                'A port must not add global variables or change MAIN or the external script list (docs/mission-porting.md, saves). ' +
                'Stop and report to the architect; only a decided save break is compiled with -SkipSaveCheck.')
        }
        $note = if ($padding) { "; $padding bytes of string padding differ, which the game never reads" } else { '' }
        "Save check against ${baseline}: passed (globals, used objects, external script list and MAIN code unchanged$note)"
    }

    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    "Wrote $OutDir"
    foreach ($name in 'main.scm', 'script.img') {
        $from = Join-Path $stage $name
        $to   = Join-Path $OutDir $name
        $hash = (Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash
        $state = 'new'
        if (Test-Path -LiteralPath $to) {
            $state = if ((Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash -eq $hash) { 'unchanged' } else { 'changed' }
        }
        Copy-Item -LiteralPath $from -Destination $to -Force
        '  {0,-10} {1,8} bytes  SHA-256 {2}  {3}' -f $name, (Get-Item -LiteralPath $to).Length, $hash, $state
    }
}
finally {
    if ($proc) {
        if (-not $proc.WaitForExit(0)) { $proc.Kill() }
        $proc.Dispose()
    }
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    if ($owned) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
