# Porting a story mission to co-op

For the agents who port missions. It explains how upstream CoopAndreas turns a single-player mission into
a co-op one (learned by diffing the adapted missions against the decompiled 1.0 US `main.scm`), what the
co-op commands do in the client, what the other player sees without extra work, the patterns to reuse,
and how to compile and test. The mission list, status and porting order are in `docs/missions.md`.

## Hard rules

1. **Never break the story order.** A port changes only the inside of its own mission file
   (`CoopAndreas/scm/scripts/<SCRIPT>.txt`). The order missions unlock in, their start triggers (contact
   points, phone calls, markers in MAIN or in other scripts) and the progression and story flags stay
   exactly as in the original: no skipping, no reordering, no making a later mission available early,
   not even for testing. Missions are ported, tested and played in story order.
2. **Keep the players' saves loadable.** The players continue one co-op save from package to package. A
   port must not add global variables (`$name`; the three co-op globals below exist already), must not
   change MAIN (the scripts included in `main.txt` before `INITIAL.txt`), must not add, remove or reorder
   external scripts, and must not change the external scripts that run outside missions. Use mission
   locals (`0@`…`1023@`) for new state. `scripts/compile-scm.ps1` refuses a build that breaks saves.
3. **Stop and report** to the architect instead of working around rule 1 or 2: when a port seems to need
   a trigger, flag or MAIN change, a new global, a new external script, a new co-op command (client code),
   or more than the 69,000-byte mission buffer.
4. **Don't "fix" parameter orders** that look wrong in decompiled code (see Pitfalls); the compiler
   reorders them.
5. **Compile only with `scripts/compile-scm.ps1`**, never with the Sanny Builder GUI's Compile + Copy, which
   writes into the game's `data\script`. Don't commit `main.scm`/`script.img` unless the brief says so.

## Where the story progression lives (don't touch)

The game's mission flow is spread over the MAIN section and the end of each mission:

- **Trigger threads** in the MAIN section, one per strand, each in its own file: `INT`, `SWEET`, `CRASH`,
  `RYDER`, `SMOKE`, `STRAP`, `CESAR`, `LA1FIN`, `BCRASH`, `CAT`, `TRU`, `BCESAR`, `GARAGE`, `SCRASH`,
  `WUZI`, `SYND`, `STEAL`, `TRACE`, `ZERO`, `DESERT`, `PSCH`, `CASINO`, `VCRASH`, `DOC`, `HEIST`,
  `MANSION`, `GROVE`, `RIOT` (plus `SHOOT`, `GYM`, `TRUCKS`, `QUARRYS`, `BSCHOO`, `BIKES`, `R3`, `BLOODR`,
  `HOTR`, `KICKS`, `TRI`, `ODDVEH`, `BUY1`…`BUY_PRO` for side content). Each waits for `$onmission == 0`,
  its strand counter `$flag_<strand>_mission_counter` and the player in its contact marker, then calls
  `Mission.LoadAndLaunchInternal(n)`. `docs/missions.md` lists the thread and counter of every mission.
- **`FLOW`** (`scripts/FLOW.txt`): cross-strand gates. It starts `LA1FIN` when all Los Santos strands are
  done (`sweet 9, smoke 4, strap 5, ryder 3, crash 2, cesar 1`) and `RIOT` when `grove 2` and
  `mansion 4`.
- **Phone calls**: `MOB_LA1`, `MOB_LA2`, `MOB_CAT`, `MOB_SF`, `MOB_VEG`, `MOB_RAN`, `MOB_GF` and `CELLFON`.
  They start strand threads (`CRASH`, `CESAR`, `CAT`, `TRU`, `BCESAR`, `WUZI`, `TRACE`, `ZERO`, `DESERT`,
  `CASINO`) and add contact blips, gated on counters and flags such as `$driving_test_passed`.
- **The end of each mission** (its "mission passed" block): `$flag_<strand>_mission_counter += 1`,
  `Stat.PlayerMadeProgress`, `start_new_script @<NEXT STRAND>` (for example `SWEET2` starts `SMOKE`,
  `SWEET4` starts `RYDER`, `INTRO2` starts `SWEET`), contact blips and story flags. A port keeps this
  block as it is and adds its co-op lines next to it.
