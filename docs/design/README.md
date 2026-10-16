# Mirror layer and world-state registry: decisions and plan

Status (2026-10-16): both designs reviewed by the architect; nothing is implemented. Implementation starts after the
user approves the two-player test of 0.3.103 (group 1). The designs:

- `mirror-layer.md`: everything the host's mission shows on screen reaches the other players (texts, blips, locate
  spheres and markers, timers and counters).
- `world-state.md`: the state that sets the rules of the host's world (city unlock, maximum wanted level, IPL groups
  and roads, garages and doors, zones and gang wars, mission settings, map objects, riots) reaches the other players
  and stays as the host has it.

This file records the decisions and how the two designs fit together. Where it differs from a design, this file wins.

## 1. Decisions

The user left the project decisions to the architect (2026-10-15). They are taken here and listed for the user in
Turkish; the user can override any of them.

Mirror layer (`mirror-layer.md` section 9):

| # | Decision |
|---|---|
| M-D1 | The mirror owns the coordinate and contact-point blips a mission creates; `CNetworkStaticBlip` keeps the blips the host has outside missions. |
| M-D2 | Missions 0–10 are excluded (the setup missions every client runs at New Game, and the minigames). |
| M-D3 | Rewards are not mirrored. Money and respect stay per player; a port decides (registration replays `add_score`). |
| M-D4 | Mission titles: `print_big` style 2 from any host script. |
| M-D5 | The dialogue helpers `AUDIOL`, `A_CONT`, `A_ALAP` count as the mission by name. |
| M-D6 | The layer ships with the #1–#10 conversion; #1–#10 are re-tested from a New Game (agreed with the user 2026-10-15). |
| M-D7 | Immediate text and sprites (schools, minigames, race HUD) are out of scope. |
| M-D8 | Race checkpoints and script spheres come in M5, before the batch with #22. |
| M-D9 | No command to hide a host-only message from the friend for now; the user accepted that such messages show. |
| M-D10 | `ON_MISSION_FLAG_SYNC` is resent on join (M1). |
| Intro | The skipped intro stays skipped; #2 already starts both players on BMXs (`mirror-layer.md` 4.6). |

World-state registry (`world-state.md` section 8):

| # | Decision |
|---|---|
| W-D1 | `CCityUnlockSync` moves into the registry as entry 0. |
| W-D2 | One generic `WORLD_STATE` packet at the enum position of `UPDATE_CITY_UNLOCKED`. |
| W-D3 | Gang wars run only on the host's game; the friend sees the territories and joins the fights. |
| W-D4 | Properties belong to the host. The friend doesn't buy any (their purchase is saved nowhere); the test list says so. No blocking unless it becomes a problem. |
| W-D5 | The registry mirrors the host's end-of-mission engine settings (W12); the mirror layer clears only what it drew, plus the help box. |
| W-D6 | No mirrored reward pickups; ports give the friend mission weapons. |
| W-D7 | Upstream's EnEx sync stays; the registry adds the re-apply adapter, and upstream's `Receive` masks bits 0x0008 and 0x2000 too. |
| W-D8 | **Changed from the design:** no stat-147 story guard. The server pins the story host instead (L0 below), which also keeps missions, the player state and every host-only relay on the right game. |
| W-D9 | The barriers are tested when the story reaches #28 (story order); before that only the policy test and logs. |
| W-D10 | Map exploration stays per player. |
| W-D11 | Persistent script objects (doors, gates, cabins) get their own object-sync design before the batch with #60. |

## 2. How the two designs fit together

- **Packets.** Separate types, no shared envelope. `WORLD_STATE` replaces `UPDATE_CITY_UNLOCKED` in its position;
  `MISSION_MIRROR_BLIPS`, `MISSION_MIRROR_AREAS` and `MISSION_MIRROR_HUD` are appended after `PLAYER_STATE_RESTORE` in
  merge order. One version bump for all of them.
- **Lifecycle.** One client module serves both (L0): authenticated, disconnected, host changed (local or remote),
  player joined (host side), game restarted (frame counter), save loaded (the existing `GenericLoad` hook at 0x53C70B),
  the host's mission started and ended (the host's `$onmission` edge; `ON_MISSION_FLAG_SYNC` on the others), and the
  helpers "the local game is ready" and "a mission of this game runs" (now in `CPlayerStateSave`). Modules register
  callbacks instead of each editing `PacketHandlers/system.cpp`.
