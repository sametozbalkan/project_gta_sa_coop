# Backlog

What is being worked on, what comes next and what is still open. The rules and the details of every change are in
[CLAUDE.md](CLAUDE.md), every mission with its risks in [docs/missions.md](docs/missions.md), the open tests in
[docs/test-plan.md](docs/test-plan.md).

## Current state

- **0.1.2-alpha** is the current release (GitHub pre-release, tag `v0.1.2-alpha`, 2026-10-18).
- **0.1.3-alpha is on main, not released**: the train review follow-up (complete train snapshots, train
  incarnations against stale packets, surviving wagons after a partial deletion, attachments across restarts,
  removals and reconnects, station-skip serialization). The wire format changed, so every player updates together.
  Reviewed twice by agents, all gates pass; in-game checks in `docs/test-plan.md` A5/A6. It ships with the next
  package.
- **The 0.1.1 test** (2026-10-17, three players) reached #8 Drive-By:
  - in #8 the two non-host games crashed at the same moment when the Ballas got out of their Voodoos (0x642067); the
    host's game didn't;
  - earlier, the host's game crashed while starting a story mission (EIP 0x3F800000), probably while a player was
    dead or arrested.
- **0.1.2 fixes both:**
  - Drive-By creates the Ballas' cars and defines their "leave the car" sequences in the same frame, before the cars
    have network ids, so the sequences reached the other games without their cars. Sequence tasks are now named when
    the sequence is performed, the other games skip a command for a car or character they don't have, and a
    leave-car task without its vehicle no longer crashes.
  - The death/arrest check no longer reads an empty gosub stack in a mission's first frame.
  - The wire format is 0.1.1's, but the version changed so that 0.1.1 and 0.1.2 games don't mix: every player, the
    host included, updates. Co-op saves keep loading.
- **Waiting for a test** (`docs/test-plan.md`): #1–#10 from a New Game, the 0.1.1 changes (vehicle sync, a restart
  during a session, a player leaving mid-mission, story missions starting only on the host, Alt+Tab keys) and the
  0.1.2 fixes. #1–#6 last passed on 0.3.102; 0.1.0 (the mirror layer, the world-state registry, story host pinning)
  was never tested by two players.
- **Group 2 (#11–#17) starts only after the group 1 test is approved.**

## Mission groups

Missions are rebuilt in story order and ship in groups, one package each. A merged group is packaged with the test
plan, and the next group starts only after that test is approved; bug fixes and other work go on meanwhile. No mission
is skipped: a group stops at a blocked mission until the blocker is solved. In solo minigames the second player
watches. Ports never change the story order or the progression flags, and co-op saves keep loading from one package
to the next (`scripts/compile-scm.ps1` checks this).

- Los Santos: group 1 #1–#10 (the intro and Sweet's missions; #1–#5 come from upstream), group 2 #11–#17 (Smoke,
  Ryder), group 3 #18–#22 (the music missions and Cesar), group 4 #23–#28 (Crash, Doberman, Los Sepulcros, Reuniting
  The Families, The Green Sabre).
