# Trains for two players (design)

Status: designed 2026-10-17 on main `f23b23c`, not implemented. For the architect to review and take to the user; the
decisions are in section 11. Group 2 (#11–#17) needs packages T1 and T2 before #13 Wrong Side of the Tracks and #16
Catalyst can be ported (section 10).

The model stays as it is: missions run only on the host, the other players see their reflection. Today the other players
never see a train of the host: their game has no copy of it, a blip on it is a dot that follows the host's position, and
anyone riding on or in a train is a ped gliding through the air on the other screen. This design makes the host's trains
(mission and random) appear on every game, in the same place and moving with it, as real trains the other players can
ride on, ride in and drive.

Sources: the client and server code in this checkout, the mission sources under `CoopAndreas/scm/scripts`, plugin-sdk,
gta-reversed, `dumpbin /DISASM` of `C:\Games\GTASA_Coop\gta_sa.exe` (1.0 US) and values read from it with
`tools/pe_peek.py` and a small reader (scratch, `%TEMP%\gtasa-agent\trains-design\`), and three ReVa decompiles (marked
"ReVa"; the server was unavailable for most of the session). Most engine facts below were read from the 1.0 bytes, and
gta-reversed is cited only where its code agrees with them. "bytes" in a source column means read from the 1.0 exe.

Words used here: the *host* runs the story; the *friend* is the other player (with two players, one game). A *train* is
a chain of *carriages* (`CTrain` objects linked by `m_pPrevCarriage`/`m_pNextCarriage`); the *engine* is the first
carriage, the *caboose* the last. *Rail distance* is a carriage's position along its *track* (metres from the track's
start). A *copy* is the friend's local train that mirrors one of the host's.

## 1. Goals and non-goals

Goals:

- Every train on the host's game, mission or random, exists on the other games as a train of the same models on the same
  track, at the same rail distance (with the lag every mirrored entity has), moving smoothly, stopping at the same
  stations, removed when the host's goes.
- A train is a network vehicle like any other for everything that names a vehicle: blips on it are entity blips, peds
  and players sit in its carriages through the existing seat sync, shots at it hit it, replayed script commands with a
  train handle name the right carriage.
- The friend can stand or be attached on top of a moving train (#16, #74), ride inside the passenger carriage, and drive
  a locomotive (free roam, the freight side mission), and the host sees it.
- Peds and players attached to a vehicle (the Vagos on the train roof in #13, CJ on the caboose and Ryder in the
  Picador's bed in #16) are attached on every game.
- One authority: the host's game simulates every synced train. No train is simulated twice.
- No script change is needed for the mirror. Ports only make gameplay decisions (the friend's bike in #13, the friend's
  seat in #16). No change to MAIN, globals or saves.

Non-goals:

- Objects on trains (the crates of #16 and #74, the probe object of the quarry mission): the mission object design,
  written in parallel, owns objects. This design defines the train side of that interface (6.4).
- Trains created by the friend's own side missions (the quarry mission run on the friend's game): they stay local for
  now (decision T-D6).
- Lag compensation: a copy lags the host by the same one-way latency plus 50 ms buffer as every other mirrored vehicle
  and ped (5.4).
- Rewriting the vehicle sync protocol. Carriages use it for their identity, with a few rules for train carriages (6.1).

## 2. Inventory

Method: every script under `CoopAndreas/scm/scripts` (all files `scm/main.txt` includes) scanned for the train commands,
the variables that hold train or carriage handles and every line using them, and the train models 449, 537, 538, 569,
570 and 590 (scratch `train_inventory.py`). Opcode names are the mode's (`CoopAndreas/sdk/Sanny Builder 4/data/
sa_sbl_coopandreas/sa_coop.json`); the script sources use the class syntax in brackets.

### 2.1 Commands

| Opcode | Command (class syntax) | 1.0 US case: what it does | Source |
|---|---|---|---|
| `06D7` | switch_random_trains (`Game.SwitchRandomTrains`) | 0x497F51: `CTrain::DisableRandomTrains(!state)` 0x6F5DB0 | bytes (jump table 0x498F10, slot 51) |
| `06D8` | create_mission_train (`Train.Create`) | 0x497F85: `CTrain::CreateMissionTrain(pos, direction, type, &first, NULL, -1, -1, true)` 0x6F7550, returns the engine's vehicle handle | bytes (slot 52; call 0x497FE4) |
| `06D9` | delete_mission_trains (`World.DeleteMissionTrains`) | 0x498001: `RemoveMissionTrains` 0x6F6A20: every carriage with the mission flag, except the local player's vehicle | bytes |
| `06DA` | mark_mission_trains_as_no_longer_needed | 0x49800D: `ReleaseMissionTrains` 0x6F6B60: clears the mission flag of every carriage except the local player's vehicle | bytes |
| `06DB` | delete_all_trains (`World.DeleteAllTrains`) | 0x498019: `RemoveAllTrains` 0x6F6AA0: every train with no carriage that is the local player's vehicle | bytes |
| `06DC` | set_train_speed (`Train.SetSpeed`) | 0x498025: `SetTrainSpeed` 0x6F5E20: `m_fTrainSpeed = speed / 50`, negated for a counter-clockwise train | bytes; gta-reversed |
| `06DD` | set_train_cruise_speed | 0x49805A: `SetTrainCruiseSpeed` 0x6F5E50: autopilot cruise speed (byte +0x3D0) | bytes; gta-reversed |
| `06DE` | get_train_caboose | 0x498090: `FindCaboose` 0x6F5E70 | bytes |
| `078A` | get_train_carriage | 0x46BB68: `FindCarriage` 0x6F5EB0, -1 when missing | bytes |
| `07BD` | delete_mission_train (`Train.Delete`) | 0x46C615: `RemoveOneMissionTrain` 0x6F5DC0: this carriage and every one behind it | bytes |
| `07BE` | mark_mission_train_as_no_longer_needed | 0x46C647: `ReleaseOneMissionTrain` 0x6F5DF0 | bytes |
| `07C7` | set_mission_train_coordinates (`Train.SetCoordinates`) | 0x46C928: `SetNewTrainPosition` 0x6F7140: sets the position, then `FindPositionOnTrackFromCoors` | bytes |
| `0981` | has_train_derailed | 0x478952: bit 0 of byte +0x5B9 (`bNotOnARailRoad`) | bytes |
| `09AE` | is_char_in_any_train | 0x4793B0: in a vehicle whose type +0x590 is 6 | bytes |
| `09CF` | set_train_forced_to_slow_down | 0x47AA49: bit 1 of byte +0x5B9 | bytes |
| `09E3` | find_train_direction | 0x47AFD2: bit 6 of byte +0x5B8 (clockwise) | bytes |
| `0A06` | is_next_station_allowed | 0x47B698: `IsNextStationAllowed` 0x6F7260 | bytes |
| `0A07` | skip_to_next_allowed_station | 0x47B6C0: `SkipToNextAllowedStation` 0x6F72F0 (call 0x47B6DC) | bytes |
| `0464` | attach_char_to_car (`Char.AttachToCar`) | 0x48A7B4: `CPed::AttachPedToEntity` 0x5E7CB0 (call 0x48A84D) | bytes |
| `0465` | detach_char_from_car | 0x48A861: `CPed::DettachPedFromEntity` 0x5E7EC0 (call 0x48A894) | bytes |
| `0681` | attach_object_to_car (`Object.AttachToCar`) | 0x495425: `CPhysical::AttachEntityToEntity` 0x54D570 (call 0x4954D6) | bytes |
| `0683` | attach_car_to_car (`Car.AttachToCar`) | 0x49557B | bytes (case only) |

A train handle is the engine's ordinary vehicle handle, so the car commands (`Car.LockDoors`, `Car.SetProofs`,
`Car.GetSpeed`, `Car.Locate2D`, `Car.IsStopped`, `Car.Explode`, `Car.StartPlayback`, `Car.FreezePosition`,
`Blip.AddForCar`, `Task.EnterCarAsPassenger`, `Char.CreateInsideCar`) work on carriages too, and the missions use them.
`09F3` and `086F` have no class name and no use.

### 2.2 Story missions and side missions

| # | Mission | Script | What it does with trains | The friend must see | The friend must be able to |
|---|---|---|---|---|---|
| 1 | In the Beginning | `INTRO` | the original intro's train at Grove Street (lines 330–636), unreachable after the skip at line 122 | – | – |
| 11 | OG Loc | `TWAR7` | random trains off and every train deleted at the start (163–164), on again at the end (3417): the chase of Freddy runs along the tracks | no train on the tracks during the chase | chase in their own vehicle |
| 13 | Wrong Side of the Tracks | `SMOKE3` | random trains off and every train deleted; a Brown Streak (`Train.Create(1725.55, -1953.28, 12.5, 11, 0)`: engine + 2 passenger carriages) at Unity Station; proofs and locked doors on all three carriages; four Vagos created at the station and attached to the engine's roof (`Char.AttachToCar(34@..37@, 40@, 0, 5/2/-3/-6.1, 3.0, 0, 360, Mp5)`), with blips; repositioned with `Train.SetCoordinates` for the chase; cruise speed set from CJ's distance (10 to 32.5); two oncoming Brown Streaks created and deleted (`41@`, direction 1); a Glendale parked at (2200, -1732) that the train smashes (`HasBeenDamagedByCar(57@, 40@)` then `Explode`); the train reaching (2790.5, 223.6) with Vagos left plays a cutscene with the camera on the train and fails the mission; `SetForcedToSlowDown` off, on again at cleanup | the train in step, the Vagos on top and their fire, the oncoming trains, the car being hit, the escape | follow on their own bike and shoot the Vagos (hits go through the existing shot sync); not board the locked carriages |
| 16 | Catalyst | `RYDER3` | random trains off; a freight engine + flatbed (type 10, direction 1) at the yard (2278, -1145), locked, frozen; an ammo-train object and three gun crates attached to the flatbed (the caboose); after the yard fight the train runs on car recording 490 and the Picador on recording 486 with Ryder attached in its bed; CJ is attached to the caboose (`Char.AttachToCar($scplayer, 163@, 0, -8, 0.9, 0, 360, Unarmed)`, proofs, no collision) and throws new crate objects whose velocity adds the caboose's speed; Ryder catches crates within 1 m of a point in front of him; 10 crates in 90 s; then the train is deleted (`DeleteMissionTrains`, `Car.Delete(129@)` on the engine alone); a second train (`183@`) only in R*'s debug-key block (keyboard keys, unreachable in play) | the train moving in step, CJ on the caboose, the crates on it and in the air, Ryder catching | ride in the Picador or follow it; optionally stand on the train (T3, decision T-D5) |
| 27 | Reuniting The Families | `DRUGS4` | random trains off, every train deleted; a lone Brown Streak engine (type 15) standing at (2285, -1186), proofs, set to 10 when Sweet's car reaches (2262, -1142); deleted later | the train set piece | drive past it |
| 40 | Wear Flowers In Your Hair | `GARAG1` | random trains off, then on | – | – |
| 51 | Outrider | `SYN3` | random trains off, then on | – | – |
| 52 | Snail Trail | `SCRASH2` | a Brown Streak (type 11) at Cranberry Station; the reporter boards passenger carriage 1 (`Task.EnterCarAsPassenger(78@, 82@)`); a blip on the train; speed and cruise changes, stops (`Locate2D`, `IsStopped`); oncoming freight or streak trains (random types 10–13, direction 1, up to three at a time) created at five points along the route, each after a 100 m `ClearArea`; the train put at Market Station with `SetCoordinates`, where the reporter is created again inside carriage 2 (0129) and gets out; random trains off, on at the end | the train with its blip, the reporter boarding and leaving | follow it in their own vehicle |
| 54 | Pier 69 | `SYN5` | random trains off | – | – |
| 56 | Mountain Cloud Boys | `WUZI1` | random trains on at the end | – | – |
| 63 | Test Drive | `STEAL2` | random trains off; a San Fierro tram (type 9) driven by car recordings 77, 78, 80, 83 (paused and resumed) | the tram set piece | avoid it |
| 64 | Customs Fast Track | `STEAL4` | random trains off, then on | – | – |
| 74 | Green Goo | `DES10` | random trains off; a freight train (type 13: engine + 5 flatbeds) with 12 guards attached on the flatbeds (`Char.AttachToCar`), crates attached (`Object.AttachToCar`, some detached and attached again), a car attached to a flatbed (`Car.AttachToCar(103@, 62@)`), cruise 5 then 10; CJ lands on it with the jetpack | the train, guards, crates, the car on it | jetpack onto it and fight on it (T3) |
| 94 | Vertical Bird | `MANSIO2` | random trains on | – | – |
| 103 | End Of The Line (3) | `FINALEC` | random trains off, every train deleted; a Brown Streak engine (type 15) at (2135, -1953), speed 30, cruise 12.5, proofs, released at (2227, -1604) | the train during the chase | avoid it |
| side | Freight Train (mission 126) | `FREIGHT` | started by `R3` when the player sits in a freight or streak engine and the cities passed are above 1 (after #61); deletes every other train, random trains off; the player drives between stations, fails on `HasDerailed` | the train | drive it (T5) |
| side | Quarry (118) | `QUARRY` | one level: a Brown Streak (type 11) with an invisible box attached to its front as a probe; barrels on the track explode the train | the train | help with their own vehicle |
| side | Taxi (121) | `TAXIODD` | random trains on (2953) | – | – |

No other script creates or deletes trains. Blips on trains: `SCRASH2` (`Blip.AddForCar(81@)`) and `QUARRY`
(`Blip.AddForCar(90@)`); `SMOKE3` blips the Vagos on the roof and `SCRASH2` the reporter in the carriage. `DES10` gives
CJ the jetpack (`Task.Jetpack`, line 142).

### 2.3 MAIN threads and external scripts (every game runs them)

| Script | Kind | What it does |
|---|---|---|
| `TRAINSL` | MAIN thread | starts external script 33 `TRAINS` while the player sits in a passenger carriage (570) |
| `TRAINS` | external 33 | for a passenger in a Brown Streak carriage: prints `TRAINS` once it moves; on Cross, fades and calls `SkipToNextAllowedStation` (0A07), which moves the train to the next station of an open city and passes game time (3.3) |
| `R3` | MAIN thread | offers the freight side mission while the player sits in a freight or streak engine (537/538), the cities passed are above 1, the train hasn't derailed and no mission runs; launches mission 126 |
| `HJ` | MAIN thread | the unique-jump thread skips while the player is in a train (09AE) |

### 2.4 What group 2 needs first

For #11 the friend must have no train of its own on the tracks (today it can: 4.1). #13 needs the host's mission trains
on the friend's game with the Vagos attached to the roof and the carriages locked. #16 needs the train in step with the
host's recording-driven one, CJ attached to the caboose, Ryder attached to the Picador, and the object design's crates.
Group 2 has no train a player rides inside or drives.

## 3. Engine facts on 1.0 US

### 3.1 Tracks, configurations, stations

- Four tracks, read by `CTrain::InitTrains` 0x6F7440 (called from `CGame::ReInitGameObjectVariables` 0x53BE9C,
  `CGame::InitialiseWhenRestarting` 0x53C717 and `CFileLoader::LoadLevel` 0x5B9319) through `ReadAndInterpretTrackFile`
  0x6F55D0: track 0 `data\paths\tracks.dat` (926 nodes, an 18.1 km loop through all three cities, the main line), track
  1 `tracks3.dat` (300 nodes, 5.2 km, the San Fierro tram loop), track 2 `tracks2.dat` (67 nodes, 1 km), track 3
  `tracks4.dat` (656 nodes, 13.6 km). File names at 0x8724DC, 0x8724C4, 0x8724AC, 0x872494 (bytes); node counts and
  lengths from the files in the game copy.
- Per track: node array `pTrackNodes` 0xC38024, node count `NumTrackNodes` 0xC38014, total length 0xC37FEC (each `[4]`;
  bytes, gta-reversed). A node (`CTrainNode`, 10 bytes) holds x, y, z as int16 ×8 and the distance from the start as
  uint16 ×3 (gta-reversed `TrainNode.cpp`; 1/3 m resolution). InitTrains also clears `bDisableRandomTrains` 0xC38010 and
  the generator state 0xC3800C (bytes), so a new game or a load always starts with random trains on.
- Sixteen train configurations at 0x8D44F8, 64 bytes each, model ids ended by 0 (bytes): 0 freight+4 flatbeds, 1 and 2
  streak+2 coaches, 3 freight+3 flatbeds, 4 streak+2 coaches, 5 streak+3 coaches, 6 freight+2 flatbeds, 7 streak+2
  coaches, 8 tram+tram, 9 tram, 10 freight+flatbed, 11 streak+2 coaches, 12 freight+3 flatbeds+freight, 13 freight+5
  flatbeds, 14 tram, 15 streak. Models: 449 tram, 537 freight, 538 streak, 569 freiflat, 570 streakc, 590 freibox
  (unused by any configuration) — the six `train` entries of `data\vehicles.ide`.
- Six stations at 0x8D48F8 (bytes): (1741, -1954) Unity, (1297, -1898) Market, (-1945, 128) Cranberry, (1434, 2632),
  (2783, 1758), (2865, 1281). `IsNextStationAllowed` 0x6F7260 compares the next station's level
  (`CTheZones::GetLevelFromPosition` 0x572300) with int stat 181 (cities passed) + 1.0 (0x858624).

### 3.2 Building a train

`CTrain::CreateMissionTrain(CVector pos, bool clockwise, uint type, CTrain** first, CTrain** last, int node, int track,
bool mission)` 0x6F7550, cdecl (bytes 0x6F7550–0x6F78F0; gta-reversed doesn't reverse it). Its two callers are the
`06D8` case (0x497FE4: node and track -1, mission true) and `DoTrainGenerationAndRemoval` (0x6F7C2C: the generator's
node and track, mission false).

1. With node < 0, `FindClosestTrackNode` 0x6F6BD0 searches all four tracks for the node nearest to `pos` and returns the
   node and the track. The engine's rail distance starts at that node's distance.
2. For each model of the configuration: `new CTrain(model, 4)` (`CVehicle::operator new` 0x6E2D50, constructor 0x6F6030,
   created by PERMANENT_VEHICLE), then status 4 (byte +0x36), node index +0x5A0, rail distance +0x5A8, track +0x5C0,
   `bIsLocked` (byte +0x428 bit 3: "locked by the script, cannot be removed", plugin-sdk), mission flag (bit 5 of
   +0x5B8) and clockwise flag (bit 6). A mission train gets `bStopsAtStations` (bit 7) cleared, and its engine is moved
   to `pos` and placed with `FindPositionOnTrackFromCoors` 0x6F6CC0. Each further carriage's `m_fLength` (+0x5AC) is
   minus its own collision-box length for a clockwise train, plus the previous carriage's for a counter-clockwise one,
   and its rail distance shifts by that much. Front flag (bit 3) on the first, last flag (bit 4) on the last;
   `m_pPrevCarriage` +0x5D0 and `m_pNextCarriage` +0x5D4 linked with `CEntity::RegisterReference` 0x571B70, so a deleted
   neighbour clears them. `ProcessControl` (vtable +0x28) runs once on each new carriage.
3. Every carriage is removed and added again, last first: `CWorld::Remove` at 0x6F7845, **`CWorld::Add` at 0x6F784B**.
4. Unless the engine is a tram (model 449), `CPopulation::AddPedInCar(engine, true, -1, 0, 0, 0)` 0x613A00 puts a driver
   in it and gives it decision maker 6. AddPedInCar adds no ped to the world (no `CWorld::Add` call in
   0x613A00–0x613CD0).
5. Byte +0x5B9 bit 2 (`bIsStreakModel`) is set on every carriage when the chain holds a 538.

The handle 06D8 returns is the engine's (`CPool<CVehicle>::GetRef` 0x424160 on the first carriage).

Trains are never in a save: the PC save holds no vehicles at all. `CPools::SaveVehiclePool` 0x5D4800 and
`CPools::LoadVehiclePool` 0x5D2A20 have no reference in the 1.0 exe (ReVa cross-references, an E8/E9 scan of every code
section), so the `CTrain` constructor call inside the loader (0x5D2B15) never runs.

### 3.3 Movement

`CTrain::ProcessControl` 0x6F86A0 (vtable 0x872370, slot +0x28 at 0x872398; bytes 0x6F86A0–0x6F9DA7; gta-reversed's
reversed version agrees except where noted):

- **Following carriages** (front flag clear): speed = the previous carriage's speed, rail distance = the previous
  carriage's + `m_fLength` (0x6F8E25–0x6F8E75). A carriage that lost its previous one keeps going with its speed times
  0.99 (double at 0x872500) per time step. So a train's whole state is the engine's: track, rail distance, speed,
  direction.
- **Engine, status not 0** (any non-player status, 0x6F8871): drives towards the autopilot cruise speed (+0x3D0) with
  gas and brake (+0x5B0, +0x5B4); with `bStopsAtStations` it brakes for stations (`FindMaximumSpeedToStopAtStations`
  0x6F5BA0) and waits 10 s (20 s for a streak), measured from +0x5BC. `bForceSlowDown` brakes at the forbidden crossings
  of closed cities (`CGameLogic::CalcDistanceToForbiddenTrainCrossing` 0x4418E0).
- **Engine, status 0 (a player drives)**: gas and brake from the driver's pad (`CPlayerPed::GetPadFromPlayer` 0x609560
  when the driver is a player). For both paths the speed gains gas/256 × time step × 0.002 / (carriages + 3) and loses
  brake/256 × time step × 0.006 / (carriages + 3), then decays by 0.99975 per step (0x859AA0, 0x858F44, 0x859CDC, double
  0x872508); rail distance += time step × speed (0x6F8C24–0x6F8C3B). Above 0.95 × the derail speed the pad shakes.
- **Position**: the rail distance wraps modulo the track length; the node index follows it node by node; the carriage's
  centre is the midpoint of the track points at its rail distance and rail distance + its length (+ the suspension
  offset); forward along the track, negated for counter-clockwise; move and turn speed from the position change; then
  `RemoveAndAdd`, and the front and last carriages mark what they hit (`MarkSurroundingEntitiesForCollisionWithTrain`
  0x6F6640).
- **Units**: rail distance in metres; `m_fTrainSpeed` +0x5A4 in metres per time step (1/50 s), signed along the track;
  `SET_TRAIN_SPEED 30` is 0.6, 30 m/s.
- **Recording-driven trains** (#16 recording 490, #63 the tram): `CVehicleRecording::SaveOrRetrieveDataForThisFrame`
  0x45A610 sets the engine's matrix from the recording and calls `FindPositionOnTrackFromCoors` (call 0x45A861;
  gta-reversed `VehicleRecording.cpp`), which projects the engine onto its own track (+0x5C0, it never changes track;
  its first instruction is a Hoodlum stub at 0x403C30) and sets its rail distance (the projected point minus half its
  length, 0x858B8C = 0.5) and its speed (±|move speed|). The carriages follow in their own `ProcessControl`.
- **Teleports**: `SetNewTrainPosition` 0x6F7140 (07C7) sets the position and tail-jumps to
  `FindPositionOnTrackFromCoors`. `SkipToNextAllowedStation` 0x6F72F0 (0A07) puts the engine 20 m (0x858BA4) before the
  next allowed station with speed ±0.1, loads the scene there (`CStreaming::LoadScene` 0x40EB70,
  `LoadAllRequestedModels` 0x40EA10) and calls `CGameLogic::PassTime` 0x4414C0 with distance × 0.05 + 23 minutes.
- `CTrain::UpdateTrains` 0x6F5900 (called by `CGame::Process` at 0x53BFDD) is empty.

### 3.4 Random trains

`CTrain::DoTrainGenerationAndRemoval` 0x6F7900, called only at **0x43426D** in `CCarCtrl::GenerateRandomCars` 0x4341C0
(which `CGame::Process` calls at 0x53C1C1; it returns at once during a cutscene `CCutsceneMgr::ms_running` 0xB5F851,
while `CGangWars::DontCreateCivilians` 0x4439C0 is true, and in an interior `CGame::currArea` 0xB72914). Bytes
0x6F7900–0x6F8167:

- Every 3 s: whether the local player (`FindPlayerCoors(-1)` 0x56E010) is within 60 m (0x858B34) of a station
  (0xC38068). The rest runs every frame near a station, else every 950 ms.
- **Removal first**: a non-mission train is deleted (`CWorld::Remove` at **0x6F7ACA**, then its deleting destructor)
  when every carriage is more than 220 m (0x863994) from the camera (`TheCamera` 0xB6F028) in 2D and none is the local
  player's vehicle (`FindPlayerVehicle(-1, false)` 0x56E0D0). Only the local player and the local camera count.
- Then nothing more while `bDisableRandomTrains` 0xC38010 is set.
- **At most one random train**: state 0 (0xC3800C) generates only when no non-mission engine exists. It picks a random
  node of track 0; if it lies within 70 m of the camera (100 m near a station) and the tunnel attributes allow it
  (`CCullZones::FindTunnelAttributesForCoors` 0x72D9F0), a random direction, then walks the node index away until the
  node is at least 170 m away; configuration = a counter + 1 modulo 8 (0xC38064), so configurations 0–7. In the San
  Fierro weather region (`CWeather::WeatherRegion` 0xC81314 = 2) it tries a tram on track 1 the same way (configuration
  8 or 9). State 1 requests the models (`CStreaming::RequestModel` 0x4087E0) and, once they are loaded and the node is
  more than 60 m away, calls `CreateMissionTrain(…, node, track, false)` at 0x6F7C2C; speed `rand() % 30 + 15` on track
  0 (halved within 300 m of (2222, -1750)), `rand() % 7 + 7` for a tram, capped by the station rule; the same value as
  cruise speed; then marks the models deletable (0x409C10, 0x409C70).

### 3.5 People on and in trains

- **Inside**: a passenger carriage (570) has seats like any vehicle; the TRAINS script needs `IsSittingInAnyCar`. The
  locomotives and the tram have a driver seat; a player who enters as driver gives the train status 0 (a player drives).
  Streak coaches stopped at a station take random peds on and off while the train isn't a mission train and the local
  player isn't aboard (`ProcessControl` 0x6F8E81–0x6F8F75: `RemoveRandomPassenger` 0x6F6850,
  `AddNearbyPedAsRandomPassenger` 0x6F8170).
- **Standing on top**: a ped is carried by what it stands on. `CPed::ProcessEntityCollision` 0x5E2530 (gta-reversed
  address note) stores a vehicle or object the ped collides with (entity type 2 or 4, test at 0x5E2A6B) at +0x568
  (gta-reversed `m_standingOnEntity`, with `RegisterReference` at 0x5E2A87) and at +0x584 (`m_pContactEntity`,
  0x5E2AB5), and the contact point minus the entity's position at +0x56C; `CPed::UpdatePosition` 0x5E1B10 reads +0x568,
  +0x56C and +0x578 to move the ped with it (bytes); gta-reversed clears +0x568 in `CPhysical::ProcessCollision` when
  the ped stands on nothing. The only other writers are the constructor and `PositionAttachedPed` (a disassembly scan of
  `.text` and `.HOODLUM`).
- **Attached** (`0464`): `CPed::AttachPedToEntity` 0x5E7CB0 (thiscall: entity, CVector offset, uint16 turret position,
  float angle limit in radians — the script's degrees × 0x8595EC, weapon; `ret 1Ch`) stores the entity at +0xFC
  (`CPhysical::m_pAttachedTo`) with `RegisterReference`, the offset at +0x76C, the position at +0x778 (uint16) and the
  angle limit at +0x77C (plugin-sdk calls +0x778 a float `m_fTurretAngleA`; the bytes write a word). For a player ped
  (ped type +0x598 is 0 or 1) it clears the collision flag, hands the weapon to the player data and **sets the local
  camera's weapon mode** (`CCamera::SetNewPlayerWeaponMode` 0x50BFB0 with 0x2D or 0x2E); for other peds it gives the
  weapon (0x5E6080) and makes it current (0x5E61F0). Then `CPed::PositionAttachedPed` 0x5DFDF0. `0465` detaches with
  0x5E7EC0; `0681` attaches objects with `CPhysical::AttachEntityToEntity` 0x54D570.

### 3.6 Derailment

Only while a player drives (status 0, test of byte +0x36 at 0x6F8C2D): at more than 1.0 (0x8D4944, 50 m/s, a `.data`
value) on a curve (dot product of the last two node directions below 0.996, 0x8D4940) with no carriage in a tunnel
(`IsInTunnel` 0x6F6320, a vertical line 100 m up), every carriage gets `bNotOnARailRoad` (byte +0x5B9 bit 0;
gta-reversed's loop sets it only on the engine), its physical flags at +0x40 cleared by 0xFFFDFFFB and
`SetIsStatic(false)` (vtable +0x10); from then on each carriage is a free physics body (`CPhysical::ProcessControl`
0x5485E0 at 0x6F8E1B, the derailed path at 0x6F9A31). Nothing puts it back on the rails. `FREIGHT` fails on it and `R3`
won't start the mission in it.

### 3.7 Removal paths

| Path | 1.0 US | Deletes |
|---|---|---|
| `RemoveOneMissionTrain` (07BD) | `CWorld::Remove` at 0x6F5DD7 | the given carriage and every one behind it |
| `RemoveMissionTrains` (06D9) | 0x6F6A7B | every mission carriage except the local player's vehicle |
| `RemoveAllTrains` (06DB) | 0x6F6B31 | every train with no carriage that is the local player's vehicle |
| `DoTrainGenerationAndRemoval` | 0x6F7ACA | far non-mission trains (3.4) |
| `DELETE_CAR` (00A6) on a carriage (`Car.Delete(129@)` in #16) | 0x467B3C | that carriage only; the ones behind it become orphans (3.3) |
| `CWorld::ClearForRestart` (a new game or load) | 0x53C5CB | every vehicle |
| `CTrain::Teleport` (vtable +0x38, 0x6F7190) | `CWorld::Remove` 0x6F7194, `CWorld::Add` 0x6F7249 | nothing (out and back in) |

Every carriage dies through `CTrain::~CTrain` 0x6F54D0, whose `CVehicle::~CVehicle` call at 0x6F5510 the vehicle sync
protocol already redirects (`VehicleHooks.cpp`).

### 3.8 Streaming

`CreateMissionTrain` requests nothing: the configuration's models must be loaded before (the generator's state 1 and
every mission script wait for `HasModelLoaded`). A copy must request and load them first, as
`CNetworkVehicle::CreateVehicle` does for cars (`CStreaming::RequestModel` with GAME_REQUIRED,
`LoadAllRequestedModels`), and release them when its last copy goes.

### 3.9 Fields a sync reads or writes

| Field | Offset | Source |
|---|---|---|
| vehicle type / subtype (6 = train) | +0x590 / +0x594 | bytes (09AE case, generator loop) |
| status (bits 3–7 of the byte; 0 player, 4 abandoned) | +0x36 | bytes |
| cruise speed | +0x3D0 (autopilot +0x40) | bytes (generator 0x6F7D1A) |
| driver | +0x460 | bytes (0x6F787D) |
| vehicle flags (`bIsLocked` = bit 3) | +0x428 | bytes; plugin-sdk `CVehicle.h` |
| door lock | `m_eDoorLock` | plugin-sdk; already carried by the vehicle sync's updates |
| node index (int16) | +0x5A0 | bytes |
| speed (float, m per time step, signed) | +0x5A4 | bytes |
| rail distance (float) | +0x5A8 | bytes |
| length to the previous carriage | +0x5AC | bytes |
| gas / brake | +0x5B0 / +0x5B4 | bytes |
| flags, low byte: initialised, stopped at station, passengers can enter, front, last, mission, clockwise, stops at stations | +0x5B8 | bytes; gta-reversed `Train.h` (offset checked there with `VALIDATE_OFFSET`) |
| flags, high byte: not on a rail road, force slow down, streak | +0x5B9 | bytes |
| time stopped at a station | +0x5BC | bytes |
| track (uint8) | +0x5C0 | bytes |
| passenger state | +0x5CA | bytes |
| previous / next carriage | +0x5D0 / +0x5D4 | bytes |
| size | 0x6AC | `new` size at 0x6F7614; plugin-sdk `VALIDATE_SIZE` |
| ped: attached to, offset, position, angle | +0xFC, +0x76C, +0x778, +0x77C | bytes (0x5E7CB0) |
| ped: standing on, contact offset | +0x568 (also +0x584), +0x56C | bytes (0x5E2A85, 0x5E2AB5, 0x5E1B10); gta-reversed |
| vehicle: created by (4 = PERMANENT for every carriage) | +0x4A4 | bytes (`push 4` at 0x6F762F) |

### 3.10 Where the references differ from 1.0

- gta-reversed's derail loop sets `bNotOnARailRoad` on the engine only; 1.0 sets it on every carriage (0x6F8DF3).
- plugin-sdk types +0x778 of `CPed` as a float; `AttachPedToEntity` stores a uint16 there and the angle at +0x77C.
- gta-reversed doesn't reverse `CreateMissionTrain`, `DoTrainGenerationAndRemoval`, `RemoveAllTrains`,
  `FindPositionOnTrackFromCoors`, `IsNextStationAllowed` or `SkipToNextAllowedStation`; this design read them from the
  bytes.

## 4. Current state in this build

### 4.1 Random trains run on every game, unsynced

- At start-up `CPatch::TemporaryPatches` (`CCore.cpp:88`, initGameEvent.after) switches random trains off
  (`CPatch.cpp:13`) and NOPs the `GenerateRandomCars` call at 0x53C1C1 (`CPatch.cpp:19`), which is where
  `DoTrainGenerationAndRemoval` runs from.
- The `PLAYER_HANDSHAKE` handler, which **every** game runs when it authenticates, calls
  `CPatch::RevertTemporaryPatches` (`PacketHandlers/system.cpp:86`): random trains on again (`CPatch.cpp:35`) and the
  0x53C1C1 call restored (`CPatch.cpp:38`). `RevertTemporaryPatchesForHost` (`system.cpp:115`, only on the game that
  becomes host) repeats the train line and changes nothing more for trains. `CTrain::InitTrains` clears the flag at
  every new game or load as well (3.1).
- So the premise that upstream keeps random trains off on a game that isn't the host does not hold: since upstream's
  handshake handler, the friend's game generates its own random trains near its own camera, and the host's game its own.
- Neither is networked: the carriages enter the world through the `CWorld::Add` call at 0x6F784B, which is not among the
  redirected sites (`Hooks/WorldHooks.cpp:304–310`), so no `VEHICLE_SPAWN` is ever sent for a train. Each player sees
  trains the other doesn't, and the crossing barriers (`CObject::ProcessTrainCrossingBehaviour` 0x5A0B50, which asks
  `FindNearestTrain` at 0x5A0B93) close for different trains on each screen.
- The world-state registry (`MissionSettings`, `CWorldStateEntriesMission.cpp:95` reads, `:124` applies through
  `CWorldStateEntries.h:244`) makes the friend's flag follow the host's: off while a host mission switches random trains
  off (#11, #13, #16, #27, #40, #51, #52, #54, #63, #64, #74, #103), on otherwise, which is the value upstream already
  left. **It turns nothing on that was off**; it is the only thing today that stops the friend's game generating trains
  during those missions. What it can't do: a host mission's `DELETE_ALL_TRAINS` (06DB isn't replayed) leaves the
  friend's existing random train running until it is 220 m from the friend's camera — in #11 the friend can meet a train
  on the tracks of the chase that the host never sees.

### 4.2 Mission trains

Only on the host. The other games have no copy: `CNetworkVehicle::CreateVehicle` refuses `VEHICLE_TRAIN`
(`CNetworkVehicle.cpp:164`) and a train model's record is never treated as a lost copy (`:104`), but no `VEHICLE_SPAWN`
for a train reaches them anyway (4.1). The mirror layer sends a blip on a train as a coordinate that follows the host's
position (`CMirrorBlips.cpp:290`, comment at `:52`). The four removal sites of trains are classified "train" with the
note "trains never become copies" (`CNetworkVehicleManager.cpp:114–117`).

### 4.3 A player on or in a train today

- **Inside or driving**: `UpdateDriver` and `UpdatePassenger` find no record for the carriage and send nothing, and no
  on-foot update goes out while the player sits in a vehicle (`Main.cpp:177–218`); `VEHICLE_ENTER` is sent only for a
  network vehicle (`Hooks/TaskHooks.cpp:13–20`). So the other screen shows the player frozen where they boarded, until
  they get out (`VEHICLE_EXIT` is sent for any vehicle) and the on-foot updates move them to the exit point. The other
  player can't board, since they have no such train.
- **On top**: on-foot updates carry the absolute position (every 50 ms) and the handler sets it
  (`PacketHandlers/players.cpp`); the other screen shows the player flying along the track at roof height, a speed ×
  latency behind (2–4 m at 20 m/s and 100–150 ms), falling between updates.
- **Attached** (CJ in #16): the same as on top.
- Scripts that read the train on the player's own game (`TRAINS`, `R3`, `FREIGHT`) act on that game's own train.

### 4.4 Peds and identities

- The host's mission peds on or in a train are network peds (the redirected `CWorld::Add` calls in the 009A, 0129 and
  01C8 cases, 0x467726, 0x469D6D, 0x47D9B7) but are synced on foot: the ped sync's in-vehicle branch needs the vehicle's
  record (`CNetworkPedManager.cpp:63–67`), so a ped in a carriage, attached to it or standing on it glides along the
  track at the host's absolute positions on the other games.
- A train handle in a replayed command, or in a bullet hit, becomes vehicle id **0**:
  `CNetworkEntitySerializer::SetEntity` sets the vehicle type and leaves `entityId` at 0 when the vehicle has no record
  (`CNetworkEntitySerializer.cpp:77–83`), so the other game acts on whichever vehicle has id 0. Mission ports of
  `SCRASH2` would hit it with `Task.EnterCarAsPassenger(78@, 82@)` (05CA is replayed). The same pitfall as for
  unconfirmed vehicles (`mirror-layer.md` 3.10).

### 4.5 Findings

1. Not a registry bug: the friend's own random trains come from upstream's handshake (`system.cpp:86` →
   `CPatch.cpp:35/38`), and the registry only switches them off with the host's missions.
2. Shipped behaviour that matters for group 2: the friend's game keeps its own random train through the host's
   `DELETE_ALL_TRAINS` in #11 (the chase along the tracks), #13 and #16; the friend can be hit by a train the host never
   sees, and the host sees the friend's ped thrown by nothing.
3. A player in a train is invisible to the other game's sync (frozen at the boarding point) in free roam today.
4. The entity id 0 hazard for train handles (4.4) waits for the first registered port of a train mission.

## 5. Architecture

### 5.1 Options

| Option | How | For | Against |
|---|---|---|---|
| **A. Track mirror with carriage identities (recommended)** | The host simulates every train. The others create the same train with the engine's own `CreateMissionTrain` (same configuration, track and node) and drive the copy's engine from the host's state (track, rail distance, speed, direction), so the carriages follow by the engine's own rules. Each carriage is also a network vehicle (an id from the vehicle sync protocol) for identity only | the copy is exact (same track data, carriages placed by the same code), smooth at 5 Hz, cheap; blips, seats, peds in carriages, shots and replayed commands work through the existing paths; one authority | touches the vehicle sync in a few defined places (6.1); a new client module |
| B. Trains as ordinary network vehicles | every carriage is a physics copy driven by idle updates, like a car | reuses everything | a copy can't be built carriage by carriage (3.2); a carriage off its track is a physics body that tilts, collides with its neighbours and drifts off the curve between updates; 6× the traffic |
| C. Replay the train commands | ports register train missions; the others replay 06D8/06DC/06DD | no new packet | random trains, recordings, joins and frame-time drift are not covered; only registered scripts |
| D. Track mirror with its own train ids | as A, but carriages are identified by (train id, carriage index) instead of vehicle ids | no vehicle-sync change | every consumer of a vehicle id needs a parallel path: blips, the opcode serializer, shots, players and peds entering, sitting in, driving and leaving a carriage |

A against D: the cost of A is about ten small rules in the vehicle sync (6.1); D would rebuild the seat sync for trains.

### 5.2 Overview

```
host game                                   server                           other games
CTrain chains in the vehicle pool ─scan─┐
  (06D8, the random generator)          ├─ VEHICLE_SPAWN per carriage ─ ids ─── records without vehicle
                                        ├─ TRAIN_EVENT SPAWN (descriptor,       CreateMissionTrain(config, track, node)
                                        │    carriage ids) ── host relay ────── bind the carriages to the records
engine state each frame ────────────────┼─ TRAIN_STATE 5 Hz ── host relay ───── copy engine put at the rail distance
                                        │                                       in the ProcessControl wrapper
~CVehicle of a carriage ────────────────┼─ VEHICLE_REMOVE + TRAIN_EVENT REMOVE  whole copy removed (players out first)
attached peds and players (owners poll) ┴─ ENTITY_ATTACH ───── relay ────────── AttachPedToEntity on the copies
```

New client module `CTrainSync` (`client/src/CTrainSync.{h,cpp}`, logic without game types in `CTrainSyncPolicy.h`, test
`tests/train_sync_test.cpp`); packets in `shared/network/packets/train.h`; server relay `server/src/PacketHandlers/
train.cpp`. Game thread only (packet handlers already run there).

### 5.3 Host side

- **Finding trains**: every frame, scan the vehicle pool for carriages of type 6 with no previous carriage (a chain
  head: an engine, or an orphan after a `DELETE_CAR` on a carriage). A head not registered yet starts a train: walk the
  chain, give each carriage a record with `CNetworkVehicle::CreateHosted` (temp id, `VEHICLE_SPAWN`) unless it has one
  already (a lone carriage made with `Car.Create` and a train model would come through a redirected `CWorld::Add`; no
  script does that, and saves hold no trains, 3.2). This finds trains from every path (06D8, the generator) without a
  hook on them.
- **Spawn**: once every carriage has its id (`VEHICLE_CONFIRM`), send `TRAIN_EVENT SPAWN`: train id (host-local, 0–31),
  track, configuration index, direction, the engine's node index, per carriage the vehicle id and model, door locks, the
  track layout hash (7), and the current state.
- **State**: every 200 ms per train, and at once when the train starts or stops moving, jumps (a rail distance change
  more than 5 m off its speed, from 07C7, 0A07 or a recording restart; a jump counter goes up), changes direction or
  derails: track, rail distance and speed of the head, flags, gas and brake (for the copy's engine sound).
- **Carriage properties**: door lock, health and the wrecked state per carriage, sent as an event on change
  (`Car.LockDoors` in #13 and #16, `Car.Explode` of the quarry train).
- **Removal**: the ~CVehicle hook's `OnVehicleDestroyed` sees a carriage of a registered train: the record goes as today
  (`VEHICLE_REMOVE`, the host is its syncer), and `TRAIN_EVENT REMOVE` goes for the train. A chain that lost a carriage
  but still exists is removed and found again by the next scan as new chains.
- **Random trains kept near the friend**: `DoTrainGenerationAndRemoval` counts only the host's camera and the host's own
  vehicle (3.4), so a random train the friend rides far from the host would vanish under the friend. A wrapper on the
  call at 0x43426D sets the mission flag (bit 5 of +0x5B8) on every carriage of a non-mission train within 220 m of a
  network player for the duration of the call, and restores it after. Side effect: such a train no longer counts as
  "the" random train, so the generator may make a second one near the host (decision T-D3).

### 5.4 Receivers: copies

- **Creating**: when the gate is open (authenticated, a host known, a frame since this game's restart,
  `CSessionLifecycle::IsLocalGameReady()`, as the registry's gate): load the configuration's models, call
  `CreateMissionTrain(pos 0, direction, configuration, &first, &last, node, track, false)`, check the carriage count and
  models against the descriptor (else log and drop the train), bind each carriage to its record (6.1), set the mission
  flag on every carriage (protects it from this game's removal paths and switches the streak coaches' random passengers
  off), clear `bStopsAtStations` and `bForceSlowDown`, apply the door locks, then the state. The copy's engine keeps the
  driver ped `CreateMissionTrain` gives it (a local cosmetic ped, not networked); it goes when the host's state says the
  engine has no NPC driver.
- **Driving the copy** — in the existing `CVehicle__ProcessControl_Hook` (CTrain slot 0x872398 already points there,
  `VehicleHooks.cpp:537`), before the original 0x6F86A0 for a copy's head that hasn't derailed: rail distance = the
  display value (below), speed 0, cruise speed 0, status forced to 4 for the call if it was 0 (a local player drives
  it); the original then integrates nothing (gas and brake work out to 0, so no acceleration, no station logic, no pad,
  no derail check: 0x6F8C2D skips it for status 4) and places every carriage from that rail distance with its own code;
  after the call: status, cruise speed and gas/brake back, speed = the host's signed speed (the carriages processed
  later, and the move speed scripts read, follow it). The following carriages run the original unchanged.
- **Display rail distance**: `d += v × dt / 20 ms` each frame from the last state, plus a correction of the error to the
  last state's prediction, spread over about 0.5 s and never against the direction of motion; the error is the shortest
  difference modulo the track length. A jump counter change or an error above 25 m snaps the copy (the matrix set first,
  so the frame's move speed stays small). No state for 500 ms (the host alt-tabbed, paused or lost): the copy holds.
- **Lag**: the state is applied when the packet buffer hands it over (50 ms after its stamp) and dead-reckoned from
  there, not from the stamp: the copy then lags the host like every mirrored car and ped, so the host's bike next to the
  train, the Vagos on it and the train keep their places relative to each other. Time-aligning trains alone would put
  them 2–4 m ahead of everything else.
- **Lost copies**: a copy carriage this game destroys (its own `DELETE_ALL_TRAINS` in the freight mission, a replayed
  `ClearArea`, its own restart) marks the train lost; the whole copy chain is deleted (players taken out,
  `CNetworkVehicle::TakePlayersOut`) and made again from the descriptor at the next state, while the host still has it.
- **No own trains while following**: the wrapper at 0x43426D skips `DoTrainGenerationAndRemoval` altogether on a game
  that has a host and isn't it (no generation, no removal of copies). When a game starts following (a host becomes
  known), it deletes the trains it made itself, except a mission train of its own running mission. The registry's
  `MissionSettings` entry keeps writing the host's flag; it no longer matters for generation there.

### 5.5 Random and mission trains together

All of them are the host's: the generator runs only on the host, near the host's camera, so the friend sees random
trains near the host and none alone in another city (decision T-D2); the train the friend rides is kept (5.3). Mission
trains follow the same path; their mission flag on the host only decides the host's own cleanup (`ReleaseMissionTrains`
at the mission end, `RemoveMissionTrains`), and the copies follow whatever the host's game does.

### 5.6 Riding on, riding in, driving

- **Attached** (#13 Vagos, #16 CJ and Ryder, #74 guards): package T2 mirrors attachments (6.5). The owner of the ped
  (its syncer) or the player's own game polls `m_pAttachedTo` and the attach fields (3.5) and sends `ENTITY_ATTACH` with
  the target's vehicle id; the others attach their copy to their copy of the vehicle with `AttachPedToEntity` through
  its non-player branch (the ped type set to a non-player type for the call, so it doesn't switch this game's camera
  mode at 0x50BFB0), and stop applying on-foot positions to it while attached (rotation, aim and weapon still apply).
- **Standing on top** (#16 if the friend stands on the train, #74): package T3 adds a vehicle-relative position to the
  on-foot updates of players and peds that stand on a moving network vehicle (6.4): the receiver places the ped at its
  copy of the carriage times the offset, so it stays on the roof instead of sliding off by speed × latency. Kept for 2 s
  after leaving the roof while airborne within 10 m, so a jump between carriages doesn't drop the ped behind.
- **Inside** (free roam, #52's reporter, the TRAINS ride): with carriage records, the existing seat sync works:
  `VEHICLE_ENTER`, passenger updates and `PED_PASSENGER_UPDATE` seat the player or ped in the copy carriage, the copy
  carries them. The TRAINS script's skip on the friend's game is forwarded (T4): a redirect of the call at 0x47B6DC
  sends `TRAIN_EVENT REQUEST skip` for a copy to the host, which runs `SkipToNextAllowedStation` on its train; everyone
  aboard moves with it and the host's passed time reaches the friend through the weather and time sync.
- **Driving**: the host's game always simulates (decision T-D4). A friend who drives a copy's locomotive sends driver
  updates as for any vehicle; the host's game keeps that player in the driver seat (the existing warp in the driver
  update handler) and its `ProcessControl` reads the friend's replayed keys: `CVehicle__ProcessControl_Hook` applies the
  network player's pad context and gives its ped the PLAYER1 type for the call, and `CPlayerPed::GetPadFromPlayer`
  0x609560 returns pad 0 for ped type 0, pad 1 for type 1 and NULL otherwise (ReVa decompile), which the train's player
  path would dereference. The train answers the friend's throttle after the round trip (100–250 ms); a train needs
  seconds to change speed noticeably. The positions in the friend's driver updates are ignored for a carriage (6.1). The
  freight side mission run by the friend then reads its copy's state (speed, stations, derailed), which is the host's.

### 5.7 Derailment

Decided on the host (only a player-driven train, 3.6). The state's derailed bit makes the receivers set the same flags
on their copy carriages (as 0x6F8DF0–0x6F8E17 does); from then the host's game sends the derailed carriages' physics as
ordinary idle updates (the generic vehicle sync, unblocked for derailed carriages, 6.1) and the copies follow them like
cars. Until T5, a derailed train is removed from the receivers instead.

### 5.8 Lifecycle

| Event | Host | Receivers |
|---|---|---|
| A train is created | records, then `SPAWN` once confirmed | records from `VEHICLE_SPAWN`, copy from `SPAWN` (either order) |
| A player joins (also during a mission) | the server sends every carriage record (its join snapshot); on `PLAYER_JOINED` the host sends `TRAIN_EVENT SNAPSHOT` to that player (`SendFromHostToPlayer`), and the attach mirror its attachments | copies made and bound; attachments applied when both ends exist |
| The host starts a new game or loads | `ClearForRestart` destroys its trains: `REMOVE_SYNCER_RESTARTED` for the records (the server removes them, the host being the sender); an empty `SNAPSHOT` with reset 2 s into its new game | every copy removed |
| The host leaves | the server removes the host's vehicles as today | 250 ms after the host loss (`CSessionLifecycle`), every copy removed, local players taken out where they are; with no host left, this game generates its own trains again |
| The friend restarts | – | copies destroyed by `ClearForRestart`; made again from the kept descriptors when the gate opens |
| The host alt-tabs | its trains stop with its game | copies hold after 500 ms without state |
| The host goes indoors or a cutscene runs | trains keep moving, no generation or removal (3.4) | copies keep following |

### 5.9 Hooks

- Existing, reused: CTrain's `ProcessControl` slot 0x872398 (already `CVehicle__ProcessControl_Hook`); the five
  `~CVehicle` call sites and the `ClearForRestart` call of the vehicle sync protocol.
- New in T1: `RedirectCall` at 0x43426D (bytes `E8 8E 36 2C 00`, target 0x6F7900, checked at start-up and skipped with a
  log line otherwise). New in T4: 0x47B6DC (`E8 0F BC 27 00`, target 0x6F72F0). `tools/audit_hooks.py`: 417 → 418 → 419
  sites, the same 17 known non-call entries.
- Not hooked: `CreateMissionTrain`'s `CWorld::Add` at 0x6F784B stays unredirected on purpose: the host networks its
  carriages explicitly, and a copy must never be networked by the game that makes it.

### 5.10 Bandwidth

| Traffic | Size | Rate |
|---|---|---|
| `TRAIN_STATE` | about 20 bytes with headers | 5 Hz per train, plus events; 1–2 trains usual, 5 at most (#52) |
| carriage `VEHICLE_SPAWN` and `TRAIN_EVENT SPAWN` | 6 × 25 + 40 bytes | per train created |
| `ENTITY_ATTACH` | about 30 bytes | per attach or detach |
| vehicle-relative on-foot (T3) | +7 bytes per on-foot update | 20 Hz while on a moving vehicle |
| derailed carriages (T5) | an idle update each | 6.7 Hz per carriage while derailed |

Under 300 B/s usual and well under 1 KB/s in #52; small next to the player and vehicle sync and fine for the Steam
tunnel.

## 6. Interfaces

### 6.1 Vehicle sync protocol

Carriages are ordinary records for their identity; the train module owns their motion. Every rule below keys on the
record's model being one of the six train models (a shared list in `shared/network/vehicle_sync.h`, so the server, which
has no model info, applies it too). No new field in `VEHICLE_SPAWN`.

- **Ids**: from the server's round robin as for any vehicle; a train of six carriages takes six. The descriptor lists
  them in order; a receiver binds an id only to a record of the same model with no live vehicle (a reused id can't
  capture another vehicle).
- **Spawn and remove**: `VEHICLE_SPAWN` per carriage from the host's `CreateHosted`, server-stamped as today; a
  receiver's `CNetworkVehicle` constructor already makes no vehicle for a train model and no lost copy. The train module
  binds the copy: `m_pVehicle`, the pool reference (`Capture`) and `GuardCopy`, whose 150 ms re-apply then covers it
  like any copy (MISSION_VEHICLE and `m_nTimeTillWeNeedThisCar` +0x4D0; no `CTrain` code in 0x6F5370–0x6F9DB0 reads the
  created-by value at +0x4A4, and the PC save holds no vehicles, 3.2). Removal: the host's ~CVehicle path as today. On a
  receiver, `OnRemovePacket` for a bound carriage removes the whole copy chain (players out first) instead of that one
  carriage; the other carriages' removals then find no vehicle.
- **Syncer**: always the host. Server: a driver update for a carriage seats the driver but doesn't reassign the syncer,
  and no passenger becomes a carriage's syncer (`VehicleSeatPolicy`, with cases in `tests/vehicle_sync_test.cpp`).
  Without this, a friend who drove a locomotive would become its syncer, and the friend leaving or restarting would
  remove it on the host's game too (`RemoveAllHostedAndNotify` removes a leaving syncer's vehicles that aren't the host
  mission's).
- **Updates**: the host's `UpdateIdle` sends nothing for a carriage that hasn't derailed; a receiver's idle and driver
  update handlers don't write the matrix, speed, colours, health or paint of a carriage that hasn't derailed (seat,
  keys, health and weapon of the player still apply; a derailed carriage takes idle updates like a car, 5.7);
  `PED_DRIVER_UPDATE` the same for a ped in a carriage's driver seat (#52 creates the reporter inside carriage 2 with
  0129, which seats him as its driver). `ApplyAssignment` changes only `m_bSyncing` for a carriage (no takeover,
  recreation or status change).
- **Removal-site classes** (`CNetworkVehicleManager.cpp:114–117`): on the host the four "train" sites remove the host's
  own trains (records end in the ~CVehicle hook); on a receiver 0x6F5DD7, 0x6F6A7B and 0x6F6B31 can remove copies (its
  own scripts' 07BD, 06D9, 06DB: lost copies, made again) and 0x6F7ACA never runs (5.4). The note "trains never become
  copies" goes.
- **Seats and players**: unchanged. Taking players out before a vehicle is deleted, and keeping a network player's ped
  when a vehicle it sits in is destroyed (`PlayerHooks`, slot 0x86D1BC), work for carriages as for cars.

### 6.2 Mirror layer

- **Blips**: a blip on a carriage resolves as a `VEHICLE` entity once the carriage has its record: the host side drops
  the train exception at `CMirrorBlips.cpp:290` (only while train sync is on); a receiver resolves the id to its bound
  copy carriage (`CMirrorBlips.cpp:613–622` already does it for vehicles). Until a copy is bound, the coordinate
  fallback stays. `Coop.Update*BlipForNetworkPlayer` on a train works the same way.
- **Replayed commands**: with records, `CNetworkEntitySerializer` names the carriage, which fixes 4.4 for trains. Worth
  adding when a train mission is ported: `0158` point_camera_at_car (#13's final shot), `0464`/`0465` are covered by T2
  instead (no replay needed, the mirror covers unregistered missions too).
- **Texts, areas, HUD**: unchanged; the locate commands on trains in the scripts draw no spheres.

### 6.3 World-state registry

`MissionSettings` keeps mirroring the host's `bDisableRandomTrains` (W12): harmless, and the right value on a game that
plays alone after the host left. While a host is known the train module's suppression at 0x43426D decides; the flag no
longer turns generation on or off on the followers. No change to the registry. The city unlock (entry 0) keeps
`IsNextStationAllowed` the same on both games.

### 6.4 Mission object design: points on a moving carriage

The train side of the interface, for objects on a train and thrown off it (#16, #74) and for T3's peds:

```cpp
// shared, private build: a point on a network vehicle (a train carriage or any other vehicle)
struct SVehiclePoint
{
    uint8_t vehicleid;    // network vehicle id; a carriage has its own (6.1)
    int16_t x, y, z;      // offset in the vehicle's frame (right, forward, up), 1/128 m, ±256 m
};
```

- **Naming**: always the carriage the point is on (the one whose bounding box contains it), not the engine: each
  carriage follows the curve on its own. A carriage is also (train id, index) in the descriptor; prefer the vehicle id.
- **Keeping in step**: a copy carriage's matrix is computed every frame from the host's rail distance by the engine's
  own code (5.4), so `world = M.pos + M.right × x + M.forward × y + M.up × z` with the receiver's copy matrix is the
  same spot on the same carriage as on the host, one lag later, for as long as the train exists. Attached things use the
  engine's attach functions with the same offset (`AttachEntityToEntity` 0x54D570 for objects and cars,
  `AttachPedToEntity` 0x5E7CB0 for peds), and the engine keeps them there.
- **Thrown objects** (#16): the start as a point on the caboose and the velocity relative to it (the object's velocity
  minus the caboose's); the receiver adds its copy's velocity. The flight is then relative to the train, as on the host.
- **Time**: everything mirrored is applied when handled, with the same lag; nothing needs a timestamp beyond the
  packet's.
- **Attach records**: `ENTITY_ATTACH` (6.5) has room for objects and cars as entity kinds; the object design can use it
  with its object ids, or its own packet with `SVehiclePoint`.

### 6.5 The attach mirror (T2)

`ENTITY_ATTACH` (EVENT channel, reliable): sender (filled in by the server), entity kind (player, ped; object and car
reserved), entity id, attached or not, target vehicle id, offset (3 floats), for peds the position (0–3), the angle
limit and the weapon; for objects and cars a rotation (3 floats). Sent by the entity's owner (its own game for a player,
the syncer for a ped) on change, and its current attachments when a player connects (any game: `PLAYER_CONNECTED`
reaches everyone). Receivers attach when both their copies exist and again after either is recreated (ped recovery, a
lost train copy). Works for every network vehicle, so Ryder in the Picador's bed (#16) and the car on the flatbed (#74,
kind car) use it too.

### 6.6 Session lifecycle

Subscribes like the registry: `PLAYER_JOINED` (snapshot), `HOST_CHANGED`, `DISCONNECTED`, `GAME_RESTARTED`,
`SAVE_LOADED`; `IsHost`, `HasHost`, `IsLocalGameReady` for the gate.

## 7. Wire format

New packet types, appended after `MISSION_MIRROR_HUD` (and after what the object design appends, in merge order):

| Packet | Channel | Fields |
|---|---|---|
| `TRAIN_STATE` | SYNC (unreliable) | train id (u8), sequence (u8), flags (u8: clockwise, derailed, engine has an NPC driver, stopped at a station), jump counter (u8), track (u8), rail distance (f32), speed (i16, ×10,000 m per time step), gas and brake (u8 each, ÷2) |
| `TRAIN_EVENT` | EVENT (reliable) | kind (u8): SPAWN {train id, track, configuration, direction, node (u16), count, per carriage {vehicle id, model (u16), lock (u8)}, layout hash (u32), state}; REMOVE {train id}; CARRIAGES {train id, per carriage lock, health (u16), wrecked}; SNAPSHOT {reset, count, SPAWN records}; REQUEST {train id, kind (skip to next station)} |
| `ENTITY_ATTACH` (T2) | EVENT (reliable) | 6.5 |
| on-foot updates (T3) | as today | optional `SVehiclePoint` (presence bit) in `PLAYER_ONFOOT_UPDATE` and `PED_ONFOOT` |

Layout hash: FNV-1a over the four node counts, the four track lengths and the sixteen configuration rows; a mismatch
turns train sync off on that game with one log line (both games run the same Steam data, so it shouldn't happen).

Server: `TRAIN_STATE` and `TRAIN_EVENT` from the host relayed to everyone else (`CHostManager::SendFromHostToOthers`,
the sender filled in), `SNAPSHOT` to one player (`SendFromHostToPlayer`), `REQUEST` from another player relayed to the
host only. No train state on the server. Every package is a protocol change: version bump and a new package for the
friend.

`--coopdiag`: `CoopAndreas_train_sync.log` (spawn, bind, lost, recreated, snap, derail, request, attach events; at most
4096 records), a `train-sync:` line in `CoopAndreas_diag.log` (trains, copies, bound carriages, largest correction,
snaps).

## 8. Risks and open questions

1. **The copy override.** Signs (clockwise, counter-clockwise, a carriage that is a head after a deletion), the forced
   status while the friend drives, and the station timer (+0x5BC) must leave the copy's engine exactly where the host's
   state puts it. The policy test covers the rail arithmetic; only a test drive shows the rest.
2. **Vehicle-sync interplay.** A path not listed in 6.1 could still write a carriage's position or hand its syncer to
   the friend. The `--coopdiag` log names every carriage write that doesn't come from the train module.
3. **Attaching remote players.** `AttachPedToEntity`'s player branch changes the local camera (3.5); the non-player
   branch gives the ped a weapon with 30,000 rounds (the remote player's weapon sync overwrites it). To check in T2.
4. **Standing on a roof** before T3: absolute positions make a standing player slide off on the other screen.
5. **Far from the host**: the host's game simulates a train the friend rides 2 km away without loading the collision
   there, so `IsInTunnel` is always false and a fast player-driven train could derail inside a tunnel on the host only
   (3.6).
6. **A second random train** near the host while one is kept near the friend (5.3).
7. **The friend's own trains** (their quarry mission) stay on their screen only, and their own `DELETE_ALL_TRAINS` in
   the freight mission deletes copies that come back a moment later.
8. **Orphan carriages**: after a `DELETE_CAR` on an engine (#16's `Car.Delete(129@)`), the carriages behind it roll on
   with the 0.99 decay on the host, and the random-train removal never looks at them (it only checks front carriages,
   0x6F7A2D); they stay until a script deletes them. The copies show the same.
9. **Two trains on one track** (#13's oncoming trains, #52's traffic trains): whatever the host's engine does is
   mirrored; not checked whether trains hit each other.
10. **Recording trains** (#16, #63): the host's rail distance comes from `FindPositionOnTrackFromCoors` every frame; if
    the recording leaves the track the copy shows the projection.
11. **Driver ped of a copy**: a local cosmetic ped; shooting it on the friend's game kills only that one.
12. **Not tested in game.**

## 9. In-game test plan (two players)

Not tested in game. For the user (host) and the friend, both starting with the `CoopAndreas (diagnostics)` shortcut
so `--coopdiag` writes the train log. The architect turns this into the test plan (`docs/test-plan.md`).

A. Free roam, after T1 and T2:

1. Host: start the server and the game, load the co-op save. Friend: connect.
2. Both: go to Unity Station (Los Santos) and wait for a train. Both say on voice when it stops at the platform: it
   stops at the same moment and the same place on both screens (within a few metres).
3. Friend: stand at a level crossing while the train passes: the barriers close for both.
4. Host: drive more than 300 m away while the friend stays by the moving train: the train keeps running for the friend.
5. Friend: wait until the train stops at a station and get into a passenger carriage. Host: the friend is inside the
   carriage and rides with it (not standing on the platform). Friend: get out at the next station.
6. Host: get into the locomotive as driver and drive; the friend rides in a carriage: smooth on the friend's screen;
   brake at a station.
7. Friend: drive the locomotive with the host in a carriage: it reacts to the friend's keys a moment late; the host sees
   it move.
8. Friend: alt-tab for 30 s while the host rides the train; back in the game, the train is where the host's is.
9. Friend: leave the game and reconnect while a train runs: it appears.
10. Friend: ride a passenger carriage and press the button the TRAINS help shows: both arrive at the next station (T4).
11. Host: drive the freight train fast through a curve outside a town until it derails: the friend sees the carriages
    come off the rails and tumble (T5).

B. At the story point:

12. #11 OG Loc: no train on the tracks during the chase, on either screen.
13. #13: the friend sees the Brown Streak with four Vagos on top from Unity Station on, follows on their own bike,
    shoots them; hits count; the oncoming trains appear on both screens; the parked car is hit; the friend can't get
    into the carriages.
14. #16: the friend sees the train at the yard with the crates, then moving with CJ standing on the last carriage; the
    friend rides with Ryder (or as decided in T-D5) and sees the crates thrown and caught.
15. Later, in order: #27 the train in the escape, #52 the reporter boarding and the blip on the train, #74 the guards on
    the train and landing on it with the jetpack (T3), #103 the train in the chase, the freight side mission (T5).

## 10. Work packages

All `gtasa-coder-high`, each in its own worktree from the then-current `main`. Gates: `scripts/build.ps1`,
`tools/audit_hooks.py` where hook sites change, `tests/train_sync_test.cpp` (new, `/W4 /WX` like the other policy
tests), `tests/vehicle_sync_test.cpp` for the carriage rules. Each is a protocol change for the next package.

| WP | Content | Size | Needed by |
|---|---|---|---|
| T1a Carriage identity | the shared train-model list; server: no syncer change for carriages from driver or passenger updates (`VehicleSeatPolicy`, tests); client: no idle updates for carriages that haven't derailed, the handlers don't move a carriage, `ApplyAssignment` for carriages, chain removal hook in `OnRemovePacket`, the binding API for `CTrainSync` | 1 agent job | #13, #16 |
| T1b Train mirror | `CTrainSync` (host scan, carriage records, `TRAIN_STATE`/`TRAIN_EVENT` SPAWN, REMOVE, CARRIAGES, SNAPSHOT; receivers' copies, override in `CVehicle__ProcessControl_Hook`, correction, lost copies, lifecycle, gate), the 0x43426D wrapper (no generation on followers, random trains kept near network players), follower clean-up, mirror blips' train exception, derailed trains removed on receivers, diagnostics, policy header and test, server relays | 2 agent jobs (one large) | #11 (no friend's trains), #13, #16 |
| T2 Attach mirror | `ENTITY_ATTACH` for players and peds attached to network vehicles (cars included), owner polling, receiver attach without the camera branch, on-foot positions ignored while attached, reconnect and recreation | 1 agent job | #13 (Vagos), #16 (CJ, Ryder) |
| T3 Vehicle-relative on foot | `SVehiclePoint` in player and ped on-foot updates while standing on a moving network vehicle (the entity at +0x568, 3.5), sticky for jumps | 1 agent job | #16 only if T-D5 puts the friend on the train; #74; also #80 Stowaway (inside the moving plane) and boats |
| T4 Riding inside | the 0x47B6DC redirect and `REQUEST` (TRAINS skip for the friend), entering stopped carriages checked, the streak coaches | 1 agent job | free roam; #52 |
| T5 Derailment and driving | derailed copies following idle updates, a player driving a copy (forced status, keys), the freight side mission for the friend | 1 agent job | free roam; the freight mission after #61 |

Order: T1a, then T1b; T2 can start after T1a (it works on cars from the start); then the #13 and #16 ports
(`gtasa-coder`, gameplay only: the friend's bike in #13, the friend's place in #16). T3 before #74 (or with group 2 if
T-D5 says so), T4 before #52, T5 before the freight mission is played. For group 2: T1a + T1b + T2, about four agent
jobs.

Docs after merging (architect): CLAUDE.md fork changes, `docs/mission-porting.md` (trains are entities; a blip on a
train is an entity blip; ports don't wait for train network ids), `docs/missions.md` risks of the train missions,
`docs/design/README.md` decisions, the test plan (`docs/test-plan.md`).

## 11. Decisions for the architect or the user

| # | Decision | Options and what the players get | Recommendation |
|---|---|---|---|
| T-D1 | How trains reach the friend | **Track mirror** (A): the friend's train is a real train on the same track at the host's position, smooth, can be ridden. **Vehicle copies** (B): the carriages are pushed around like cars, they wobble on curves and can tip over. **Replay** (C): only in ported missions, drifts apart | track mirror |
| T-D2 | Random (ambient) trains | **Only the host's game makes them**: both see the same train near the host and can ride it together; a friend alone in another city sees no ambient trains. **Each game makes its own** (today): each sees trains the other doesn't, they can't ride one together, and a train can hit you that the other never sees. **None in co-op**: no ambient trains at all, and the freight side mission can't start (it needs one) | only the host's |
| T-D3 | A random train the friend rides far from the host | **Kept** while the friend is near it: the friend can ride across the map; meanwhile a second ambient train may appear near the host. **Removed** when far from the host: the train vanishes under the friend | kept |
| T-D4 | Who moves a train a player drives | **Always the host's game**: when the friend drives, the train reacts to the friend's keys about a tenth to a quarter of a second late (a heavy train hides it); everyone sees the same train. **The driver's game**: no delay for the driver, more code (handing the train over), more ways to go wrong | the host's game |
| T-D5 | The friend's place in #16 Catalyst | **In Ryder's Picador** (passenger seat or the bed): sees the train, CJ on it, the crates flying and caught (the fight at the yard before it is fought together as usual). **On the train with CJ**: needs package T3 and a port command to put the friend there; the friend still can't throw crates (only the host's throws count) | in the Picador for group 2 |
| T-D6 | Trains made by the friend's own side missions (the quarry mission run by the friend) | **Only on the friend's screen** (for now). **Synced with the friend as their owner**: later, more code | only the friend's screen |
| T-D7 | Players standing on a moving train (T3) | **Before #74 Green Goo** (when the friend has to land on a train). **With group 2** (only if T-D5 puts the friend on the train) | before #74, unless T-D5 says otherwise |
| T-D8 | The "skip to the next station" button for the friend riding inside a passenger carriage | **Forwarded to the host**: the train moves to the next station for everyone aboard, and game time passes for both. **Off for the friend**: the button does nothing for them | forwarded |
| T-D9 | When the host leaves | **The friend's trains disappear** (a friend aboard is left standing where the train was). **They stay frozen** on the tracks until the host is back | disappear |