- **Server relay.** One helper for "from the host to everyone else", used by every new handler.
- **Join snapshot.** From the host's `PLAYER_CONNECTED` branch: world state first, then the mission view (including
  `ON_MISSION_FLAG_SYNC` and the last `load_mission_text`). Nothing depends on their relative order.
- **Mission end.** Registry: the engine settings `CMissionCleanup::Process` resets on the host (W12). Mirror: its own
  blips, areas, timers and the help box. Upstream's `ON_MISSION_FLAG_SYNC` cleanup stays.
- **Host leaves or changes.** The registry keeps the world and stops enforcing; the mirror drops everything it drew.
- **Blips that outlive a mission.** The mirror keeps owned blips while the host has them; unowned coordinate blips stay
  with `CNetworkStaticBlip`; the registry handles no blips.
- **Objects.** Neither design: an object a mission blips becomes a moving coordinate blip (mirror); doors and gates
  wait for W-D11.
- **Diagnostics.** With `--coopdiag`: bounded event logs `CoopAndreas_world_state.log` and `CoopAndreas_mirror.log` next
  to the game executable (like `CoopAndreas_ped_sync.log`), and one summary line each in `CoopAndreas_diag.log`.

## 3. Version

The package with these changes is **0.1.0-alpha**: group 1 in our own numbering (CLAUDE.md, Version), one bump for all
new packet types; `client/version.rc`, `server/version.rc` and the README badges change with it.

## 4. Packages and order

All `gtasa-coder-high` unless noted, each in its own worktree from the then-current `main`.

| Wave | Package | Content | Design |
|---|---|---|---|
| 1 | L0 shared foundation | The lifecycle module (section 2), the server relay helper, and host pinning: `story-host` in `server-config.ini` names the player who is always the host. When set, nobody else becomes host: a player who connects first waits without a host, and if the story host leaves nobody takes over until they are back. Empty keeps upstream's rule (the first player). `deploy.ps1` adds the line to the game copy's config when missing, with the nickname from its `coopandreas.ini`. No wire change. | this file |
| 2 | M1 mirror core and texts | family rule, post-command call, text opcodes, title rule, receiver table guard, join replay of texts and `ON_MISSION_FLAG_SYNC` | mirror 8 |
| 2 | WS-1 registry core | registry, entries 0 and 1, `WORLD_STATE`, version bump | world-state 7 |
| 3 | M2 blips, M3 areas, M4 timers and counters | merge in this order, packet types appended in the same order | mirror 8 |
| 3 | WS-3 doors and garages, WS-4 mission settings | the friend's mod shops since #10, shops after a restart, traffic after a failed mission | world-state 7 |
| 4 | C1 conversion of #1–#10 (`gtasa-coder`) | remove the display relays by `mirror-layer.md` 4.2, compile, bytes per mission | mirror 4 |
| 4 | Docs (architect) | porting guide (`mirror-layer.md` section 5), `docs/missions.md`, CLAUDE.md fork changes, the Turkish test list (mirror section 7, world-state section 6 A) | – |

Then build, deploy and package 0.1.0-alpha, and wait for the user's test. Later, each before the batch that needs it:

| Package | Needed by |
|---|---|
| M5 race checkpoints and script spheres | #22 (group 3) |
| WS-2 barriers and roads | #28 (group 4); earlier if a batch has room |
| WS-5 gangs | #25 (group 4) |
| WS-6 map objects | #42 |
| Object-sync design (W-D11) | #60 |
| WS-7 riots and relationships | #99 |

## 5. Gates

As in CLAUDE.md, plus: `tests/mission_mirror_test.cpp` and `tests/world_state_test.cpp` (`/W4 /WX`, like the existing
policy tests), `tools/audit_hooks.py` at its baseline plus the 18 sphere sites of M3 (and 5 if the optional timer
wrappers are built), and `scripts/compile-scm.ps1` with its save check for C1.

## 6. Risks to watch

- Mirror: a helper script other than the three named ones printing mission texts; the text-table reload clearing the
  friend's own minigame texts; a text printed every frame (measure before throttling); the rows still to confirm
  (mission id after 0417, the handle store of the other creating commands).
- World state: the *verify* rows (garage functions, forced weather, the density and gang globals), identity by index
  (the layout header disables an entry instead of writing wrong slots), whether the friend's kills count in the host's
  gang war.
- Both: nothing is tested in game; the first package re-tests #1–#10 from a New Game.
