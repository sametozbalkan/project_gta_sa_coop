"""Check every address CoopAndreas patches with RedirectCall / Nop(addr, 5) against an exe on disk.

A RedirectCall or a 5-byte Nop only makes sense on a `call rel32` (E8) or `jmp rel32` (E9). Anything else
in the given exe means the hook would corrupt code there.

usage: audit_hooks.py <gta_sa.exe> <client/src dir>
"""
import os
import re
import struct
import sys

exe_path, src_dir = sys.argv[1], sys.argv[2]
data = open(exe_path, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
optsz = struct.unpack_from("<H", data, pe + 20)[0]
base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
sections = []
for i in range(nsec):
    off = pe + 24 + optsz + i * 40
    vsize, rva, rawsize, rawptr = struct.unpack_from("<IIII", data, off + 8)
    sections.append((rva, max(vsize, rawsize), rawptr, rawsize))


def byte_at(va):
    rva = va - base
    for srva, span, rawptr, rawsize in sections:
        if srva <= rva < srva + span and rva - srva < rawsize:
            return data[rawptr + rva - srva]
    return None


HEX = re.compile(r"0x[0-9A-Fa-f]{6,7}")
sites = {}  # va -> (kind, file:line)
for root, _, files in os.walk(src_dir):
    for name in files:
        if not name.endswith((".cpp", ".h")):
            continue
        path = os.path.join(root, name)
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
        array_mode = False
        for no, line in enumerate(lines, 1):
            code = line.split("//")[0]
            if re.search(r"(Addresses|Positions|hookPositions)\s*(\[\])?\s*=", code):
                array_mode = True
            kind = None
            if "RedirectCall" in code or array_mode:
                kind = "call"
            elif re.search(r"patch::Nop\(\s*0x[0-9A-Fa-f]+\s*,\s*5\s*\)", code):
                kind = "nop5"
            if kind:
                for h in HEX.findall(code):
                    va = int(h, 16)
                    if 0x401000 <= va < 0x857000:
                        sites.setdefault(va, (kind, f"{os.path.relpath(path, src_dir)}:{no}"))
            if array_mode and "}" in code:
                array_mode = False

bad = []
for va, (kind, where) in sorted(sites.items()):
    b = byte_at(va)
    if b not in (0xE8, 0xE9):
        bad.append((va, kind, b, where))
print(f"checked {len(sites)} call/nop sites in {os.path.basename(exe_path)}")
for va, kind, b, where in bad:
    shown = "unmapped" if b is None else f"0x{b:02X}"
    print(f"  NOT a call/jmp: 0x{va:X} ({kind}) first byte {shown}  <- {where}")
if not bad:
    print("  all sites start with E8/E9")
