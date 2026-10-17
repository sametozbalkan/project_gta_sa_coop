# Mission mirror layer (design)

Status: designed 2026-10-16 on main `f5cffae`; M1–M4 implemented for 0.1.0 (M5 later). Where the code differs from this
design, `README.md` section 7 says so. The user approved
the idea; the architect's decisions on section 9 and the merged plan with the world-state registry are in
`README.md` in this folder.

The model stays as it is: missions run only on the host, the other players see their reflection. The mirror layer
makes everything the host's mission shows on screen appear on the other players' screens, in client code, with no
lines in mission scripts. Ports then only make gameplay decisions, and an unported mission becomes followable.

Sources used: the client and server code in this checkout, the mission sources under `CoopAndreas/scm/scripts`,
plugin-sdk, gta-reversed, the ReVa project of the 1.0 US `gta_sa.exe`, `dumpbin` disassembly, and bytes read from
`C:\Games\GTASA_Coop\gta_sa.exe`. Every address below names where it was checked.

## 1. Goals and non-goals

Goals:

- Mirrored automatically for every mission the host runs (section 3.2 says which scripts count):
  - objective, dialogue and fail texts, help boxes, mission titles, "Mission passed" with its tune;
  - blips on vehicles, characters, objects, pickups and coordinates, with every colour, scale, display, sprite,
    friendly and interior change, and their removal;
  - locate spheres (the yellow mission markers) and the other 3D markers missions draw;
  - on-screen timers and counters with live values;
  - mission dialogue audio (mirrored today already).
- An unported mission reaches "the friend sees the objectives and can help".
- Ports shrink to gameplay decisions; #1–#10 are converted and their display relays removed.
- Mission scripts do not grow. Reuniting The Families (#27, 561 bytes free) becomes portable with gameplay lines only.
- Host-authoritative: only the host sends mirror data, receivers only display it and never send it back.
- Saves: the layer needs no SCM change; the conversion of #1–#10 changes mission code only (no globals, no MAIN).

Non-goals (first version):

- Cutscenes, camera, fades, widescreen and player control. They stay with `Coop.EnableSyncingThisScript`
  (`COpCodeSync`), because they need placement decisions a port makes.
- Gameplay state: money, respect, the mission-passed stat, wanted commands, tasks, weapons, positions (section 9, D3).
- Immediate-mode script drawing: `display_text` and its variants, `draw_sprite`, rectangles. They appear in the
  driving and flying schools, the lowrider game in #10 and the race HUD of `CPRACE` (and in `INTRO`'s unreachable
  body), all solo minigames.
- Radar and HUD toggles (`display_radar`, `display_hud`, zone and car names, `set_radar_zoom`, the two blip-level
  switches). They are view settings tied to scenes; the friend may be driving while the host's scene runs.
- Choosing per message whether the friend should see it. The user accepted that host-only messages show too.

## 2. Inventory

