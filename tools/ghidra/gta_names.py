"""Collect gta_sa.exe 1.0 US names from gta-reversed and plugin-sdk into a TSV for ApplyGtaNames.java.

Usage:
  python -I tools/ghidra/gta_names.py <gta-reversed dir> <plugin-sdk dir> <out.tsv> [gta_sa.exe]

Sources, in priority order (the first name an address gets wins; later ones only count as agreement
or disagreement in the summary):
  gta-reversed  RH_Scoped*Install macros (function names), the vtable address of RH_ScopedVirtualClass,
                and StaticRef<T>(0xADDR) / StaticRef<T, 0xADDR>() globals (data labels)
  plugin-sdk    plugin_sa/game_sa: META ids, addrof/ctor_addr/dtor_addr = ADDRESS_BY_VERSION(1.0 US, ...)
                and "// Converted from ... Class::Name(...) 0xADDR" comments; after those, the old-style
                "#define FUNC_<Class>__<Method> 0xADDR" lines as Class::Method (split at the last "__":
                FUNC_CText__TKey__Read is CText__TKey::Read, plugin-sdk's class CText__TKey)
Installs that gta-reversed comments out still carry a valid name and address; they are kept and
flagged "commented". With gta_sa.exe given, function addresses outside the executable sections and
data addresses outside the image are dropped. Output columns:
  address  kind(func|vtable|data)  name(Ns::Name)  source  origin(file:line)  flags
"""
import re
import struct
import sys
from pathlib import Path

INSTALL_FORMS = {
    # macro: (index of name arg, index of address arg, kind, index of explicit display name or None)
    "Install": (0, 1, "member", None),
    "GlobalInstall": (0, 1, "global", None),
    "OverloadedInstall": (0, 2, "member", None),
    "GlobalOverloadedInstall": (0, 2, "global", None),
    "NamedGlobalInstall": (0, 2, "global", 1),
    "NamedInstall": (0, 2, "member", 1),
    "VMTOverloadedInstall": (0, 2, "member", None),
    "NamedVMTInstall": (0, 2, "member", 1),
    "VMTInstall": (0, 1, "member", None),
    "ConstructorInstall": (None, 0, "ctor", None),
    "VMTDestructorInstall": (None, 0, "vdtor", None),
    "DestructorInstall": (None, 0, "dtor", None),
}
SCOPE_FORMS = {"Class", "NamedClass", "VirtualClass", "Namespace", "NamespaceName"}
MACRO_RE = re.compile(r"\bRH_Scoped(\w+)\s*\(")
STATICREF_RE = re.compile(r"&\s*([A-Za-z_]\w*)\s*=\s*StaticRef\s*<")
HEX_RE = re.compile(r"^0x[0-9A-Fa-f]+$")
IDENT_RE = re.compile(r"^[A-Za-z_~][\w]*$")


def comment_mask(text):
    """Return a bytearray: 1 where the character is inside a // or /* */ comment."""
    mask = bytearray(len(text))
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            mask[i:j] = b"\1" * (j - i)
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            mask[i:j] = b"\1" * (j - i)
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
        else:
            i += 1
    return mask


def split_args(text, open_pos):
    """Split the macro argument list starting at text[open_pos] == '(' into top-level arguments."""
    args, cur, depth, i, n = [], [], 0, open_pos + 1, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            cur.append(text[i:j + 1])
            i = j + 1
            continue
        if text.startswith("operator", i) and not (i and (text[i - 1].isalnum() or text[i - 1] == "_")):
            m = re.match(r"operator\s*(\(\)|\[\]|[^\w\s,()]+|\s+\w+)", text[i:])
            if m:
                cur.append(m.group(0))
                i += len(m.group(0))
                continue
        if c in "([{<":
            depth += 1
        elif c in ")]}>":
            if depth == 0 and c == ")":
                args.append("".join(cur).strip())
                return args, i
            depth -= 1
        elif c == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    return None, n


def clean_name(name):
    name = re.sub(r"\s+", " ", name.strip()).lstrip(":")
    name = re.sub(r"\s*([<>,*&])\s*", r"\1", name)
    return name.replace(" ", "_")


QUALIFIED_RE = re.compile(r"\b([A-Za-z_]\w*)(?:<[^<>;(){}]*>)?::(~?[A-Za-z_]\w*)\b")


