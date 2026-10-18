# Story missions: status, risks and porting order

Every story mission of GTA San Andreas in story order, with its script, its source file under
`CoopAndreas/scm/scripts/`, its mission number (`DEFINE MISSION n` in `scm/main.txt`), its size in the
committed co-op `main.scm` and how much of the 69,000-byte mission buffer is left, its co-op status, the
thread that starts it (with the strand counter value it waits for) and the co-op risks to plan for. How to
port a mission: `docs/mission-porting.md`.

The rule that frames this list: **the story order is never broken.** Missions are ported, tested and
played strictly in this order. A risky mission is flagged here, never moved, skipped or unlocked early,
not even for testing. A port changes only the inside of its own mission; the triggers in the trigger
column belong to MAIN and stay as they are.

## Status legend

| Status | Meaning |
|---|---|
| **Adapted** | Upstream ported it with co-op commands; upstream's README at the imported commit `cec2a7d` lists it as checked. |
| **Ported (test pending)** | Our fork ported and compiled it with the size/save checks passing. Two-player acceptance is still pending (`docs/test-plan.md`). |
| **…, tested** | Played through in one of our two-player tests (#1–#6: group 1 test on 0.3.102-alpha, 2026-10-15/16). |
| **Converted (0.1.0, test pending)** | A port (upstream's or ours) moved onto the mission mirror layer for 0.1.0: its copies of the host's displays are gone, gameplay and friend-only displays stay (`docs/design/mirror-layer.md` 4.2). Tested again from a New Game (`docs/test-plan.md`). |
| **Ported (retest pending)** | Our two-player test found a bug in or around the mission; the fix ships in the build named in the risks column and the mission is tested again. |
| **Skipped** | Upstream replaced the mission body with the state the mission leaves behind (In the Beginning). |
| **Warning** | Vanilla mission plus two `Coop.AddChatMessage` lines ("This mission is currently unsupported…") after `script_name`. The chat line shows only on the host (`1D00` is not synced). Otherwise mirrored only, like Untouched. |
| **Warning (lost)** | The warning lines sit before the mission's label, so they compile into the tail of the previous mission after its last `return` and never run (upstream bug). They add 210 bytes to the previous mission. Mirrored only, like Untouched. |
| **Untouched** | Mirrored only (no port yet): the vanilla decompile. The mission runs on the host; the other player follows it through the mission mirror layer (title, objective texts and subtitles, help boxes, blips, locate spheres and markers, timers and counters, "Mission passed" with the pass rewards), hears the dialogue and can help, but gets none of the cutscenes, camera, wanted commands or placement, and the mission's progress conditions read only the host (see the porting guide). |

Found by decompiling the vanilla `data\script\main.scm` and the committed co-op `main.scm` with the same
Sanny Builder and diffing every mission with labels, floats and two decompiler naming quirks normalized.
Besides the missions marked here, upstream changed only MAIN (see the porting guide) and five external
scripts: the shopkeeper scripts BARBER (`BARB.txt`), TATTOO (`TATTO.txt`), JUNKFUD (`JFUD.txt`) and
FOODBRAINS (`PSHOP.txt`), which reuse a shopkeeper the other player's game already created, and HOTDOG
(`HOTDOGV.txt`), which lost an unreachable `terminate_this_script`.

## Test history

Two-player tests so far; what is still open is in `docs/test-plan.md`.

- **0.3.102-alpha** (2026-10-15/16, two players, from a New Game): #1–#6 passed, with no crash reported in #3's
  restaurant scene, whose NPC crash 0.3.102 fixed. #7 failed: the host vanished from the friend's screen after the
  opening cutscene, and the friend's game crashed at the Binco door (0x4D68BA). Fixed in 0.3.103, whose own test was
  skipped; its checks moved to the next test.
- **0.1.0-alpha** (2026-10-16, #2–#10 converted to the mirror layer): never tested by two players.
- **0.1.1-alpha** (2026-10-17, three players): played up to #8 Drive-By, where both non-host games crashed when the
  Ballas got out of their Voodoos (0x642067). Earlier in that session the host's game crashed while starting a story
  mission (EIP 0x3F800000), probably with a player dead or arrested. Both are fixed in 0.1.2.
- **0.1.2-alpha** (2026-10-18): not tested yet.

## Story order

Size = bytes in the committed co-op `main.scm`; free = 69,000 − size. Trigger = the MAIN-section thread
that launches the mission and the value of its strand counter `$flag_<strand>_mission_counter` it waits
for (from the thread's source; MAIN = `scripts/MAIN.txt`). Risks come from what the original mission does;
check the source before porting. "Layer + registration" in the risks marks the missions that, from the
script's structure, may need no more than the mirror layer and `Coop.EnableSyncingThisScript()` (2 bytes,
which adds the cutscenes, fades, camera, controls and wanted commands; `docs/design/mirror-layer.md` 4.5);
to be confirmed when the mission comes up in order.

### Los Santos

| # | Mission | Script / file | No. | Size (free) | Status | Trigger | Co-op risks |
|---|---|---|---|---|---|---|---|
| 1 | In the Beginning | `INTRO` | 2 | 12,635 (56,365) | **Skipped, tested** | MAIN (new game) | Upstream starts with the end-of-mission setup and calls the original cleanup (starts `INT`, `MOB_LA1`, `MOB_RAN`, `INTROST`); intro cutscene and BMX ride never play. The contact blip is host-only. |
| 2 | Big Smoke | `INTRO1` | 11 | 25,212 (43,788) | **Converted (0.1.0, test pending)** | INT =0 | BMX ride: each extra player gets a BMX and a blip. |
| 3 | Ryder | `INTRO2` | 12 | 19,450 (49,550) | **Converted (0.1.0, test pending)** | INT =1 | Barber: stores every player's hair (`GetNetworkPlayerInternalId`). |
| 4 | Tagging Up Turf | `SWEET1` | 13 | 19,237 (49,763) | **Converted (0.1.0, test pending)** | SWEET =0 | Tag counting relies on our fork's tag sync. Two players ride in Sweet's car, a third in Ryder's. |
| 5 | Cleaning The Hood | `SWEET1B` | 14 | 20,478 (48,522) | **Converted (0.1.0, test pending)** | SWEET =1 | Uses `TeleportPlayersToHostSafely`. |
| 6 | Drive-Thru | `SWEET3` | 15 | 19,039 (49,961) | **Converted (0.1.0, test pending)** | SWEET =2 | Friend gets a separate Greenwood and joins the chase; all seats in Sweet's car remain occupied by the original cast. Arrivals mainly follow the host. |
| 7 | Nines And AK's | `SWEET2` | 16 | 24,663 (44,337) | **Converted (0.1.0, test pending)** | SWEET =3 | Friend rides in the rear and joins the gas-tank shot. Remote bullet impact on objects (`OB-0`) is implemented; bottle practice objects remain host-only until `OB-1` mirrors host script objects. Group 1 test (0.3.102): the host vanished on the friend's screen after the opening cutscene and the friend crashed at the Binco door (0x4D68BA): a vehicle deleted on the friend's side had destroyed the host's ped there. Fixed in 0.3.103 (players are taken out before a vehicle is deleted, a lost remote player ped is recreated); retest pending. |
| 8 | Drive-By | `SWEET4` | 17 | 25,768 (43,232) | **Converted (0.1.0, test pending)** | SWEET =4 | Separate Greenwood and Tec-9 for the friend; departure/arrival gates include them. The vehicle-health counter is mirrored (M4); wanted checks read the host. 0.1.1 test (2026-10-17, three players): both other games crashed when the Ballas left their Voodoos (0x642067; the sequences' `Task.LeaveCar` went out before the cars had network ids). Fixed in 0.1.2; retest pending. |
| 9 | Sweet's Girl | `HOODS5` | 18 | 16,719 (52,281) | **Converted (0.1.0, test pending)** | SWEET =5 | Friend helps clear attackers, then uses rear-right seat in a four-door rescue car. Sweet's health counter is mirrored (M4). |
| 10 | Cesar Vialpando | `SWEET6` | 19 | 37,796 (31,204) | **Converted (0.1.0, test pending)** | SWEET =6 | Mod shop, betting and lowrider rhythm game remain host-only; friend rides along or watches, and receives the shared outcome. |
| 11 | OG Loc | `TWAR7` | 27 | 32,711 (36,289) | Untouched | SMOKE =0 | Chase Freddy on a bike; carries the lost SMOKE2 warning. Random trains are suppressed on followers (`T1b`, `0x43426D`). Probably layer + registration, with the friend in their own vehicle. |
| 12 | Running Dog | `SMOKE2` | 28 | 12,075 (56,925) | **Warning (lost)** | SMOKE =1 | Chase with Smoke in the car; carries the lost SMOKE3 warning. Probably layer + registration, with the friend in their own vehicle. |
| 13 | Wrong Side of the Tracks | `SMOKE3` | 29 | 20,874 (48,126) | **Warning (lost)** | SMOKE =2 | Bike + train chase, Smoke shoots from the back seat: the bike's two seats are taken. Train + Vagos roof attachment mirror (`T1a`+`T1b`+`T2`) is implemented; port gives the friend a second Sanchez. |
| 14 | Just Business | `DRUGS1` | 30 | 52,907 (16,093) | Untouched | SMOKE =3 | Bike chase with Smoke shooting; long; large. Remote bullet impact on objects (`OB-0`, storm drain cover and explosive barrels) is implemented; host object creation/health mirror waits for `OB-1`. |
| 15 | Home Invasion | `GUNS1` | 24 | 41,369 (27,631) | Untouched | RYDER =0 | Stealth (noise meter), crates into a truck; carries the lost RYDER3 warning. |
| 16 | Catalyst | `RYDER3` | 25 | 20,419 (48,581) | **Warning (lost)** | RYDER =1 | Catch crates on a moving train; carries the lost RYDER2 warning. Train + CJ/Ryder attachment mirror (`T1a`+`T1b`+`T2`) is implemented; friend rides in Ryder's Picador (`T-D5`); crate objects wait for `mission-objects.md`. |
| 17 | Robbing Uncle Sam | `RYDER2` | 26 | 42,276 (26,724) | **Warning (lost)** | RYDER =2 | Forklift crate loading (single-seat vehicle), truck escape. Remote bullet impact on objects (`OB-0`, keypads and crates) is implemented; host object creation/health mirror and forklift hand-over wait for `OB-1`/`OB-3`. |
| 18 | Life's a Beach | `MUSIC1` | 31 | 15,958 (53,042) | Untouched | STRAP =0 | Dance minigame (solo rhythm game). |
| 19 | Madd Dogg's Rhymes | `MUSIC2` | 32 | 10,815 (58,185) | Untouched | STRAP =1 | Stealth burglary (noise meter). |
| 20 | Management Issues | `MUSIC3` | 33 | 26,942 (42,058) | Untouched | STRAP =2 | Limo chauffeur, chase. |
| 21 | House Party | `MUSIC5` | 34 | 18,732 (50,268) | Untouched | STRAP =3 and =4 | Two parts (cutscene, then defending the party). |
| 22 | High Stakes, Low Rider | `CESAR1` | 36 | 5,520 (63,480) | Untouched | CESAR =0 | Lowrider race. The race itself is `CPRACE` (mission 35), which `CESAR` launches after this mission: its race checkpoints need mirror package M5 and its race HUD (immediate text) isn't mirrored. Likely layer + registration. |
| 23 | Burning Desire | `CRASH1` | 22 | 42,144 (26,856) | Untouched | CRASH =0 | Burning house rescue (fire is not synced), unlocks girlfriend Denise. |
| 24 | Gray Imports | `DRUGS3` | 23 | 19,960 (49,040) | Untouched | CRASH =1 | Shootout at the docks. Likely layer + registration. |
| 25 | Doberman | `CRASH4` | 21 | 6,916 (62,084) | Untouched | SWEET =7 | Gang war to take a territory: gang wars and zones aren't synced. |
| 26 | Los Sepulcros | `SWEET7` | 20 | 32,992 (36,008) | Untouched | SWEET =8 | Full car of homies, cemetery hit. Raises the host's respect (stat 68) during the mission: `0623` adds 35 for each of three Ballas that die, `0629` sets it to 200 if it is still below 100. The mirror sends only the pass frame's `0109`/`0998`, so the friend's respect doesn't follow: decide at the port. |
| 27 | Reuniting The Families | `DRUGS4` | 37 | 68,439 (**561**) | Untouched | LA1FIN =0 (FLOW starts LA1FIN after all LS strands) | **Size**: no room for an upstream-style adaptation, but with the mirror layer a minimal port fits (registration 2 bytes, collecting the players 11, a teleport 11; see below). Motel shootout, SWAT helicopter, chase with Sweet shooting; scripted train in the escape is mirrored by `CTrainSync`. |
| 28 | The Green Sabre | `LA1FIN2` | 38 | 22,329 (46,671) | Untouched | LA1FIN =1 | Heavy cutscenes; ends Los Santos (CJ taken out of town). |

### Badlands

| # | Mission | Script / file | No. | Size (free) | Status | Trigger | Co-op risks |
|---|---|---|---|---|---|---|---|
| 29 | Badlands | `BCRASH1` | 39 | 14,095 (54,905) | Untouched | BCRASH =0 | Hill climb to a cabin, kill a witness; photo. |
| 30 | First Date | `CATALIN` | 40 | 7,604 (61,396) | Untouched | CAT (after the trailer cutscene) | Meeting Catalina; short. Likely layer + registration. |
| 31 | Tanker Commander | `CAT3` | 43 | 18,827 (50,173) | Untouched | CAT | Truck with a tanker trailer: trailer sync is open upstream. |
| 32 | Body Harvest | `TRUTH1` | 46 | 16,627 (52,373) | Untouched | TRU =0 | Combine harvester (single seat). |
| 33 | Local Liquor Store | `CAT1` | 41 | 32,811 (36,189) | Untouched | CAT | Robbery with Catalina, chase. |
| 34 | Against All Odds | `CAT4` | 44 | 19,047 (49,953) | Untouched | CAT | Safe blowing, escape. |
| 35 | Small Town Bank | `CAT2` | 42 | 34,499 (34,501) | Untouched | CAT | Bank robbery, escape. |
| 36 | King in Exile | `CATCUT` | 45 | 1,138 (67,862) | Untouched | CAT | Cutscene only. Likely layer + registration. |
| 37 | Wu Zi Mu | `BCESAR4` | 48 | 2,807 (66,193) | Untouched | BCESAR (counter steps) | Race. `BCESAR` also launches `CPRACE` races and external scripts `BCESAR2`/`BCESAR3`; `$flag_bcesar_mission_counter > 9` gates Are You Going To San Fierro?. |
| 38 | Farewell, My Love... | `BCESAR4` | 48 | (same script) | Untouched | BCESAR (counter steps) | Race, same script as Wu Zi Mu. |
| 39 | Are You Going To San Fierro? | `TRUTH2` | 47 | 18,632 (50,368) | Untouched | TRU =1 | Flamethrower on weed fields, helicopter, drive the Mothership; ends the Badlands. |

### San Fierro

| # | Mission | Script / file | No. | Size (free) | Status | Trigger | Co-op risks |
|---|---|---|---|---|---|---|---|
| 40 | Wear Flowers In Your Hair | `GARAG1` | 49 | 25,053 (43,947) | Untouched | GARAGE =0 | Drive around picking up guests (seats). |
| 41 | 555 WE TIP | `SCRASH3` | 51 | 18,656 (50,344) | Untouched | SCRASH =0 | Plant a car, valet uniform. |
| 42 | Deconstruction | `DECON` | 50 | 21,314 (47,686) | Untouched | GARAGE =1 | Bulldozer and site (single-seat vehicles). Remote bullet impact on objects (`OB-0`, explosive barrels) is implemented; host object creation/health mirror (`OB-1`/`OB-2`), persistent cabins (`OB-4`) and map swap (`WS-6`) wait for their packages. |
| 43 | Back to School | `DSKOOL` | 71 | 46,648 (22,352) | Untouched | TRACE (garage counter > 0) | **Driving school**: timed solo tests with medals; its `$driving_test_passed` is read by `MOB_SF` phone calls. Large. |
| 44 | Air Raid | `ZERO1` | 72 | 17,958 (51,042) | Untouched | ZERO =0 | RC minigame (RC vehicles not synced). |
| 45 | Supply Lines... | `ZERO2` | 73 | 9,678 (59,322) | Untouched | ZERO =1 | RC plane, fuel timer. |
| 46 | New Model Army | `ZERO4` | 74 | 27,340 (41,660) | Untouched | ZERO =2 | RC tanks minigame. |
| 47 | Photo Opportunity | `SYN1` | 58 | 16,422 (52,578) | Untouched | SYND =0 | Camera photos. |
| 48 | Jizzy | `SYN2` | 59 | 31,812 (37,188) | Untouched | SYND =1, =2 | Club, car chase. |
| 49 | T-Bone Mendez | `DRIV2` | 65 | 13,337 (55,663) | Untouched | SYND =3 | Bike chase with a passenger shooter (two seats). |
| 50 | Mike Toreno | `DRIV3` | 66 | 16,343 (52,657) | Untouched | SYND =4 | Car chase and rescue. |
| 51 | Outrider | `SYN3` | 60 | 19,634 (49,366) | Untouched | SYND =5 | Bike, escort. |
| 52 | Snail Trail | `SCRASH2` | 52 | 21,841 (47,159) | Untouched | SCRASH =1 (SYND =6) | Follow a train, photos, kill. Train mirror, entity blip on the train and passenger carriage doors (`T1b`+`T4`) are implemented. |
| 53 | Ice Cold Killa | `SYND4` | 61 | 13,784 (55,216) | Untouched | SYND =6 (SCRASH =2) | Stealth infiltration. |
| 54 | Pier 69 | `SYN5` | 64 | 33,714 (35,286) | Untouched | SYND =7 | Sniper, boat chase. |
| 55 | Toreno's Last Flight | `SYN6` | 62 | 9,351 (59,649) | Untouched | SYND =8 | Rocket launcher vs. helicopter. |
| 56 | Mountain Cloud Boys | `WUZI1` | 53 | 28,542 (40,458) | Untouched | WUZI =0 | Ambush fight. |
| 57 | Ran Fa Li | `FARLIE4` | 54 | 9,617 (59,383) | Untouched | WUZI =1 | Drive and protect. |
| 58 | Lure | `DRIV6` | 55 | 13,009 (55,991) | Untouched | WUZI =2 | Lure enemies, car. |
| 59 | Amphibious Assault | `WUZI2` | 56 | 27,266 (41,734) | Untouched | WUZI =3 | Swimming, stealth on a ship. |
| 60 | The Da Nang Thang | `WUZI5` | 57 | 28,705 (40,295) | Untouched | WUZI =4 | Helicopter gunner, ship assault. |
| 61 | Yay Ka-Boom-Boom | `SYN7` | 63 | 17,060 (51,940) | Untouched | SYND =9 (WUZI =5) | Car bomb via the mod garage. |
| 62 | Zeroing In | `STEAL1` | 67 | 9,716 (59,284) | Untouched | STEAL =0 (after buying Wang Cars, `BUYPRO1`) | Chase with a tracker. |
| 63 | Test Drive | `STEAL2` | 68 | 32,513 (36,487) | Untouched | STEAL =1 | Steal cars, drive with a passenger; scripted tram is mirrored (`CTrainSync`). |
| 64 | Customs Fast Track | `STEAL4` | 69 | 35,971 (33,029) | Untouched | STEAL =2 | Steal cars from a ship. |
| 65 | Puncture Wounds | `STEAL5` | 70 | 9,262 (59,738) | Untouched | STEAL =3 | Spikes chase. |

### Desert

| # | Mission | Script / file | No. | Size (free) | Status | Trigger | Co-op risks |
|---|---|---|---|---|---|---|---|
| 66 | Monster | `TORENO1` | 75 | 14,623 (54,377) | Untouched | DESERT =0 | Monster truck race. |
| 67 | Highjack | `TORENO2` | 76 | 16,854 (52,146) | Untouched | DESERT =1 | Jump from a car onto a moving truck (special mechanic). |
| 68 | Interdiction | `DES3` | 77 | 27,316 (41,684) | Untouched | DESERT =2 | Helicopter, protect a drop. |
| 69 | Verdant Meadows | `DESERT4` | 78 | 445 (68,555) | Untouched | DESERT =3 | Airstrip purchase (property purchase via `BUYPRO1` starts `PSCH`). Likely layer + registration (`OB-0` remote bullet impacts on objects implemented; `OB-1`/`OB-2` cover host object creation/blips where applicable). |
| 70 | Learning to Fly | `DESERT5` | 83 | 30,641 (38,359) | Untouched | PSCH (desert counter > 3) | **Flight school**: timed solo tests; `$pilot_test_passed` gates N.O.E. |
| 71 | N.O.E. | `DESERT6` | 79 | 10,814 (58,186) | Untouched | DESERT =5, pilot test passed | Fly under radar (single-seat plane). |
| 72 | Stowaway | `DESERT9` | 80 | 22,689 (46,311) | Untouched | DESERT =6 | Parachute into a flying plane's hold. |
| 73 | Black Project | `MAF4` | 81 | 30,721 (38,279) | Untouched | DESERT =7 | Area 69 stealth, jetpack (jetpack pickup sync is open upstream). |
| 74 | Green Goo | `DES10` | 82 | 4,603 (64,397) | Untouched | DESERT =8 | Jetpack onto a train. Train and guard attachment mirror (`T1b`+`T2`) are implemented; needs `T3` (vehicle-relative on-foot sync) and mission objects (`mission-objects.md`) for the crates. |

### Las Venturas

| # | Mission | Script / file | No. | Size (free) | Status | Trigger | Co-op risks |
|---|---|---|---|---|---|---|---|
| 75 | Fender Ketchup | `CASINO1` | 84 | 19,775 (49,225) | Untouched | CASINO =0 | Hostage on the hood, fear meter (solo mechanic). |
| 76 | Explosive Situation | `CASINO2` | 85 | 20,505 (48,495) | Untouched | CASINO =1 | Quarry dynamite with a timer. |
| 77 | You've Had Your Chips | `CASINO3` | 86 | 14,546 (54,454) | Untouched | CASINO =2 | Chip factory fight. |
| 78 | Don Peyote | `CASINO4` | 88 | 46,275 (22,725) | Untouched | CASINO =3 | Long desert search with two passengers. Large. |
| 79 | Architectural Espionage | `HEIST1` | 96 | 24,204 (44,796) | Untouched | HEIST =0 | Camera, stealth. |
| 80 | Key To Her Heart | `HEIST3` | 97 | 23,327 (45,673) | Untouched | HEIST =1 | Girlfriend Millie dating (solo girlfriend system). |
| 81 | Dam And Blast | `HEIST2` | 98 | 19,367 (49,633) | Untouched | HEIST =2 | Skydive onto the dam, stealth, bombs. |
| 82 | Cop Wheels | `HEIST4` | 99 | 13,400 (55,600) | Untouched | HEIST =3 | Collect police bikes into a truck. |
| 83 | Up, Up and Away! | `HEIST5` | 100 | 21,341 (47,659) | Untouched | HEIST =4 | Leviathan magnet lifts an armoured van. |
| 84 | Intensive Care | `CASINO5` | 89 | 13,204 (55,796) | Untouched | CASINO =4 | Ambulance chase. |
| 85 | The Meat Business | `CASINO6` | 90 | 32,803 (36,197) | Untouched | CASINO =5 | Meat factory shootout. |
| 86 | Fish in a Barrel | `CASINO7` | 87 | 376 (68,624) | Untouched | CASINO =6 | Cutscene only. Likely layer + registration. |
| 87 | Misappropriation | `VCRASH1` | 93 | 8,234 (60,766) | Untouched | VCRASH =0 | Chase and photo. |
| 88 | Madd Dogg | `DOC2` | 95 | 14,990 (54,010) | Untouched | DOC =0 | Catch Madd Dogg with a truck (timing). |
| 89 | Freefall | `CASINO9` | 91 | 12,735 (56,265) | Untouched | CASINO =7 | Mid-air plane jump, dogfight. |
| 90 | High Noon | `VCR2` | 94 | 5,203 (63,797) | Untouched | VCRASH =1 | Pulaski chase and showdown. Likely layer + registration. |
| 91 | Saint Mark's Bistro | `CASIN10` | 92 | 20,832 (48,168) | Untouched | CASINO =8 | Fly to Liberty City, shootout in a separate interior. |
| 92 | Breaking the Bank at Caligula's | `HEIST9` | 101 | 50,751 (18,249) | Untouched | HEIST =5 | Casino heist: long, many phases and cutscenes. Large. |

### Return to Los Santos

| # | Mission | Script / file | No. | Size (free) | Status | Trigger | Co-op risks |
|---|---|---|---|---|---|---|---|
| 93 | A Home In The Hills | `MANSIO1` | 102 | 39,453 (29,547) | Untouched | MANSION =0 | Mansion assault. |
| 94 | Vertical Bird | `MANSIO2` | 103 | 53,511 (15,489) | Untouched | MANSION =1 | Harrier jet (single seat). Large. |
| 95 | Home Coming | `MANSIO3` | 104 | 7,916 (61,084) | Untouched | MANSION =2 | Short; cutscenes (`OB-0` remote bullet impacts on objects implemented; `OB-1`/`OB-2` cover host object creation/blips where applicable). |
| 96 | Cut Throat Business | `MANSON5` | 105 | 13,845 (55,155) | Untouched | MANSION =3 | Chase, boat. |
| 97 | Beat Down on B Dup | `GROVE1` | 106 | 21,475 (47,525) | Untouched | GROVE =0 | Crack den fight. |
| 98 | Grove 4 Life | `GROVE2` | 107 | 5,486 (63,514) | Untouched | GROVE =1 | Gang war for territories (not synced). |
| 99 | Riot | `RIOT1` | 108 | 5,963 (63,037) | Untouched | RIOT =0 (FLOW starts RIOT) | Cutscene, riot ambience. |
| 100 | Los Desperados | `RIOT2` | 109 | 43,554 (25,446) | Untouched | RIOT =1 | Defend Cesar's turf, fights. |
| 101 | End Of The Line (1) | `FINALEA` | 110 | 33,286 (35,714) | Untouched | RIOT =2 | Crack palace assault. |
| 102 | End Of The Line (2) | `FINALEB` | 111 | 18,915 (50,085) | Untouched | RIOT =3 | Fight Big Smoke (`OB-0` remote bullet impacts on objects implemented; `OB-1`/`OB-2` cover host object creation/health mirror where applicable). |
| 103 | End Of The Line (3) | `FINALEC` | 112 | 57,946 (11,054) | Untouched | RIOT =4 | Fire-truck ladder chase with Sweet hanging on (`T2` attachment mirror); scripted train in the chase is mirrored (`CTrainSync`); ending. Large. Pays $250,000 (`0109`) frames before its pass block, so the mirror doesn't send it (the 150 respect of the pass frame reaches both): decide at the port. |

Story-relevant helpers that are not missions of their own: `BUYPRO1` (mission 134, property purchases;
buying Wang Cars starts `STEAL`, buying the Verdant Meadows airstrip starts `PSCH`) and `CPRACE`
(mission 35, race tournament; `CESAR`, `BCESAR`, `TRACE`, `HOTR` and `KICKS` start it). Both have to work
for the host when the story reaches them.

## Size budget

The game reads each mission into a 69,000-byte buffer (plugin-sdk `CTheScripts.h`: `MissionBlock[69000]`;
gta-reversed `TheScripts.h`: `MISSION_SCRIPT_SIZE`), and `scripts/compile-scm.ps1` refuses a build where a
mission is larger. Upstream's adaptations made their missions 14–29 % larger:

| Mission | Vanilla | Co-op | Growth |
|---|---|---|---|
| In the Beginning (`INTRO`, skipped) | 10,682 | 12,635 | +1,953 (18 %) |
| Big Smoke (`INTRO1`) | 22,607 | 25,664 | +3,057 (14 %) |
| Ryder (`INTRO2`) | 16,760 | 19,958 | +3,198 (19 %) |
| Tagging Up Turf (`SWEET1`) | 16,143 | 20,795 | +4,652 (29 %) |
| Cleaning The Hood (`SWEET1B`) | 18,387 | 22,116 | +3,729 (20 %) |

The five largest missions in `main.scm`:

| Mission | Script | Size | Free | Story |
|---|---|---|---|---|
| Reuniting The Families | `DRUGS4` | 68,439 | 561 | yes (#27) |
| Pool | `POOL2` | 62,754 | 6,246 | no (pool minigame) |
| Initial 1 | `INITIAL` | 58,214 | 10,786 | no (new-game setup) |
| End Of The Line (3) | `FINALEC` | 57,946 | 11,054 | yes (#103) |
| Vertical Bird | `MANSIO2` | 53,511 | 15,489 | yes (#94) |

The next story missions by size: Just Business (`DRUGS1`, 52,907), Breaking the Bank at Caligula's
(`HEIST9`, 50,751), Back to School (`DSKOOL`, 46,648), Don Peyote (`CASINO4`, 46,275), Los Desperados
(`RIOT2`, 43,554). With an upstream-style 15–30 % adaptation, every mission above about 53,000 bytes would
be at risk. Much of that growth was copies of the host's displays for the other players; with the mirror
layer a port adds only gameplay lines and friend-only displays, so ports grow less. Reuniting The Families
still has no room for an upstream-style adaptation, but a minimal port fits in its 561 bytes:
registration 2 bytes, collecting the players 11 and a teleport to the host 11.

### When a ported mission doesn't fit

Options, in the order to try them; the last three are architect decisions:

1. **Trim inside the mission.** Vanilla missions carry debug code that does nothing in the release game:
   keyboard skips (`is_ps2_keyboard_key_pressed`), `write_debug`, `save_*_to_debug_file` and debug
   branches. Upstream already removed a debug skip in Big Smoke. Removing it changes only the mission.
2. **Write the co-op parts compactly.** Upstream unrolls blocks for `$NETWORK_PLAYER[0]`, `[1]` and `[2]`;
   one `for $temp_int = 0 to 2` loop or a `gosub` subroutine inside the mission does the same in a third
   of the bytes.
3. **Handle only the players this build has.** Two players means one network player; dropping the code
   for the second and third network player saves most of the co-op bytes, but diverges from upstream.
4. **Move code into an external script.** Only into an existing external script whose entry stays in the
   list (for example one the mission already owns, like `BCESAR2`/`BCESAR3` for the Badlands races):
   adding an external script adds an entry to the `main.scm` header, which moves MAIN and breaks the
   players' saves (`docs/mission-porting.md`, saves). The same goes for splitting a mission into a new
   mission slot.
5. **A later client patch that raises the limit.** The mission buffer is the last 69,000 bytes of
   `CTheScripts::ScriptSpace`; a patch would have to give the game a larger buffer and update every place
   that uses the size. Not part of any port; no such patch exists.

## Porting order

Strictly the story order of the tables above, one mission after the other, because the players play the
story in that order on one continuing save:

1. Re-test the four adapted missions and the skipped intro in game (#1–#5); fix what fails there first.
2. Port #6 onward in sequence. A risky mission (bold or noted risks above) is ported in its place; if
   it can only be made partly co-op, the port says what the other player can't do. If a mission is
   blocked (size, a missing client feature such as gang wars, trailers or RC vehicles), the porting agent
   stops and reports to the architect; the batch ends there and the next mission waits.
3. Batches follow the regions: Los Santos (#6–#28), Badlands (#29–#39), San Fierro (#40–#65), Desert
   (#66–#74), Las Venturas (#75–#92), return to Los Santos (#93–#103).

Missions to plan for early, in their place in the order: Reuniting The Families (#27, size), Doberman
(#25) and Grove 4 Life (#98, gang wars), Back to School (#43) and Learning to Fly (#70, solo tests),
Tanker Commander (#31, trailer), Zero's RC missions (#44–#46), and End Of The Line (3) (#103, size).
