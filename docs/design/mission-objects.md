# Mission objects for two players (design)

Status: designed 2026-10-17 on main `f23b23c`; nothing implemented. Decisions are open (section 11); the architect
takes them to the user. Where a decision is taken, `README.md` in this folder records it.

The model stays as it is: missions run only on the host. Today an object the host's mission creates exists only on the
host's game, so the other player sees no crates, gates, barrels or doors of the mission, and can't carry, lift or
shoot any of them. This design makes the host's mission objects exist on the other games, moving as on the host, and
lets the other player take part where group 2 needs it: carrying a box in #15 Home Invasion, loading crates with a
forklift in #17 Robbing Uncle Sam, and seeing the crates of #16 Catalyst fly, get caught and pile up in the pickup. It
also draws the line for the persistent script objects (doors, gates, cabins `INITIAL` creates on every game), which
decision W-D11 left to an object-sync design.

"Host" is the player whose game runs the story; "the friend" or "the other player" is everyone else (one game with two
players). Mission numbers are those of `docs/missions.md`.

Sources: the mission sources under `CoopAndreas/scm/scripts` (scanned with small scripts that aren't part of the
repository), the mode's command list (`sa_coop.json` and `CustomVariables.ini` in
`CoopAndreas/sdk/Sanny Builder 4/data/sa_sbl_coopandreas/`), plugin-sdk, gta-reversed, the 1.0 US `gta_sa.exe` of the
game copy read with `dumpbin /DISASM` and `tools/pe_peek.py`, and ReVa decompiles of the analyzed project where named.
Every address names where it was checked; "gta-reversed only" marks a claim the implementing package checks in the exe
before relying on it.

## 1. Goals and non-goals

Goals:

- Every object the host's mission family creates (the family of `mirror-layer.md` 3.2: the mission script, scripts
  running in the mission block, `AUDIOL`/`A_CONT`/`A_ALAP`; missions 0–10 excluded as in M-D2) exists on the other
  games as a local copy, with the host's model, position, rotation, interior, visibility, collision, broken and
  damaged state, attachment and holder, and goes away when the host deletes it.
- Moving objects move smoothly: scripted movers (gates, doors, lifts), physics objects (thrown, falling, pushed crates
  and barrels) and objects carried by peds, vehicles and other objects.