- **`BUYPRO1`** (property purchases): buying Wang Cars starts `STEAL`, buying Verdant Meadows starts
  `PSCH`.
- **`$onmission`** (global 409; Sanny Builder also calls it `$flag_player_on_mission`): the on-a-mission
  flag that the trigger threads, save points and the client's mission-flag sync read.

Upstream's existing co-op changes in MAIN, which stay: `INT` runs `Coop.EnableSyncingThisScript()`; the
`INT` and `SWEET` triggers use `Coop.LocateAllPlayersOnFoot3D` (every player in the marker, host only)
instead of `Char.LocateOnFoot3D($scplayer, …)`; `INTROST` (Grove Street voice-over) runs on the host only;
`IMPEXPM` differs only by a decompiler alias. Other strands' triggers still start a mission when the host
alone stands in the marker; changing that is a trigger change (rule 3).

## How co-op runs a mission

- The **host** is the first player connected to the server (`server/src/CNetworkPlayerManager.cpp`,
  `AssignHostToFirstPlayer`). Every client runs `main.scm` from a new game, but missions start only from
  the host's trigger threads: on the other client the `INT`/`SWEET` triggers use
  `Coop.LocateAllPlayersOnFoot3D` (false there), and the other strand threads are never started, because
  the mission ends that start them run only on the host. So only the host's counters and strand threads
  advance, and the host's game holds the story. (This follows from the scripts and the client code; it is
  not verified in game.)
- A mission script runs only on the host. The other player sees its effects through: entity sync
  (vehicles and peds the host creates become network entities), `COpCodeSync` (below), the packets the
  co-op commands send, and a few client hooks.
- **Either player dying or getting busted fails the mission**: the client replaces
  `CRunningScript::DoDeatharrestCheck` (`client/src/Hooks/PlayerHooks.cpp`) to treat any network player
  who is dead, arrested or dying as wasted/busted. No script code is needed for that.
- The host's `$onmission` is sent to the other clients (`client/src/Main.cpp`, `ON_MISSION_FLAG_SYNC`).
  When it drops to 0 the other client removes its co-op checkpoint and entity blips, turns widescreen off,
  gives the player back the controls and clears the fade (`client/src/PacketHandlers/scripts.cpp`).
- Mission dialogue audio played on the host (any script) is played on the other clients too
  (`client/src/Hooks/MissionAudioHooks.cpp`); its subtitles are `print_now` texts and are not.
- Coordinate and contact-point blips the host creates show on every client (`CNetworkStaticBlip`, radar
  sprites 0, 1, 5–40, 42–55, 58–63). Blips on cars, peds and objects do not.

### What `COpCodeSync` replays on the other client

