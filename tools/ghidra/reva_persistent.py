"""ReVa MCP server (stdio) on a persistent Ghidra project.

mcp-reva always opens a throw-away project under %TEMP%, so gta_sa.exe would be imported and
analyzed again (about 4 minutes) in every session. This wrapper starts the same ReVa headless
server on an existing project instead: the one-time analysis and the names from
tools/ghidra/ApplyGtaNames.java stay. Edits made through ReVa are kept only after its
checkin-program tool saves them.

Run it with the Python of the ReVa uv tool (it has pyghidra, reva_cli and the MCP SDK):
  <uv tool dir>\\reverse-engineering-assistant\\Scripts\\python.exe -I reva_persistent.py
      --project-dir <dir with gta_sa.gpr> --reva-ext <ReVa extension dir with Module.manifest>
      [--project-name gta_sa] [--settings-dir <Ghidra settings dir>] [--temp-dir <dir>]
      [--disable-tool-group ID]...
Environment: GHIDRA_INSTALL_DIR (required), JAVA_HOME_OVERRIDE (optional, else Ghidra's
LaunchSupport finds the JDK). The ReVa extension is loaded as an external module
(-Dghidra.external.modules), so nothing is installed into Ghidra or its user settings.
Ghidra's temp folder (-Dapplication.tempdir) is --temp-dir, by default ghidra-temp next to the
project folder: every program open creates a buffer file there (Application.createTempFile), and
Windows' temp clean-up once removed the default %TEMP%\\ghidra while the server ran, after which
every tool answered "Program not found ... Did you mean /gta_sa.exe".
Ghidra refuses a project path with a folder that starts with '.', such as .tools; such a
path is opened through its 8.3 short form (C:\\...\\TOOLS~1\\...).
Ghidra allows one process per project: while this server runs, analyzeHeadless cannot open
the project. When it is already locked, a temporary copy is opened instead (edits are lost);
copies older than two days are removed at the next start.
stdout carries MCP only; logs go to stderr and Ghidra's application.log.
"""
import argparse
import asyncio
import ctypes
import shutil
import sys
import tempfile
import time
import uuid
from pathlib import Path

COPY_PREFIX = "reva_persistent_"


def log(msg):
    print(f"reva_persistent: {msg}", file=sys.stderr, flush=True)


def has_dot_folder(path):
    return any(part.startswith(".") for part in path.parts[1:])


def ghidra_project_dir(path):
    """Absolute path without a '.'-folder (Ghidra's ProjectLocator rejects those). Not resolve():
    that would expand an 8.3 name back to .tools."""
    path = path.absolute()
    if not has_dot_folder(path):
        return path
    buf = ctypes.create_unicode_buffer(32768)
    if ctypes.windll.kernel32.GetShortPathNameW(str(path), buf, len(buf)):
        short = Path(buf.value)
        if not has_dot_folder(short):
            return short
    sys.exit(f"reva_persistent: Ghidra cannot open a project under {path} (a folder starts with '.') "
             "and the volume has no 8.3 short name for it")


def is_lock_error(e):
    """True when a (Java) exception or one of its causes is Ghidra's project LockException."""
    while e is not None:
        if "LockException" in type(e).__name__ or "lock" in str(e).lower():
            return True
        e = e.getCause() if hasattr(e, "getCause") else None
    return False


def copy_project(project_dir, name):
    for old in Path(tempfile.gettempdir()).glob(COPY_PREFIX + "*"):
        if old.is_dir() and time.time() - old.stat().st_mtime > 2 * 86400:
            shutil.rmtree(old, ignore_errors=True)
    tmp = Path(tempfile.mkdtemp(prefix=COPY_PREFIX))
    shutil.copy2(project_dir / f"{name}.gpr", tmp / f"{name}.gpr")
    shutil.copytree(project_dir / f"{name}.rep", tmp / f"{name}.rep",
                    ignore=shutil.ignore_patterns("*.lock", "*.lock~"))
    return tmp


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--project-dir", required=True, type=Path)
    ap.add_argument("--project-name", default="gta_sa")
    ap.add_argument("--reva-ext", required=True, type=Path)
    ap.add_argument("--settings-dir", type=Path)
    ap.add_argument("--temp-dir", type=Path)
    group = ap.add_mutually_exclusive_group()
    group.add_argument("--disable-tool-group", action="append", metavar="ID")
    group.add_argument("--tool-group", action="append", metavar="ID")
    args = ap.parse_args()

    project_dir = ghidra_project_dir(args.project_dir)
    if not (project_dir / f"{args.project_name}.gpr").is_file():
        sys.exit(f"reva_persistent: no {args.project_name}.gpr in {project_dir}")
    ext = args.reva_ext.resolve()
    if not (ext / "Module.manifest").is_file():
        sys.exit(f"reva_persistent: {ext} is not a Ghidra module (no Module.manifest)")

    from pyghidra.launcher import HeadlessPyGhidraLauncher
    launcher = HeadlessPyGhidraLauncher(verbose=False)
    launcher.add_vmargs(f"-Dghidra.external.modules={ext}")
    if args.settings_dir:
        args.settings_dir.mkdir(parents=True, exist_ok=True)
        launcher.add_vmargs(f"-Dapplication.settingsdir={args.settings_dir.resolve()}")
    temp_dir = (args.temp_dir or project_dir.parent / "ghidra-temp").absolute()
    temp_dir.mkdir(parents=True, exist_ok=True)
    launcher.add_vmargs(f"-Dapplication.tempdir={temp_dir}")
    launcher.start()

    from java.io import File  # noqa: E402 (needs the JVM)
    from java.lang import System
    System.setOut(System.err)  # keep stdout for the MCP stream
    from reva.headless import RevaHeadlessLauncher
    from reva_cli.stdio_bridge import ReVaStdioBridge

    api_key = f"ReVa-{uuid.uuid4()}"
    temp_copy = None

    def start(location):
        server = RevaHeadlessLauncher(None, True, True, File(str(location)), args.project_name, api_key)
        if args.disable_tool_group:
            server.setDisabledToolGroups(",".join(args.disable_tool_group))
        if args.tool_group:
            server.setEnabledToolGroups(",".join(args.tool_group))
        server.start()
        return server

    try:
        server = start(project_dir)
    except Exception as e:  # LockException arrives wrapped in IOExceptions
        if not is_lock_error(e):
            raise
        temp_copy = copy_project(project_dir, args.project_name)
        log(f"{project_dir} is in use by another Ghidra process; opened a temporary copy {temp_copy} "
            "(changes in this session are not kept)")
        server = start(temp_copy)
    try:
        if not server.waitForServer(60000):
            sys.exit("reva_persistent: ReVa server did not start within 60 s")
        port = server.getPort()
        log(f"ReVa on 127.0.0.1:{port}, project {project_dir if temp_copy is None else temp_copy}")
        asyncio.run(ReVaStdioBridge(port, api_key=api_key).run())
    except KeyboardInterrupt:
        pass
    finally:
        server.stop()
        if temp_copy is not None:
            shutil.rmtree(temp_copy, ignore_errors=True)


if __name__ == "__main__":
    main()