Method: a scan of the class-syntax calls in the mission sources, in the story order of `docs/missions.md`
(102 scripts; #37 and #38 share `BCESAR4`). Calls in source, not executions. `INTRO` (#1) keeps the original mission
body after its early `terminate_this_script`; that unreachable code is counted, so "first #1" can mean that dead code.
"Mirror" says how the layer covers the command.

### 2.1 Texts

| Opcode | Command | Calls | Missions | First | Mirror |
|---|---|---:|---:|---|---|
| `00BC` | print_now | 2,324 | 97 | #1 | text |
| `00BE` | clear_prints | 1,070 | 89 | #1 | text |
| `03D5` | clear_this_print | 486 | 65 | #1 | text |
| `00BB` | print | 363 | 45 | #8 | text |
| `03E6` | clear_help | 219 | 46 | #1 | text |
| `03E5` | print_help | 173 | 45 | #1 | text (synced today for registered scripts) |
| `00BA` | print_big | 155 | 100 | #2 | text; from MAIN threads only style 2 (mission titles) |
| `054C` | load_mission_text | 107 | 102 | #1 | text (receivers need the table for the keys) |
| `01E3` | print_with_number_big (M_PASS) | 91 | 90 | #2 | text |
| `0512` | print_help_forever | 91 | 23 | #4 | text |
| `03EB` | clear_small_prints | 25 | 7 | #23 | text |
| `01E5` | print_with_number_now | 17 | 8 | #20 | text |
| `03D6` | clear_this_big_print | 8 | 3 | #15 | text |
| `0A0E` | clear_this_print_big_now | 5 | 5 | #25 | text |
| `01E4` | print_with_number | 4 | 3 | #15 | text |
| `02FD` | print_with_2_numbers_now | 4 | 1 | #66 | text |
| `09C1` | add_next_message_to_previous_briefs | 3 | 2 | #66 | text |
| `0217`, `02FF`, `0302`, `0303`, `0308`, `036D`, `0384`, `0513` | other print variants | 0 | – | – | text (listed for completeness) |
| `0A2C`, `0A19` | draw_subtitles_before_fade, set_area_name | 3 | 2 | #65 | no (render flags the host's mission cleanup resets) |

Mission titles are printed by the MAIN strand threads, not by the missions: 121 `print_big` calls with style 2
(BottomRight) in 34 MAIN-section threads (`INT`, `SWEET`, `SMOKE`, `RYDER`, … and side activities such as `ODDVEH`).
Each is a title: 116 sit within eight lines before a mission launch, the other five are the `CAT` and `TRI` titles.
Eleven more style-2 calls are inside missions; no external script uses the style.

Immediate text, not mirrored: `display_text` 90 calls in 4 scripts, `display_text_with_number(s)` 15, `use_text_commands`
15, `draw_sprite` 46 (the schools), plus 39 in `CPRACE`.

### 2.2 Blips

| Opcode | Command | Calls | Missions | First |
|---|---|---:|---:|---|
| `0187` | add_blip_for_char | 588 | 63 | #2 |
| `018A` | add_blip_for_coord | 562 | 89 | #3 |
| `0186` | add_blip_for_car | 319 | 69 | #1 |
| `04CE` | add_short_range_sprite_blip_for_coord | 78 | 10 | #3 |
| `02A8` | add_sprite_blip_for_coord | 48 | 12 | #1 |
| `0188` | add_blip_for_object | 41 | 15 | #15 |
| `03DC` | add_blip_for_pickup | 38 | 20 | #9 |
| `02A7` | add_sprite_blip_for_contact_point | 30 | 29 | #1 |
| `0570` | add_short_range_sprite_blip_for_contact_point | 10 | 10 | #2 |
| `06C4`, `0161`, `0888` | searchlight, car (old), dead char | 8 | 4 | #29 |
| `0164` | remove_blip | 2,598 | 102 | #1 |
| `018B` | change_blip_display | 445 | 42 | #1 |
| `07E0` | set_blip_as_friendly | 281 | 77 | #1 |
| `0168` | change_blip_scale | 241 | 11 | #14 |
| `08DC` | set_blip_entry_exit | 94 | 15 | #3 |
| `08FB` | set_coord_blip_appearance | 90 | 12 | #9 |
| `07BF` | set_blip_always_display_on_zoomed_radar | 81 | 3 | #59 |
| `0165` | change_blip_colour | 71 | 19 | #4 |

All of them are covered through the radar trace state (3.5), so the change commands need no list of their own.
Not mirrored: `set_radar_zoom` (42 calls, 11 missions), `show_blips_on_all_levels` and `hide_all_frontend_blips` (8).

### 2.3 Spheres, checkpoints and markers

| Command | Calls | Missions | First | Mirror |
|---|---:|---:|---|---|
| locate and area commands with the sphere flag set | 196 (168 literal, 28 variable) | 79 | #2 | areas |
| of which `00FE`, `0100`, `00FF`, `01AF`, `0102`, `01B0` | 78, 30, 22, 27, 7, 9 | | | |
| of which entity-relative (`0104`, `0206`) | 4 | | | |
| `03A1` draw_sphere | 10 | 2 (TWAR7 #11, WUZI5 #60) | #11 | areas |
| `06D5`/`06D6`/`07F3` create/delete/move race checkpoint | 49 / 64 / 1 | 9, plus `CPRACE` | #22 (`CPRACE`, the race `CESAR` launches after #22's intro) | areas (later package) |
| `03BC`/`03BD` add/remove sphere | 2 / 4 | 1 (HEIST5 #83) | #83 | areas (later package) |

The other 1,810 locate calls draw nothing.

### 2.4 Timers and counters

| Opcode | Command | Calls | Missions | First |
|---|---|---:|---:|---|
| `0151` | clear_onscreen_counter | 115 | 34 | #8 |
| `014F` | clear_onscreen_timer | 65 | 18 | #15 |
| `03C4` | display_onscreen_counter_with_string | 41 | 28 | #8 |
| `03C3` | display_onscreen_timer_with_string | 23 | 15 | #15 |
| `0396` | freeze_onscreen_timer | 18 | 4 | #15 |
| `04F7` | display_nth_onscreen_counter_with_string | 7 | 5 | #16 |
| `014E` | display_onscreen_timer | 5 | 3 | #42 |
| `0890` | set_timer_beep_countdown_time | 3 | 2 | #43 |
| `059C` | set_onscreen_counter_flash_when_first_displayed | 1 | 1 | #15 |

The two-counter commands (`07B5`–`07B8`, a "value / maximum" display) and `set_onscreen_counter_colour` (`093C`) are
not used by the story; the state copy covers them anyway.

Vanilla timers and counters always read a global variable: the handlers take its offset with
`GetIndexOfGlobalVariable` (0x464700) and pass it as a 16-bit value (dumpbin of the 014E/03C3/03C4 handlers at
0x47C284, 0x484C60–0x484CFC). The bars in #8 (vehicle health), #9 (Sweet's health), #15 (noise) and the side missions
are counters. All covered through the timer state (3.7).

### 2.5 Mission end, HUD, audio

| Command | Calls | Missions | First | Mirror |
|---|---:|---:|---|---|
| `0394` play_mission_passed_tune | 95 | 92 | #2 | text path (it has no entity parameter) |
| `0318` register_mission_passed | 97 | 96 | #2 | no: a stat (D3) |
| `0998` award_player_mission_respect | 65 | 65 | #2 | no: game state (D3) |
| `0109` add_score | 54 | 49 | #1 | no: game state (D3) |
| `03E7` flash_hud_object | 5 | 2 | #3 | text path (a harmless one-shot) |
| `0581` display_radar, `0826` display_hud, `09BA`/`09B9` zone/car names | 76 | 17 | #1 | no |
| `03CF`/`03D1`/`040D` load/play/clear mission audio | 888 / 589 / 1,334 | 91 | #1 | mirrored today |
| `097A`, `097B`, `09F1`, `09F7` report mission audio event | 158 | 37 | #3 | no: sound effects |

Dialogue audio is mirrored today by `client/src/Hooks/MissionAudioHooks.cpp` (call sites 0x4851BB preload and
0x48521C play, host only, every script): the receiver preloads the same slot and plays it when loaded. That is cheap
and worth keeping. The subtitle of a line is a `print_now` of the line's key, printed by the mission itself or by the
MAIN dialogue helpers `AUDIOL`, `A_CONT` and `A_ALAP`, which missions start for that purpose (`AUDIOL` alone is started
102 times by `RYDER2`; also by #10, #33, #42, #48 and others).

### 2.6 What the story needs first

Texts, entity and coordinate blips and locate spheres from #1. Counters from #8 (today "host-only" in the #8 and #9
ports). Pickup blips from #9. `draw_sphere` from #11. Object blips and timers from #15. Race checkpoints from #22
(`CPRACE`). Script spheres from #83. The side missions (taxi, paramedic, vigilante, firefighter) use timers, counters
and many blips, which makes them a test bed outside the story order.

Display load of the first ten missions (texts / blip commands / spheres drawn / timer and counter commands / co-op
display commands): #1 28/10/0/0/0, #2 58/30/5/0/22, #3 79/88/4/0/18, #4 121/76/5/0/53, #5 72/47/3/0/44,
#6 89/54/3/0/23, #7 172/63/3/0/19, #8 33/192/5/10/15, #9 23/46/2/3/13, #10 38/32/2/2/10.

## 3. Architecture

### 3.1 Overview

The recommendation is a mix:

- **Texts at the opcode level**, through the existing `COpCodeSync` replay. The command carries the GXT key and the
  numbers; the receiver runs the same command in its dummy script. No new packet.
- **Blips, timers and counters as engine state.** The host learns at creation which radar traces and timer entries
  belong to its mission, then compares their state every frame and sends what changed. Every colour, scale, display,
  appearance and removal comes along without listing the change commands, the join snapshot is the same state, and a
  blip whose entity has no network id yet is fixed by the next difference instead of by a waiting loop in the script.
- **Spheres at the engine call sites.** Locate spheres are drawn again in every frame the script evaluates the locate,
  so the host records the calls the mission makes in that frame and sends the set when it changes.

Pure opcode level would need a mapping for every blip handle in script variables and a model of each change command;
pure engine level loses the GXT key (`CMessages` only gets the decoded text, and SA's GXT stores key hashes, not keys).

Data flow:

```
host game                                     server                 other players
script command ─┬─ COpCodeSync post-command ──┬─ OPCODE_SYNC (texts) ─┬─ relay ─┬─ replay in dummy script
                │   (ownership of blips,      │                       │         │
                │    timers; mission family)  └─ per-frame difference │         ├─ blips  (CMirrorBlips)
                └─ sphere call sites ─────────── MISSION_MIRROR_*  ───┘ host    ├─ areas  (CMirrorAreas)
                                                 (SCRIPT, reliable)     only    └─ timers (CMirrorHud)
```

### 3.2 Which calls are the host's mission

Every script command runs through one call: `CRunningScript::Process` (0x469F00) calls the handler at 0x469FF5 after
`mov ecx,esi` at 0x469FF3 (bytes `8B CE FF D2`, ECX = opcode, ESI = script; ReVa decompile). `COpCodeSync` already
redirects 0x469FF3: it stores the script and opcode before the handler and calls `BuildAndSendOpcode` after it. The
layer adds one call at the start of `BuildAndSendOpcode` (`CMissionMirror::OnCommandExecuted`) and an accessor for the
script that is running a command (needed by the sphere hooks, which run inside handlers).

A script belongs to the host's mission (the "family") when the local player is the host, the command is not a
network replay (`COpCodeSync::bProcessingNetworkOpcode`), the current mission is not excluded (D2), and one of:

1. it is the mission script: `m_bIsMission` (+0xDC) is set; the `load_and_launch_mission` (0417) handler sets it on
   the script it starts, together with +0xC6, `m_pBaseIP` (+0x10) = 0xA7A6A0 and `bAlreadyRunningAMissionScript`
   (0xA444B1) = 1 (dumpbin 0x4899F0–0x489A14);
2. its instruction pointer (+0x14) lies in the mission block, 0xA7A6A0 to 0xA7A6A0 + 69,000 (the 0417 handler reads
   0x10D88 bytes there, 0x4899D1): a sub-script of the mission;
3. it is one of the MAIN dialogue helpers `AUDIOL`, `A_CONT`, `A_ALAP` (by `m_szName`, +0x08) while a host mission runs
   (D5). Missions start them to play a line and print its subtitle.

In addition, `print_big` with style 2 counts from any host script: that is how the MAIN strand threads print the
mission title right before they launch the mission (D4).

There is no general "started by the mission" rule: at their end missions start the next strand thread (`SWEET4`
starts `RYDER`, `TRUTH2` starts `GARAGE` and `MOB_SF`), and some start streamed scripts (parachute, dealer) whose texts
are for the host. A generic rule would pull those in; the instruction pointer and the three names cover what missions
start for their own display.

The host records the mission when 0417 executes: the new mission script is the head of `CTheScripts::pActiveScripts`
(0xA8B42C; `StartNewScript` 0x464C20 inserts at the head, ReVa decompile), and the mission id is `ScriptParams[0]`
(0xA43C78), which the handler at 0x489960–0x489AA9 does not write (to confirm in the implementation). The mission ends
when that script leaves the active list (checked once per frame); `mission_has_finished` (00D8, handler 0x46945E)
runs `CMissionCleanup::Process` (0x468560, call at 0x469471) just before.

| Source of a display call | How it is told apart | Mirrored |
|---|---|---|
| The mission script | `m_bIsMission` | yes |
| A sub-script of the mission | instruction pointer in the mission block | yes |
| `AUDIOL`, `A_CONT`, `A_ALAP` during a mission | name, mission running | yes |
| Strand threads printing the mission title | `print_big` style 2 | title only |
| Other MAIN threads (`HELP`, `FLOW`, phone calls, save points, strand triggers) | none of the above | no |
| External scripts (shops, gyms, casino, girlfriends, parachute) | code outside `ScriptSpace` (own streamed buffer) | no |
| Engine messages (garages, Pay 'n' Spray, stat messages, zone and car names, the HUD) | not a script command | no |
| Missions 0–10 (Initial 1/2 and Intro run on every client at New Game; arcade, betting, pool, lowrider, Beefy Baron) | mission id at 0417 | no (D2) |
| Side missions 113–134 (schools, taxi, paramedic, vigilante, races, property purchase) | mission scripts | yes (the friend sees the host's side mission) |
| The receivers' own scripts and anything they replay | only the host sends; replays don't reach `BuildAndSendOpcode` | no |

### 3.3 Interception points

| What | 1.0 US | Use | Source |
|---|---|---|---|
| Every command | 0x469FF3 in `CRunningScript::Process` 0x469F00 | existing `COpCodeSync` redirect; one new call after the handler | bytes; ReVa |
| Parameters and text labels | 0x46421D (end of `CollectParameters`), 11 `ReadTextLabelFromScript` sites | existing `COpCodeSync` hooks | existing code |
| Handle a creating command returns | `ScriptParams[0]` 0xA43C78 after the handler; the blip handlers end in `mov [0xA43C78],edi` + `StoreParameters` 0x464370 (0x47C64C for 0161, 0x47CFF6 for 0186/018A, 0x485504 for 03DC) | read in the post-command call | dumpbin |
| Locate spheres | `CTheScripts::HighlightImportantArea` 0x485E00, called at 0x467989, 0x467A96, 0x467FA0, 0x46805E (`ProcessCommands100To199`), 0x4801D7 (`700To799`), 0x48704F, 0x4873A4, 0x4876B2, 0x4879B2, 0x487CA9, 0x487F13 (the `Locate*Command` helpers), 0x488E6A, 0x489111, 0x489378 (the `*InAreaCheckCommand` helpers) | 14 new `RedirectCall`s | ReVa cross-references; E8 targets read from the file |
| Angled areas | `HighlightImportantAngledArea` 0x485EF0, called at 0x488374, 0x488735, 0x488AFA | 3 new `RedirectCall`s | same |
| `draw_sphere` (03A1) | `C3dMarkers::PlaceMarkerSet` 0x725BA0 called at 0x4845F8: id = script + IP, type 1 (cylinder), colour 255/0/0/228, pulse 2048, 0.1, no rotation | 1 new `RedirectCall` | dumpbin; plugin-sdk |
| Radar traces | `CRadar::ms_RadarTrace` 0xBA86F0, 175 entries of 0x28 (`tRadarTrace`) | host reads, receiver writes fields as `CNetworkStaticBlip` does | plugin-sdk `CRadar.h`; ReVa |
| Radar calls | `SetEntityBlip` 0x5839A0, `SetCoordBlip` 0x583820, `SetShortRangeCoordBlip` 0x583920, `GetActualBlipArrayIndex` 0x582870, `ClearBlip` 0x587CE0, `ChangeBlipColour` 0x583AB0, `ChangeBlipScale` 0x583CC0, `ChangeBlipDisplay` 0x583D20, `SetBlipSprite` 0x583D70, `SetCoordBlipAppearance` 0x583E50, `SetBlipFriendly` 0x583EB0, `SetBlipEntryExit` 0x583F00 | receiver | plugin-sdk `CRadar.cpp`; call sites 0x47C640, 0x47CEE6, 0x47CFE0, 0x47CEF0 checked |
| Interior entrances | `CEntryExitManager::FindNearestEntryExit` 0x43F4B0, `mp_poolEntryExits` 0x96A7D8 | receiver re-resolves a blip's entrance | plugin-sdk; gta-reversed |
| Pickups | `CPickups::aPickUps` 0x9788C0 (0x20 each, compressed position ×8), `GetActualPickupIndex` 0x4552A0 | host position of pickup blips | plugin-sdk |
| Timers and counters | `CUserDisplay::OnscnTimer` 0xBA1788: clock (0x40: var offset, key[10], +0x38 enabled, +0x39 direction, +0x3C beep seconds), 4 counters (0x44: var offset, second var offset, key[10], +0x12 type, +0x3F flash, +0x40 colour), +0x150 display, +0x151 paused | host reads, receiver writes | plugin-sdk; handlers load `ecx = 0xBA1788`; ReVa `COnscreenTimer::Process` 0x44CD30 |
| Timer value | `COnscreenTimerEntry::Process` 0x44CB10 reads and writes `*(int*)(ScriptSpace + varId)` in 32-bit arithmetic, counts the clock, clears it below 0, beeps under +0x3C; the display code (`COnscreenTimer::ProcessForDisplay` 0x44CC20, a stub to its relocated copy) reads the counters the same way | receiver points varId at its own buffer | ReVa decompile |
| Timer adds | `AddClock` 0x44CD50 at 0x47C2AC, 0x47C2C3 (014E), 0x484CA7 (03C3); `AddCounter` 0x44CDA0 at 0x484CFC (03C4, always slot 0), 0x48C57D (04F7) | optional receiver wrappers (3.7) | E8 targets; dumpbin |
| Script spheres | `ScriptSphereArray` 0xA91268 (16), drawn every frame by `DrawScriptSpheres` 0x4810E0 (a stub to 0x15647C0) with the same marker as `draw_sphere` | later package | gta-reversed; plugin-sdk; ReVa |
| Race checkpoints | `ScriptCheckpointArray` 0xA44070 (20 × used, id, `CCheckpoint*`), `AddScriptCheckpoint` 0x4935A0, `RemoveScriptCheckpoint` 0x4936C0, `CCheckpoint` (0x38: type, colour +0x08, position +0x10, direction +0x1C, size +0x2C) | later package | plugin-sdk; gta-reversed |
| Text tables | `TheText` 0xC1B340 (+0x24 mission table name); `LoadMissionText` 0x69FBF0 clears all messages first (`CMessages::ClearAllMessagesDisplayedByGame(1)`); `CText::Get` 0x6A0050 returns an empty string for a missing key | receiver guard (3.4) | plugin-sdk `CText.h`; ReVa; gta-reversed `Text.cpp` |
| Mission cleanup | `CMissionCleanup::Process` 0x468560 (also clears the help box unless a minigame runs, widescreen, mission audio, script checkpoints) | receiver end-of-mission cleanup (3.11) | ReVa decompile; plugin-sdk `CMissionCleanup.h` |

All `RedirectCall` sites hold an E8 call to the stated target in the file; `tools/audit_hooks.py` must stay at its
baseline plus these 18 sites (and 5 more if the optional timer wrappers are built). None of them is hooked today.

### 3.4 Texts

- `COpCodeSync::IsOpcodeSyncable` gets one more condition: the opcode is in the mirror's text list and the script is
  in the family. The list is the "text" rows of 2.1 plus `0394` and `03E7`. The opcodes are added to
  `syncedOpcodes[]` as plain entries, because `COpCodeSync::HandlePacket` drops an opcode with parameters that is
  missing from that table.
- Mission titles: `00BA` from any host script is collected; `BuildAndSendOpcode` sends it from a script that is
  neither registered nor in the family only when its style parameter is 2.
- One send path: a registered script (`Coop.EnableSyncingThisScript`) in the family still sends each command once.
  The layer itself never duplicates; the co-op print commands in the ported missions do (section 4).
- Receiver: the existing replay. Guard: before replaying a key-based text, check that the key resolves; a mission
  table key does not when the receiver's own external script (a gym, for example) loaded another table. Then load the
  host's table (remembered from the last mirrored `054C`) and replay. Loading clears the receiver's messages, as it
  always does. Main table keys (titles, M_PASS, most help) always resolve.
- No CoopAndreas GXT exists: no `.gxt` file in the repository or package, the only text hook is the "(Coop)" save name
  prefix (0x5D1BB2), and the ported missions use vanilla keys. Both games have the same `american.gxt`, so keys always
  match; a player with another game language sees the text in their own language.
- Texts are sent as they come. A script that prints the same text every frame costs one small reliable packet per
  frame; the diagnostic counters (section 8) show whether a throttle is needed.

### 3.5 Blips

Ownership: in the post-command call, after a creating command (`0161`, `0167`, `0186`, `0187`, `0188`, `018A`, `02A7`,
`02A8`, `03DC`, `04CE`, `0570`, `06C4`, `0888`) run by a family script, the handle in `ScriptParams[0]` is owned.
Blips the engine creates inside other commands (recruits, gang wars) are not.

Host, once per frame after `CGame::Process` (Main.cpp `gameProcessEvent`): for each owned handle,
`GetActualBlipArrayIndex`; a handle that no longer resolves was removed. Otherwise build the state: type, entity
reference (3.10), position (the entity's, the coordinate, or the entrance centre of `m_pEntryExit`), colour (raw, it
can be an RGBA value), sprite, display, size, friendly, short range, "remain", coordinate appearance. Send what changed.
Owned blips stay mirrored after the mission ends for as long as the host keeps them (the next mission's contact point,
shop icons a mission adds).

Coordinate and contact-point blips the host creates outside missions keep the existing static path
(`CNetworkStaticBlip`, a full snapshot of all such blips with allowed sprites). Two small changes keep the paths apart
(D1): the host's `CNetworkStaticBlip::Send` skips owned traces, the receiver's `CNetworkStaticBlip::Create` leaves the
traces the mirror made. After the host loads a save, the loaded blips are unowned and go the static way.

Receiver: a map from the host handle to the local blip. An entity reference that resolves gives an entity blip
(plugin-sdk `CRadar::SetEntityBlip`, as `CNetworkEntityBlip` does); one that doesn't gives a coordinate blip at the
host's position, radar only (display BLIP_ONLY, so no ground marker jumps around), switched to the entity blip as soon
as it resolves. Fields are written into the trace like `CNetworkStaticBlip::Create` does. An entrance is resolved with
`FindNearestEntryExit` near the sent position and set with `SetBlipEntryExit`, so the blip moves between the entrance
and the inside like the host's. A blip on the receiver's own player is not created.

### 3.6 Spheres and markers (areas)

Host: the wrappers at the 17 highlight sites and the `draw_sphere` site call the original, then, when the running
script is in the family, record `{id, kind, centre, half sizes or radius, z, time}`. The angled version is recorded as
the axis-aligned box of its edge midpoints, which is what 0x485EF0 renders. Per frame: an area seen in the last
250 ms is active. The host sends the active set when it changes (added, removed, moved more than 0.5 m, resized), at
most 10 times a second, and an empty set at once when the mission ends. Entity-relative locates (4 calls) move with
their target and send at that rate.

Receiver: draws every active area each frame where `CNetworkCheckpoint::Process` draws today:
`HighlightImportantArea(localId, x - hx, y - hy, x + hx, y + hy, z)` for areas, `PlaceMarkerSet(localId, 1, pos, r,
255, 0, 0, 228, 2048, 0.1f, 0)` for `draw_sphere`. Local ids are the host ids with the top bits set, away from the
script-pointer ids the receiver's own scripts use.

Later package: race checkpoints and script spheres are persistent objects, not per-frame calls. The host owns them
like blips (handle in `ScriptParams[0]` after `06D5`/`03BC`) and lists them in the same set; the receiver creates them
once with `AddScriptCheckpoint` (and the sphere marker per frame) and removes them when they leave the set.

### 3.7 Timers and counters

Ownership: the post-command call keeps a copy of the five var offsets of `OnscnTimer`, refreshed after every command;
an entry that became non-zero during a family command is owned and gets a new generation number.

Host per frame: send the full timer state when an owned entry is added, removed or changes key, type, colour, flash,
direction, beep time or the paused flag; send values when they change (counters at most 4 times a second, the clock
once a second while it runs).

Receiver: a small buffer in the DLL holds the values (clock, four counters, four second values). Each mirrored entry
gets `varId = (uint32_t)&buffer[i] - 0xA49960`; since the engine computes `ScriptSpace + varId` in 32-bit arithmetic,
it reads and writes the buffer. The receiver's own globals, which have the same offsets as the host's, are never
touched. Entries are added with `AddClock`/`AddCounter` in the host's slot when free, else in another free one, and
cleared with `ClearClock`/`ClearCounter` (0x44CE60, 0x44CE80) with the same offsets. The engine counts the clock down
locally and beeps; the host's value corrects it; the generation number keeps a clock the receiver already ran out from
coming back with a late update. Local scripts come first: the mirror never writes a slot a local script holds.
Optional: wrappers at the five `Add*` call sites vacate a mirrored slot when a local script asks for it (otherwise a
gym counter on the friend's side doesn't show while the host's mission holds that slot).

### 3.8 Packets, relay and bandwidth

New file `shared/network/packets/mirror.h`, three types appended to `ePacketType` after `PLAYER_STATE_RESTORE`, all on
the SCRIPT channel (reliable, ordered with `OPCODE_SYNC`):

| Packet | Fields |
|---|---|
| `MISSION_MIRROR_BLIPS` | `bReset`; count (0..175); per entry: host handle (uint32), op (full, removed, position); full: type (1..8), entity (`CNetworkEntitySerializer`, `NOTHING` when not resolvable), position (`WorldPositionCompressed`), colour (uint32), sprite (0..63), display (0..3), size, friendly, short range, remain, appearance (0..2), entrance flag + position; position: position |
| `MISSION_MIRROR_AREAS` | count (0..32); per entry: host id (uint32), kind (area, cylinder marker, race checkpoint, script sphere), centre, half sizes or radius, z; race checkpoint: type, direction, radius |
| `MISSION_MIRROR_HUD` | clock (on, generation, key[10], direction, beep seconds, value); four counters (on, generation, slot, key[10], type, colour, flash, value, second value); paused |

Texts stay in `OPCODE_SYNC`, with keys and numbers.

Server: one handler per type, `if (pNetworkPlayer->m_bIsHost) GetPacketFactory().SendToAll(*packet, pNetworkPlayer);`.
It keeps no state: the host sends the snapshot on join.

| Traffic | Size | Rate |
|---|---|---|
| Text command (`OPCODE_SYNC`) | 15–30 B | per print; dialogue about one line per 2–4 s |
| Blip, full state | about 30 B | on creation and change; 0.3–3 KB bursts at phase changes |
| Blip, position | about 17 B | up to 4 Hz for objects, pickups, trains and unresolved entities; 0.5 Hz for other entity blips |
| Area set | 5 B + about 22 B per area | on change, up to 10 Hz |
| Timer state | up to about 110 B | on change; counters up to 4 Hz, clock 1 Hz |

Well under 1 KB/s on average, small next to the player, vehicle and ped sync, and fine for the Steam tunnel.

Protocol impact: new packet types, so the version is bumped (the next number of the scheme in CLAUDE.md) and the
friend needs the new package; the version check keeps mixed builds apart.

### 3.9 Live values

- Timer and counter values: the host reads `ScriptSpace + varId` every frame and sends changes; the receiver's engine
  counts the clock between corrections.
- Entity blips follow the receiver's copy of the entity by themselves.
- Coordinate fallbacks (objects, pickups, trains, unresolved entities) move with the host's position updates.
- Entity-relative areas move with the area updates.

### 3.10 Handle mapping

| Host blip target | Wire | Receiver |
|---|---|---|
| Vehicle with a network id (in `CNetworkVehicleManager::m_pVehicles`) | `VEHICLE` id | `GetVehicle(id)->m_pVehicle`, validated |
| Vehicle without one yet (created this frame, confirm pending), or a train | `NOTHING` + position | coordinate fallback until an id arrives; trains always (the receiver doesn't create them: `CNetworkVehicle::CreateVehicle` returns false for `VEHICLE_TRAIN`) |
| Ped with a network id (in `CNetworkPedManager::m_pPeds`) | `PED` id | `GetPed(id)->GetLivePed()` |
| Ped without one yet | `NOTHING` + position | fallback |
| A player ped | `PLAYER` id (host = `m_nMyId`) | that player's live ped; none for the own player |
| Object, pickup, searchlight | `NOTHING` + position | coordinate blip moved by position updates |
| Coordinate, contact point | position (+ entrance) | coordinate blip |

Entity sync for an unported mission: the `CWorld::Add` hook (`client/src/Hooks/WorldHooks.cpp`) networks every vehicle
and every ped with ped type above 1 that the host's game adds while connected, mission entities included (special
character models too). Objects, pickups, fires, script spheres and searchlights are not networked, and receivers don't
create trains.

Network ids: a hosted entity gets a temporary id; the server assigns the real one (`PED_SPAWN`/`VEHICLE_SPAWN` →
`PED_CONFIRM`/`VEHICLE_CONFIRM`, about one round trip plus the 50 ms packet buffer). Until then the entity is only in
the temporary list: `m_nPedId` reads 0 (vehicles -1), `Coop.GetPedNetworkId` returns -1, and the managers don't find
it. The server's free ids start at 0, so 0 is a valid id; the layer tests membership in the manager list, never the
value. Pitfall found on the way: `CNetworkEntitySerializer::SetEntity` sets the type but leaves `entityId` at its
default 0 for an unconfirmed ped or vehicle, so `Coop.Update*BlipForNetworkPlayer` called before the confirm blips
whatever entity has id 0 on the friend's side (the ports avoid it by waiting).

### 3.11 Lifetime and join snapshot

| Event | Host | Receivers |
|---|---|---|
| A blip or timer changes | next frame's difference | apply |
| The mission ends | family cleared; empty area set at once; blips and timers keep following the host (cleanup removes them, the next contact point stays) | the existing `ON_MISSION_FLAG_SYNC` 1→0 cleanup also clears the help box (as `CMissionCleanup::Process` does on the host) and drops areas |
| The host loads a save (the `GenericLoad` call at 0x53C70B, already hooked by `CPlayerStateSave`) | owned sets cleared; blips `bReset`, empty areas, empty timers | drop mirrored state; the static snapshot brings the loaded coordinate blips |
| The host disconnects or the host changes | – | on `PLAYER_DISCONNECTED` of the host or `PLAYER_ASSIGN_HOST`: drop everything mirrored; a new host starts empty |
| A player joins mid-mission | on `PLAYER_CONNECTED` (host branch, next to the static blip and tag snapshots): blips `bReset` + full state, areas, timers, a replay of the last `054C`, the current objective text and help (if still showing, with the time left), and `ON_MISSION_FLAG_SYNC` | apply; entities the server replays later are resolved when they arrive |
| A receiver alt-tabs | – | the reliable packets are kept by `CNetworkKeepAlive` and applied on return |
| A receiver runs its own mission or external script | – | texts share the message queue and help box; timers local-first; blips are separate traces |

`ON_MISSION_FLAG_SYNC` is sent today only when the flag changes, so a player who joins during a mission never gets it;
resending it on join is one line and part of the snapshot.

## 4. Ported missions and the #1–#10 conversion

### 4.1 What shows twice if nothing changes

The ports send their own copies: `Coop.PrintNowForNetworkPlayer` next to the host's `print_now`, gosub helpers that
relay subtitles and fail reasons, `Coop.Update*BlipForNetworkPlayer` on entities the host blips,
`Coop.UpdateCheckpointForNetworkPlayer` next to the host's locate sphere. With the layer on, a `print_now` copy only
restarts the same text, but a `print` copy shows twice in the queue, a blip copy puts a second icon on the target, and
a checkpoint copy draws a second marker at the same place. The conversion ships in the same package as the layer (D6).

### 4.2 Conversion rules

1. Remove what copies a host display at the same point: copies of host prints and dialogue lines, relay calls and
   their setup lines and helpers (`*_COOP_PRINT`, `*_COOP_PRINT_NOW(_FOR)`, `*_COOP_SUBTITLE`, `*_COOP_FAIL(ED)`), blips
   on entities the host blips, checkpoints at the host's sphere, and the `Remove*`/`Clear*` lines that only pair with
   them.
2. Keep what is about the friend only: the blue blip on the friend's own vehicle (`$NETWORK_PLAYER_VEHICLE`), a
   per-player destination (a separate parking spot), hints that depend on the friend's own state ("get back in the
   car" while the host is in it), help for what only the friend does (the drive-by help in #6), and the cleanup of
   those.
3. Keep the gameplay: registration, `CollectNetworkPlayersForTheMission`, teleports and placement, seats, own vehicles,
   combined progress conditions, weapons, and the waits for network ids that serve tasks or teleports. Only waits that
   served blips go.
4. Remember that the friend now also sees the host's displays; a friend-only text should not contradict them. Send it
   right after the host's own print of that phase (the later one stays on screen), or use the queued
   `Coop.PrintForNetworkPlayer`.

### 4.3 Per mission

Counts of direct co-op display lines (prints / blips / checkpoints), in brackets the likely copies (lines whose key,
entity or coordinates the host also uses in the same file), and the relay gosubs. Bytes are estimates from the SCM
encoding (opcode 2, int8 2, int16 3, float 5, variable 3, array element 7, string 9; `for` loop about 39, validity `if`
about 20): the low end is the clear copies and relays, the high end all display code with its wrappers.
`compile-scm.ps1` gives the exact figure.

| # | Script | Prints / blips / checkpoints (copies) | Relay gosubs (helpers) | Keep | Est. saved |
|---|---|---|---|---|---|
| 1 | `INTRO` | 0 / 0 / 0 | – | the host-only contact blip (static path) | 0 |
| 2 | `INTRO1` | 8 (8) / 4 (2) / 1 (1) | – | the BMX blips on `$NETWORK_PLAYER_VEHICLE` | 0.4–0.5 KB |
| 3 | `INTRO2` | 5 (5) / 2 (0) / 2 (2) | – | the blue marker on `200@` (check whose car it is) | 0.4–0.8 KB |
| 4 | `SWEET1` | 26 (26) / 7 (2) / 4 (4) | – | the per-player "get in Sweet's car" hints and car blip (`203@`, Ryder's car for a third player) | 1.2–2.2 KB |
| 5 | `SWEET1B` | 16 (16) / 6 (6) / 6 (4) | – | two checkpoints at Sweet's house (check against the host's sphere) | 1.2–1.8 KB |
| 6 | `SWEET3` | 11 (8) / 6 (4) / 1 (0) | 7 (`COOP_PRINT`, `COOP_FAIL`) | own Greenwood blip and "get in the car", per-player drive-thru checkpoint (`130@`/`133@`), `HELP31`/`HELP32` | 0.8–1.1 KB |
| 7 | `SWEET2` | 7 (6) / 2 (2) / 2 (1) | 25 (`COOP_PRINT_NOW`, `_FOR`, `COOP_FAILED`) | the follow-Smoke checkpoint (`208@`–`210@`) | 1.2–1.6 KB |
| 8 | `SWEET4` | 9 (8) / 2 (0) / 0 | – | own Greenwood blip; the red blip on `150@` if the host has none | 0.3–1.0 KB |
| 9 | `HOODS5` | 3 (2) / 2 (0) / 2 (2) | 22 (`COOP_PRINT`, `SEVILLE_BLIPS`, `CHECKPOINT_*`, `BLIP_ON/OFF`, `NO_CHECKPOINT`) | the seat logic (not display) | 1.3–1.7 KB |
| 10 | `SWEET6` | 3 (2) / 0 / 2 (2) | 21 (`COOP_SUBTITLE`, `COOP_PRINT`) | – | 0.5–0.7 KB |

#6–#10 together: about 4 KB with rules 1–3 (the relays alone are about 2.7 KB), up to about 6 KB if the per-player
objective state machines also go. #2–#5: another 3–5 KB. These missions have 30–49 KB free, so the gain is clarity;
the size gain matters for the ports to come. The counters of #8 and #9, "host-only" in their ports, become visible to
the friend.

### 4.4 Co-op commands that stay useful

| Commands | After the layer |
|---|---|
| `1D00` chat line | unchanged (host only, not mirrored) |
| `1D01`–`1D03`, `1D0E`–`1D1C` (players, validity, host, locate all, registration, network ids, ped claims, teleport) | unchanged: gameplay |
| `1D04`–`1D07` entity blip for a network player | friend-only blips (own vehicle, a target only the friend chases) |
| `1D08`–`1D0C` texts for a network player | friend-only texts |
| `1D0D` clear entity blips | cleanup of friend-only blips |
| `1D10`/`1D11` checkpoint for a network player | a friend-only destination |

### 4.5 Unported missions with the layer alone

The friend sees the mission title, every objective, dialogue subtitle and help, the blips on the mission's vehicles and
characters (objects, pickups and trains as moving coordinate blips), the locate spheres and `draw_sphere` markers, the
timers and counters, "Mission passed" with the tune or "Mission failed", and hears the dialogue. The friend can drive
there, ride as a passenger where a seat is free, and fight: the friend's shots are replayed in the host's game with
the network entity they hit (`PLAYER_BULLET_SHOT`), and the mission checks its targets whoever kills them.

Still missing without a port: the cutscenes, fades and scripted camera (the friend keeps their own view), placement
and teleports, a vehicle when the mission's seats are full, progress conditions for the friend (only the host's
position counts), the mission's wanted level commands, and the rewards. And as today, the friend dying or getting
busted fails the host's mission (`CRunningScript__DoDeatharrestCheck_Hook` replaces 0x485A50 for every mission).

A port can be one line: `Coop.EnableSyncingThisScript()` (2 bytes) after `script_name` gives the friend the cutscenes,
fades, camera, frozen controls, wanted commands and rewards of `COpCodeSync`, without placement.

Likely enough with the layer and that one line (from the scripts' structure; to confirm when each comes up in
order): #22 High Stakes, Low Rider (the friend follows the race; race checkpoints need package M5), #24 Gray Imports,
#30 First Date, #36 King in Exile and #86 Fish in a Barrel (cutscene only), #69 Verdant Meadows, #90 High Noon;
probably #11 OG Loc and #12 Running Dog with the friend in their own vehicle. Not without a real port: the two-seat
bike missions with a shooter (#13, #14, #49), stealth (#15, #19, #53, #79, #81), single-seat vehicles (#17, #32, #42,
#94), minigames and schools (#18, #43, #70), RC (#44–#46), gang wars (#25, #98), special mechanics (#67, #72, #83, #89),
and #27, whose 561 free bytes now fit a minimal port (registration 2 bytes, player collection 11, a teleport 11).

### 4.6 The skipped intro (optional)

`INTRO` (#1) starts with the state the mission leaves behind (fade in, controls, zone names, roads, entry-exits, the
host-only contact blip), runs the original cleanup and terminates at line 122. The original body stays in the file,
unreachable: the `PROLOG1` and `PROLOG3` cutscenes, a scene with a police car (596), the BMX (481) where CJ is dropped
off, with its blip and the "INTROB" text, the ride to Grove Street, and the loading texts.

Making it co-op is hard for a reason the layer doesn't change: MAIN launches missions 0–2 on every client at New Game,
usually before the friend is connected and possibly before the host is assigned, so both games would play their own
intro. It would take a host-only branch at the start of `INTRO` (the original body on the host, the end state
elsewhere), registration for the cutscenes, a BMX for the friend as in #2, and the host waiting until the friend is
connected. Recommendation: keep it skipped. #2 already starts both players on BMXs.

## 5. Changes to the porting guide

For `docs/mission-porting.md` once the layer ships:

- "How co-op runs a mission": a paragraph on the mirror: what every host mission shows reaches the other players;
  which scripts count; what is not mirrored.
- "What `COpCodeSync` replays": texts are now mirrored for all host missions; the table stays for registered scripts
  (cutscenes, camera, controls, tasks, weapons, wanted, rewards).
- Replace the "Not replayed…" paragraph: texts, subtitles, entity blips, spheres, timers and counters are mirrored; not
  mirrored are immediate text and sprites, the radar and HUD toggles, and (until package M5) race checkpoints and
  script spheres.
- Patterns: drop "Objective texts and dialogue" and the relay helpers; "Blips and markers" becomes friend-only displays;
  "Counters, timers": mirrored; "Fail conditions": the host's fail text reaches everyone; "Mission end": clean up only
  friend-only displays, given weapons and vehicles; "Entities the mission creates": wait for network ids only before
  tasking or teleporting them.
- New: "Friend-only displays" (4.2 rule 4, 4.4) and the accepted cost that host-only messages show to everyone.
- Checklist step 4: "what must they see" becomes "what must they see that differs from the host".
- Size budgets: ports grow less; #27 becomes portable.
- `docs/missions.md`: "Untouched" now means "mirrored only"; mark the converted missions.

## 6. Risks and open questions

1. Family gaps: a MAIN helper other than the three named ones, or a streamed script a mission starts, could print
   mission texts. The scan found only `AUDIOL`, `A_CONT`, `A_ALAP`; a port that finds another adds its name.
2. Host-only messages reach the friend ("Get back in Sweet's car" while the friend sits in it). Accepted; if testing
   shows it confuses, a co-op command that skips the next display command can follow (D9).
3. Text tables on the receiver: replaying a mission-table key reloads the host's table, which clears the friend's own
   messages and breaks the texts of a gym or minigame the friend is in at that moment.
4. Static-path interplay (D1): two upstream lines change; a blip owned by the mirror must never also be in the static
   snapshot.
5. The mission id is read from `ScriptParams[0]` after 0417; confirm the handler leaves it, else take it in the
   `CollectParameters` hook.
6. The creating-command tails were read for 0161, 0186, 018A and 03DC; confirm the others (`0187`, `0188`, `02A7`,
   `02A8`, `04CE`, `0570`, `06C4`, `0888`, `06D5`, `03BC`) store the handle in `ScriptParams[0]` the same way.
7. A text printed every frame sends a reliable packet every frame; measure before throttling.
8. Trains, `CPRACE` and moving objects rely on position updates; check the feel in #13, #16, #22, #52.
9. Mirrored traces end up in the friend's save if the friend saves (only the host's save is used).
10. The world-state registry designed in parallel (`docs/design/world-state.md`) may also look at blips that outlive a
    mission (contact points, shop icons). This design keeps owned blips mirrored for as long as the host has them and
    leaves unowned coordinate blips to `CNetworkStaticBlip`; align the two when merging.
11. Ports stay registered with `COpCodeSync`, and registered scripts now sync the added text opcodes too (`INT` gains
    `clear_help`). Harmless as far as the scan shows.

## 7. In-game test plan (two players)

Not tested in game. The architect turns this into the test plan (`docs/test-plan.md`).

1. Both install the same package; the version in the bottom-left corner matches. Both start with the
   `CoopAndreas (diagnostics)` shortcut. New Game; the host connects first.
2. #1 to #10 in order (converted). In each mission the friend checks: the title, every objective text and subtitle
   exactly once, help boxes, the blips on Sweet, Smoke, Ryder, their cars and the targets (one icon each), the
   coordinate blips, the yellow markers where the host has them, the pickup blip in #9, the counters in #8 (vehicle
   health) and #9 (Sweet's health) with moving values, "Mission passed" with the tune and money text, and nothing left
   over after the mission (blips, markers, help box, counters).
3. Fail once on purpose (let Sweet die in #4, or the friend dies): "Mission failed" and the reason on both screens.
4. Mid-mission join: during #6 or #8 the friend leaves the game and reconnects; after joining the friend sees the
   current objective, the blips, the marker and the counter.
5. Save and load: after #10 the host saves at a safe house, both quit, the host loads, the friend reconnects; the next
   contact point shows on both, no stale mission blips.
6. Timers: the host starts a side mission (taxi, paramedic or vigilante); the friend sees the countdown run smoothly
   and stay in step, the counters, the target blips; after the end nothing is left.
7. Local first: the friend works out on a gym machine while the host's side mission shows a counter; both counters
   appear.
8. The friend alt-tabs for 30 s during a mission; back in the game, texts and blips are current.
9. The host quits mid-mission: the friend's mirrored blips, markers and counters go away, no crash.
10. When #11 OG Loc comes up in order, before porting: the host plays it, the friend follows and reports what is
    missing.

## 8. Work packages

For `gtasa-coder-high` unless noted; each in its own worktree from the then-current `main`. Gates: `build.ps1`,
`tools/audit_hooks.py` where hook sites change, the policy test.

| Package | Scope | Files | Depends on | Protocol |
|---|---|---|---|---|
| M1 core and texts | Family (mission script and id at 0417, mission-block IP, named helpers, exclusions); post-command call and current-script accessor in `COpCodeSync`; text opcodes in `syncedOpcodes[]` and the family condition in `IsOpcodeSyncable`; the title rule; receiver table guard; join snapshot of `054C`, objective, help and `ON_MISSION_FLAG_SYNC`; help box in the end-of-mission cleanup; `--coopdiag` counters (family, owned, mirrored, packets per type) | new `client/src/CMissionMirror.{h,cpp}`, `client/src/CMirrorPolicy.h`, `tests/mission_mirror_test.cpp`; changed `COpCodeSync.{h,cpp}`, `PacketHandlers/system.cpp`, `PacketHandlers/scripts.cpp`, `Debug/CDiagLog.cpp`, `CCore.cpp` | – | none new; behaviour change |
| M2 blips | Ownership, per-frame difference, entity resolution and fallback, entrances, static-path changes, reset on load and host change, snapshot | new `client/src/CMirrorBlips.{h,cpp}`, `client/src/PacketHandlers/mirror.cpp`, `server/src/PacketHandlers/mirror.cpp`, `shared/network/packets/mirror.h`; changed `packet_types.h`, `CNetworkStaticBlip.cpp`, `Main.cpp`, `system.cpp`, `CPlayerStateSave.cpp` (load callback) | M1 | new type `MISSION_MIRROR_BLIPS`; version bump (D11) |
| M3 areas | The 18 call-site wrappers, recorder, per-frame set, receiver drawing, snapshot | new `client/src/CMirrorAreas.{h,cpp}`; changed `mirror.h`, both `mirror.cpp`, `packet_types.h`, `Main.cpp`, `system.cpp` | M1 | new type `MISSION_MIRROR_AREAS` |
| M4 timers and counters | Ownership by comparison, state and values, receiver buffer entries, local first, optional `Add*` wrappers | new `client/src/CMirrorHud.{h,cpp}`; changed as M3 | M1 | new type `MISSION_MIRROR_HUD` |
| M5 race checkpoints and script spheres | Persistent entries in the area set | `CMirrorAreas`, `mirror.h` | M3; before #22 | area packet fields |
| C1 conversion (`gtasa-coder`) | #1–#10 by 4.2, compile, bytes per mission | `CoopAndreas/scm/scripts/{INTRO1,INTRO2,SWEET1,SWEET1B,SWEET3,SWEET2,SWEET4,HOODS5,SWEET6}.txt` | M1–M4 merged | none |
| D1 docs (architect) | Porting guide (section 5), `docs/missions.md`, CLAUDE.md fork changes | docs | M1–M4 | – |

Order: M1, then M2, M3 and M4 in parallel (merge in that order and append their packet types in merge order), then C1
and D1, build, package with the test plan (section 7). M5 before the batch that reaches #22.

The policy test covers the pure logic without the game: the family predicate, the title rule, the mission exclusion,
the blip difference, the area set with its grace time and thresholds, and the clock generation rule.

## 9. Decisions

| # | Decision | Recommendation |
|---|---|---|
| D1 | Coordinate and contact blips a mission creates: owned by the mirror (one path for all its blips, changes of display and scale included), or left to `CNetworkStaticBlip` (fewer changes, its gaps stay: display and scale changes don't trigger a resend) | the mirror owns them |
| D2 | Exclude missions 0–10 (Initial 1/2 and Intro run on every client; arcade games, betting, pool, lowrider game, Beefy Baron) | exclude |
| D3 | Rewards in unported missions (money, respect, mission-passed stat) | not mirrored; registration or a port decides (betting and payments use `add_score` too, as in #10) |
| D4 | Mission titles from the MAIN strand threads by the style-2 rule | yes |
| D5 | The dialogue helpers `AUDIOL`, `A_CONT`, `A_ALAP` by name | yes |
| D6 | Ship the layer together with the #1–#10 conversion and re-test from a New Game | yes |
| D7 | Immediate text and sprites (schools, minigames, race HUD) | out of scope; revisit if a mission needs it |
| D8 | Race checkpoints and script spheres in a later package | yes, before #22 |
| D9 | A co-op command that keeps the next display command from the friend | not now; only if testing shows confusing messages |
| D10 | Resend `ON_MISSION_FLAG_SYNC` on join | yes, in M1 |
| D11 | The version number of the package | the architect, by the scheme in CLAUDE.md |
