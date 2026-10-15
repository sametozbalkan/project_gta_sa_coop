"""Extract a zip into a destination directory, refusing absolute paths, '..', drive letters and symlinks."""
import os
import posixpath
import stat
import sys
import zipfile

archive_path, destination = sys.argv[1], os.path.abspath(sys.argv[2])
os.makedirs(destination, exist_ok=True)

count = 0
with zipfile.ZipFile(archive_path) as archive:
    for member in archive.infolist():
        name = posixpath.normpath(member.filename.replace("\\", "/"))
        if name in ("", "."):
            continue
        if name.startswith("/") or name == ".." or name.startswith("../") or ":" in name:
            raise SystemExit(f"unsafe path: {member.filename!r}")
        if stat.S_ISLNK(member.external_attr >> 16):
            raise SystemExit(f"symlink refused: {member.filename!r}")
        target = os.path.abspath(os.path.join(destination, *name.split("/")))
        if os.path.commonpath([destination, target]) != destination:
            raise SystemExit(f"escapes destination: {member.filename!r}")
        if member.is_dir():
            os.makedirs(target, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with archive.open(member) as src, open(target, "wb") as dst:
            while chunk := src.read(1 << 20):
                dst.write(chunk)
        count += 1

print(f"extracted {count} files into {destination}")
