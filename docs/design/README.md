# Mirror layer and world-state registry: decisions and plan

Status (2026-10-16): both designs reviewed by the architect, decisions confirmed by the user (section 1). Implementation
started the same day; the user then skipped the 0.3.103 test, so 0.1.0 goes straight to the two-player test with the
0.3.103 checks in its test plan (now `docs/test-plan.md`; the tag `v0.3.103-alpha` keeps the 0.3.103 source). The
designs:

- `mirror-layer.md`: everything the host's mission shows on screen reaches the other players (texts, blips, locate
  spheres and markers, timers and counters).
- `world-state.md`: the state that sets the rules of the host's world (city unlock, maximum wanted level, IPL groups
  and roads, garages and doors, zones and gang wars, mission settings, map objects, riots) reaches the other players
  and stays as the host has it.

This file records the decisions and how the two designs fit together. Where it differs from a design, this file wins.

## 1. Decisions

The user left the project decisions to the architect (2026-10-15). These were put to the user and confirmed on
2026-10-16 with two changes by the user: rewards are mirrored (M-D3) and the friend's kills count in the
host's gang wars (W-D3).

Mirror layer (`mirror-layer.md` section 9):

| # | Decision |
|---|---|
| M-D1 | The mirror owns the coordinate and contact-point blips a mission creates; `CNetworkStaticBlip` keeps the blips the host has outside missions. |
| M-D2 | Missions 0–10 are excluded (the setup missions every client runs at New Game, and the minigames). |
| M-D3 | **Rewards are mirrored** (the user's decision): the money and respect a host mission gives when it is passed reach every player as their own: positive `add_score` (`0109`) and `award_player_mission_respect` (`0998`) run by the mission family in the frame the mission passes (with `register_mission_passed` `0318`, the `M_PASS` print or `play_mission_passed_tune` `0394`). Bets and in-mission costs stay with the host, so `0109` leaves `COpCodeSync`'s general replay (today registered ports replay every `add_score`, #10's bet included). M1 checks the passed blocks of every story mission so the rule catches each reward and no bet. The mission-passed stat and progress stay per player. |
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
| W-D3 | Gang wars start only in the host's game; the friend sees the territories and fights alongside. The friend's kills must count in the host's war (the user's requirement): WS-5 checks it and forwards them if they don't. |
| W-D4 | Properties belong to the host. The friend doesn't buy any (their purchase is saved nowhere); the test plan says so. No blocking unless it becomes a problem. |
| W-D5 | The registry mirrors the host's end-of-mission engine settings (W12); the mirror layer clears only what it drew, plus the help box. |
| W-D6 | No mirrored reward pickups; ports give the friend mission weapons. |
| W-D7 | Upstream's EnEx sync stays; the registry adds the re-apply adapter, and upstream's `Receive` masks bits 0x0008 and 0x2000 too. |
| W-D8 | **Changed from the design:** no stat-147 story guard. The server pins the story host instead (L0 below), which also keeps missions, the player state and every host-only relay on the right game. Confirmed by the user. |
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
new packet types; `client/version.rc`, `server/version.rc` and the README badges change with it. The architect bumps it when packaging; work packages don't.

## 4. Packages and order

All `gtasa-coder-high` unless noted, each in its own worktree from the then-current `main`.

| Wave | Package | Content | Design |
|---|---|---|---|
| 1 | L0 shared foundation | The lifecycle module (section 2), the server relay helper, and host pinning: `story-host` in `server-config.ini` names the player who is always the host. When set, nobody else becomes host: a player who connects first waits without a host, and if the story host leaves nobody takes over until they are back. Empty keeps upstream's rule (the first player). `deploy.ps1` adds the line to the game copy's config when missing, with the nickname from its `coopandreas.ini`. The no-host state must be safe: what happens to a departed host's entities (as upstream does for any leaving player), and every server and client use of the host that assumes one exists. No wire change. | this file |
| 2 | M1 mirror core, texts and rewards | family rule, post-command call, text opcodes, title rule, receiver table guard, join replay of texts and `ON_MISSION_FLAG_SYNC`, mission-pass rewards (M-D3) | mirror 8, this file |
| 2 | WS-1 registry core | registry, entries 0 and 1, `WORLD_STATE`; no story guard (W-D8) | world-state 7 |
| 3 | M2 blips, M3 areas, M4 timers and counters | merge in this order, packet types appended in the same order | mirror 8 |
| 3 | WS-3 doors and garages, WS-4 mission settings | the friend's mod shops since #10, shops after a restart, traffic after a failed mission | world-state 7 |
| 4 | C1 conversion of #1–#10 (`gtasa-coder`) | remove the display relays by `mirror-layer.md` 4.2, compile, bytes per mission | mirror 4 |
| 4 | Docs (architect) | porting guide (`mirror-layer.md` section 5), `docs/missions.md`, CLAUDE.md fork changes, the test plan (mirror section 7, world-state section 6 A) | – |

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

## 7. Where the code differs from the designs

Recorded at merge time (CLAUDE.md, Fork changes, has the details of each package).

- **Rewards** (M-D3): mirrored for every host mission, registered or not; `0109` and `0998` left the general replay.
  `mirror-layer.md` 4.5 still says registration gives the rewards. The mission-passed stat (147) still follows the
  host in registered ports, because `0318` stays in `COpCodeSync`'s list.
- **Packets:** one header per mirror package (`mirror_blips.h`, `mirror_areas.h`, `mirror_hud.h`) instead of one
  `mirror.h`; every new relay goes through `CHostManager::SendFromHostToOthers`/`SendFromHostToPlayer`.
- **Areas** carry the host's area code, so an interior marker isn't drawn on the street outside.
- **Timers:** the five optional `Add*` wrappers were built (gym machines and side missions share counter slots);
  hook audit baseline 411.
- **EnEx:** the per-player mask is 0xA01A (from the 1.0 bytes), not 0x0008|0x2000; the adapter feeds the registry
  a locally built FULL with each `ENEX_SYNC` (a registry API for local sources is a later cleanup) and waits during
  the friend's own girlfriend date and mod garage scripts.
- **Garages:** Michelle's garage stays per player.
- **Mission settings:** checked every frame, not every 250 ms, and reset on the friend when the host leaves during
  its mission.
- **Lifecycle:** a takeover's join snapshot and PLAYER_JOINED events wait for the first server-time answer;
  subscribers live in a function-local static.
- **Respect** (P1, not in the designs): mission, kill and clothes respect are kept with the player state (CAPS v2).
