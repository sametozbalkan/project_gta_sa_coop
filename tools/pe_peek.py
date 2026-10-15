"""Print the uint32 stored at given virtual addresses of a PE file, mapped through its section table."""
import struct
import sys

path, vas = sys.argv[1], [int(v, 16) for v in sys.argv[2:]]
data = open(path, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
num_sections = struct.unpack_from("<H", data, pe + 6)[0]
opt_size = struct.unpack_from("<H", data, pe + 20)[0]
image_base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
sections = []
for i in range(num_sections):
    off = pe + 24 + opt_size + i * 40
    name = data[off:off + 8].rstrip(b"\0").decode(errors="replace")
    vsize, rva, rawsize, rawptr = struct.unpack_from("<IIII", data, off + 8)
    sections.append((name, rva, max(vsize, rawsize), rawptr, rawsize))

print(f"{path}\n  image base 0x{image_base:X}, sections: " + ", ".join(s[0] for s in sections))
for va in vas:
    rva = va - image_base
    for name, srva, span, rawptr, rawsize in sections:
        if srva <= rva < srva + span:
            delta = rva - srva
            value = struct.unpack_from("<I", data, rawptr + delta)[0] if delta + 4 <= rawsize else None
            shown = f"0x{value:08X}" if value is not None else "(not in file data)"
            print(f"  VA 0x{va:X} -> {name}+0x{delta:X}: {shown}")
            break
    else:
        print(f"  VA 0x{va:X}: not mapped")