def parse_gta_reversed(root, entries):
    src = root / "source"
    files = []
    for path in sorted(src.rglob("*")):
        if path.suffix not in (".cpp", ".h", ".hpp") or "reversiblehooks" in path.parts:
            continue
        files.append((path.relative_to(root).as_posix(), path.read_text(encoding="utf-8", errors="replace")))
    # Every Class::member spelled anywhere: inside a class's InjectHooks, RH_ScopedGlobalInstall(fn)
    # binds &fn by normal name lookup, which finds a static member Class::fn before a free function.
    members = set()
    for _, text in files:
        members.update(QUALIFIED_RE.findall(text))
    for rel, text in files:
        if "RH_Scoped" in text:
            parse_rh_file(text, rel, entries, members)
        if "StaticRef" in text:
            parse_staticrefs(text, rel, entries)


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def parse_rh_file(text, rel, entries, members):
    mask = comment_mask(text)
    scope = None
    for m in MACRO_RE.finditer(text):
        form = m.group(1)
        args, end = split_args(text, m.end() - 1)
        if args is None:
            continue
        commented = bool(mask[m.start()])
        origin = f"{rel}:{line_of(text, m.start())}"
        if form in SCOPE_FORMS:
            if commented:
                continue
            if form in ("Class", "VirtualClass", "NamedClass", "Namespace"):
                scope = re.sub(r"<[A-Z]\w{0,2}>", "", clean_name(args[0]))  # CQuadTreeNode<T> -> CQuadTreeNode
            else:  # NamespaceName("Display") only names a category of global installs
                scope = None
            if form == "VirtualClass" and len(args) >= 2 and HEX_RE.match(args[1]):
                entries.append((int(args[1], 16), "vtable", f"{scope}::vftable", "gta-reversed", origin, ""))
            continue
        if form not in INSTALL_FORMS:
            continue
        name_i, addr_i, kind, display_i = INSTALL_FORMS[form]
        if addr_i >= len(args) or not HEX_RE.match(args[addr_i]):
            entries.append((None, "func", form, "gta-reversed", origin, "non-literal"))
            continue
        addr = int(args[addr_i], 16)
        flags = ["commented"] if commented else []
        if "Overloaded" in form:
            flags.append("overload")
        if "VMT" in form:
            flags.append("virtual")
        if kind in ("ctor", "dtor", "vdtor"):
            base = {"ctor": "Constructor", "dtor": "Destructor", "vdtor": "DeletingDestructor"}[kind]
            if scope is None:
                continue
            name = f"{scope}::{base}"
        else:
            fn = clean_name(args[name_i])
            if fn in ("nullptr", "NULL", "0"):  # commented-out placeholders
                continue
            if display_i is not None and display_i < len(args):
                lit = args[display_i].strip()
                if lit.startswith('"') and IDENT_RE.match(lit.strip('"')):
                    fn = lit.strip('"')
            plain = fn.split("<")[0]
            if kind == "member" and scope and "::" not in fn:
                name = f"{scope}::{fn}"
            elif kind == "global" and scope and "::" not in fn and \
                    (scope.split("::")[-1].split("<")[0], plain) in members:
                name = f"{scope}::{fn}"
                flags.append("static-member")
            else:
                name = fn
        entries.append((addr, "func", name, "gta-reversed", origin, ",".join(flags)))


CLASS_RE = re.compile(r"\b(class|struct|namespace)\s+(?:alignas\s*\([^)]*\)\s*)?([A-Za-z_]\w*)\s*(?:final\s*)?(?::[^;{]*)?\{")