- The other player can take part where group 2 needs it: carry a liftable box (#15), lift crates with a forklift their
  game drives (#17), and see an NPC catch and throw objects (#16, #17). The host's mission sees those actions through
  vanilla commands on `$NETWORK_PLAYER[i]`, so ports stay small.
- The other player's shots damage the host's objects (keypads in #17, the storm drain cover in #14, bottles in #7).
- Persistent script objects: the doors, gates and cabins the scripts outside missions create on every game follow the
  host's game whenever the host's missions change them, also after a join, a restart or a load.
- Host-authoritative: the host decides existence and script state; another game simulates an object only while the
  host has handed it over.
- Saves stay loadable: no MAIN, global or external-script change; the copies never enter a save.

Non-goals:

- Map objects (IPL buildings and the dummies the streamer turns into objects: lamp posts, fences, street furniture,
  glass). They stay per game; the script commands on them (`0363`, `03B6`) are the registry's WS-6.
- Pickups (W-D6 stays: per game; ports give the friend mission weapons).
- Explosions, fires and particles. `ADD_EXPLOSION` is not synced in this build (`client/src/Hooks/WorldHooks.cpp:56`,
  the send is commented out "needs to be completely reworked"); section 11 O-D7 proposes a small separate step.
- Full distributed physics (every bump hands an object over). Only the hand-overs of section 5.8.
- Objects of the other player's own scripts (their gym, shops, casino games, girlfriends) and of the minigames 3–10.
- Cutscene objects (type 4): a cutscene replayed through `COpCodeSync` builds its own on each game.

## 2. Inventory

Method: every class-syntax call of an object command in `scm/main.txt` and its includes (303 files), classified by
`main.txt` (MAIN section = the files before `INITIAL.txt`, missions, external scripts), in the story order of
`docs/missions.md` (102 story scripts). Call sites in source, not executions: one create in a loop can make seven
objects. Object commands = the commands with an `Object` parameter or return value, the `Object` class, and the
"closest object of type" commands.

Totals: 7,316 object command call sites, 4,240 of them in the 102 story scripts. 64 story scripts create objects (780
create call sites); 387 create call sites are outside the story (57 scripts: `INITIAL`, `INITIL2`, MAIN threads, side
missions, external scripts).

### 2.1 Commands

Story = calls in story missions (missions using it, first mission); other = other missions (`INITIAL`, `INITIL2`, side
missions, minigames); MAIN; EXT = external scripts. "Mirror" says how this design covers the command (5.x).

| Opcode | Command | Story | Other | MAIN | EXT | Mirror |
|---|---|---|---:|---:|---:|---|
| `0107` | Object.Create | 393 (50, #3) | 110 | 23 | 69 | creation (5.3) |
| `029B` | Object.CreateNoOffset | 387 (38, #12) | 92 | 22 | 71 | creation |
| `0108` | Object.Delete | 467 (51, #3) | 52 | 5 | 79 | removal |
| `01C4` | Object.MarkAsNoLongerNeeded | 185 (21, #13) | 13 | 0 | 23 | stays mirrored (5.10) |
| `01C7` | Object.DontRemove | 27 (7, #23) | 51 | 16 | 0 | stays mirrored; persistent if in a persistent global (5.9) |
| `0177`, `01BC`, `0453`, `07C4` | SetHeading, SetCoordinates, SetRotation, SetQuaternion | 334, 102, 70, 1 | 66, 81, 24, 0 | 16, 29, 0, 0 | 33, 49, 25, 0 | transform state |
| `035C`, `0815`, `08D2` | PlaceRelativeToCar, SetCoordinatesAndVelocity, SetScale | 237 (#43), 10, 6 | 162, 0, 1 | 0, 2, 0 | 0, 6, 3 | transform state; scale |
| `034E`, `034D` | Slide, Rotate | 71 (13, #17), 27 (4, #32) | 0, 1 | 2, 12 | 2, 1 | motion (5.6) |
| `075A`, `0836`, `083A` | PlayAnim, SetAnimSpeed, SetAnimCurrentTime | 13 (6, #23), 3, 4 | 8, 8, 8 | 0 | 65, 4, 29 | not mirrored (risk 9) |
| `0392`, `0382`, `0550` | SetDynamic, SetCollision, FreezePosition | 72 (20, #15), 151 (27, #15), 66 (20, #14) | 24, 41, 13 | 4, 6, 6 | 9, 13, 11 | flags state |
| `0381`, `038C`, `05A1`, `05A2` | velocity commands | 10 (5, #16), 5, 1, 2 | 4, 4, 0, 2 | 3, 0, 0, 0 | 8, 1, 0, 0 | motion |
| `0906`, `0908`, `09CA`, `0875`, `07F7`, `04D9`, `050E` | mass, turn mass, proofs, only damaged by player, collision damage effect, records collisions, collision with car | 1, 1, 23, 46, 61, 9, 18 | | | | host only (physics stays with the owner) |
| `071F`, `0723` | SetHealth, Break | 65 (15, #14), 45 (15, #7) | 0, 1 | 0 | 0 | broken state (5.4) |
| `0750`, `0566`, `035D`, `0905` | SetVisible, SetAreaVisible, MakeTargetable, LockDoor | 31 (13, #23), 23 (4, #15), 51 (10, #7), 1 (#79) | 13, 5, 0, 0 | 2, 0, 0, 0 | 11, 0, 0, 2 | flags state (target and lock: host only) |
| `0681`, `069A`, `069B`, `0682` | AttachToCar, AttachToObject, AttachToChar, Detach | 94 (13, #16), 10 (3, #3), 1 (#100), 14 (6, #44) | 35, 1, 0, 4 | 0 | 1, 7, 5, 10 | attachment (5.7) |
| `0939`, `04F4` | Car.AttachToObject, Char.AttachToObject | 4 (#64), 7 (6, #13) | 0, 8 | 0 | 0, 4 | the vehicle or ped side (vehicle and ped sync) |
| `070A`, `070B`, `07C9` | Task.PickUpObject, Char.DropObject, Task.ComplexPickupObject | 37 (18, #3), 25 (7, #3), 3 (2, #72) | 14, 4, 0 | 0 | 22, 9, 0 | holder (5.7, 5.8) |
| `08E9`, `0737` | Object.SetAsStealable, Char.IsHoldingObject | 3 (#15), 13 (4, #15) | 0, 19 | 0 | 0, 16 | liftable flag; holder |
| `078B`, `0796`–`0799` | winch, rope (magnet cranes) | 2 (#83), 23 (#42, #64) | 1 | 6 | 6 | attachment as seen (risk 8) |
| `03CA`, `01BB`, `0176`, `0400` | DoesExist, GetCoordinates, GetHeading, GetOffsetInWorldCoords | 428 (38, #7), 83, 13, 26 | | | | host only (queries read the host's object) |
| `0366`, `08FF`, `071E`, `0685`, `04E6`, `04EA`, `04E7`, `04DA` | damaged, damaged by weapon, health, attached, locate, in area, in water, collided | 38, 15, 18, 17, 25, 1, 4, 6 | | | | host only |
| `0897`, `0179`, `023B`, `0471`–`0476`, `0458` | vehicle/char touching or locating an object, player targeting it | 12 (#17), 6 (#16), 2, 9, 3 (#7) | | | | host only; they see the friend once the friend's actions reach the host's copy (5.8) |
| `0188` | Blip.AddForObject | 41 (15, #15) | 198 | 0 | 1 | entity blip on the copy (6.2) |
| `097B`, `097C`, `066D`, `06CA` | audio event at object, audio attached, particle on object, searchlight on object | 70 (13, #16), 10 (4, #27), 19 (3, #68), 18 (2, #59) | | | | not mirrored (sound and effects; risk 9) |
| `0655`, `06E2` | Task.LookAtObject, Task.GoToObject | 3, 1 | | | | ped tasks (not mirrored) |
| `0363`, `03B6`, `0716`, `07F0`, `088D`, `0985`, `0A46` | map objects: visibility, building swap, smashed tests, collision, brains | 10, 3, 45, 3, 11, 1, 2 | | 4 | 13 | not this design (WS-6) |

### 2.2 Classes

| Class | What | Commands | Examples (first story use) |
|---|---|---|---|
| Static props | created, placed, maybe rotated, frozen or resting; deleted at the end | `0107`/`029B`, `0177`, `0453`, `0550`, `0382` | cutscene prop at Smoke's house (#11), the rail-yard gantry (#13), the skip (#14), road-work barriers (#29), the wrecked cars of #103 |
| Scripted movers | moved by the script frame after frame | `034E` Slide, `034D` Rotate, `01BC` per frame, `0815` | the base gates and warehouse doors of #17, the compound doors of #24, Wong's gate (#61), jet doors (#89), lifts (#94) |
| Physics objects | dynamic, falling, pushed, thrown, broken | `0392`, `0381`, `038C`, `0723`, `071F` | bottles (#7), barrels (#14), thrown crates (#16), forklift crates (#17), road barriers (#20), portaloos (#42) |
| Carried or attached | held by a ped (hold task), attached to a vehicle, a ped or an object | `070A`, `070B`, `0681`, `069A`, `069B` | Ryder's cigar (#3), the boxes of #15, crates on the caboose and in the pickup (#16), the load in the Mule (#17), cargo on train carriages (#74) |
| Interactive | the player shoots, lifts, carries, mounts or crane-lifts them | `08E9`, `0737`, `0366`, `08FF`, `04F4`, rope commands | the storm drain cover (#14), liftable boxes (#15), keypads and forklift crates (#17), the minigun mount (#44), the magnet crane (#64) |
| Persistent | created outside missions on every game (`INITIAL`, `INITIL2`, MAIN threads), kept with `01C7`; missions open, move, break, delete or rebuild them | `0107` + `01C7` in `INITIAL`, then any of the above in a mission | the door of the burning house (#23), the motel poster (#27), the bank doors (#35), the cabins (#42), the Chinatown gate (#56), the freighter door (#60), Area 69's doors (#73), Carter's wall (#101) |

### 2.3 Per mission

Objects each story mission creates or changes (from the scan), the class, and what the friend needs. "See" means the
copy of 5.5; "do" names an interaction of 5.8 or a port decision. Rows after #17 come from the scan and are confirmed
when each mission is ported. Missions without object commands, by number: 1, 2, 4, 6, 9, 10, 21, 22, 28, 30, 31, 36,
37, 38, 45, 47, 50, 51, 52, 57, 62, 66, 67, 69, 78, 80, 82, 86, 87, 90, 91, 95, 97, 98, 99 and 102.

| # | Script | Objects | Class | Friend |
|---|---|---|---|---|
| 3 | `INTRO2` | Ryder's cigar and its glow (attached to the cigar) | carried by an NPC | see it in Ryder's hand |
| 5 | `SWEET1B` | 4 cigars picked up and dropped by peds | carried by NPCs | see (cosmetic) |
| 7 | `SWEET2` | 18 wine bottles for the bottle practice (targetable, broken by shots) | interactive, physics | see; shooting them needs OB-0 |
| 8 | `SWEET4` | a beer bottle in a ped's hand | carried | see (cosmetic) |
| 11–17 | group 2 | 2.4 | | |
| 18 | `MUSIC1` | 2 woodpiles | physics | see |
| 19 | `MUSIC2` | a juice can in a ped's hand | carried | see |
| 20 | `MUSIC3` | 24 road barriers | physics | see them fall when the limo hits them |
| 23 | `CRASH1` | burning house: 8 windows, 7 doors (one replaces `INITIAL`'s `$g_BD_DOOR`), 9 breakable wall pieces, 5 ammo boxes, fire objects; `$g_BD_DOOR` deleted and rebuilt | movers, breakable, persistent | see the same walls, doors and windows; never two doors in one place (5.9) |
| 24 | `DRUGS3` | barrels (four attached to two vehicles), 8 truck doors and 8 ramps at the docks, a sliding compound door, a keypad | physics, attached, movers | see; the barrels ride on their vehicles |
| 25 | `CRASH4` | a cigar | carried | see |
| 26 | `SWEET7` | the casket | static | see |
| 27 | `DRUGS4` | 7 trolleys, 6 scaffolds, 6 animated fences, motel vents, skylight, motel door, cut-up cop parts; `$advert_f1` deleted, rebuilt, broken | physics, movers, persistent | see; one poster |
| 29 | `BCRASH1` | 3 road-work barriers | static | see |
| 32 | `TRUTH1` | 18 weed plants, 7 hay bales (the combine) | physics | see them fly |
| 33 | `CAT1` | 4 briefcases | carried | see |
| 34 | `CAT4` | safes, a door, satchel charges | breakable | see |
| 35 | `CAT2` | 18 ATMs, the bank safe; `INITIAL`'s bank doors deleted and rebuilt | breakable, persistent | see; doors as on the host |
| 39 | `TRUTH2` | 44 tests whether the grass plants of the weed farm (map objects of the `TRUTHSFARM` IPL group) are burnt (`0716`) | map objects | per game: the friend's flamethrower burns only their own plants; a port or WS-6 decides |
| 40 | `GARAG1` | cigars, 2 park benches | carried, static | see |
| 41 | `SCRASH3` | a lamp post's collision switched off and on (`088D`) | map object | per game (WS-6) |
| 42 | `DECON` | 7 portaloos, 5 barrels, cabins (`$init_portakabin`, persistent, broken and deleted), building swap (WS-6) | physics, persistent | see; the cabins stay as the host left them |
| 43 | `DSKOOL` | 46 traffic cones and the course pieces, placed relative to the car (231 calls) | static, physics | see (school, solo) |
| 44 | `ZERO1` | minigun mount (the player is attached to it), Wong's roof pieces, RC bombs | interactive | see |
| 46 | `ZERO4` | barrels, planks, RC heli magnets (attach) | physics, attached | see |
| 48, 58 | `SYN2`, `DRIV6` | a phone in hand | carried | see |
| 49 | `DRIV2` | 3 drug packets passed between bikes (attach, hold) | carried | see |
| 53 | `SYND4` | a phone | carried | see |
| 54, 55 | `SYN5`, `SYN6` | sniper prop, woodpile; a box | static, physics | see |
| 56 | `WUZI1` | bins, Chinatown garage doors, stall, table; `INITIAL`'s Chinatown gate broken and deleted for good | movers, persistent | the gate gone as on the host |
| 59 | `WUZI2` | 4 searchlights (base, housing, bulb) on the ship | movers | see them sweep |
| 60 | `WUZI5` | containers, the refugees' crate with doors and padlock, katanas; `INITIAL`'s freighter door rotated open | movers, persistent | see; the door open as on the host |
| 61 | `SYN7` | Wong's gate (slides), a ramp; crack factory (WS-6) | mover | see |
| 63 | `STEAL2` | road barriers, boxes, skip, dumpster, ramp | physics | see |
| 64 | `STEAL4` | containers moved by the magnet crane (rope); the persistent crane parts moved | rope-attached, persistent (mission-only) | see the crane and containers move |
| 65 | `STEAL5` | spike strips | static | see |
| 68 | `DES3` | the dropped package with its parachute, holdalls, tear-gas canister props | physics, attached | see |
| 70, 71 | `DESERT5`, `DESERT6` | base-jump target; ammo capsules | static, attached | see |
| 72 | `DESERT9` | crate stacks, barrels, gun crates inside the flying Andromada | physics in a moving vehicle | see (vehicle-relative, 5.7) |
| 73 | `MAF4` | 63 searchlight parts, vent covers, blast and lab doors (persistent, slid and rebuilt) | movers, persistent | see; doors as on the host |
| 74 | `DES10` | cargo and crates attached to train carriages, smash boxes, the goo canister | attached to a train | see (needs the train side, 6.4) |
| 75 | `CASINO1` | the Four Dragons garage door (persistent, slides) | persistent mover | see it open |
| 76 | `CASINO2` | dynamite, gun boxes, quarry gates, plunger, barriers, signs | carried, movers | see |
| 77 | `CASINO3` | 18 chip machines and parts to destroy, containers; loading bay shutter and Four Dragons door (persistent) | breakable, persistent | see |
| 79 | `HEIST1` | doors, aircon units, the plans (photographed), Four Dragons door | movers, persistent | see |
| 81 | `HEIST2` | smash boxes, cargo, containers, satchels | breakable | see |
| 83 | `HEIST5` | minigun mount; the van on the Leviathan's magnet (winch) | interactive | see |
| 84 | `CASINO5` | a wheelchair (attached) | attached | see |
| 85 | `CASINO6` | carcasses on hooks, meat, bags, keypads, freezer door | attached, movers | see |
| 88 | `DOC2` | smash boxes in the truck | attached, breakable | see |
| 89 | `CASINO9` | jet baggage doors (slide), a box | movers | see |
| 92 | `HEIST9` | money bags (carried), doors, generators to destroy, vault door, keypad; casino door (persistent) | carried, breakable, persistent | see; carry like #15 if ported so |
| 93 | `MANSIO1` | parachute packs (carried), boxes, cargo door, ramp | carried, movers | see |
| 94 | `MANSIO2` | keypads, missiles; carrier doors and lifts (persistent, deleted and rebuilt) | movers, persistent | see |
| 96 | `MANSON5` | a water ramp | static | see |
| 100 | `RIOT2` | basketballs; the flat doors (persistent) rotated open | persistent movers | see |
| 101 | `FINALEA` | barrels, a door, a window; Carter's wall (persistent, broken, rebuilt on fail) | breakable, persistent | see |
| 103 | `FINALEC` | 22 wrecked cars along the chase | static | see |

Most objects alive at once (upper bound: distinct handle variables, arrays counted by size): `DSKOOL` (#43) 51 with its
46 cones, `DESERT9` (#72) 43, `RYDER2` (#17) 40, `DRUGS4` (#27) 31, `CASINO6` (#85) 30, and about 25 in #16, #23, #24
and #73. Every game also has about 70 persistent script objects (2.5).

### 2.4 Group 2 in detail (#11–#17)

| # | Script | Objects | Friend must see | Friend can do |
|---|---|---|---|---|
| 11 | `TWAR7` | `LAE_SMOKECUTSCENE` at Smoke's house (2055, -1695), created before the intro cutscene, deleted after it | the prop during the cutscene | nothing object-related |
| 12 | `SMOKE2` | the same prop around its cutscene; a pistol pickup (host only, W-D6) | the prop | — |
| 13 | `SMOKE3` | the same prop; `IMMMCRAN`, the rail-yard gantry at (2194, -1912): an enemy riding the train is knocked off and attached to it (`04F4`); `IMY_TRACK_BARRIER` at the track end (deleted once off screen) | the gantry (or the enemy hangs in the air), the barrier | — |
| 14 | `DRUGS1` | the same prop; `KMB_SKIP`, frozen (a fixed obstacle); `STORM_DRAIN_COVER` with 150 health: shot open with the Micro SMG, else the bike is damaged; 5 explosive `BARREL4` | the skip with collision, the cover breaking, the barrels | shoot the cover and the barrels (OB-0); the explosion itself is not mirrored (O-D7) |
| 15 | `GUNS1` | 6 `DB_AMMO` boxes (7 created, one deleted) in the colonel's house (interior 8), liftable (`08E9`), dynamic, each with an object blip; carried out to the van (model 609); delivered with Triangle at the van's back (`Char.LocateOnFoot3D` + `Pad.IsButtonPressed`), then deleted; a box in water is lost | the boxes and their blips inside the house, a box in the host's hands while the host carries it, delivered boxes gone | carry boxes too and deliver them (OB-3 + port, O-D4) |
| 16 | `RYDER3` | the freight train (frozen, then on car recording 490); on its caboose a cargo prop `AMMOTRN_OBJ` and 3 `CR_GUNCRATE` attached (`0681`); 4 crates on the platform (deleted when the train leaves); a bin; the throw: the host holds Circle/L1 for a power bar, release creates a crate at the camera's look-at point with the train's speed plus the throw (`029B`, `0392`, `0382`, `0381`); up to 10 in flight; within 1 m of Ryder's catch point Ryder catches it (`Task.PickUpObject($ryder, crate, …, "CATCH_BOX", "BOX")`); when the catch task ends a crate is attached to the Picador's bed (5 positions) and the thrown one deleted; a crate that hits anything for more than 4 frames breaks (`0723`, a particle); after 5 the crates are `CR_AMMOBOX` (used-object −135; −151 is `CR_GUNCRATE`), and Ryder complains when an explosion hurts him; a crate more than 100 m away is released; 10 needed in 90 s | the train with its cargo (6.4), the crates flying, Ryder catching, the bed filling, breaks | fight in the train-yard shootout before the train leaves (the Vagos guarding the yard, ped models 108–110, and a Greenwood of Ballas, 102 and 104); during the throws nobody attacks, so the friend rides in the Picador's free passenger seat and watches, unless O-D2 gives them a throw: the throw reads the host's pad, camera and power bar, and the catch is Ryder's scripted task, so throwing or catching for the friend needs client support |
| 17 | `RYDER2` | base gates `KMB_FRONTGATE` ×2 and warehouse doors `WAREHOUSE_DOOR2B` ×3 that slide open (`034E`); 6 frozen `WAREHOUSE_DOOR1`, 2 deleted when keypad `61@` is shot; 2 `SEC_KEYPAD` (health 10, shot to open); 17 `CJ_CARDBRD_PICKUP` crates (dynamic, mass 1000) lifted with the forklift (530, single seat); 6 must be loaded: a crate within the box at the Mule's back (offset 0, -2.61, 1.04; 0.71 × 1.0 × 1.12) while the host sits in the forklift → a short host-only cut, the crate deleted, a load box attached in the Mule (`308@`–`313@`); a shot crate gives a weapon, armour or health pickup (host only) and is deleted; too few crates left fails the mission; a collision box `NF_PED_COLL` attached at the Mule's back, a fake Mule collision; the escape: Ryder (attached in the back) holds a `CM_BOX` and throws it out (drop, quaternion, velocity, collision with the truck ignored); a Patriot touching a box or a shot box explodes (`Fx.AddExplosion`) | the gates and doors opening, the keypads, the crates and the forks lifting them, the load in the truck, Ryder's boxes falling out | shoot the keypads (OB-0); load crates with a second forklift (OB-3, O-D3); ride in the Mule's free front seat in the escape and shoot |

Group 2 has no persistent object (none of the 68 persistent globals of 2.5 is used by #11–#17).

### 2.5 Persistent objects

Created on every game by `INITIAL` (51 create call sites), `INITIL2` (4), MAIN (12 `AD_FLATDOOR`), `APCHECK` (8
airport gates), `GFINIT` (7 girlfriend doors), `CRANES`, `IMPEXPM`, `PROJECT`, `BBCHAL`, `APGATE`; 68 globals hold
them (`CustomVariables.ini` names the indices; the decompile keeps the original indices, so both games have the same).
Story missions use these (global index, model):

| Global (index) | Model | Story missions | End state after the mission |
|---|---|---|---|
| `$g_BD_DOOR` (2665) | `CR1_DOOR` | #23 deletes it, uses its own door `114@` at the same place, rebuilds it at the end | restored |
| `$advert_f1` (2659) | `MD_POSTER` | #27 deletes, rebuilds, breaks it, rebuilds it at the end | restored |
| `$pc_door`, `$2661`–`$2663` (2660–2663) | `CR_DOOR_01/03` | #35 deletes them at the start, rebuilds them (twice), damage checks | restored |
| `$init_portakabin` (2671–2676) | `PORTAKABIN` | #42 deletes and rebuilds them with blips, breaks them; deletes `[2]` | some broken or gone: changed for good |
| `$china_town_gate` (2666) | `CT_GATEXR` | #56 deletes and rebuilds it, rotates, breaks and deletes it | gone for good |
| `$w5_storm_freighter_door` (2702) | `FREIGHT_SFW_DOOR` | #60 rotates it open | open (to confirm at #60) |
| `$frontdoorr_a51`, `$frontdoorl_a51`, `$launchdoor_a51`, `$a51ventcover` (2667, 2668, 2670, 5177) | Area 69 doors | #73 slides, deletes, rebuilds them | restored (to confirm) |
| `$fourdragons_door` (2658), `$ca3_loadingbay` (2664) | Four Dragons door, shutter | #75, #77, #79 slide and move them | restored |
| `$g_VB_REAR_DOOR`, `$g_VB_SMALL_LIFT`, `$g_VB_BIG_LIFT` (5290–5292) | carrier door and lifts | #94 deletes and rebuilds them | restored |
| `$h9_door_c` (2703) | `AB_CASDORLOK` | #92 rotates it, resets its heading | restored |
| `$2709`–`$2720` (2709–2720; MAIN and `INITIAL`) | `AD_FLATDOOR` | #100 rotates them open | open (to confirm at #100) |
| `$carterwall` (2721) | `IMY_SHASH_WALL` | #101 deletes and rebuilds it, breakable; rebuilt on fail | broken on pass |
| `$magno_*` (2727–2732) | magnet crane parts and their LODs | #64 moves them; the external `CRANE2` lets each player work the crane | per player |

The airport gates (`APCHECK`/`APGATE`), the girlfriend doors (`GFINIT`/`GFDATE`), the cranes (`CRANE1`–`CRANE3`) and
the import/export board (`IMPEXPM`) are driven by each game's own scripts for its own player and are not mirrored
outside the host's missions. `DOORS` (MAIN) rotates the flat doors but nothing starts it.

This changes the timing of W-D11: the first persistent object a mission changes is #23's door (group 4), and the first
one changed for good is #42's cabins; #56 removes the Chinatown gate for good.

### 2.6 What the story needs first

Group 2: static props (#11–#14), physics objects and breaks (#14, #16, #17), scripted movers (#17), attachments to
network vehicles (#16 bed, #17 load), NPC holds (#16 catch, #17 throw), the carry hand-over (#15), the forklift
hand-over (#17), the other player's shots on objects (#14, #17), object blips as entity blips (#15), and the train
side for the caboose cargo (#16). Group 3 (#18–#22): physics props only. Group 4 (#23–#28): persistent objects
(#23 and #27), breakable walls and windows (#23), barrels and objects attached to trucks (#24). Later: the burnt weed
plants of #39 (map objects, outside this design), train cargo (#74), the magnet crane (#64), searchlights (#59, #73),
objects inside flying vehicles (#72).

## 3. Engine facts (1.0 US)

### 3.1 Pool and handles

- `CPools::ms_pObjectPool` 0xB7449C: 350 slots (`push 15Eh` before the pool constructor 0x550640, stored at 0x5510AC
  in `CPools::Initialise` 0x550F10), stride 0x19C (the pool holds `CCutsceneObject`-sized slots; `CObject` is 0x17C).
  Bytes; gta-reversed `Pools.cpp` agrees.
- A handle is `(index << 8) | id`, the slot's byte-map value (`CPools::GetObjectRef` 0x550020). `CPools::GetObject`
  0x550050 returns NULL unless the byte map equals the handle's low byte; a free slot never matches (bit 0x80 set).
  The command cases use `CPool<CObject>::GetAtRef` 0x465040 (the same check) and `GetRef` 0x465070. Bytes.
- `CPools::SaveObjectPool` 0x5D4940 saves only objects of type 2 (`cmp byte ptr [ecx+13Ch],2` at 0x5D496F and
  0x5D49B4), with pool reference, model and a 0x30-byte save structure. Bytes.

### 3.2 Creation, release, cleanup

- `0107` (case 0x46976F) and `029B` (case 0x47F4F1): `CObject::Create(model, 0)` 0x5A1F60 (two arguments are pushed;
  plugin-sdk's wrapper passes one); type +0x13C = 2 (`OBJECT_MISSION`) when the script's +0xC7 is 0 and +0xC9 is 0xFF,
  else 6 (`OBJECT_MISSION2`, external scripts); z −100 finds the ground (0x569660); `0107` adds the distance from the
  centre of mass to the base (0x536BE0); position 0x420B80, zero orientation 0x439A80; a LOD atomic model gets
  `SetupBigBuilding` 0x533150; `CTheScripts::ClearSpaceForMissionEntity` 0x486B00 (clears vehicles and peds only:
  gta-reversed `FindObjectsKindaColliding(…, vehicles, peds)`); `CWorld::Add` 0x563220; the handle goes to
  `ScriptParams[0]` (0x469890, 0x47F603) before `StoreParameters` 0x464370; a script with +0xC6 (uses mission cleanup)
  adds it to `CMissionCleanup` 0xA90850 with `AddEntityToList(handle, 3)` 0x4637E0. Bytes.
- `0108` (case 0x4698C2): null-safe; `CWorld::Remove` 0x563280, `CWorld::RemoveReferencesToDeletedObject` 0x565510,
  the deleting destructor (vtable slot 0), then `RemoveEntityFromList(handle, 3)` 0x4654B0. `01C4` (case 0x47D78B):
  `CTheScripts::CleanUpThisObject` 0x4866C0 and the list removal. `01C7` (case 0x47D813): only the list removal, no
  object access. Bytes.
- `CTheScripts::CleanUpThisObject` 0x4866C0 (a jump into `.HOODLUM`, body 0x156B890): for types 2 and 6 the object
  becomes type 3 (`OBJECT_TEMPORARY`), `m_nRemovalTime` +0x150 = now + 20,000,000 ms, +0x14A = −1, bit 0x80 of the
  flags +0x140 cleared, `CObject::nNoTempObjects` 0xBB4A70 incremented. Bytes; gta-reversed's version matches.
- `CMissionCleanup::Process` 0x468560 walks 75 entries (`mov ebp,4Bh` at 0x468A99); an object entry (case 0x468B31)
  goes through `CleanUpThisObject` (call at 0x468B5D). So at a mission's end its objects stay in the world as
  temporary objects. Bytes.
- `CPopulation::ManageObject` 0x615DC0 (called from 0x61613A and 0x616236): nothing for objects
  `CObject::CanBeDeleted` 0x59F120 refuses (types 2, 4, 5 and 6, jump table 0x59F13C); a temporary object is deleted
  when the centre of the world (the local player or camera) is more than 54.5 m away (0x858FF4), more than 25 m
  (0x858FE8) and the object is off screen (`CEntity::GetIsOnScreen` 0x534540), or when `m_nRemovalTime` has passed.
  Bytes.
- `CObject::DeleteAllMissionObjects` 0x5A1910 (types 2 and 6) and `DeleteAllTempObjects` 0x5A18B0 are called only from
  `CWorld::ClearForRestart` (0x56437C, 0x564377; no other call in any section): a new game or load. Bytes.

### 3.3 Physics, freezing, attachment

- `CPhysical` fields: physical flags +0x40, move speed +0x44, turn speed +0x50, mass +0x8C, turn mass +0x90,
  `m_pAttachedTo` +0xFC, attach offset +0x100, attach rotation +0x10C, attach quaternion +0x118, ignored-collision
  entity +0x128. Bytes: `AttachEntityToEntity` 0x54D570 writes +0xFC/+0x100/+0x10C/+0x118–+0x124/+0x128; `0685` reads
  +0xFC (0x4956F7); `050E` writes +0x128 (0x48CB44); `CObject::ProcessControl` reads +0x44 (0x5A2293). plugin-sdk
  `CPhysical.h` agrees.
- `0681` (case 0x495425): `AttachEntityToEntity(vehicle, offset, rotation in degrees × 0.017453)` 0x54D570 (constant
  at 0x8595EC), which registers the reference (`CEntity::RegisterReference` 0x571B70), makes the parent the
  ignored-collision entity and calls `PositionAttachedEntity` 0x546FF0. The quaternion form 0x54D690 takes the offset
  relative to the fork frame when an object is attached to a forklift (the model check `cmp word ptr [edi+22h],212h`
  at 0x54D70D, the frame at +0x698, `m_aCarNodes[CAR_MISC_A]`). `0682` (case 0x4954E2) is null-safe and calls
  `DettachEntityFromEntity` 0x5442F0. Bytes.
- `PositionAttachedEntity` (a detour stub at 0x546FF0, body from 0x546FF6; ReVa decompile): the parent matrix is a
  bike's lean matrix, or the fork frame's matrix (`RwFrameGetLTM` 0x7F0990) for an object on a forklift; an attached
  object stops being static and gets physical flag 0x2000000 (attached). Detaching: on a forklift when the fork angle
  is 0 and the previous angle (+0x86E) was not (the fork came all the way down), or when the object hits a building
  harder than a tenth of its mass; on a dumper when the bed moves; a vehicle attached to a vehicle that tilts past
  45°. Matches gta-reversed `Physical.cpp`.
- `0550` (case 0x48D621) sets or clears physical flags 0x2004 (`bDisableCollisionForce`, `bDontApplySpeed`). `0392`
  (case 0x4841E3) calls `SetIsStatic` (vtable +0x10) and `AddToMovingList` 0x542800 / `RemoveFromMovingList` 0x542860.
  `0381` (case 0x48352B) multiplies by 0.02 (0x858B38) before setting the move speed (0x441130): velocities are per
  1/50 s. Bytes.
- An entity's area code (interior) is +0x2F (`0566`, case 0x48D9B2). Bytes.

### 3.4 Carry and pick-up

- `070A` `Task.PickUpObject` (case 0x46D166 → 0x46AF50 → body 0x156B440): a `CTaskSimpleHoldEntity` of 0x3C bytes
  (`CTask::operator new` 0x61A5A0; constructors 0x6913A0 with anim ids or 0x691470 with anim names) set as a secondary
  task (`CTaskManager::SetTaskSecondary` 0x681B60; the Hoodlum layer computes the slot argument at run time). The
  constructor (a stub at 0x6913A0, body from 0x6913A7; 7 arguments, `ret 1Ch`): vtable 0x870B2C, held entity +0x08,
  offset +0x0C, bone +0x18, bone flags +0x19, anim id +0x20, group +0x24; it sets the entity's "don't stream out" flag
  (+0x1C bit 0x400) and registers a reference to it (0x571B70), so a deleted held object nulls the task's pointer; the
  task type is 0x133 (307, `TASK_SIMPLE_HOLD_ENTITY`, 0x691460). Bytes.
- `0737` (case 0x46DE3E): `CPedIntelligence::GetTaskHold` 0x600FF0 returns the secondary task in slot 4 when its type
  is 0x133, else (unless told not to) the simplest active task of type 0x134 (pick up) or 0x135 (put down); the held
  entity is at task +0x08; object −1 means "holds anything". Bytes; ReVa decompile.
- "Stealable" (`08E9`, case 0x475C40) is bit 0x2000 of the object flags +0x140 (gta-reversed calls it `bIsLiftable`).
  Bytes. Its readers (a scan of the whole disassembly, `.HOODLUM` included, for a +0x140 load followed by a 0x2000
  test): 0x43E9A9 in `CEntryExit::ProcessStealableObjects` 0x43E990 (an object carried through a door takes the new
  area code), 0x471664 (in the `0866` GetClosestStealableObject case 0x4715B8), 0x56DE3C in
  `CPlayerInfo::FindObjectToSteal` 0x56DBD0, 0x59160D (interior furniture), 0x69314E and 0x6932E5
  (`CTaskSimpleHoldEntity` dropping). `FindObjectToSteal` is called only at 0x5704E4 in `CPlayerInfo::Process`, the
  on-foot side of the enter/exit-vehicle key handler (the in-car side builds `CTaskComplexLeaveCar`, 0x63B8C0 at
  0x5703F7 and 0x57049C) when the player is not attached (`cmp [eax+0FCh],ebp` at 0x5704D5). `FindObjectToSteal` (ReVa
  decompile; gta-reversed marks it unreversed) scans the object lists of the repeat sectors around a point 1.5 m in
  front of the player and returns a liftable object within about 2.1 m of it (squared distance below 4.5) that passes
  a facing score; it tests neither the object type nor static or dynamic. The found object goes to a 0x34-byte task
  (constructor 0x6919C0, the layout of gta-reversed's `CTaskComplexGoPickUpEntity`: entity at +0x0C with a registered
  reference, anim group +0x2C) given through a script-command event (`CEventScriptCommand` 0x4B0A00,
  `CEventGroup::Add` 0x4AB420 on the intelligence's event group +0x68). So the engine itself picks up a liftable
  object when the player presses enter-vehicle facing it; #15's boxes need no script task, and any liftable object in
  the world qualifies, a copy included.

### 3.5 Forklift and dumper

- `CObject::ProcessControl` 0x5A2130: an object with damage intensity (+0xD8 > 0), the damaged bit 0x80000 of +0x140,
  not attached, not a crane part (0x59F050), without infinite mass or disabled move force (0x60 of +0x40), whose
  damage entity (+0xDC) is a dumper (0x196) or a forklift (0x212) with fork angle `m_wMiscComponentAngle` +0x86C above
  0, and not carried by a rope (`CRopes::IsCarriedByRope` 0x555F80), takes collision damage (`ObjectDamage` 0x5A0D90
  with weapon 49 above 5.0, 0x858C80) and, when the impact's upward speed exceeds 0.3 (0x858C24) and its speed and
  turn match the vehicle's (squared difference below 0.0001), attaches itself to the vehicle
  (`AttachEntityToEntity(vehicle, NULL, NULL)`) with elasticity 0.2. The crane-part check 0x59F050 lists the crane
  magnet, harness, mini magnet and wrecking ball. Bytes up to 0x5A2240 and a ReVa decompile; the dumper needs no fork
  angle in 1.0 (gta-reversed `Object.cpp` requires it for both, wrongly).
- The fork lifts by attachment: `PositionAttachedEntity` places the crate relative to the fork frame and drops it when
  the fork comes all the way down or the crate hits a building (3.3). The fork angle is in the driver update already
  (`m_wMiscComponentAngle`, `client/src/CNetworkVehicleManager.cpp:425`, `PacketHandlers/vehicles.cpp:171`; the
  previous angle +0x86E is not sent), so a copy of the forklift raises its forks on the other games.
- In #17 the crates have mass 1000 and turn mass 1000 (`0906`, `0908`), so only the forks move them.

### 3.6 Thrown, breakable and damaged objects

- Thrown objects are script physics: #16 creates the crate, makes it dynamic with collision and sets a velocity; #17
  drops the held box and adds velocity. Nothing in the engine throws them.
- `0723` Break (case 0x46D754): a glass object (`CGlass::IsObjectGlass` 0x46A760) breaks through
  `CGlass::WindowRespondsToCollision` 0x71BC40; any other object gets `BreakManager_c::Add` 0x59E9B0 on `g_breakMan`
  0xBB4240 (the falling pieces), loses collision and visibility (entity flags +0x1C `&= ~0x81`), becomes static and
  gets bit 0x400 (broken) of +0x140. The object stays in the pool until the script removes it. Bytes; gta-reversed
  names the functions.
- `0366` HasBeenDamaged (case 0x4831F8 → 0x1569D20): entity flag 0x200 (rendered damaged) or not visible. Damage
  itself is `CObject::ObjectDamage` 0x5A0D90 (plugin-sdk, gta-reversed), reached by bullets
  (`CWeapon::DoBulletImpact`), collisions and explosions.

### 3.7 Stale object handles in vanilla commands

`GetAtRef` returns NULL for a deleted object's handle. Null-safe on 1.0: `0108` Delete, `01C4` MarkAsNoLongerNeeded,
`01C7` DontRemove (no access), `03CA` DoesExist (case 0x485063), `0682` Detach, `0685` IsAttached, `04E6` Locate3D
(test at 0x48C2B8); `0737` IsHoldingObject doesn't crash but answers a stale object handle like −1, "holds any object"
(the NULL test at 0x46DECC). Every other case read dereferences the result at once: `01BB` (+0x14 at 0x47D582),
`01BC`, `0177` (`CWorld::Remove(NULL)` at 0x47CD0B), `0381`, `0382` (+0x1C), `0392` (+0x1C), `0550` (+0x40), `0566`
(writes +0x2F), `0750` (+0x1C), `034E` (+0x14), `0366`, `0723`, `0681`, `0453`, `08E9` (+0x140), `04D9`, `04DA`. On
NULL that is an access violation. Bytes (a scan of the instructions after each `GetAtRef` call in these cases). The
friend's scripts never hold a handle of a mirrored copy (5.11), and persistent objects are never deleted on the
friend's game (5.9), so no new stale handle appears.

## 4. Current state in this build

| What | Host | Friend's game today |
|---|---|---|
| Objects the host's mission creates (all classes of 2.2) | exist | nothing; the host's crates, gates, barrels and boxes are invisible and have no collision |
| A blip on such an object | entity blip | radar-only coordinate blip following the host's object (`client/src/CMirrorBlips.cpp:317`, decision M-D1) |
| A locate sphere drawn at an object (`04E5`/`04E6` with the sphere flag) | drawn | drawn by the mirror's areas (M3) at world coordinates |
| A ped or vehicle attached to an object (`04F4`, `0939`) | attached | the ped or vehicle follows its own sync; the object is missing |
| An object held by a ped (`070A`) | in the hand | nothing; no task sync for holds (`CTaskSync.cpp` covers climb, jump, wander) |
| The other player's shots at an object | — | `PLAYER_BULLET_SHOT` reaches the host with no victim: `CollectShotSnapshot` decides "network entity" from the shooter's type, not the victim's (`client/src/Hooks/PlayerHooks.cpp:105`), so the host replays the shot with victim NULL; objects, glass and buildings are never hit by a remote player's bullets |
| Explosions | local | not sent (`WorldHooks.cpp:56`) |
| `COpCodeSync` with object parameters | — | `eSyncedParamType::OBJECT` is "not in use yet" (`COpCodeSync.h:21`); no object command is replayed |
| Entity sync | — | `CWorld__Add_Hook` networks vehicles and peds only (`WorldHooks.cpp:77`) |
| Persistent objects (`INITIAL` etc.) | changed by its missions | the friend's own copies, as its own new game made them; a door the host's mission opened or deleted stays closed on the friend's game and blocks them (first at #23 during the mission, for good from #42) |
| Pickups | per game | per game: a pickup the host's mission creates (#12's pistol, #17's crate pickups) exists only on the host (W-D6) |
| Trains | exist | no copy (`CNetworkVehicle::CreateVehicle` returns false for `VEHICLE_TRAIN`, `client/src/CNetworkVehicle.cpp:164`); objects attached to carriages have nothing to ride on |

## 5. Architecture

### 5.1 Options

| Option | How | For | Against |
|---|---|---|---|
| A. Opcode replay | give `COpCodeSync` an object parameter type (host handle → id → receiver handle), replay the creating and changing commands of registered missions | small; reuses the replay | only registered scripts, misses joins and restarts, misses everything the engine does (physics, forks, breaks, holds), every command needs its own semantics, double effects (two physics simulations) |
| B. Server entities like vehicles | server ids, a syncer per object, spawn and confirm, every game may create objects | general | heavy (the vehicle protocol needed two review rounds for ordering and ids), and only the host's missions create objects that matter |
| C. Host-authoritative state mirror | the host owns the objects its mission family creates and sends their state; the others keep local copies and apply it; ownership can be handed to another game for an interaction | covers every script and the engine, joins, restarts; identity is the host's own handle; the same pattern as the blips (M2) and the registry | a new module and three packet types |

Recommendation: C, the **mission object mirror** (`CMirrorObjects`), with a narrow ownership hand-over for the
interactions group 2 needs (5.8), and A's object parameter type added only where a port wants to task the other
player's ped with an object (5.8, last paragraph).

### 5.2 Overview

```
host game                                          server                    other games
0107/029B by the family ── OnCommandExecuted ──── MISSION_OBJECTS ─ relay ── copies (type 6), state
   (5.3)                    (creation at once)     (SCRIPT, reliable,  host    attach / holder / flags
end of the script frame ─── state differences ────  host -> others)    only    (5.5, 5.7)
after CGame::Process ────── motion of moving ──── MISSION_OBJECT_MOTION ───── interpolation (5.6)
   (5.4)                    objects               (SYNC, from the owner)
                                                   MISSION_OBJECT_CLAIM <──── the friend's game:
                                                   (EVENT, others -> host)    holds / lifts a copy (5.8)
```

New files (private build, new code in new files, flat in `client/src` like the other mirror modules):
`CMirrorObjects.{h,cpp}`, `CMirrorObjectsPolicy.h` (logic without GTA), `PacketHandlers/mirror_objects.cpp`,
`shared/network/mirror_objects.h` (wire constants), `shared/network/packets/mirror_objects.h`,
`server/src/PacketHandlers/mirror_objects.cpp`, `tests/mirror_objects_test.cpp`. Small edits: `packet_types.h` (three
types appended after `MISSION_MIRROR_HUD`), `CMissionMirror.cpp` (two calls), `Main.cpp` (one per-frame call),
`CMirrorBlips.*` (6.2), `Hooks/PlayerHooks.cpp` (OB-0). No new hook site: the post-command call, the end of the script
frame and the per-frame events exist.

### 5.3 Which objects the mirror owns

- **Mission objects.** In `CMissionMirror::OnCommandExecuted`, after `0107` or `029B` run by a family script on the
  host, the new handle in `ScriptParams[0]` (3.2) is owned, and its creation goes out at once (before any later
  command of the same frame, so a replayed command or a blip that names it arrives after it). Owned objects stay
  mirrored after the mission, for as long as the host's pool slot keeps them (released ones become temporary on the
  host and go when the host's game deletes them, 3.2).
- **Persistent records** (5.9): a compiled table of the persistent globals of 2.5, by global index.
- **A family create that lands in a persistent global** (`$advert_f1 = Object.CreateNoOffset(…)` in #27,
  `$china_town_gate` in #56, `$carterwall` in #101, `$init_portakabin(…)` in #42, …): after each family create the
  host compares the new handle with the table's globals (the command stored it there before the post-command call); a
  match makes it the persistent record's object, not a new mission object, so the friend's own object behind the same
  global is reused and never doubled. Only when the friend has no object there does the mirror make a copy.
- Not owned: objects of MAIN threads, external scripts and missions 0–10 (every game runs its own), objects created
  before this game became host, map objects.

### 5.4 Host capture

Per owned object, the state that matters to the others:

| Field | Source |
|---|---|
| model | `m_nModelIndex` +0x22 (the resolved model, also for the header's used-object ids) |
| area code | +0x2F |
| flags | visible and collision (+0x1C 0x80 / 0x01), static (+0x1C 0x04), frozen (+0x40 0x2004), liftable (+0x140 0x2000), broken (+0x140 0x400), rendered damaged (+0x1C 0x200), scale (`m_fScale`) when not 1 |
| transform | position and rotation (quaternion) of the matrix |
| parent | `m_pAttachedTo` +0xFC with offset +0x100 and rotation (+0x10C, or the quaternion +0x118); the parent as a network vehicle, ped or player id, a mirrored object, or a train carriage (6.4) |
| holder | the ped whose hold task (`GetTaskHold`, entity at +0x08) holds it: network ped id or player id, with the task's offset (+0x0C), bone (+0x18), bone flags (+0x19) and anim (+0x20, +0x24), so the receiver builds the same hold |
| motion | move and turn speed, for extrapolation |
| owner | host, or the player it is handed to (5.8), with an epoch |

At the end of the script frame (`CMissionMirror::OnScriptsProcessed`) the host compares each owned object with what it
last sent and sends a FULL for any field change other than motion. After `CGame::Process` (Main.cpp's per-frame event,
physics done) it samples the transforms of moving owned objects (5.6). An object whose slot no longer resolves (3.1)
is removed. A held or attached object sends no motion: its copy follows its parent locally. The host tracks its
objects whenever it is the host, also with nobody else in the session (a joiner gets them); it sends nothing while
alone, before the first server-time answer (the blips' rule), or for an object owned by another game.

### 5.5 Receiver copies

- Created with `CObject::Create(model, 0)` after the model is streamed in (`CStreaming::RequestModel` and
  `LoadAllRequestedModels`, as `CNetworkVehicle::CreateVehicle` does), type **6** (`OBJECT_MISSION2`): population
  management never deletes it (3.2), `SaveObjectPool` never saves it (3.1), a restart deletes it like any mission
  object, and it is in no cleanup list of the receiver. No `ClearSpaceForMissionEntity` (it could delete the
  receiver's vehicles). A LOD atomic model gets `SetupBigBuilding`, as the create commands do. `CWorld::Add` as usual
  (the `CWorld::Add` hook passes objects through).
- Ordinary objects with the host object's flags: a crate the host has dynamic is dynamic, a frozen gate is frozen, an
  invisible, collision-less, broken or damaged object is the same. So the receiver's player and vehicles collide with
  them as on the host, and a light crate or barrel the friend drives into gives way at once (a frozen copy would stop
  the friend's car like a wall). The host's state stays authoritative: while the host moves the object its motion
  drives the copy (5.6); while both rest, a copy more than 0.3 m or 10° from the host's state for 1 s is put back. The
  same push usually happens on the host too, where the friend's car is a network vehicle, and its motion comes back.
- A break (`broken` set) replays the effect on the copy (`BreakManager_c::Add`, or the glass call for glass) before it
  goes invisible, so the friend sees the crate shatter.
- A pool or model failure leaves the record without a copy and retries every second (the vehicles' lost copies).
- An area code other than the receiver's own keeps the copy invisible by the engine's own rule, as on the host.

### 5.6 Motion and smoothing

- The host samples a moving object (speed above 0.05 m/s, a turn, or a position change of 1 cm or a rotation change of
  0.5° since the last sample) at 10 Hz, 20 Hz above 5 m/s (thrown crates at train speed), and sends a reliable FULL
  with the resting transform once it has been still for 300 ms or became static.
- Objects the host moves without physics (static or frozen ones a script slides, rotates or places, `034E`, `034D`,
  per-frame `01BC`): receivers keep the last two samples, stamped with the host's server time, and show the object 100
  ms behind (the 50 ms packet buffer plus one sample interval), interpolating position and rotation; past the last
  sample they extrapolate with the velocity for up to 250 ms, then hold. The final FULL puts the gate exactly where
  the host's stopped.
- Dynamic objects (falling, thrown, pushed): each sample sets position, rotation, move and turn speed on the copy, and
  the receiver's physics runs in between, as the vehicle copies are driven today; a sample less than 0.1 m from the
  copy only corrects the speeds, so contact with the ground stays smooth. A sample more than 2 m off snaps either
  kind.

### 5.7 Attachments and parents

- An attached object is sent once as "attached to parent P with offset and rotation"; the receiver attaches its copy
  to its own copy of P with `AttachEntityToEntity` (the quaternion form for exact rotation), so it rides along without
  jitter: the crates in the Picador's bed (#16), the load in the Mule (#17), ramps on trucks (#24). Detach is a FULL.
- Parents: a network vehicle (its `CNetworkVehicle` copy), a network ped or player ped, another mirrored object
  (`069A`: the cigar's glow in #3), a train carriage (6.4). Until the parent resolves on the receiver (no network id
  yet, no copy, a train today), the copy falls back to world motion at 20 Hz, as the blips fall back to coordinates.
- A forklift parent places the copy relative to the fork frame (3.3, 3.5), and the receiver's forklift copy has the
  driver's fork angle, so a crate on the forks of the host's forklift sits on the forks of the friend's copy. The
  receiver's engine may drop such a copy on its own (its fork reaches 0, the crate touches a building, 3.3); while the
  host still reports it attached, the receiver attaches it again.
- A held object (holder set) gets the same hold on the receiver: the copy of the holding ped gets a
  `CTaskSimpleHoldEntity` with the copy (constructor 0x6913A0, 3.4) in secondary slot 4, the slot `GetTaskHold` reads
  (`TASK_SECONDARY_PARTIAL_ANIM` in gta-reversed `TaskManager.h`), set with 0x681B60, so Ryder catches and carries the
  crate in #16, throws the box in #17, and the host carries a box in #15 with the carry animation on the friend's
  screen. Fallback when the task can't be given (ped not ready, anim block missing): attach the copy to the ped's
  right hand position.
- Objects resting in a moving vehicle without attachment (the Andromada's cargo in #72) are a later refinement
  (vehicle-relative motion, OB-5); group 2 has none.

### 5.8 Interaction hand-over

The host owns every object by default. Another game takes it over only for an interaction its own engine performs:

| Interaction | Detected on the friend's game | Host side after the grant |
|---|---|---|
| Carry (#15, later #92) | the local player's hold task holds a mirrored copy (`GetTaskHold`, 3.4): the engine's own pick-up of a liftable copy with the enter-vehicle key, or a replayed `Task.PickUpObject` | the host's copy of the friend's player ped gets the hold task with the host's object, so vanilla `Char.IsHoldingObject($NETWORK_PLAYER[i], box)` is true on the host; the object follows the friend |
| Fork carry (#17, also a dumper) | a mirrored copy is attached to a forklift or dumper that this game syncs (it drives it) | the host attaches its object to its copy of that vehicle (same offset); `Object.IsAttached`, `Object.Locate3D` and the truck test of #17 read the host's object as the friend moves it |
| Free motion after either | the object is let go (dropped, fork lowered) and moves | the host applies the friend's motion until the friend releases it |

Rules (`CMirrorObjectsPolicy.h`, unit-tested):

1. The friend's game sends CLAIM (object, reason, transform) and treats the copy as its own meanwhile.
2. The host grants if the object exists, nobody else owns it and no host script command changed it in the last 500 ms;
   it sends OWNER (object, player, epoch + 1) to everyone. Otherwise OWNER (host, epoch + 1) with a FULL; the friend's
   game drops its local interaction (clears the hold task, detaches) and takes the host's state.
3. The owner sends MOTION with its epoch; every game drops motion whose sender and epoch don't match the last OWNER.
4. The owner sends RELEASE (epoch, final state) once the object rests or 3 s after it was let go; the host takes it
   back (OWNER host, epoch + 1).
5. A host script command on a handed-over object (delete, place, attach, freeze, collision, visibility, break, another
   ped's pick-up) revokes it first (rule 2's refusal path), so the mission always wins. The post-command call sees it
   from the command's parameters, which `COpCodeSync`'s parameter hook collects for the commands the mirror asks for, as
   it does for the texts (`ScriptParams` itself is not enough: `070A` collects twice and overwrites it).
6. A player who leaves loses everything: the host's PLAYER_LEFT subscriber drops what the player held at the ped's
   position before `CDepartedPlayerPeds` parks the stand-in (it runs after the lifecycle subscribers,
   `PacketHandlers/system.cpp:69` then `:77`), so a box isn't carried 200 m below ground.
7. When both grab the same object, the first CLAIM the host handles wins; the host's own player always wins over a
   pending claim.

The friend's engine does both interactions by itself on the copies of 5.5: `FindObjectToSteal` takes any liftable
object in the world in front of the player (3.4), so a copy with the host's liftable bit is picked up with the
enter-vehicle key, and the fork's auto-attach (3.5) works on a dynamic copy as on the host's crate. Between the claim
and the grant the friend's game stops applying the host's state to that copy.

Not handed over: pushes and knocks (the host's physics with the friend's network vehicle or ped does them; latency is
acceptable for knock-overs), shots (OB-0 sends the friend's bullets to the host's objects), mission-scripted throws
and catches (an NPC's task on the host, mirrored as a holder).

For ports that want to give the other player's ped a task with an object (the friend catching a crate, a cutscene
hold), OB-3 adds `eSyncedParamType::OBJECT` to `COpCodeSync` (host handle → mirrored id → the receiver's copy) and
`070A`, `070B` to `syncedOpcodes[]` for PED parameters that are network players.

### 5.9 Persistent objects

Identity is the global variable that holds the object (2.5): both games run the same `main.scm`, so global 2666 is the
Chinatown gate on both. The client has a compiled table of the story's persistent globals (2.5) with a flag: *story*
(its state is the host's for good: doors, gates, cabins, the Carter wall) or *mission-only* (followed only while the
host's mission changes it: the magnet crane of #64). The policy test compares the table with `INITIAL.txt`,
`INITIL2.txt`, `MAIN.txt` and `CustomVariables.ini`, as the special-character test compares its list with the scripts.

- Host: every 250 ms it reads each table global's handle and object (the same capture as 5.4, plus "absent" when the
  global's handle no longer resolves) and sends a persistent FULL on change; moving persistent objects (a sliding
  door) send motion like mission objects.
- Friend: its own object behind the same global takes the host's state. **Absent means hidden** (invisible, no
  collision), never deleted, so no script on the friend's game meets a stale handle (3.7). A persistent record whose
  host object comes back (rebuilt with `DontRemove`) shows the friend's own object again at the host's transform; the
  friend's global still names it. These are the friend's own objects (type 2), so the friend's own save keeps the
  mirrored state, which is harmless (the friend's saves are not used).
- Re-apply: the friend keeps the host's records and applies them again whenever its game becomes ready after its own
  new game (its `INITIAL` rebuilt everything in the new-game state), the registry's gate (authenticated, a host known,
  `IsLocalGameReady`, no mission of its own).
- Boundary with the world-state registry: this module owns script-created persistent objects; the registry's WS-6
  keeps map objects (building swaps `03B6`, hidden map objects `0363`). The registry could carry the rest state as an
  entry instead, but the same objects move during missions (5.6), and two owners of one object would fight; one module
  is simpler (O-D6).
- Not needed for group 2 (2.4); needed from #23 (group 4).

### 5.10 Lifetime

| Event | Host | Others |
|---|---|---|
| The family creates an object | FULL at once | create the copy |
| A state change | FULL at the end of the script frame | apply |
| The mission ends | nothing special: the objects become temporary on the host (3.2) and stay mirrored; `01C7` ones stay mission objects | nothing: copies follow the host until it deletes them; a released crate the host's game deletes by distance disappears for the friend too |
| The host deletes an object | REMOVED | delete the copy (a hold task on it ends by its reference, 3.4) |
| A player joins | the whole set with `bReset` to that player alone (`SendFromHostToPlayer`): mission objects, owners, persistent records | build everything; parents that arrive later resolve then (5.7) |
| The host's game restarts (new game or load) | its objects are gone (3.2): an empty set with `bReset` to everyone, then 2 s into the new game every persistent record again | delete all copies; apply persistent records (rollback after an older load, "host wins") |
| The host leaves or the host changes | — | delete every mission object copy and drop handed-over state; persistent objects stay as last applied (no rollback, the registry's rule) |
| A receiver restarts | — | its `ClearForRestart` deletes the copies; it keeps the host's set and rebuilds the copies and persistent states once its game is ready |
| A receiver alt-tabs | — | reliable packets wait in `CNetworkKeepAlive`; motion is dropped and the next sample or FULL corrects |
| The friend holds a mirrored object and the host deletes it | REMOVED | the copy goes; the friend's carry ends |

### 5.11 Stale handles in the other player's scripts

- The other games' scripts never get a handle of a copy: no script creates them, and no story mission runs there.
- Two engine paths could find a copy: "closest object of type" queries (`0363`, `0716`, `07F0`, `088D`, `0985`) and
  `GetClosestStealableObject` (`0866`, used by the burglary side mission `BURGJB`). A friend running their own
  burglary next to the host's #15 boxes could pick one up through their script; acceptable, and the hand-over rules
  handle it.
- Persistent objects are hidden, never deleted (5.9).

## 6. Interfaces

### 6.1 Vehicle sync protocol

- Parents are network vehicles by id; a copy attaches to `CNetworkVehicle::m_pVehicle` after the pool-generation check
  (`IsVehiclePointerValid` doesn't check generations). A vehicle that is lost and recreated (`copy-create-failed`) or
  deleted makes its attached copies fall back to world motion until the next FULL.
- A forklift or dumper the friend drives is synced by the friend's game (the driver's game syncs, vehicle protocol),
  so its forks and the crate on them are the friend's (5.8). The host keeps the crate's mission meaning (count,
  delete).
- The truck bed: script attachments (#16, #17) are attachments. Physics cargo in a moving vehicle (#72) waits for
  OB-5.
- The vehicle protocol's lesson on ordering applies: everything about an object comes from the host on one reliable
  channel, so no reordering between creation, state and removal; motion is unreliable and stamped; the identity is the
  host's pool handle with its generation byte, so a stale motion sample can't land on a new object in the same slot.

### 6.2 Mirror layer

- Blips (M2): `MISSION_MIRROR_BLIPS` gains an entity kind OBJECT (the mirrored id); a receiver puts an object blip
  (`SetEntityBlip` with `BLIP_OBJECT`) on its copy instead of the radar-only coordinate blip that follows the host
  (`CMirrorBlips.cpp:317`), and the 4 Hz position packets for it stop. A copy that isn't there yet keeps the fallback.
- Areas (M3): unchanged; spheres at objects are world positions already.
- Family (M1): the same rule decides which creates are owned (`CMissionMirror::IsFamilyScript`).
- Order in `OnScriptsProcessed`: object state before blips, so a blip finds its object.

### 6.3 World-state registry

- WS-6 (map objects): building swaps (`03B6`) and hidden map objects (`0363`); unchanged.
- Persistent script objects: this design (5.9), not a registry entry (O-D6).
- Mission settings (W12) and the rest: unaffected.

### 6.4 Train design

The train design (written in parallel) decides whether trains exist on the other games. Objects need, from the train
side:

1. A network identity for every carriage of a mission train (the caboose of #16 from `Train.GetCaboose`, the carriages
   of #74 from `Train.GetCarriage`), known to all games, so a parent can be "carriage C".
2. On the other games, a copy of each carriage whose position follows the host's within about 0.5 m and moves smoothly
   (not in 10 Hz steps), and a lookup carriage id → local `CVehicle*`. Objects attached to it then ride along through
   the engine's attachment (5.7); so does a player attached to it (#16's host).
3. The carriage's velocity on the receiver (thrown crates inherit it; the crate's own velocity is sent anyway).
4. Order: a train deleted by `World.DeleteMissionTrains` may go before or after the objects attached to it; the object
   mirror copes (fallback to world motion until its own REMOVED).

Until the train side exists, crates on a carriage fall back to world motion at 20 Hz, which shows them moving with the
train with some stepping, over an empty track.

### 6.5 Porting guide

What a port still does by hand once the mirror exists (for `docs/mission-porting.md`):

- Nothing for the host's objects: no copies, no relays, no waits for object ids.
- Progress conditions for the other player, with vanilla commands on `$NETWORK_PLAYER[i]` (they see the hand-over on
  the host): `Char.IsHoldingObject($NETWORK_PLAYER[i], box)` (#15), "any player in any of the mission's forklifts"
  (#17); the departed-player rules apply to these as to every use.
- Keys: `Pad.IsButtonPressed` reads only the host's pad; a friend-side action triggered by a key (#15's Triangle at
  the van) becomes a position condition (O-D4).
- Extra vehicles and seats for the other player (a second forklift in #17, the Picador's passenger seat in #16, the
  Mule's front seat in #17).
- Never delete or recreate a persistent global for the other player; never create an object "for the friend" that
  copies one of the host's.
- Report a mission whose objects need something the mirror doesn't do (an object that must react to the friend's own
  physics beyond 5.8).

## 7. Wire format and bandwidth

Three packet types appended after `MISSION_MIRROR_HUD`; one version bump with the package; the friend needs it.

| Packet | Channel | From → to (server) | Fields |
|---|---|---|---|
| `MISSION_OBJECTS` | SCRIPT (reliable, ordered with `OPCODE_SYNC` and the mirror) | host → others (`SendFromHostToOthers`), or one player (`SendFromHostToPlayer`); sender filled in | `bReset`; count (0..32); per entry: id (mission: the host's pool handle, 17 bits; persistent: global index, 16 bits; 1 bit kind), op (FULL, REMOVED, OWNER); FULL: model u16, area u8, flags u16, scale (when ≠ 1), position (`WorldPositionCompressed`, 9 B), rotation (smallest-three quaternion, 4 B), velocity (`MoveSpeedCompressed`), parent (kind, id, offset 3 × 16-bit fixed at 1 mm, rotation 4 B), holder (kind, id); OWNER: player id or host, epoch u8 |
| `MISSION_OBJECT_MOTION` | SYNC (unreliable, stamped) | the owner → everyone else; sender filled in | count (0..24); per entry: id, epoch, position, rotation, velocity, turn speed |
| `MISSION_OBJECT_CLAIM` | EVENT (reliable) | a non-host game → the host only | op (CLAIM with reason HOLD or VEHICLE, RELEASE), id, epoch, transform, velocity |

The server keeps no state: the host's OWNER entries tell every game whose motion to accept. It relays
`MISSION_OBJECTS` only from the host (as the other mirror packets), `MISSION_OBJECT_MOTION` from anyone (receivers
filter), `MISSION_OBJECT_CLAIM` from a non-host to the host.

| Traffic | Size | Rate |
|---|---|---|
| Creation or state change (FULL) | 30–45 B | per change; bursts at phase changes (#17's 17 crates and 11 doors: about 1.2 KB once) |
| Removal | 4 B | per deletion |
| Motion | about 24 B per object | 10 Hz while moving (20 Hz fast), none at rest, none while attached or held |
| Join snapshot | 40 B per object + 30 B per persistent record | once: about 2 KB for #17, about 1.5 KB of persistent records |

Worst cases in the story: #16 with three crates in flight at 20 Hz, 1.5 KB/s; #17's escape with five boxes falling and
two gates sliding, 1.7 KB/s; #43's cones knocked over by the school car, up to 5 KB/s for a second (solo school). On
average well under 1 KB/s, small next to the player, vehicle and ped sync, and fine for the Steam tunnel.

## 8. Risks and open questions

1. **Hold tasks on copies**: giving a network ped or a remote player's ped a `CTaskSimpleHoldEntity` (5.7) is untested
   in game; the constructor, its fields and the slot are read from 1.0 (3.4), and the host sends the fields of its own
   task (5.4). A remote player's ped replays keys every frame; whether its own task logic drops a hold it didn't start
   (a weapon switch, a fall, entering a car) is open. Fallback: attach to the hand.
2. **The friend's own pick-up** goes through the engine's script-command event (3.4) on the friend's game: in game the
   key and the facing must feel the same as on the host; the claim and grant (5.8) add about one round trip before the
   host's mission counts the box.
3. **Copies that move locally** (5.5) may slide or fall differently from the host's object before the host's
   correction or the friend's claim; the corrections snap them. If the forklift feels wrong, the fallback is host-side
   forks (the host's copy of the friend's forklift lifts the host's crate; about 150 ms late on the friend's screen).
   The receiver's engine can also drop a crate from a forklift copy by itself (5.7); it is attached again.
4. **Trains** (6.4): #16's caboose cargo and #74 depend on the train design.
5. **Explosions** aren't mirrored: #17's exploding boxes vanish without a blast on the friend's screen; #14's barrels
   too (O-D7, EX-1 for the script ones). A replayed explosion damages the receiver's copies of network vehicles and
   peds locally; their syncers' updates win, as for any local damage.
6. **Persistent end states** marked "to confirm" in 2.5 (#60, #73, #100) are read when those missions come up.
7. **Pool pressure**: 350 object slots on each game, about 70 persistent, plus the friend's own world; a copy that
   can't be created waits (5.5).
8. **Ropes and winches** (#42, #64, #83): an object carried by a crane magnet or the Leviathan's winch is an
   attachment the engine makes through `CRopes`; mirrored as seen (world motion) until a port needs better.
9. **Not mirrored**: object animations (`075A`: fences, basketballs, gym equipment), sounds and particles on objects,
   the searchlights' light cones (the objects move, the light is the host's `06CA`).
10. **OB-0 changes shot behaviour**: with the victim's type deciding, a remote player's bullets hit objects, glass and
    building surfaces on every game (as the receiver's own ray finds them). A shot that the receiver's ray resolves to
    another object than on the shooter's screen (latency) hits that one.
11. **Released temporary objects** disappear on the host when the host's player is more than 54.5 m away (3.2), also
    when the friend stands next to them.
12. **The friend's saves**: copies are type 6 and never saved (3.1), so the friend's own save gets no orphan crates.
13. Nothing here is tested in game.

## 9. In-game test plan (two players)

**Not tested in game.** For the user (host) and the friend, with the package that has OB-0 to OB-3 and the group 2
ports; both start with the `CoopAndreas (diagnostics)` shortcut (`--coopdiag` writes `CoopAndreas_objects.log`).

1. Both install the same package; the version in the bottom-left corner matches. The host loads the co-op save before
   #11; the friend connects.
2. #13: at the rail yard the friend sees the gantry; the enemy knocked off the train hangs on it, not in the air.
3. #14: the friend shoots the storm drain cover open (the host's bike passes) and a barrel; both break on both
   screens.
4. #15: both enter the house; the friend sees six boxes with blips on them. The host picks one up: the friend sees it
   in the host's hands. The friend presses enter-vehicle at another box and carries it out to the van; it counts when
   the friend reaches the van's back; it disappears on both screens. A box dropped in the pool is lost for both.
5. #15, leaving: the friend quits while carrying a box: on the host the box lies where the friend was; the mission
   goes on; the friend reconnects and sees the remaining boxes.
6. #16: the friend fights at the train yard, then sits in the Picador's passenger seat. The friend sees the train and
   its cargo, each crate the host throws, Ryder catching it, the bed filling up, and missed crates breaking.
7. #17: the base gates and warehouse doors slide open on both screens; the friend shoots a keypad and the doors behind
   it open. The friend takes the second forklift, lifts a crate and puts it into the Mule's back: the counter drops by
   one, a box appears in the Mule on both screens. The host loads one too.
8. #17, join: after three crates the friend leaves and reconnects: gates open, crates where they lie, three boxes in
   the Mule.
9. #17, escape: the friend rides in the Mule's front seat; Ryder's boxes fall out behind the truck on the friend's
   screen and the Patriots run into them.
10. The host quits during #17: every mission object disappears on the friend's screen, no crash.
11. Alt-tab: the friend alt-tabs for 30 s during #17; back in the game the gates, crates and load are current.
12. Group 4, when it comes (OB-4): #23's door is one door on the friend's screen; after #42, the cabins and later the
    Chinatown gate (#56) are as on the host, also after the friend restarts their game.

## 10. Work packages

For `gtasa-coder-high` unless noted; each in its own worktree from the then-current `main`. Gates: `build.ps1`, the
policy test, `tools/audit_hooks.py` (no new site expected).

| Package | Scope | Size | Needed by |
|---|---|---|---|
| OB-0 Remote shots on objects | `CollectShotSnapshot` decides from the victim's type (`PlayerHooks.cpp:105`); objects, glass and buildings then go the receiver's own ray (`PacketHandlers/players.cpp:190`); a ped victim without a network id goes as no victim instead of id 0 | 0.5 job (small) | #14, #17 (and #7's bottles) |
| OB-1 Object mirror core | 5.3–5.7 and 5.10–5.11: owned creates, capture, FULL/REMOVED/reset, motion, receiver copies (type 6, the host's flags, interpolation and corrections), attachments to vehicles, peds, players and objects, holders mirrored on ped copies, breaks, join snapshot, lifecycle, `--coopdiag` log and summary line, `MISSION_OBJECTS` and `MISSION_OBJECT_MOTION`, server relays, policy test | 2 jobs (host side and wire; receiver side), or one long one | group 2 |
| OB-2 Object blips | entity kind OBJECT in `MISSION_MIRROR_BLIPS`, receiver entity blips on copies | 0.5 job (can ride with OB-1) | #15 |
| OB-3 Hand-over | 5.8: claims, grants, epochs, release, revoke, departed players, carry and fork carry, the host's hold task on its copy of the friend's ped, `MISSION_OBJECT_CLAIM`; `COpCodeSync` object parameters for `070A`/`070B` | 1–2 jobs | #15, #17 |
| OB-4 Persistent objects | 5.9: the table and its policy test, host capture, hidden-not-deleted, re-apply after the friend's restart | 1 job | #23 (group 4) |
| OB-5 Vehicle-relative cargo | physics objects resting in a moving vehicle | 0.5 job | #72 |
| EX-1 Script explosions (O-D7) | `Fx.AddExplosion` (`020C`), `AddExplosionNoSound` (`0565`) and `AddExplosionVariableShake` (`0948`) of the host's mission family go through the mirror's opcode path like its texts (`syncedOpcodes[]` and `CMissionMirror::WantsCommand`); no entity parameters | 0.5 job | #14, #17 (if O-D7 (b)) |
| Ports #11–#14 (`gtasa-coder`) | by the porting guide; the mirror covers their objects | 1–2 jobs | group 2 |
| Port #15 (`gtasa-coder`) | the friend's boxes count at the van (O-D4), the friend's noise (O-D4) | 1 job | group 2 |
| Port #16 (`gtasa-coder`) | the friend's seat and role (O-D2); the train side for the cargo | 1 job, after the train design | group 2 |
| Port #17 (`gtasa-coder`) | second forklift (O-D3), the friend's crates count, escape seat | 1 job | group 2 |

Order for group 2: OB-0 and OB-1 (with OB-2) first, then OB-3 (and EX-1 if chosen); then the ports in story order (#11
to #17), #16 also after the train package. OB-4 before group 4, OB-5 before the batch with #72.

What each group 2 mission needs:

| Mission | Packages |
|---|---|
| #11–#13 | OB-1 (static props) |
| #14 | OB-1, OB-0 (the cover and barrels), EX-1 if chosen |
| #15 | OB-1 (boxes, the host's carry on the friend's screen), OB-2 (box blips), OB-3 (the friend carries) |
| #16 | OB-1 (thrown crates, Ryder's catch as a mirrored hold, the bed), the train side (the caboose cargo) |
| #17 | OB-0 (keypads), OB-1 (gates, doors, crates, load, Ryder's throws), OB-3 (the friend's forklift), EX-1 if chosen |

## 11. Decisions

For the architect to take to the user. Each says what the players get.

| # | Decision | Options and what they mean in game | Recommendation |
|---|---|---|---|
| O-D1 | How far the friend's hands reach | (a) **look only**: the friend sees every crate, gate and box move as on the host but can't carry or lift anything; (b) **carry and forklift**: the friend can pick up and carry boxes (#15) and lift crates with a forklift they drive (#17); everything else (pushing, knocking over) happens through the host's game a moment later; (c) everything physical shared: every bump moves the object on both screens at once (much larger, more bugs) | (b) |
| O-D2 | #16 Catalyst, the friend's part | (a) the friend fights in the train-yard shootout, then rides in the pickup's passenger seat and watches the host throw and Ryder catch (nobody attacks during the throws); (b) the friend also rides on the train and throws crates: needs two small co-op commands (the friend's button and camera direction, read on the host), a friend-only power bar, and the train side to put the friend on the caboose; (c) the friend catches crates in the pickup's bed in Ryder's place: a replayed catch task, but the catch stays automatic, so the friend still only watches | (a) for group 2; (b) as a later improvement if the user wants the throws shared |
| O-D3 | #17 Robbing Uncle Sam, loading | (a) a second forklift for the friend: both load crates into the truck, six in total; (b) one forklift: the host loads, the friend guards the yard (there is little left to guard by then) | (a) |
| O-D4 | #15 Home Invasion, the friend's boxes and noise | delivery: (a) a box counts when the friend reaches the van's back holding it; (b) the friend presses a key there (needs new client code). Noise: (c) only the host's noise fills the meter (the friend can be loud); (d) the friend's noise counts too (the colonel wakes up for either) | (a) and (d) |
| O-D5 | Mission objects after a mission ends | (a) they stay on the friend's screen as long as on the host's (crates left lying around vanish when the host walks away); (b) they vanish on the friend's screen when the mission ends | (a) |
| O-D6 | Doors, gates and cabins of the story (persistent objects) | (a) the friend's world follows the host's for good: after #42 the cabins, after #56 the Chinatown gate are as on the host, also after the friend restarts; (b) only while the host's mission runs: afterwards the friend's world goes back to its own (gates come back, doors block); (c) in the world-state registry instead of this module (same effect as (a), two owners of the same objects) | (a), in this module, before group 4 |
| O-D7 | Explosions | (a) as now: an exploding box or barrel just disappears on the friend's screen (no blast, no damage there); (b) a small package mirrors the explosions of the host's mission scripts (`Fx.AddExplosion` and its variants, used by #14 and #17): the friend sees and hears the blast, and it damages what is near on the friend's game; engine explosions (shot barrels, cars) stay as now | (b) in group 2, if small enough |
| O-D8 | Pickups from crates (#17 gives weapons, armour, health from shot crates) | (a) host only (W-D6): the friend sees the crate break but finds no pickup; (b) mirrored pickups (a new design) | (a) |
| O-D9 | The friend's shots on objects (OB-0) | (a) yes: the friend can shoot open keypads and covers, and glass and bottles break on both screens; (b) no | (a) |
| O-D10 | Two players grabbing the same box | the host's game decides: the first grab it sees wins, the host's own player wins over a pending grab | as stated |
| O-D11 | Version of the package | the architect, by the scheme in CLAUDE.md (new packet types: a bump) | — |
