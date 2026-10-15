# GTA SA co-op workspace

Goal: play the GTA San Andreas story in 2-player co-op with a friend, on a private build of CoopAndreas (GPL-3.0).

Repository: github.com/sametozbalkan/project_gta_sa_coop (public), branch `main`. The user allowed the architect to commit and push there (2026-10-15). Local only (git-ignored): `.tools/`, `backups/`, `dist/`, build outputs.

## Layout
- `CoopAndreas/` — CoopAndreas (github.com/Tornamic/CoopAndreas) imported with a `git subtree --squash` style commit at upstream `cec2a7d`; our changes are ordinary commits on top (see Fork changes). Update from upstream with `git subtree pull --prefix=CoopAndreas https://github.com/Tornamic/CoopAndreas.git main --squash`. plugin-sdk is vendored in `CoopAndreas/third_party/plugin-sdk`. Build output: `CoopAndreas/build/windows/x86/<mode>/` (git-ignored).
- `.tools/` (local only) — `xmake/` (xmake 3.1.1, portable); `SannyBuilder4/` (Sanny Builder 4.2.0 with the `sa_sbl_coopandreas` mode copied in from `CoopAndreas/sdk/Sanny Builder 4/`); `xdelta3/` (official xdelta3 3.1.0 x86_64, same SHA-256 as the one gtasa-open-downgrader bundles); `patches/Patches_v2.zip` (our verified copy of the downgrade patches, SHA-256 `f7df4272…265b`, pinned in Kur.ps1; `Kur.bat -PatchesZip <path>` or putting it next to Kur.ps1 skips the download); `gta-reversed/` (shallow clone of github.com/gta-reversed/gta-reversed, read-only reference for 1.0 US functions and addresses).
- `scripts/build.ps1` — builds client, server, proxy and launcher (`-Mode debug` for a debug build). Uses VS 2026 / MSVC x86. From a linked git worktree it uses the main checkout's `.tools`.
- `scripts/deploy.ps1` — installs the build into the game copy and creates the `CoopAndreas` / `CoopAndreas Server` shortcuts there; refuses unless `gta_sa.exe` is 1.0 US. Supports `-WhatIf`.
- `scripts/package.ps1` — assembles `dist/CoopAndreas-Kurulum/` + `.zip` (~34 MB) from the current build, `dist/CoopTunnel`, `.tools/xdelta3` and a GPL source folder (`KAYNAK/`: upstream commit + `coopandreas-ozel.patch` + tunnel source). Rerun after every build that the friend should get.
- `tunnel/` — CoopTunnel (C#, .NET 10, Facepunch.Steamworks 2.3.3): carries CoopAndreas' UDP 6767 over Steam P2P with AppID 480 (Spacewar), because the user's line is behind CGNAT. Modes: `host` (next to server.exe, forwards each Steam friend to 127.0.0.1:6767 through its own UDP socket, rejects non-friends unless `--allow-all`), `join` (listens on 127.0.0.1:6767 for the game), `test` (20 pings + route details). Messages carry a 1-byte tag: 0 game, 1 ping, 2 pong, 3 hello. Facepunch 2.3.3 reads the remote identity of a connection as `ip:::` with the current Steam client, so the joiner sends its SteamID64 in a hello and the host checks the friendship from that (unverified peers get no server traffic and are closed after 10 s). `timeBeginPeriod(1)` keeps the 1 ms pump loops from sleeping 15.6 ms. Steam refuses a connection to your own account, so end-to-end tests need two accounts; the first two-account test (2026-10-15) passed: 20/20 pings, Steam ping 13 ms, 0% loss, with ~90 ms spikes from the host's Wi-Fi. `tunnel/package/` holds the `.bat` launchers and `KULLANIM.txt` that go next to the published exe.
- `installer/` — the friend's installer source (`Kur.ps1` for Windows PowerShell 5.1, `Kur.bat`, `OKU-BENI.txt`). It finds the Steam GTA SA, copies it to `C:\Games\GTASA_Coop`, verifies `Patches_v2.zip` by a pinned SHA-256 (downloads it if not next to the installer), applies it with `tools\xdelta3.exe` checking every MD5, then installs the mod, CoopTunnel, a pre-filled `coopandreas.ini` (Steam name, 127.0.0.1:6767; only if missing) and desktop shortcuts. Re-running it on an existing 1.0 US folder only refreshes the mod files. Tested end to end on 2026-10-15: 50 s, 422 files identical to the working install.
- `tools/` — Python debugging helpers from the first session, run with `python -I`: `sample_main_thread.py <pid> <plugin-sdk game_sa dir>` (where a hung game's main thread is, symbolized from plugin-sdk addresses), `scm_at_ip.py <main.scm> <crash log>` (which script read a bogus opcode), `pe_peek.py <exe> <va…>` (bytes at virtual addresses, e.g. plugin-sdk version signatures), `audit_hooks.py <exe> <client/src>` (RedirectCall/Nop sites that aren't calls in a given exe), `upnp_external_ip.py`, `apply_downgrade.py`, `safe_unzip.py`.
- `YAPILACAKLAR.md` — backlog and work plan (Turkish, for the user).
- `backups/` (local only) — the Steam-era save files, kept untouched, and `CoopAndreas-upstream-clone.git` (the git dir of the original upstream clone from before the import; safe to delete).
- `dist/` (local only) — `CoopTunnel/` + `CoopTunnel.zip`: publish with `dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:EnableCompressionInSingleFile=true -p:DebugType=none -o ../dist/CoopTunnel` from `tunnel/`, then copy `tunnel/package/*` next to it. `CoopAndreas-Kurulum/` + `.zip` come from `package.ps1`.
- Game copy: `C:\Games\GTASA_Coop`, copied from Steam and downgraded on 2026-10-15 with the gtasa-open-downgrader v2 patches (applied with official xdelta3 3.1.0). `gta_sa.exe` = 1.0 US Hoodlum, MD5 `170b3a9108687b26da2d8901c6948a18`. The Steam install is the pristine backup.

## Debugging
- Crashes: `C:\Games\GTASA_Coop\CoopAndreas_crashes\*.log` (registers, stack, active scripts with IPs).
- Hangs/black screens: launch with `--coopdiag` ("CoopAndreas (tani)" shortcut) and read `CoopAndreas_diag.log`; sample the live main thread with `tools/sample_main_thread.py`.

## Rules
- Never modify the Steam install (`C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto San Andreas`); work only on `C:\Games\GTASA_Coop`.
- Everything targets the 1.0 US executable (`PLUGIN_SGV_10US`, hard-coded 1.0 US hook addresses); don't reverse-engineer the Steam `gta-sa.exe`.
- The user wants no ties to third parties: no CoopAndreas Discord or serial key, no telemetry. Keep it that way in new changes.
- Start the game with `gta_sa.exe --coop` (the `CoopAndreas` shortcut); the proxy `eax.dll` only loads the mod with `--coop`, so plain `gta_sa.exe` is vanilla single-player. The server is `server\server.exe` (config `server-config.ini` next to it, UDP port 6767).
- The client loads its own `CoopAndreas\main.scm` and `CoopAndreas\script.img`; the game's `data\script` stays vanilla. Don't let Sanny Builder's copy-to-game-directory overwrite it.
- GPL-3.0: when the friend gets our build, they get its source too.
- The repo is public: no game files, downgrade patches, saves, secrets or the friend's personal details in it.

## Working model (architect + coder agents)
- This session is the project's architect (the user's decision, 2026-10-15; they leave the decisions to it): it plans the work, writes work-package briefs, reviews and merges what comes back, builds, deploys to the game copy, packages for the friend, commits and pushes, keeps this file and `YAPILACAKLAR.md` current, and talks to the user in Turkish. It may code small things itself.
- Coding and checking go to Opus 5.5 subagents defined in `C:\Users\samet\.claude\agents\` (the user's pattern from their other projects; `memory: user`; the brief carries only the task; coders never commit):
  - `gtasa-coder` (effort medium): installer, scripts and tunnel changes, docs, client/server features that copy an existing pattern, mission ports that follow the porting guide.
  - `gtasa-coder-high` (effort high): new hooks and anything that needs 1.0 US addresses, packet changes across `shared/`, client and server, host/opcode sync, threading (ENet vs. the game loop), crash and hang investigations, and review-only briefs.
- New or changed agent definitions reach a running session only after a while (in wave 1 they showed up after the agents had been dispatched). Until the Agent tool accepts the type, dispatch `general-purpose` with model opus and the definition's rules pasted above the brief.
- Agents run in their own git worktrees (Agent `isolation: "worktree"`, under `.claude/worktrees/`) from a base commit on `main` named in the brief, so several can work at once; a fresh worktree builds everything in about 30 s. The architect merges them into `main` one at a time (commit in the worktree, cherry-pick onto `main`, remove the worktree and its branch), rebuilds after each and resolves overlaps (packet IDs, handler tables, hook registration).
- Only the architect deploys (`scripts/deploy.ps1`), packages (`scripts/package.ps1`, the tunnel publish to `dist/`) and touches git history. Nobody starts or stops `gta_sa.exe`, `server.exe` or `CoopTunnel.exe` unless the brief says so; the user may be playing with the friend.
- Every new 1.0 US address, offset or calling convention cites its source (plugin-sdk, gta-reversed, or bytes read from `C:\Games\GTASA_Coop\gta_sa.exe` with `tools/pe_peek.py`); never a guess. A packet change updates `shared/`, client and server together and means the friend needs a new package.
- Gates: `scripts/build.ps1` for C++ (plus `tools/audit_hooks.py` when hook or patch sites change), `dotnet build -c Release` in `tunnel/`, a Windows PowerShell 5.1 parse of changed `.ps1` files (and `deploy.ps1 -WhatIf`), a Sanny Builder CLI compile for missions (command not established yet). Only the user can test in game, two-player features with the friend; reports say what wasn't tested in game and give the test steps.
- Git: `core.autocrlf=true` (working tree CRLF, index LF). `origin` is the user's repo; never push anywhere else.

## Fork changes (keep them small for upstream merges)
Committed on top of the upstream import.
- `COOP_NO_SERIAL_CHECK` (defined in `xmake.lua`, used in `client/src/CLaunchManager.cpp`): no launcher serial check.
- `COOP_NO_DISCORD_RPC` (defined in `xmake.lua`, used in `client/src/CDiscordRPC.cpp` and `CDiscordRPCMgr.cpp`): no Discord Rich Presence. `Discord_Initialize` (named-pipe IPC to the Discord client, `discord-<appid>` URL handler written to HKCU) and the presence updates are compiled out, so no discord-rpc code is linked into the client; the `discordrpc` target still builds.
- Crash reports are opt-in: defaults in `client/src/CConfigLoader.h`, `CConfigLoader.cpp` and `CCrashReporter.h` are `false` (`report-crashlogs` in `coopandreas.ini`).
- Frame limiter always on (`CPatch::PatchFramerate`, byte patch at `0x748D68`). Upstream removes the 14 ms frame delay and relies on the menu's Frame Limiter; with it off the game runs at ~2000+ FPS, per-frame time rounds to 0 ms and New Game sits on a black screen.
- `--coopdiag` (`client/src/Debug/CDiagLog.*`, started from `CCore::Init`): writes `CoopAndreas_diag.log` in the game folder every 2 s (frame counter, game time, pause/menu/fade, network, focus, script IPs). It keeps writing if the game loop hangs.

## Known limitations
- Old saves can't be loaded: the co-op `main.scm` differs, so saved script IPs land in garbage (crash in e.g. `quarrys`). Always New Game.
- While alt-tabbed GTA SA sleeps in `WinMain` and nobody services ENet, so after ~30 s the server drops the client (no auto-reconnect).

## Missions
Mission sources are `CoopAndreas/scm/scripts/*.txt` (decompiled SA scripts plus co-op commands), included from `scm/main.txt`. They compile with Sanny Builder 4 in the `sa_sbl_coopandreas` mode, which reads IDE/GXT files from the game directory. CLI flags per `.tools/SannyBuilder4/data/cli.sbf`: `--no-splash`, `--mode <id>`, `--compile <in> [out]` (not tried yet). `deploy.ps1` installs the committed `scm/main.scm` + `script.img`.