def parse_staticrefs(text, rel, entries):
    """Name StaticRef globals; the enclosing class/namespace comes from a rough brace scan."""
    mask = comment_mask(text)
    opens = []  # (position of '{', name or None)
    for m in CLASS_RE.finditer(text):
        if not mask[m.start()]:
            opens.append((m.end() - 1, m.group(2)))
    named_open = {pos: name for pos, name in opens}
    stack, events = [], []
    for i, c in enumerate(text):
        if mask[i] or c not in "{}":
            continue
        if c == "{":
            stack.append(named_open.get(i))
        elif stack:
            stack.pop()
        events.append((i, tuple(x for x in stack if x)))
    import bisect
    keys = [e[0] for e in events]
    for m in STATICREF_RE.finditer(text):
        if mask[m.start()]:
            continue
        args_start = m.end() - 1
        # StaticRef<T, 0xADDR>() or StaticRef<T>(0xADDR)
        depth, j = 0, args_start
        while j < len(text):
            if text[j] == "<":
                depth += 1
            elif text[j] == ">":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        tmpl = text[args_start + 1:j]
        tail = text[j + 1:j + 40]
        hm = re.match(r"\s*\(\s*(0x[0-9A-Fa-f]+)\s*\)", tail)
        addr = hm.group(1) if hm else None
        if addr is None:
            tm = re.search(r",\s*(0x[0-9A-Fa-f]+)\s*$", tmpl)
            addr = tm.group(1) if tm else None
        if addr is None:
            continue
        k = bisect.bisect_right(keys, m.start()) - 1
        scope = "::".join(events[k][1]) if k >= 0 else ""
        name = f"{scope}::{m.group(1)}" if scope else m.group(1)
        entries.append((int(addr, 16), "data", name, "gta-reversed", f"{rel}:{line_of(text, m.start())}", ""))


PSDK_META_RE = re.compile(
    r"\b(META_BEGIN|META_BEGIN_OVERLOADED|CTOR_META_BEGIN|CTOR_META_BEGIN_OVERLOADED|DTOR_META_BEGIN|"
    r"DEL_DTOR_META_BEGIN)\(([^,)]+)[^)]*\)(?:(?!META_END).){0,400}?static const int id = (0x[0-9A-Fa-f]+)",
    re.S)
PSDK_ADDROF_RE = re.compile(
    r"\b(addrof|addrof_o|ctor_addr|ctor_addr_o|dtor_addr|del_dtor_addr)\(([^,)]+)[^)]*\)\s*=\s*"
    r"ADDRESS_BY_VERSION\(\s*(0x[0-9A-Fa-f]+)")
PSDK_CONVERTED_RE = re.compile(r"//\s*Converted from\b(.*?)\b(0x[0-9A-Fa-f]{5,8})\s*$", re.M)
# the greedy class group makes the method the part after the last "__"
PSDK_FUNC_DEFINE_RE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+FUNC_(\w+)__(\w+)[ \t]+(0x[0-9A-Fa-f]+)\b", re.M)


def psdk_name(form, q):
    q = clean_name(q)
    if form.startswith("CTOR") or form.startswith("ctor"):
        return f"{q}::{q.split('::')[-1].split('<')[0]}"
    if form.startswith("DTOR") or form.startswith("dtor"):
        return f"{q}::~{q.split('::')[-1].split('<')[0]}"
    if form.startswith("DEL_DTOR") or form.startswith("del_dtor"):
        return f"{q}::DeletingDestructor"
    return q


