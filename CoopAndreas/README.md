# Source: client, server and mission scripts

This folder holds the source of the mod's game client (`CoopAndreasSA.dll`), its server, the `eax.dll` proxy and
the co-op mission scripts. It began as an import of [CoopAndreas](https://github.com/Tornamic/CoopAndreas) by
Tornamic (upstream commit `cec2a7d`, imported on 2026-10-15) and has been changed a lot since; the folder keeps the
upstream name for now.

- What the project is, and how to install, play, test and report: the [README](../README.md).
- Every change against the upstream import: [CLAUDE.md](../CLAUDE.md), section "Fork changes".
- Designs, mission notes and the test plan: [docs](../docs).
- Bug reports for this build go to this repository's issues, not to upstream's.

| Folder | What |
|---|---|
| `client/` | the game client: network sync, hooks, the mission mirror layer, the world-state registry |
| `server/` | the dedicated server (`server.exe`, ENet over UDP 6767) |
| `shared/` | packet definitions and rules that client and server share |
| `proxy/` | `eax.dll`, which loads the client when the game starts with `--coop` |
| `scm/` | the co-op `main.scm` and `script.img`, and the mission sources in `scm/scripts/` |
| `sdk/` | the Sanny Builder 4 mode the mission scripts are compiled with |
| `tests/` | policy tests that run without the game |
| `third_party/` | vendored libraries: plugin-sdk, ENet, Dear ImGui, INIReader, utf8, Discord RPC (compiled out) |
| `launcher/` | upstream's launcher; it is built but not used or shipped |

Build everything with `scripts/build.ps1` from the repository root (see the README's Building section). The code is
licensed under GPL-3.0 ([LICENSE](LICENSE)); upstream's copyright notices stay in the files.
