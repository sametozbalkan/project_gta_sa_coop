"""Apply the gtasa-open-downgrader v2 manifest to a game folder, same steps as its PatchWorker.

usage: apply_downgrade.py <game_dir> <patches_dir> <xdelta3.exe>

Pre-flight checks every file against the manifest before anything is changed. Each output is written
to a temp file and only replaces the original after its MD5 matches the manifest's target hash.
No backups are made here: the untouched Steam install is the backup.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys

game_dir, patches_dir, xdelta = sys.argv[1], sys.argv[2], sys.argv[3]
manifest = json.load(open(os.path.join(patches_dir, "manifest.json"), encoding="utf-8"))
EXE_NAMES = ("gta_sa.exe", "gta-sa.exe")


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
    return h.hexdigest()


def target_path(rel):
    path = os.path.join(game_dir, *rel.split("/"))
    if rel in EXE_NAMES and not os.path.exists(path):
        other = os.path.join(game_dir, EXE_NAMES[1] if rel == EXE_NAMES[0] else EXE_NAMES[0])
        if os.path.exists(other):
            return other
    return path


# 1) pre-flight: every file must be either already patched or a known source version
plan = []
for entry in manifest["files"]:
    rel, action, want = entry["path"], entry.get("action", "patch"), entry["target_hash"]
    path = target_path(rel)
    have = md5(path) if os.path.exists(path) else None
    if have == want:
        plan.append((entry, path, None))
        continue
    if action == "copy":
        payload = os.path.join(patches_dir, entry.get("payload", "gta_sa.exe"))
        if md5(payload) != want:
            sys.exit(f"ABORT: payload for {rel} does not match its target hash")
        plan.append((entry, path, payload))
    else:
        source = next((s for s in entry.get("sources", []) if s.get("hash") == have), None)
        if not source:
            sys.exit(f"ABORT: {rel} has unknown hash {have}; nothing was changed")
        patch = os.path.join(patches_dir, *source["patch"].split("/"))
        if not os.path.exists(patch):
            sys.exit(f"ABORT: patch file missing: {source['patch']}")
        plan.append((entry, path, patch))
print(f"pre-flight ok: {len(plan)} files")

# 2) apply
for entry, path, src in plan:
    rel, want = entry["path"], entry["target_hash"]
    if src is None:
        print(f"  already 1.0  {rel}")
        continue
    tmp = path + ".tmp"
    if entry.get("action") == "copy":
        shutil.copyfile(src, tmp)
    else:
        result = subprocess.run([xdelta, "-d", "-f", "-s", path, src, tmp], capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(f"FAILED: xdelta3 on {rel}: {result.stderr.strip()}")
    if md5(tmp) != want:
        os.remove(tmp)
        sys.exit(f"FAILED: {rel} output hash mismatch")
    os.replace(tmp, path)
    print(f"  patched      {rel}")

# 3) the downgrader keeps both executable names pointing at the 1.0 exe
exe_entry = next(e for e in manifest["files"] if e["path"] in EXE_NAMES)
exe_path = target_path(exe_entry["path"])
for name in EXE_NAMES:
    other = os.path.join(game_dir, name)
    if os.path.abspath(other) != os.path.abspath(exe_path):
        shutil.copyfile(exe_path, other)

# 4) final verification of every target
bad = [e["path"] for e in manifest["files"] if md5(target_path(e["path"])) != e["target_hash"]]
bad += [n for n in EXE_NAMES if md5(os.path.join(game_dir, n)) != exe_entry["target_hash"]]
if bad:
    sys.exit(f"VERIFY FAILED: {bad}")
print("DONE: all files match the v1.0 US target hashes")