def parse_plugin_sdk(root, entries):
    base = root / "plugin_sa" / "game_sa"
    files = []
    for path in sorted(base.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(root).as_posix()
        files.append((rel, text))
        for m in PSDK_META_RE.finditer(text):
            entries.append((int(m.group(3), 16), "func", psdk_name(m.group(1), m.group(2)), "plugin-sdk",
                            f"{rel}:{line_of(text, m.start())}", "meta"))
        for m in PSDK_ADDROF_RE.finditer(text):
            entries.append((int(m.group(3), 16), "func", psdk_name(m.group(1), m.group(2)), "plugin-sdk",
                            f"{rel}:{line_of(text, m.start())}", "addrof"))
        for m in PSDK_CONVERTED_RE.finditer(text):
            sig = m.group(1)
            paren = sig.find("(")
            if paren < 0:
                continue
            nm = re.search(r"((?:[\w~<>,*]+::)*operator\s*\S+?|[A-Za-z_~][\w:~<>,*]*)\s*$", sig[:paren])
            if not nm:
                continue
            entries.append((int(m.group(2), 16), "func", clean_name(nm.group(1)), "plugin-sdk",
                            f"{rel}:{line_of(text, m.start())}", "converted"))
    # The old-style FUNC_ defines (CMessages.cpp, CMissionCleanup.h, CText.cpp, CTheZones.cpp, CZone.cpp) come
    # last, so they only name addresses that nothing above names.
    for rel, text in files:
        if "FUNC_" not in text:
            continue
        mask = comment_mask(text)
        for m in PSDK_FUNC_DEFINE_RE.finditer(text):
            if mask[m.start(1)]:
                continue
            entries.append((int(m.group(3), 16), "func", f"{m.group(1)}::{m.group(2)}", "plugin-sdk",
                            f"{rel}:{line_of(text, m.start(1))}", "define"))


def pe_sections(exe):
    data = exe.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    out = []
    for i in range(count):
        off = pe + 24 + opt + i * 40
        vsize, rva, rawsize = struct.unpack_from("<III", data, off + 8)
        chars = struct.unpack_from("<I", data, off + 36)[0]
        out.append((base + rva, base + rva + max(vsize, rawsize), bool(chars & 0x20000000)))
    return out


def normal(name):
    """Comparable form: template arguments dropped, Ns::Constructor == Ns::Ns, Ns::Destructor == Ns::~Ns."""
    while True:
        stripped = re.sub(r"<[^<>]*>", "", name)
        if stripped == name:
            break
        name = stripped
    parts = name.split("::")
    if len(parts) >= 2:
        cls = parts[-2].split("<")[0]
        if parts[-1] in (cls, "Constructor"):
            parts[-1] = "<ctor>"
        elif parts[-1] in ("~" + cls, "Destructor"):
            parts[-1] = "<dtor>"
    return "::".join(parts).lower()


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    gr, psdk, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    sections = pe_sections(Path(sys.argv[4])) if len(sys.argv) > 4 else None
    raw = []
    parse_gta_reversed(gr, raw)
    parse_plugin_sdk(psdk, raw)

    stats = {"non-literal": 0, "outside-exe": 0}
    chosen = {}   # (addr, kind) -> entry
    agree = disagree = 0
    disagreements = []
    for e in raw:
        addr, kind, name, source, origin, flags = e
        if addr is None:
            stats["non-literal"] += 1
            continue
        if sections is not None:
            ok = any(lo <= addr < hi and (x or kind != "func") for lo, hi, x in sections)
            if not ok:
                stats["outside-exe"] += 1
                if "--verbose" in sys.argv:
                    print(f"outside: 0x{addr:08X} {kind} {name} {origin}")
                continue
        key = (addr, "data" if kind == "data" else "code")
        prev = chosen.get(key)
        if prev is None:
            chosen[key] = e
            continue
        if normal(prev[2]) == normal(name):
            agree += 1
            # an uncommented gta-reversed install beats a commented one with the same name
            if "commented" in prev[5] and "commented" not in flags and prev[3] == source:
                chosen[key] = e
            continue
        disagree += 1
        if prev[3] == source == "gta-reversed" and "commented" in prev[5] and "commented" not in flags:
            chosen[key] = e
        disagreements.append((addr, prev[2], prev[3], name, source, origin))

    rows = sorted(chosen.values(), key=lambda r: (r[0], r[1]))
    with out.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# address\tkind\tname\tsource\torigin\tflags\n")
        for addr, kind, name, source, origin, flags in rows:
            f.write(f"0x{addr:08X}\t{kind}\t{name}\t{source}\t{origin}\t{flags}\n")
        for addr, n1, s1, n2, s2, origin in disagreements:
            f.write(f"#disagree\t0x{addr:08X}\t{n1} ({s1})\t{n2} ({s2})\t{origin}\n")

    def count(pred):
        return sum(1 for r in rows if pred(r))
    print(f"raw entries: {len(raw)} (gta-reversed {sum(1 for r in raw if r[3] == 'gta-reversed')}, "
          f"plugin-sdk {sum(1 for r in raw if r[3] == 'plugin-sdk')})")
    print(f"skipped: non-literal address {stats['non-literal']}, outside gta_sa.exe {stats['outside-exe']}")
    print(f"unique addresses written: {len(rows)}")
    for kind in ("func", "vtable", "data"):
        for source in ("gta-reversed", "plugin-sdk"):
            n = count(lambda r: r[1] == kind and r[3] == source)
            if n:
                print(f"  {kind:6} from {source:12} {n}")
    print(f"  of which commented-out gta-reversed installs: {count(lambda r: 'commented' in r[5])}")
    print(f"  of which plugin-sdk FUNC_ defines: {count(lambda r: r[5] == 'define')}")
    print(f"same address, same name from another entry: {agree}; different name (first kept): {disagree}")


if __name__ == "__main__":
    main()
