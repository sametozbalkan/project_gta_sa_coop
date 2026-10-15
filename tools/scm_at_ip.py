"""Show the bytes of main.scm around each script's current IP from a CoopAndreas crash log."""
import re
import sys

SCRIPT_SPACE = 0xA49960  # CTheScripts::ScriptSpace, GTA SA 1.0 US

scm = open(sys.argv[1], "rb").read()
log = open(sys.argv[2], encoding="utf-8", errors="replace").read()
for name, cip in re.findall(r"^\s+(\S+)\s+BIP \w+\s+CIP (\w+)", log, re.M):
    off = int(cip, 16) - SCRIPT_SPACE
    if not 2 <= off < len(scm):
        print(f"{name:8} CIP {cip}: outside main.scm")
        continue
    opcode = int.from_bytes(scm[off - 2:off], "little")
    around = scm[off - 2:off + 14]
    text = "".join(chr(b) if 32 <= b < 127 else "." for b in around)
    flag = "  <-- 0x5254 'TR'" if opcode == 0x5254 else ""
    print(f"{name:8} off 0x{off:06X} last opcode 0x{opcode:04X} | {around.hex(' ')} | {text}{flag}")