`client/src/COpCodeSync.cpp`. After every command a script executes on the host, if the script
registered itself with `Coop.EnableSyncingThisScript()` (`1D12`, matched by the first 7 characters of the
script name) and the opcode is in the list below, the opcode and its parameters go to the other clients,
which execute it in a dummy script. Ped, player and vehicle handles are translated to network ids; a
command whose entity has no network instance on the receiver is dropped. The task commands marked T
(`CTaskSequenceSync`'s list) are sent from any script on any client, for peds that client controls or for
players; the host ignores every other opcode it receives.

| Area | Opcodes |
|---|---|
| Messages | `00BA` print_big, `00BE` clear_prints, `01E3` print_with_number_big, `0318` register_mission_passed, `03E5` print_help, `054C` load_mission_text, `0998` award_player_mission_respect |
| Population | `01EB` car density, `03DE` ped density, `0395` clear_area, `09D2` ambient crime, `06D0` emergency services, `014C` switch_car_generator |
| Camera | `015F` set_fixed_camera_position, `0160` point_camera_at_point, `02EB` restore_camera_jumpcut, `0373` set_camera_behind_player, `0920`, `0925`, `092F`, `0930`, `0936` (vector move/track), `0A0B` load_scene_in_direction, `052C` set_player_drunkenness |
| Scenes | `016A` do_fade, `02A3` switch_widescreen, `02E4` load_cutscene, `02E7` start_cutscene (deferred until loaded), `02EA` clear_cutscene |
| World, interiors | `0703` set_tag_status_in_area, `04BB` set_area_visible, `084D` activate_interior_peds |
| Mission wanted commands | `010D`, `010E`, `0110`, `01F7`, `01F0`, `03C7`, `0A24` (fork 0.3.101); PLAYER means the receiver's local player. Mission wanted checks still read the host. |
| Weapons | `06AB` hide_char_weapon_for_scripted_cutscene, `07A7` task_jetpack, `01B2` give_weapon_to_char, `01B9` set_current_char_weapon, `0555` remove_weapon_from_char |
| Tasks | `05C0` look_at_vehicle, `05CA`/`05CB` enter car as passenger/driver, `0603` go_to_coord_any_means, `0634`, `0673`, `0713` drive_by, `0792` clear_char_tasks_immediately, `0967`/`0968` facial talk |
| Tasks (T) | `05BA`, `05BC`, `05BD`, `05BF`, `05C9`, `05CD`, `05D3`, `05D4`, `05DE`, `0605`, `0633`, `0639`, `0647`, `0668`, `0687`, `06A9`, `0804`, `0812`, `0829`, `088A`, `0A1A`; task sequences (`open_sequence_task` … `perform_sequence_task`) of synced peds go through `CTaskSequenceSync` |
| Peds | `00A1` set_char_coordinates, `0173` set_char_heading, `0350` set_char_stay_in_same_place, `07A1` set_next_desired_move_state, `0245` set_anim_group_for_char, `0860` set_char_area_visible |
| Vehicles | `00AB` set_car_coordinates, `0175` set_car_heading, `070C` explode_car_in_cutscene |
| Other | `097A` report_mission_audio_event_at_position, `04ED`/`04EF` request/remove_animation, `01B4` set_player_control, `0109` add_score, `0247` request_model |

A player handle in these commands means "that player" on the receiver: `Char.SetCoordinates`,
`Char.GiveWeapon` or `Task.*` on `$NETWORK_PLAYER[i]` move, arm or task the other player's own ped in their
game, and `Player.SetControl($player1, …)`, `Player.AddScore($player1, …)` and
`Player.SetDrunkenness` apply to the receiver's local player. So in a synced mission script the other
player gets, without extra work: cutscenes, fades, widescreen, the camera, frozen and released controls,
big texts (mission title, M_PASS, M_FAIL), help boxes, the mission-passed money, respect and stats line,
weapons and teleports given to `$NETWORK_PLAYER[i]`, and tasks of synced peds.

Not replayed, so the other player doesn't see them unless the port adds co-op commands: `print_now` and
`print` (objective texts, dialogue subtitles), blips on entities, locate spheres and checkpoints. Not
replayed and without any co-op command today: on-screen timers and counters (`display_onscreen_timer`,
`display_onscreen_counter*`), `play_mission_passed_tune`, mission-specific HUD (bars, meters), and
`Coop.AddChatMessage` itself. Flag those in the port's report.
Crimes and the resulting wanted level remain per player; only the host's scripted wanted commands are replayed.

## Co-op commands (opcodes 0x1D00–0x1D1C)

Defined in the mode's `sa_coop.json` (`CoopAndreas` extension; `SASCM.INI` has only `1D00`–`1D0D`),
registered in `client/src/Commands/CCustomCommandRegistrar.h`, implemented in
`client/src/Commands/Commands/`. An opcode in `0x1D00`–`0x1DFF` without a registered handler crashes the
client. The commands that take a network player read its id without a null check: give them an invalid
handle and the host crashes, so always guard with `Coop.IsNetworkPlayerActorValid`. Per-player commands
send a packet that the server relays only from the host and only to that player.

| Opcode | Class syntax | What the client does |
|---|---|---|
| `1D00` | `Coop.AddChatMessage(text)` | Adds a line to the local chat (the host's only; not synced). |
| `1D01` | `Coop.GetNetworkPlayerChar(id)` | Ped handle of the network player with that player id. |
| `1D02` | `Coop.CollectNetworkPlayersForTheMission()` | Up to 3 ped handles of the other players (0 for empty slots); store them in `$NETWORK_PLAYER[0..2]`. |
| `1D03` | `Coop.IsNetworkPlayerActorValid(char)` | Condition: the handle is non-zero, exists and is a network player. |
| `1D04` / `1D05` | `Coop.UpdateCarBlipForNetworkPlayer(player, car, friendly, display, color, scale)` / `Coop.RemoveCarBlipForNetworkPlayer(player, car)` | Show / remove a blip on a network vehicle for that player only. |
| `1D06` / `1D07` | `Coop.UpdateCharBlipForNetworkPlayer(player, char, …)` / `Coop.RemoveCharBlipForNetworkPlayer(player, char)` | The same for a network ped. |
| `1D08` | `Coop.PrintForNetworkPlayer(gxt, time, flag, player)` | `print` on that player's screen. |
| `1D09` | `Coop.PrintBigForNetworkPlayer(gxt, time, style, player)` | `print_big` on that player's screen. |
| `1D0A` | `Coop.PrintNowForNetworkPlayer(gxt, time, flag, player)` | `print_now` on that player's screen (objective texts). |
| `1D0B` | `Coop.PrintHelpForNetworkPlayer(gxt, player)` | `print_help` on that player's screen. |
| `1D0C` | `Coop.ClearThisPrintForNetworkPlayer(gxt, player)` | Clears that text (small and big) on that player's screen. |
| `1D0D` | `Coop.ClearAllEntityBlipsForNetworkPlayer(player)` | Removes all co-op entity blips of that player. |
| `1D0E` | `Coop.IsHost()` | Condition: this client is the host. |
| `1D0F` | `Coop.LocateAllPlayersOnFoot3D(sphere, x, y, z, rx, ry, rz)` | Condition, host only (false elsewhere): the local player and every network player are on foot in the box. |
| `1D10` / `1D11` | `Coop.UpdateCheckpointForNetworkPlayer(x, y, z, rx, ry, rz, player)` / `Coop.RemoveCheckpointForNetworkPlayer(player)` | Draws / removes the highlighted area (the locate marker) on that player's screen; one per player, a new one replaces the old. |
| `1D12` | `Coop.EnableSyncingThisScript()` | Registers the running script's name for `COpCodeSync` (see above). |
| `1D13` / `1D14` | `Coop.GetVehicleNetworkId(car)` / `Coop.GetPedNetworkId(char)` | Network id, or -1 while the entity has no network instance yet. |
| `1D15` | `Coop.GetPedInAreaWithModel(x, y, z, rx, ry, rz, m1, m2, m3)` | First non-player ped in the box with one of the models (-1 = any), else -1. |
| `1D16` / `1D17` | `Coop.ClaimPedOnRelease(char)` / `Coop.CancelPedClaim(char)` | Network ped ownership hand-over when its controller releases it. |
| `1D18` | `Coop.IsSyncingThisPed(char)` | Condition: the ped has a network instance on this client. |
| `1D19` | `Coop.PedResetAllClaims(char)` | Clears the ped's claims (controller or host). |
| `1D1A` | `Coop.PedTakeHost(char, allowReturn)` | Host only: takes control of a network ped. |
| `1D1B` | `Coop.GetNetworkPlayerInternalId(char)` | The `$player` index of a network player (for `Player.*` commands), -1 if not one. |
| `1D1C` | `Coop.TeleportPlayersToHostSafely(p1, p2, p3)` | Host only: puts the given network players on free spots around the host (-1 or invalid handles are skipped). |

The globals for co-op state already exist (declared in `main.txt`): `$NETWORK_PLAYER[3]`,
`$NETWORK_PLAYER_VEHICLE[3]` and `$temp_int` (a loop counter shared by all ported missions; don't keep a
value in it across a `wait`).

## How upstream adapts a mission (patterns)

Taken from `INTRO1` (Big Smoke), `INTRO2` (Ryder), `SWEET1` (Tagging Up Turf) and `SWEET1B` (Cleaning
The Hood); read those files for complete examples. Upstream writes for up to three network players.

**Start**: sync the script and collect the players.

```
:SWEET1
script_name 'SWEET1'
Coop.EnableSyncingThisScript()
...
Stat.RegisterMissionGiven
$NETWORK_PLAYER[0], $NETWORK_PLAYER[1], $NETWORK_PLAYER[2] = Coop.CollectNetworkPlayersForTheMission()
$onmission = 1
```

**Per-player blocks**: loop and guard every use of a network player.

```
for $temp_int = 0 to 2
    if
        Coop.IsNetworkPlayerActorValid($NETWORK_PLAYER[$temp_int])
    then
        Char.GiveWeapon($NETWORK_PLAYER[$temp_int], WeaponType.SprayCan, 30000)
    end
end
```

**Placing the second player** at mission start, around cutscenes and at checkpoints:
`Char.SetCoordinates`/`Char.SetHeading` on `$NETWORK_PLAYER[i]` (replayed into their game), with
`Char.ClearTasksImmediately` first and `Char.SetAreaVisible(…, 0)` when leaving an interior, or
`Coop.TeleportPlayersToHostSafely($NETWORK_PLAYER[0], $NETWORK_PLAYER[1], $NETWORK_PLAYER[2])` to put them
next to the host. Upstream gives each player their own fixed spot near the host's.

**Cutscenes**: `Cutscene.Load`/`Start`/`Clear` replay in a synced script. Move the other players to the
scene's end position before or right after (`INTRO1` sets them near the house after `INTRO1A`), and hide
their weapons with `Char.HideWeaponForScriptedCutscene` like the host's.

**Entities the mission creates**: wait until they have network ids before giving them tasks or blips
for the other player, or the replayed commands are dropped:

```
$sweet_car = Car.Create(492, 2508.16, -1666.47, 13.0)
$temp_int = Coop.GetVehicleNetworkId($sweet_car)
while $temp_int == -1
    wait 0
    $temp_int = Coop.GetVehicleNetworkId($sweet_car)
end
```

**Vehicles with both players**: when the mission's car has free seats, let the players ride in it (two
players in Sweet's car in `SWEET1`; a third player gets Ryder's car). When it doesn't (bikes, full cars,
single-seat vehicles), give each network player their own vehicle in `$NETWORK_PLAYER_VEHICLE[i]` with a
blue blip, as `INTRO1` does with the BMXs, and clean them up at the end.

