"""Sample where a running 32-bit process's main thread is (read-only), and name the addresses.

usage: sample_main_thread.py <pid> <plugin-sdk game_sa dir> [samples]

The main thread is suspended for a moment per sample to read EIP and the top of its stack, then resumed.
Return addresses on the stack are recognised by the call instruction just before them.
"""
import bisect
import collections
import ctypes
import ctypes.wintypes as wt
import glob
import re
import sys
import time

pid, sdk_dir = int(sys.argv[1]), sys.argv[2]
samples = int(sys.argv[3]) if len(sys.argv) > 3 else 30
k32 = ctypes.WinDLL("kernel32", use_last_error=True)


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", wt.LONG), ("tpDeltaPri", wt.LONG),
                ("dwFlags", wt.DWORD)]


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("th32ModuleID", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("GlblcntUsage", wt.DWORD), ("ProccntUsage", wt.DWORD), ("modBaseAddr", ctypes.c_void_p),
                ("modBaseSize", wt.DWORD), ("hModule", ctypes.c_void_p), ("szModule", ctypes.c_wchar * 256),
                ("szExePath", ctypes.c_wchar * 260)]


class WOW64_FLOATING_SAVE_AREA(ctypes.Structure):
    _fields_ = [(n, wt.DWORD) for n in ("ControlWord", "StatusWord", "TagWord", "ErrorOffset", "ErrorSelector",
                                         "DataOffset", "DataSelector")] + \
               [("RegisterArea", ctypes.c_ubyte * 80), ("Cr0NpxState", wt.DWORD)]


class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [("ContextFlags", wt.DWORD)] + [(n, wt.DWORD) for n in ("Dr0", "Dr1", "Dr2", "Dr3", "Dr6", "Dr7")] + \
               [("FloatSave", WOW64_FLOATING_SAVE_AREA)] + \
               [(n, wt.DWORD) for n in ("SegGs", "SegFs", "SegEs", "SegDs", "Edi", "Esi", "Ebx", "Edx", "Ecx", "Eax",
                                        "Ebp", "Eip", "SegCs", "EFlags", "Esp", "SegSs")] + \
               [("ExtendedRegisters", ctypes.c_ubyte * 512)]


k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
k32.OpenThread.restype = wt.HANDLE
k32.OpenProcess.restype = wt.HANDLE
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]

# --- modules of the target
modules = []
snap = k32.CreateToolhelp32Snapshot(0x8 | 0x10, pid)
me = MODULEENTRY32W()
me.dwSize = ctypes.sizeof(me)
ok = k32.Module32FirstW(snap, ctypes.byref(me))
while ok:
    modules.append((me.modBaseAddr, me.modBaseAddr + me.modBaseSize, me.szModule))
    ok = k32.Module32NextW(snap, ctypes.byref(me))
k32.CloseHandle(snap)


def module_of(addr):
    for lo, hi, name in modules:
        if lo <= addr < hi:
            return lo, name
    return None, None


# --- plugin-sdk symbols for gta_sa.exe 1.0 US
symbols = {}
pattern = re.compile(r"g?addrof(?:_o)?\(([^)]+?)\)\s*=\s*(?:GLOBAL_)?ADDRESS_BY_VERSION\(0x([0-9A-Fa-f]+)")
for path in glob.glob(sdk_dir + "/*.cpp"):
    for name, addr in pattern.findall(open(path, encoding="utf-8", errors="replace").read()):
        symbols.setdefault(int(addr, 16), name.split(",")[0].strip())
sym_addrs = sorted(symbols)


def name_of(addr):
    base, mod = module_of(addr)
    if mod and mod.lower() != "gta_sa.exe":
        return f"{mod}+0x{addr - base:X}"
    i = bisect.bisect_right(sym_addrs, addr) - 1
    if i >= 0 and addr - sym_addrs[i] < 0x1000:
        return f"{symbols[sym_addrs[i]]}+0x{addr - sym_addrs[i]:X}"
    return f"gta_sa.exe:0x{addr:X}"


# --- main thread = earliest created thread of the process
snap = k32.CreateToolhelp32Snapshot(0x4, 0)
te = THREADENTRY32()
te.dwSize = ctypes.sizeof(te)
ok = k32.Thread32First(snap, ctypes.byref(te))
best = None
while ok:
    if te.th32OwnerProcessID == pid:
        h = k32.OpenThread(0x0800, False, te.th32ThreadID)  # THREAD_QUERY_LIMITED_INFORMATION
        if h:
            times = [wt.FILETIME() for _ in range(4)]
            k32.GetThreadTimes(h, *[ctypes.byref(t) for t in times])
            created = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
            if best is None or created < best[0]:
                best = (created, te.th32ThreadID)
            k32.CloseHandle(h)
    ok = k32.Thread32Next(snap, ctypes.byref(te))
k32.CloseHandle(snap)
tid = best[1]
hthread = k32.OpenThread(0x0002 | 0x0008 | 0x0040, False, tid)
hproc = k32.OpenProcess(0x0010 | 0x0400, False, pid)
if not hthread or not hproc:
    sys.exit(f"cannot open thread/process (error {ctypes.get_last_error()})")


def read(addr, size):
    buf = (ctypes.c_ubyte * size)()
    got = ctypes.c_size_t()
    if not k32.ReadProcessMemory(hproc, ctypes.c_void_p(addr), buf, size, ctypes.byref(got)):
        return b""
    return bytes(buf[:got.value])


def is_return_address(addr):
    before = read(addr - 6, 6)
    if len(before) != 6:
        return False
    if before[1] == 0xE8:  # call rel32
        return True
    if before[4] == 0xFF and (before[5] & 0x38) == 0x10:  # call reg
        return True
    if before[3] == 0xFF and (before[4] & 0x38) == 0x10:  # call [reg+disp8]
        return True
    if before[0] == 0xFF and (before[1] & 0x38) == 0x10:  # call [disp32] / [reg+disp32]
        return True
    return False


eips = collections.Counter()
last_stack = []
for _ in range(samples):
    ctx = WOW64_CONTEXT()
    ctx.ContextFlags = 0x00010000 | 0x1 | 0x2
    k32.Wow64SuspendThread(hthread)
    try:
        k32.Wow64GetThreadContext(hthread, ctypes.byref(ctx))
        stack = read(ctx.Esp, 4096)
    finally:
        k32.ResumeThread(hthread)
    eips[ctx.Eip] += 1
    last_stack = [int.from_bytes(stack[i:i + 4], "little") for i in range(0, len(stack) - 3, 4)]
    time.sleep(0.03)

print(f"main thread {tid}, {samples} samples")
print("EIP histogram:")
for eip, n in eips.most_common(12):
    print(f"  {n:3}x 0x{eip:08X}  {name_of(eip)}")
print("call stack (return addresses found on the stack, innermost first):")
shown = 0
for value in last_stack:
    if module_of(value)[1] and is_return_address(value):
        print(f"  0x{value:08X}  {name_of(value)}")
        shown += 1
        if shown >= 30:
            break