- Then the Badlands (#29–#39), San Fierro (#40–#65), the desert (#66–#74), Las Venturas (#75–#92) and the return to
  Los Santos (#93–#103).

## Next, in priority order

1. **Group 2 infrastructure: mission objects (`OB-0` and trains implemented).** The designs are in `docs/design/trains.md`
   and `docs/design/mission-objects.md`.
   - **Trains (`docs/design/trains.md`)**: decisions `T-D1`–`T-D9` confirmed (2026-10-18) and packages `T1a`, `T1b`,
     `T2`, `T4` and `T5` implemented (`CTrainSync`, carriage network vehicle identity, `ENTITY_ATTACH` for peds/players
     attached to vehicles, passenger station-skip forwarding and derailment/driving support); `T3` (vehicle-relative
     on-foot sync) is scheduled before #74 *Green Goo* (`T-D5`/`T-D7`).
   - **Mission objects (`docs/design/mission-objects.md`)**: **`OB-0` (`O-D9`, remote shots on objects) is Done**
     (`CBulletSyncPolicy.h`, `CNetworkEntitySerializer.cpp`, `Hooks/PlayerHooks.cpp`, `PacketHandlers/players.cpp`,
     `tests/bullet_sync_test.cpp`); decisions `O-D1`–`O-D8` and `O-D10`–`O-D11` remain open (below) for `OB-1`–`OB-3`
     before the group 2 ports (#15–#17). Persistent objects (`OB-4`: doors, gates, cabins) are first needed in #23.

### Mission Objects & Pickups

| Package | Scope | Needed by | Status |
|---|---|---|---|
| `OB-0` (`O-D9`) | Remote shots on objects & un-networked entity serialization (`CBulletSyncPolicy.h`, `CNetworkEntitySerializer.cpp`, `Hooks/PlayerHooks.cpp`, `PacketHandlers/players.cpp`, `tests/bullet_sync_test.cpp`) | #7, #14, #17, #42 | **Done** |
| `OB-1` | Mission object mirror core (`CMirrorObjects`, `MISSION_OBJECTS`, `MISSION_OBJECT_MOTION`) | Group 2 (#11–#17) | Design-ready |
| `OB-2` | Object entity blips in `MISSION_MIRROR_BLIPS` | #15 | Design-ready |
| `OB-3` | Interaction hand-over (carry in #15, forklift in #17, `MISSION_OBJECT_CLAIM`) | #15, #17 | Design-ready |
| `OB-4` | Persistent script objects (doors, gates, cabins) | #23 (group 4) | Design-ready |
| `OB-5` | Vehicle-relative cargo | #72 | Design-ready |

2. **The host's own connection drops mid-mission:** the network-id wait loops in INTRO1, INTRO2, SWEET1, SWEET1B and
   SWEET2 have no timeout, so the mission can hang on the host. They get SWEET3's 2-second timeout (a small change
   inside each mission).
3. **Infrastructure before the missions that need it** (`docs/design/README.md`, section 4): M5 race checkpoints and
   script spheres (#22, group 3); WS-5 gang territories and wars, with the friend's kills counting in the host's war
   (#25, group 4); WS-2 bridge barriers and roads (#28, group 4); WS-6 map objects (#42); T3 vehicle-relative on-foot
   sync (#74); WS-7 riots (#99).
4. **Client features later missions need**, when they come up: fire (#23), gang wars (#25, #98), trailers (#31), RC
   vehicles (#44–#46), parachutes (#72, #81, #89), the jetpack (#73).
5. **Mission port checker:** a script under `tools/` that checks the leaving-player rules (check and use in the same
   script frame, no `wait` between them), run by `scripts/compile-scm.ps1`.
6. **Mission size limit:** Reuniting The Families (#27) has 561 bytes left of the 69,000-byte mission buffer. With the
   mirror layer a minimal port fits (registration 2 bytes, collecting the players 11, a teleport 11), so #27 no longer
   needs a larger buffer, but co-op pool (`POOL2`) might. First findings (2026-10-16): 200,000 (MAIN) appears only in
   two reads in `CTheScripts::Init`, 69,000 only in the handler of the mission load command (0417), and the total
   269,000 as a counter in the Hoodlum exe (0x401CF8). 21 functions use the script space (0xA49960) at 58 places and
   another variable follows it directly, so it can't grow in place: it has to move (patching every use), or the
   mission block needs another way.
7. **Cleanup** (after the group 1 test, because it touches the door code under test): an API in the world-state
   registry for locally sourced entries; the door adapter uses a fake packet path today.
8. **Rename:** an independent project name (candidates: Tandem, Grove Pact, Riding Shotgun; not chosen yet). One
   package: the window title, version text and messages in game; the DLL, mod folder, ini, launcher, shortcut,
   installer, package and log names; the repository's `CoopAndreas/` folder, scripts, docs and the few code
   definitions with the name; the GitHub repository name if wanted. Kept: GPL-3.0 and LICENSE, the original copyright
   line in the DLL ("Tornamic (C) 2026", with ours next to it), one credit line in the README, the git history and the
   generic "co-op" words (the `Coop.` mission commands, the "(Coop)" prefix in saves). The installer moves an existing
   install to the new name (files, shortcuts, the nickname in the ini, the server) without touching saves. In the same
   package: ENet's license file, which `third_party/enet` lacks. Upstream isn't followed automatically.
9. **Donations:** once the Buy Me a Coffee account exists, a badge at the top of the README and `.github/FUNDING.yml`
   (GitHub's Sponsor button). Donations stay voluntary: no paid features, no early access, no GTA or Rockstar logos.

## Open decisions

- **Mission objects** (`docs/design/mission-objects.md`, O-D1–O-D8, O-D10–O-D11; `O-D9` / `OB-0` confirmed and **Done**),
  for the maintainer: whether the friend carries boxes and drives a forklift, persistent doors and gates, mission
  explosions, pickups from crates. (Train decisions `T-D1`–`T-D9` were confirmed on 2026-10-18 with the recommendations
  in `docs/design/trains.md`.)
- **At the port of the mission:** #26 Los Sepulcros sets the host's respect to 200 (`0629`) during the mission, and
  #103 End Of The Line (3) pays $250,000 frames before its pass; the mirror layer passes on neither.

## Open issues

- **Found by the group 2 designs**:
  - **Done (`OB-0` / `O-D9`)**: the friend's bullets now hit objects, glass and buildings on every game, and a shot at an
    un-networked ped or vehicle stays `NETWORK_ENTITY_TYPE_NOTHING` instead of targeting NPC 0 (`CBulletSyncPolicy.h`,
    `CNetworkEntitySerializer.cpp`, `Hooks/PlayerHooks.cpp`, `PacketHandlers/players.cpp`, `tests/bullet_sync_test.cpp`).
- **Vehicle sync** (small, rare):
  - when all 255 vehicle ids are taken the server doesn't answer a vehicle spawn (the creator's slot comes back after
    30 s);
  - a recreated copy gets its damage back only with the next damage update;
  - a car of the host's old mission that the friend drives outlives the host's restart;
  - the client's temporary ped slots don't expire;
  - some modules ask `m_serverTimeOffset != 0` for a synced clock; `CServerTime::m_bSynced` is the right check.
- **Trains and attachments** (from the 0.1.3 review, small):
  - the host's own restart clears its remote attachment records, and `PLAYER_JOINED` fires only on the host, so a
    non-host owner doesn't resend its attachments to a joiner (`trains.md` 6.5 says any game);
  - a guest who restarts while syncing a car a host script attached sees that car unattached until its new syncer
    sends a change;
  - the codex review mentioned an inherited bullet surface/piece issue in the shot sync (`OB-0` area); not
    investigated yet.
- **Task sequences** (from the 0.1.2 review):
  - a task a mission gives a character in the frame it creates it doesn't reach the other screens (from upstream; no
    known effect in #1–#10);
  - derived leave-car tasks (`LeaveCarAndDie` and the like) don't get the new no-vehicle safety;
  - `CNetworkAnimQueue::Process` drops entries added while the queues are processed (rare: it takes an animation
    block that fails to load twice).

## Decided

- Upstream PR #106 (money, wanted level, cheats) **was not taken**: a player who joins later resets the other's money,
  money earned at the same moment is lost, and cheats run on both games. Money stays per player (rebuilt missions pay
  both), cheats stay local, and wanted levels come from the host's mission commands.

## After the story (no hurry)

- Side content: races, schools, vehicle side missions and the like.
- **Co-op pool** (the maintainer's wish): the two players play against each other. The game's pool is one player
  against an NPC, so two players need the minigame's state synced. Its script `POOL2` is 62,754 bytes (6,246 left),
  so the size-limit work above helps here too.

## Known limitations

The players' list is in the README (Known limitations). For the work here:

- Single-player (Steam) saves don't load: always a New Game. Only the host saves, at a safehouse and outside a
  mission.
- Alt+Tab pauses that player's game (the host's mission waits); the connection stays up and, since 0.1.1, the keys
  they held are released on the other screens.
- The friend's saved state is matched by nickname; under another nickname it isn't found. A `.coop` file has to move
  together with its save.
- Wi-Fi power saving can cause latency spikes of about 90 ms: use a cable or the Maximum Performance power plan.

## Maintenance

- ReVa (the Ghidra MCP server, set up 2026-10-16) runs only for this checkout and only locally; `claude mcp remove
  ReVa -s local` removes it. The development PC's `JAVA_HOME` points to a JDK folder that no longer exists (the JDK
  was updated to 21.0.12); ReVa uses its own setting, and `JAVA_HOME` can be set to
  `C:\Program Files\Java\latest\jdk-21`.
- The 11 new names of the Ghidra names pipeline aren't in the Ghidra project yet: run
  `pwsh -File tools/ghidra/update-project.ps1` while no session with ReVa is open in this checkout.
- The English installer, tunnel and package names (2026-10-18) reach players with the next release. Once nobody
  plays 0.1.2 or earlier, drop the old-name notes in `README.md`, `README.tr.md` and `docs/test-plan.md` ("`Kur.bat`
  in 0.1.2 and earlier" and the like); `Install.ps1`'s `Remove-LegacyFiles` can stay.