**Objective texts and dialogue**: every `Text.PrintNow` the player should read gets a
`Coop.PrintNowForNetworkPlayer` with the same key for each network player; clear them with
`Coop.ClearThisPrintForNetworkPlayer`. Dialogue subtitles printed from a text array work the same way
(`INTRO1` sends `$intro1_chat(86@,14s)`).

**Blips and markers**: entity blips with `Coop.UpdateCarBlipForNetworkPlayer` /
`Coop.UpdateCharBlipForNetworkPlayer` (friendly blue for allies, red for targets), removed when the host's
are; destinations with `Coop.UpdateCheckpointForNetworkPlayer` next to the host's locate sphere; per
player state (has this player's blip been switched?) in a local array indexed by `$temp_int`, for example
`200@($temp_int,3i)`.

**Progress conditions for everyone**: where the original waits for the player at a place or in a car,
check the network players too and combine the results in a local flag (`INTRO1`, `SWEET1`):

```
150@ = 1
for $temp_int = 0 to 2
    if
        Coop.IsNetworkPlayerActorValid($NETWORK_PLAYER[$temp_int])
    then
        if
            not Char.LocateAnyMeans3D($NETWORK_PLAYER[$temp_int], 0, 2089.01, -1649.08, 12.54, 10.0, 10.0, 10.0)
        then
            150@ = 0
        end
    end
end
```

**"Too far away" warnings** for each player, throttled with a game-timer value per player (`INTRO1`).

**Fail conditions**: a dead or busted player already fails the mission (client hook). The mission's own
fail checks (an ally dead, a vehicle destroyed) stay on the host as in the original; send the reason to
the other player with `Coop.PrintNowForNetworkPlayer` (`SWEET1`: 'SWE1_Q', 'SWE1_P'). `Text.PrintBig('M_FAIL', …)`
replays by itself.

**Counters, timers, wanted level**: no co-op command exists; the other player doesn't see on-screen
counters or timers, and wanted levels stay per player. Keep the host's logic; mention it in the report.

**Mission end**: leave the passed block's counter, flags and `start_new_script` lines alone; add the
co-op cleanup next to the host's: for each valid network player `Coop.RemoveCheckpointForNetworkPlayer`,
`Coop.ClearAllEntityBlipsForNetworkPlayer`, give or remove mission weapons, then `$NETWORK_PLAYER[i] = 0`.

**Shared ambient peds** (upstream's shop scripts): before creating a shopkeeper, look for one the other
player's game already synced with `Coop.GetPedInAreaWithModel`, claim it with `Coop.ClaimPedOnRelease`,
and at cleanup delete it only if `Coop.IsSyncingThisPed`, else `Coop.CancelPedClaim`.

**Unsupported warning**: upstream marks unported missions with two `Coop.AddChatMessage` lines; they
belong after `script_name` inside the mission, not before the label (see Pitfalls).

## Pitfalls

- **Parameter order.** The mode uses Sanny Builder's "custom" parameter order: for opcodes whose
  `SASCM.INI` line lists the parameters out of order, the class syntax follows that line, not the bytecode
  order, and not the order of the parameter names in `sa_coop.json`. 85 opcodes are affected. Examples:
  `0702=5,%5d% = get_tags_painted_percentage_at %2d% %3d% %4d% %1d%` makes
  `World.GetPercentageTaggedInArea(89@, 90@, 91@, 88@)` compile to the bytes 88@, 89@, 90@, 91@, which is
  what vanilla has; `016A=2,fade %2d% time %1d%` makes `Camera.DoFade(1, 1000)` a fade-in of 1000 ms.
  The decompiler attaches enum names by the JSON order, hence oddities like `Camera.DoFade(0, Fade.Out)`
  (fade out over 0 ms). The source as decompiled compiles back to the original bytes (checked byte for
  byte), so never reorder such arguments by hand; when writing a new call, check the opcode's line in
  `.tools/SannyBuilder4/data/sa_sbl_coopandreas/SASCM.INI`.
- `sa_coop.json` names `1D10`'s parameters wrongly (an extra `drawSphere` first); it takes the 7 parameters
  shown above.
- Decompiler artifacts in the sources are harmless; leave them: floats rounded to 7 digits by upstream's
  decompile/recompile, `SeatIndex.TandemRear`/`FrontRight` (both 0), junk after the text in
  `Debugger.SaveStringToDebugFile` strings, `$onmission` vs `$flag_player_on_mission`.
- Upstream placed the warning lines of `RYDER3`, `RYDER2`, `SMOKE2` and `SMOKE3` before the mission label;
  they compile into the previous mission after its last `return`, never run and count toward that
  mission's size. Put new code after the label.
- Labels are global in the compile: give new labels unique names (for example `@SWEET2_COOP_1`).
- `Coop.LocateAllPlayersOnFoot3D` is false on every client but the host; `Coop.TeleportPlayersToHostSafely`
  and `Coop.PedTakeHost` are host-only.
- Sanny Builder's CLI logs only the last error and still writes output files; `compile-scm.ps1` handles
  that. Model names such as `#GREENWOO` need the game's IDE files, which the script points it at; the
  decompiled code uses numbers for vehicles and peds, and `#NAMES` only for objects in the header's
  object table.

## Size budgets

From the committed `main.scm`; `compile-scm.ps1` prints these after every compile and refuses a build
over a limit. Limits: plugin-sdk `CTheScripts.h` (`ScriptSpace[200000]`, `MissionBlock[69000]`,
`LocalVariablesForCurrentMission[1024]`) and gta-reversed `TheScripts.h`.

| Budget | Used | Limit | Left |
|---|---|---|---|
| MAIN (header + main script) | 194,184 | 200,000 | 5,816 (but MAIN must not change; rule 2) |
| Largest mission (`DRUGS4`, Reuniting The Families) | 68,439 | 69,000 | **561** |
| Most local variables in a mission | 964 | 1,024 | 60 |
| Largest external script (`VIDPOK`, video poker) | 35,122 | none (each gets its own buffer, gta-reversed `StreamedScripts.cpp`) | |

Upstream's adaptations made missions 14–29 % larger (3–4.7 KB). `docs/missions.md` lists each mission's
free bytes, the five largest missions and the options when a port doesn't fit.

## Saves

### What a save depends on

gta-reversed `CTheScripts::Save`/`Load` (0x5D4C40 / 0x5D4FD0) and `SCMChunks.hpp`:

- the whole global variable chunk of `main.scm` is written to the save and read back as raw bytes, so the
  number and meaning of the globals must stay;
- every running script that is not an external script is saved with its instruction pointer and gosub
  stack as offsets into the script space; those are the MAIN threads (missions can't run during a save),
  so any change to MAIN, or anything before it in the header that moves it, puts those offsets into the
  wrong code (the crash the Steam saves show);
- the brain table (`tScriptForBrains`, registered by MAIN at game start) refers to external scripts by
  their index in the `main.scm` list, so the list's names and order must stay;
- the mission table is saved but ignored on load, and external scripts are not saved at all (they start
  again from their beginning), so mission code, mission offsets and the external scripts' code and sizes
  may change.

`scripts/compile-scm.ps1` compares exactly these parts of the new `main.scm` with the one the saves were
made with (`-CompareTo`, by default the `main.scm` in HEAD) and copies nothing when they differ: the
global variable chunk, the used-object names (the first entry is the compiler's signature and ignored),
the mission count and MAIN size, the external script names in order, the rest of the header, and the MAIN
code byte for byte. It tolerates bytes after the NUL of 8-byte string literals, where upstream's Sanny
Builder 4.1.0 left junk that 4.2.0 writes as zeros and the game never reads. Changing an external script
that runs outside missions passes this check, because such scripts aren't saved, but it changes behaviour
for every mission and every player; that is why rule 2 still sends it to the architect.

### How co-op saving works today

`client/src/Hooks/SaveLoadHooks.cpp`:

- The save slot's name gets a "(Coop) " prefix (hook on the text lookup at 0x5D1BB2), so co-op saves are
  recognisable in the load menu.
- The ped pool is saved with only the local player's ped (the original saves every `PED_TYPE_PLAYER1`
  ped, and network players are of that type, which broke loading).
- Everything else is the single-player save: globals, MAIN threads, world state.

The other players' own state is kept next to the host's save, in `GTASAsf<n>.b.coop` (fork, 0.3.103;
`client/src/CPlayerStateSave.*`): their weapons and ammo, health, armour, money, clothes (with haircut and
tattoos), fighting style, the perks `CPlayerInfo` keeps (max health and armour, infinite sprint, fast reload,
fireproof, free health care, get out of jail free) and their own stats (fat, muscle, stamina, max health, sex
appeal, appearance, the weapon skills, driving, flying, bike and cycling skill, lung capacity, gambling).
Position, wanted level, respect, the story, properties and the city unlock are not part of it.

- Each other player's game sends its state to the host when it changes (checked every 2 s), once it is in
  plain free play (after a new game's intro).
- After a save succeeds, the host writes every state it knows: this session's latest, and the loaded save's
  entries for players not seen since. The file names the `.b` file it belongs to by size and hash, so it
  never pairs with another save; it is written to a temporary file and renamed, and removed when there is
  nothing to keep. A save made by a player who isn't the host gets no `.coop` file.
- After a load, the host reads the file (a missing, unreadable or foreign file is ignored). A player gets
  their stored state back when they connect (a new session, a reconnect after a crash, a restart of their
  game) and when the host loads a save that stores them: loading an older save rolls them back. The state is
  applied when they are standing on foot in free play; a chat line says so on both sides.
- The `.b` file, MAIN, the globals and the ped pool are unchanged, so saves stay loadable both ways; a save
  without a `.coop` file loads as before. A player is known by the nickname in `coopandreas.ini`: after
  changing it their state isn't found. Moving a save to another computer means moving its `.coop` file too.

Who should save: **the host**, at a save point, outside a mission. The story (counters, flags, strand
threads) only advances in the host's game, so a save the other player makes holds their stale story state
and must not be loaded as the host's game.

When the host loads a save, only the host's game loads it; the other client keeps its own game and gets
the state the save keeps for them (above). What else the other player sees then is unverified: their player
and the host's synced vehicles and peds should follow the network sync, and whether they need to reconnect
is not known. Also unverified: after a load, MAIN threads that registered with
`Coop.EnableSyncingThisScript()` at their start (`INT`) are not registered again in that session, because
the registration is a runtime list in the client. Old single-player saves don't load with the co-op
`main.scm` (CLAUDE.md, known limitations).

## Compile

```
pwsh -File scripts/compile-scm.ps1                      # into CoopAndreas/scm of this checkout
pwsh -File scripts/compile-scm.ps1 -OutDir $env:TEMP\x  # into a scratch folder
```

It runs Sanny Builder 4 (`.tools/SannyBuilder4`, mode `sa_sbl_coopandreas`; from a worktree the main
checkout's `.tools`) on its own invisible desktop, so nothing pops up on the screen of a player who is in
the game, and takes about 10 s. It fails, and copies nothing, when Sanny Builder logged an error (the
message shows `compile.log`, which keeps only the last error: fix it and rerun), when a dialog stays open
or the run times out, when the scripts exceed the budgets above, or when saves would break. It refuses to
start while any `sanny.exe` runs, refuses an output folder inside a game folder, and never writes into the
game copy (it reads the IDE files of `C:\Games\GTASA_Coop`). Sanny Builder's own settings
(`.tools/SannyBuilder4/data/settings.ini`) keep the game path, no update checks, and the Report and IMG
dialogs off. Run it from pwsh 7, or from Windows PowerShell 5.1 started without pwsh's `PSModulePath`.

The same sources always compile to the same bytes. Recompiling the unchanged sources reproduces the
committed `main.scm` and `script.img` except the compiler signature in the object table ("SANNY BUILDER
4.2.0" instead of 4.1.0) and the string padding bytes described above.

## Per-mission checklist

1. Read the mission's row in `docs/missions.md` (risks, free bytes) and the whole source file.
2. Check it is the next mission in story order and that the previous one is ported and tested.
3. Add `Coop.EnableSyncingThisScript()` after `script_name` and collect the players where the mission
   sets `$onmission = 1`.
4. Go through the mission phase by phase. For each: where is the second player (place them), what must
   they see (texts, blips, checkpoints), which vehicle do they use, what must they do for the mission to
   go on (combine the conditions), and what fails the mission.
5. Wait for network ids of created entities before tasking or blipping them for the other player.
6. Cutscenes: position and un-arm the other players around them.
7. Mission end: leave the passed block's progression untouched, add the co-op cleanup.
8. New state only in mission locals; no new globals, no edits outside the mission's file.
9. Compile with `scripts/compile-scm.ps1`; it must pass the size and save checks. Note the free bytes.
10. Report: what the other player sees and does in each phase, what they can't (timers, wanted checks,
    solo minigames), anything that needed rule 3, and the in-game test steps below.

## In-game test (by the user and the friend)

Mark it "oyunda doğrulanmadı" until the players have run it. The architect deploys the build; then:

1. The host starts `server.exe` (or the tunnel setup) and the game, the friend joins; load the co-op save
   from just before the mission (host only).
2. Both players walk to the mission marker; the mission starts on the host.
3. In each phase: the friend sees the objective text, the blips and the checkpoint, sits in or gets the
   right vehicle, sees the cutscenes, and is teleported where the mission needs them.
4. Fail cases: the friend dies (mission fails for both), the mission's own fail (an ally dies or the car is
   destroyed) shows its text on both screens.
5. Pass: "Mission passed" on both screens, money and respect for both, the next mission's contact appears
   for the host as in single player.
6. After the mission the friend has no leftover blip, checkpoint, frozen controls or black screen.
7. Save at a save point (host), quit, reload the save, check the story continues.

A crash leaves `C:\Games\GTASA_Coop\CoopAndreas_crashes\*.log` with the script IPs; `tools/scm_at_ip.py`
maps them to a script. A hang needs `--coopdiag` (CLAUDE.md, Debugging).
