# World-state registry (design)

Status: design only (2026-10-16, base `main` f5cffae, 0.3.103-alpha). Nothing here is implemented. Scope agreed with
the user: the state that sets the rules of the host's world is defined in one place in client code and reaches the
other players by itself, when it changes and when someone joins. The mission mirror layer
(`docs/design/mirror-layer.md`, designed in parallel) covers what the host's mission shows on screen; section 3.9
lists what both designs share. The architect's decisions and the merged plan are in `README.md` in this folder; it
replaces D8's story guard with host pinning on the server.

"Host" is the player whose game runs the story (the first player to connect). "The other player" or "the friend"
is everyone else; with two players that is one game.

## 1. Goals and non-goals

Goals:

- A client module, `CWorldStateRegistry`, made of entries. Each entry reads one piece of engine state on the host,
  detects changes by polling, serializes it, applies it on the other games and applies it again when their own
  scripts or engine change it back.
- Close the known gaps: the bridge barriers and roads (needed for #28 The Green Sabre), a late joiner's maximum
  wanted level, gang zone strengths. Also the gaps the inventory below found: mod shops closed for the friend since
  #10, shops closed again after the friend restarts their game, gang wars from #25, mission settings (traffic,
  military zones, wanted multiplier) left over on the friend after a mission ends, the destroyed crack factory and
  the Deconstruction site in San Fierro, the riots.
- Follow the project rules: no MAIN, global, external-script or SCM change; the host's save is not touched; the
  friend's own progression (money, stats, weapons, clothes, their MAIN flags) is not touched.
- No new hook sites. Polling plus the existing load hook (0x53C70B) are enough, so `tools/audit_hooks.py` keeps its
  baseline.

Non-goals:

- Mission HUD, entity blips, objective texts, checkpoints, timers: the mirror layer.
- Per-player state: money, respect, stats, weapons, clothes (`CPlayerStateSave`), MAIN's story flags (per player by
  decision). The registry never writes a script variable.
- Entities: vehicles and peds keep their entity sync; pickups and car generators get no entry (2.3).
- Rewriting upstream syncs that work: tags (`CTagSync`), static radar blips (`CNetworkStaticBlip`), weather and time
  (`CWeatherSync`), moon (`CMoonSync`). EnEx (`CEntryExitMarkerSync`) gets an adapter, not a rewrite.

## 2. Inventory

Method: the save file's blocks (gta-reversed `CGenericGameStorage::GenericSave`, 0x5D13E0) list the engine state a
single-player game keeps, which is the checklist of candidate world state. Each block was matched against the
commands that change it in `CoopAndreas/scm/scripts/*.txt` (classified by `scm/main.txt`: MAIN section = the files
included before `INITIAL.txt`, missions, external scripts) and against the 1.0 US code (addresses and sources in
3.3). Mission numbers are those of `docs/missions.md`.

Two facts frame every row:

- Every client runs MAIN from a new game: MAIN's init block, missions 0–2 (`INITIAL`, `INITIL2`, `INTRO`, launched
  unconditionally at `MAIN.txt` lines 50, 52, 387) and the threads they start (`ODDVEH`, `R3`, `GYM`, `SHOOT`,
  `BLOODR`, `HOTR`, `KICKS`, `PSAVE1`, `FLOW`, `HELP`, `COLLS`, `CRANES`, `BUY_PRO`, `VALET_L`, `ADPLANE`,
  `TRAINSL`, `IMPND_L`, `OPENUP`, `TRI`, `APCHECK`, `GFINIT`, `HJ`, `CELLFON`; `INTRO`'s cleanup starts `INT`,
  `MOB_LA1`, `MOB_RAN`, `INTROST`). Everything else (strand threads, phone scripts such as `MOB_CAT`/`MOB_SF`,
  `BUY1`–`BUY3`, mission-passed blocks) runs only on the host, because only the host's missions start it.
- The friend's MAIN keeps its new-game flags (`$flag_*_mission_counter` = 0). Its threads mostly wait on those flags
  and do nothing. The friend's `$onmission` follows the host's (`client/src/PacketHandlers/scripts.cpp`,
  `ON_MISSION_FLAG_SYNC`).

### 2.1 World state (entries)

W = world (host wins), M = world but mission-scoped (reset by the engine at every mission end).

| # | State | Changed by (story point) | Read on the host | Apply on the friend | Friend's own scripts and engine | Needed by |
|---|---|---|---|---|---|---|
| W1 | City unlock, int stat 181 (W) | `MAIN`/`INTRO` 0 at a new game (every client); `LA1FIN2` 1 (#28), `SYN7` 2 (#61), `CASIN10` 3 (#91), `FINALEC` 4 (#103) | `CStats::GetStatValue` | `CStats::SetStatValue` | own `MAIN`/`INTRO` write 0 at a new game | done (`CCityUnlockSync`), moves into the registry |
| W2 | Maximum wanted level (W; sets the chaos cap too) | `MAIN` 6, then 4 (every client); `MOB_CAT` 5 (host only; the call after the Badlands strand, before #39); `DESERT4` 6 (#69); missions set 0 and restore their saved value (synced missions replay `01F0`) | `CWanted::MaximumWantedLevel` 0x8CDEE4 | `CWanted::SetMaximumWantedLevel` 0x561E70 | own `MAIN` writes 4 at a new game | late joiner now; #39 / #69 |
| W3 | Script IPL groups `BARRIERS1`, `BARRIERS2`, `CRACK`, `CARTER`, `TRUTHSFARM` (W) | `INTRO` requests `BARRIERS1`/`2` (every client); `MAIN` requests `CRACK`; `LA1FIN2`'s cleanup removes `BARRIERS1` on pass and on fail (#28); `TRUTH2` requests `TRUTHSFARM` at its start and removes it discreetly in its cleanup (#39); `SYN7` removes `CRACK` and `BARRIERS2` (#61; its `FUKDCRAK` is not in 1.0's IMG); `FINALEA` requests `CARTER` on pass and removes it discreetly on fail (#101) | `IplDef` flags by slot | `CIplStore::RequestIplAndIgnore` / `RemoveIplAndIgnore` / `RemoveIplWhenFarAway` | own `INTRO`/`MAIN` request them again at a new game | **#28** |
| W4 | Road and ped-path switches: `ThePaths`' node switch list (W, includes mission switches) | `INTRO` switches 34 bridge boxes off (every client); `MAIN` the Grove Street cul-de-sac; missions switch boxes on/off/back to original (group 1: `INTRO1`, `SWEET1B`, `HOODS5`), the end-of-mission cleanup cuts the list to 54 entries; `SYN7` switches the bridges on (#61) | list count + 64 × 0x1C bytes | restore own boxes, replay the host's list, copy the list | own `INTRO` switches the bridges off at a new game | now (mission switches); #61 |
| W5 | Garages: type and active flag (W) | `INITIAL` deactivates the hideouts and sets the mod-shop types; `INTRO` deactivates the mod shops; `SWEET6` activates Loco Low Co and Transfender LS (#10); `TRUTH2` Transfender SF (#39); `STEAL1` Wheel Arch Angels (#62); `MOB_SF` Transfender LV (host only); `BUYPRO1` the hideouts of bought properties; `DSKOOL` off/on during the mission (#43). Doors opened and closed by missions are not mirrored (each game opens doors for its own player) | `aGarages[i]` type + inactive bit | `ActivateGarage` / `DeActivateGarage` / `ChangeGarageType` | own `INITIAL`/`INTRO` at a new game; own side mission `BURGJB` changes types | now (#10 shipped) |
| W6 | Zone info: gang strengths, population type, no-cops flag, races (W) | `INITIAL` (all zones, every client), `INTRO` (GAN1/GAN2), `INTRO2` #3, `SWEET1` #4, `SWEET1B` #5, `SWEET2` #7, `CRASH4` #25, `SWEET7` #26, `LA1FIN2` #28 (117 writes), `MOB_CAT` (host), `HEIST9` #92, `MANSIO3` #95, `GROVE1` #97, `GROVE2` #98, `RIOT2` #100; the host's gang wars | 12 of the 17 bytes per zone (2.2) | write, then `CTheZones::FillZonesWithGangColours` | own `INITIAL`/`INTRO`; own gang wars (W7) | cosmetic from #3; **#25** |
| W7 | Gang wars active, and wars run only by the host (W) | `CRASH4` on (#25; off again if it fails), `LA1FIN2` off (#28), `MANSIO3` on and off during (#95), `GROVE1` on (#97), `GROVE2` on (#98) | `CGangWars::bGangWarsActive` | `CGangWars::SetGangWarsActive`; on the friend `Provocation` and `TimeTillNextAttack` are held so no war starts there (D3) | without that, the friend's kills start wars in the friend's game and its wins change zones | **#25** |
| W8 | Gang weapons (W) | `RYDER2` Grove pistol/Tec-9 (#17); `COLLS` Grove Desert Eagle/MP5/knife when all tags are sprayed (runs on both games, tags are shared) | `CGangs::Gang[g]` weapons | `CGangs::SetGangWeapons` | own `COLLS` gives the same reward | #17 (cosmetic) |
| W9 | LA riots (W) | `INITIAL` off; `RIOT1` on (#99); `FINALEC` off (#103) | `gbLARiots` | write | none | #99 |
| W10 | Entry/exit flags (W) | `INTRO` closes the shops (every client); missions open them (`INTRO2`, `SWEET1B`, `CRASH4` Ammu-Nation…); `OPENUP`, `GYM`, `ENTEXT` (MAIN); `BUYPRO1` safehouse doors; a save load | upstream: sent after any host script runs an EnEx command, the server replays the last one to joiners | upstream `Receive`, plus a masked re-apply | own `INTRO` closes the shops again after the join replay; own `OPENUP`, `GYM` | **now** |
| W11 | Building swaps and invisible map objects (W) | `MAIN` shows the intact crack factory (every client); `DECON` swaps the Hub hole models (#42); `SYN7` shows the destroyed factory (#61); `ZERO1` (temporary) | `CTheScripts::BuildingSwapArray`, `InvisibilitySettingArray` | find the same building/object by model and position, swap or hide it | own `MAIN` at a new game | #42, #61 |
| W12 | Mission-scoped engine settings (M): car and ped density, random gang/cop creation, all-random-peds-this-type, no/free resprays, military zones, the player's wanted multiplier, random trains, ambient planes, cop bikes, forced weather | every mission; the engine resets them at every mission end (`CMissionCleanup::Process` 0x468560), which never reaches the friend; synced missions replay only some (`01EB`, `03DE`, `06D0`, `0A24`, `03C7`) | the globals (3.3) | write / engine setters | own side missions (taxi, vigilante…) set them too | **now** (a failed mission leaves the friend at its last replayed density) |
| W13 | Ped type relationships (W) | `MAIN` defaults (every client); `SWEET7` Grove respects the player (#26); `MANSIO3` (#95); mission ped types are cleared at every mission end | `CPedType` acquaintances | write | none after the new game | #26 (cosmetic) |

### 2.2 Notes per entry

- **W3, IPL groups.** Contents read from `C:\Games\GTASA_Coop\models\gta3.img` (read only): `barriers1` has 8
  roadblocks (models 4504–4509, 4521, 4522) on the roads out of Los Santos into Flint County and from Flint County
  towards San Fierro (x 14…56, y −1531…−1019; x −982…−954, y −410…−271; two at Flint, `CE_Flint*`); `barriers2` has
  16 (4510–4520, 4523–4527) on the Gant Bridge, the bridges east of San Fierro, the bridges from Red County into
  Bone County and the two bridges into Las Venturas (`CE_Makospan1ld`, `CE_Fredbar*`); `crack` 60 crack-factory
  pieces; `carter` 2; `truthsfarm` 80 greenhouses and plants. None has car generators. `fukdcrak` is in no IMG:
  `FindIplSlot` returns −1 and the vanilla `0776` handler passes the −1 on; the entry skips names without a slot.
  While a request is still streaming, the group reads ABSENT (the save's own definition of "requested" is loaded
  and dynamic streaming off); the host then sends PRESENT once it has loaded, and a repeated request on the friend's
  side is harmless.
- **W4, roads.** The engine keeps one list of switched boxes and replays it whenever a path area streams in
  (gta-reversed `LoadPathFindData`), so the node state of unloaded areas follows the list. R*'s removal of contained
  boxes copies only one element (gta-reversed marks it as a bug); mirroring the list byte for byte reproduces the
  host exactly, bug included.
- **W5, garages.** Bit 0x80 of the flags (resprays always free) belongs to Michelle's garage (`GFAGNT`, a girlfriend
  perk, per player) and is masked out. Hideout garages hold the cars stored in them (`aCarsInSafeHouse`) per game; a
  car the friend stores lives only until the friend quits (their game never saves).
- **W6, zones.** Mirrored per zone: `GangStrength[10]` (bytes 0–9), the population-type and no-cops bits of byte 15,
  the race nibble of byte 16. Not mirrored: `DealerStrength` (byte 10; 1.0's `CPopCycle::UpdateDealerStrengths`
  0x6104B0 adds 1 at random once per game minute while gang wars are active, on each game), `ZoneColor` and the
  radar-mode bits (computed from the strengths by `FillZonesWithGangColours`). The territory map draws only while
  `bGangWarsActive` is set, so W6 is visible on the friend's map only with W7.
- **W7, gang wars.** 1.0's `CGangWars::Update` (0x446610) starts an offensive war when `Provocation` exceeds 2.1
  and a defensive one when `TimeTillNextAttack` drops below 0. Holding both on the friend's game keeps all wars on
  the host; the attackers the host's game spawns are network peds the friend can fight (that their kills count for
  the host's war is untested).
- **W10, EnEx.** `switch_entry_exit` (`07FB`, handler 0x4730A8) sets bit 0x4000 (enabled) through
  `SetEnabledByName` (0x43F9B0). The engine sets bits 0x0008 and 0x2000 on transitions for the local player
  (gta-reversed `EntryExit.cpp`); the re-apply ignores them.
- **W11.** Both crack factories (`crackfact_SFS`, `CF_ext_dem_SFS` and their LODs) and `hubhole1_SFSe` are placed
  by the static text IPL `data\maps\SF\SFSe.ipl`, so the engine's arrays hold them (it skips streamed buildings) and
  they keep the same pool slot in both games.

### 2.3 State that stays per player or gets no entry

| State | Why |
|---|---|
| Money, respect, stats, weapons, clothes, fighting style, perks | per player by decision; the friend's are kept with the host's save (`CPlayerStateSave`) |
| MAIN flags and what they gate: Ammu-Nation stock, clothes on sale, girlfriends and their perks (`GFAGNT`), schools, the pilot licence and the airport gates (`APGATE`), phone calls (`MOB_*`), `OPENUP`'s San Fierro and Las Venturas properties | per player by decision. These are visible gaps for the friend; listed for later decisions, not world state |
| Story reward pickups: CJ's spray can (`SWEET1`), Emmet's pistol (`SWEET2`, `HOODS5`), the territory cash (`CRASH4`, `TCASH`), the `COLLS` rewards, save points | pickups exist per game and give per-player items or money (D6) |
| Properties for sale (`OPENUP`, `BUY1`–`BUY3`, `BUY_PRO` → `BUYPRO1`) | the host buys; the doors and garages of the host's properties reach the friend through W10 and W5. A purchase on the friend's game spends their money and is lost when they quit (D4) |
| Car generators | each game spawns its own; the host's story generators exist only on the host, whose cars reach the friend through entity sync. Upstream's blockage hook (`VehicleHooks.cpp`, 0x6F35D6/0x6F35FF) avoids most duplicates |
| Persistent script objects: the doors, gates, cranes and site cabins `INITIAL` creates with `Object.DontRemove` on every client (Four Dragons door, Chinatown gate, Area 69 doors, freighter door, `$riot2_door`, the Carter wall, the Deconstruction cabins…), which missions open, move, delete or rebuild (`FINALEA` rebuilds `$carterwall` on fail) | objects are entities and CoopAndreas has no object sync, so the friend's copies stay as `INITIAL` made them. They need an object-sync design or the mirror layer during missions, not a registry entry (D11). First visible at #42 (`DECON` breaks and rebuilds the cabins; cosmetic); a door a mission opens can block the friend from #60 (freighter door) and #73 (Area 69) on; also #92, #100, #101 |
| Save points, restart points | the host saves; restart points are filtered by W1; the girlfriend extra points are per player |
| Map exploration (`ZonesVisited` 0xBA3730, `ZonesRevealed` 0xBA372C) | each player's exploration |
| Wardrobe ownership (`CShopping`), stunt jumps, horseshoes/oysters/snapshots | per player |
| Weather and time, moon, tags, static radar blips | already synced by upstream (`GAME_WEATHER_TIME` every 2 s, `UPDATE_MOON_SIZE`, `TAG_UPDATE`/`UPDATE_ALL_TAGS`, `CREATE_STATIC_BLIP`); the one gap, forced weather, is part of W12 |

Do not add the world commands (`076C`, `0776`–`0778`, `01E7`/`01E8`/`022A`/`022B`/`091D`/`091E`, `0299`/`02B9`,
`0879`, `06C8`, `0237`, `03B6`, `0363`) to `COpCodeSync`: replay reaches only synced missions, misses joins, saves
and resets, and would apply the same change twice. The registry covers every script and the engine.

## 3. Architecture

### 3.1 Module and entries

New files (private build, new code in new files; flat in `client/src` like `CCityUnlockSync` and
`CPlayerStateSave`, so `xmake.lua`'s `client/src/*.cpp` already picks them up):

- `client/src/CWorldStateRegistry.{h,cpp}`: the entry list, `Process()`, lifecycle calls, packet handling, the
  apply gate, the story guard, diagnostics.
- `client/src/CWorldStateEntry.h`: the entry base class.
- `client/src/CWorldStateEntries*.cpp`: the entries, one file per subsystem.
- `client/src/CWorldStatePolicy.h`: the logic without GTA (blob diff, record deltas, per-entry state, gate and
  guard decisions), so `tests/world_state_test.cpp` can test it like `player_state_test.cpp`.

Changed: `shared/network/packets/world.h` (the `WorldState` packet replaces `UpdateCityUnlocked`, 3.5),
`shared/network/packet_types.h` (the renamed enum value and its string), `server/src/PacketHandlers/world.cpp` (its
host-only relay).

Small edits: `Main.cpp` (`CCityUnlockSync::Process()` becomes `CWorldStateRegistry::Process()`),
`PacketHandlers/system.cpp` (join and host change), client `PacketHandlers/world.cpp` (handler),
`Hooks/SaveLoadHooks.cpp` (one call after a successful load). `CCityUnlockSync.*` is removed (D1).

The entry contract: every entry turns its engine state into a canonical byte blob (fixed layout, engine-driven and
per-player bits masked out, a small layout header) and can make the local engine match a desired blob. Equal blobs
mean equal state, so one comparison serves both change detection on the host and re-apply on the other games.

```cpp
// private build: world-state registry entry
class CWorldStateEntry
{
public:
    virtual ~CWorldStateEntry() = default;
    virtual uint8_t Id() const = 0;                    // wire id, never reused
    virtual const char* Name() const = 0;              // diagnostics
    virtual uint32_t IntervalMs() const = 0;           // host poll and re-apply check
    virtual uint16_t RecordSize() const { return 0; }  // > 0: FULL blobs can be patched by RECORDS
    virtual bool SuspendDuringOwnMission() const { return true; }
    virtual bool Init() = 0;                           // resolve slots and names; false disables the entry
    virtual void Capture(std::vector<uint8_t>& blob) = 0;
    virtual void Apply(const std::vector<uint8_t>& desired, const std::vector<uint8_t>& local) = 0;
};
```

The registry (game thread only; packet handlers already run there):

- **Host.** Every `IntervalMs`: `Capture`; if it differs from the blob last sent, send it (FULL, or RECORDS of the
  changed records when the entry has records and fewer than half changed); remember it. A pending-full flag (join,
  became host, game restart) forces FULL.
- **Other players.** FULL replaces the desired blob, RECORDS patch it (dropped without a FULL base). Every
  `IntervalMs`, and right after a packet: if the gate is open, `Capture` the local state; if it differs from the
  desired blob, `Apply`, count it, log it (rate-limited).
- **Layout header.** Each blob starts with the record count and an FNV-1a hash of what identifies the records
  (IPL names, garage names, zone labels in index order, EnEx count). A mismatch disables that entry on that game
  and logs once, instead of writing into the wrong slots. Both games normally load the same data from the same
  package.

Apply gate on the other players: authenticated and a host is known; the story guard passes (3.6); the game runs
(frame counter advanced since the last restart, the player ped exists); for entries with
`SuspendDuringOwnMission`, no mission script of this game runs (`CTheScripts::pActiveScripts`,
`CRunningScript::m_bIsMission`, as `CPlayerStateSave` checks). That waits for the friend's own `INITIAL`/`INITIL2`/
`INTRO` at a new game and keeps the registry out of the friend's own side missions. W1 and W2 skip that last
condition (cheap, and `INTRO`'s writes are corrected at once, as `CCityUnlockSync` does today).

### 3.2 Entries

| Id | Entry | Blob | Interval | Apply |
|---|---|---|---|---|
| 0 | `CityUnlock` (W1) | u8 | every frame | `SetStatValue(181)` |
| 1 | `MaxWantedLevel` (W2) | u8 | every frame | `SetMaximumWantedLevel` |
| 2 | `IplGroups` (W3) | per known name: PRESENT (loaded and dynamic streaming off), PENDING (removal when far away), ABSENT | 500 ms | `RequestIplAndIgnore` / `RemoveIplWhenFarAway` / `RemoveIplAndIgnore`; PENDING and ABSENT count as equal |
| 3 | `RoadSwitches` (W4) | count + count × 0x1C | 1 s | `SwitchRoadsOffInArea(box, …, backToOriginal)` for each own box, then each host box in order, then copy the list and count |
| 4 | `Garages` (W5) | per garage: type, inactive bit | 1 s | `ChangeGarageType` when the type differs, then `ActivateGarage` / `DeActivateGarage` |
| 5 | `Zones` (W6) | per zone 12 bytes, `RecordSize` 12 | 1 s | write the fields, then `FillZonesWithGangColours(!bGangWarsActive)` |
| 6 | `GangWars` (W7) | u8 | 250 ms | `SetGangWarsActive`; on the other players every frame: `Provocation = 0`, `TimeTillNextAttack` ≥ 60000 |
| 7 | `GangWeapons` (W8) | 10 × 3 weapon ids | 1 s | `SetGangWeapons` |
| 8 | `MissionSettings` (W12) | the fields of 3.3 | 250 ms | direct writes and engine setters (`ForceWeather` / `ReleaseWeather`, `DisableRandomTrains`, `SwitchAmbientPlanes`) |
| 9 | `BuildingSwaps` (W11) | ≤ 25 × {old model, new model, position} | 1 s | find the building by model near the position, `ReplaceWithNewModel`, update `BuildingSwapArray` |
| 10 | `HiddenObjects` (W11) | ≤ 20 × {type, model, position} | 1 s | find the entity, hide it (`SetIsVisible(false)`, no collision), update `InvisibilitySettingArray`; one that left the host's array is shown again |
| 11 | `LaRiots` (W9) | u8 | 1 s | write `gbLARiots` |
| 12 | `PedRelationships` (W13) | the acquaintance masks | 1 s | write |
| — | `EntryExits` adapter (W10) | no wire id: upstream's `ENEX_SYNC` packet stays | 1 s | host: send `CEntryExitMarkerSync::Send()` when a masked hash of all flags changed without an EnEx command (save load); others: compare each EnEx with `ms_lastData` under the mask, re-apply the differing ones keeping bits 0x0008 and 0x2000 |

### 3.3 1.0 US addresses and their sources

"bytes" = read from `C:\Games\GTASA_Coop\gta_sa.exe` with dumpbin or `tools/pe_peek.py`; ReVa decompiles and
cross-references explain the code. Rows marked *verify* come from one reference only; their work package checks them
in the exe before use.

| Address | What | Source |
|---|---|---|
| 0x558E40 / 0x55A070 | `CStats::GetStatValue` / `SetStatValue` | existing code (`CCityUnlockSync.cpp`, `CPlayerStateSave.cpp`), plugin-sdk |
| 0x480D75 | `0318` register_mission_passed: writes `LastMissionPassedName` 0xB78A00, `IncrementStat(147)` (call 0x55C180) | bytes (jump tables 0x480F10/0x480FD8 of `ProcessCommands700To799` 0x47FA30) |
| 0x8CDEE4 / 0x8CDEE8 | `CWanted::MaximumWantedLevel` (6 in the exe) / `MaximumChaosLevel` (9200) | plugin-sdk, bytes |
| 0x561E70 | `CWanted::SetMaximumWantedLevel(int)`, cdecl | plugin-sdk; the `01F0` handler calls it at 0x47DEAA (ReVa) |
| 0x8E3FB0 | `CIplStore::ms_pPool`: 256 `IplDef` of 0x34 bytes | gta-reversed `IplStore.cpp`; bytes (`mov ecx,[8E3FB0h]` at 0x156A140; `CIplStore::Load` 0x5D54A0 loops to 0x3400) |
| `IplDef` +0x2D / +0x2F / +0x30 | loaded / dynamic streaming disabled / remove when deleted | gta-reversed `IplDef.h`; bytes (`CIplStore::Load`, `RemoveIplAndIgnore` body 0x156A16F/0x156A173) |
| slot + 25255 | IPL streaming model id | bytes (`add eax,62A7h` at 0x156A161) |
| 0x404AC0 | `CIplStore::FindIplSlot` (−1 when missing) | gta-reversed; the `0776` handler (ReVa) |
| 0x405850 / 0x405890 / 0x4058D0 | `RequestIplAndIgnore` / `RemoveIplAndIgnore` / `RemoveIplWhenFarAway`: `jmp` stubs into `.HOODLUM` | gta-reversed; bytes; called by the `0776`/`0777`/`0778` handlers at 0x46B6ED/0x46B715/0x46B73D (ReVa) |
| 0x96F050 | `ThePaths` | plugin-sdk (operand at 0x40CA27); bytes (0x40CA27 holds 0x0096F050); gta-reversed |
| `ThePaths` +0x3568 / +0x356C | switch count (u32) / 64 entries of 0x1C {xMin, xMax, yMin, yMax, zMin, zMax, off, cars} | bytes (`CPathFind::Save` 0x5D34C0); plugin-sdk (`m_dwNumForbiddenAreas`, `CForbiddenArea`); gta-reversed (`CNodesSwitchedOnOrOff`) |
| 0x452C80 | `CPathFind::SwitchRoadsOffInArea(this, xMin, xMax, yMin, yMax, zMin, zMax, off, cars, backToOriginal)` | gta-reversed; called by `01E7` (0, 1, 0) at 0x47DC30, `01E8` (1, 1, 0) at 0x47DD14, `091D` (0, 1, 1) at 0x476AB8; ped roads via 0x1565860 with cars = 0 (ReVa) |
| 0x44D3B0 | `TidyUpNodeSwitchesAfterMission`: count capped at 54 | bytes (body 0x156DE10: `cmp eax,36h`); called from the mission cleanup at 0x468678 |
| 0x96C048 | `CGarages::aGarages`, 0xD8 each: type +0x4C, door +0x4D, flags +0x4E (0x02 inactive, 0x80 resprays always free) | gta-reversed `Garages.h`/`Garage.h`; bytes (the two bodies below) |
| 0x447CB0 / 0x447CD0 | `CGarages::DeActivateGarage` / `ActivateGarage` (int16) | gta-reversed; bytes (bodies 0x1563E70 `or [eax+4Eh],2`, 0x15677F0 `and [eax+4Eh],0FDh`) |
| 0x96C024 / 0x4476D0 / 0x447680 | `NumGarages` / `ChangeGarageType` / `GetGarageNumberByName` | gta-reversed (*verify*) |
| 0x96C008 / 0x96C009 | `CGarages::NoResprays` / `RespraysAreFree` | gta-reversed; bytes (the cleanup writes 0x96C008 at 0x46861A) |
| 0xBA1DF0 / 0xBA1DE8 | `CTheZones::ZoneInfoArray` (0x11 each) / `TotalNumberOfZoneInfos` (int16) | gta-reversed `TheZones.h`; bytes (`CTheZones::Save` 0x5D2E60: `imul ecx,11h; add ecx,0BA1DF0h`) |
| 0xBA3798 (+0x1C) | navigation zones, 0x20 each; u16 zone-info index at +0x1C | gta-reversed; the `076C` handler in `ProcessCommands1900To1999` 0x46B460 (ReVa). plugin-sdk names 0xBA3798 `CTheZones::ZoneInfoArray`: that name is wrong |
| 0x572440 | `CTheZones::FillZonesWithGangColours(bool disableRadarGangColours)` | plugin-sdk, gta-reversed; bytes (call at 0x4685E9) |
| 0x6104B0 | `CPopCycle::UpdateDealerStrengths` | gta-reversed; ReVa (1.0 checks the minute change the other way round than gta-reversed) |
| 0x96AB90 / 0x96AB5C / 0x96AB48 / 0x96AB64 / 0x96AB4C | `CGangWars::bGangWarsActive` / `Provocation` / `TimeTillNextAttack` / `State` / `State2` | gta-reversed `GangWars.h`; bytes (`CGangWars::Update` 0x446610) |
| 0x446570 | `CGangWars::SetGangWarsActive(bool)` | plugin-sdk; the `0879` handler calls it at 0x471A41/0x471A52 (ReVa) |
| 0xC091F0 | `CGangs::Gang[10]`, 0x10 each, weapons `int[3]` at +4 | plugin-sdk (`CGangs.cpp`, `CGangInfo.h`), gta-reversed |
| 0x5DE550 | `CGangs::SetGangWeapons(int16, int, int, int)` | plugin-sdk; the `0237` handler calls it at 0x47ED26 (ReVa) |
| 0xB72958 | `gbLARiots` | gta-reversed `Game.h`; bytes (the `06C8` handler writes it at 0x497CEE) |
| 0xA44A30 / 0xA449E0 | `CTheScripts::BuildingSwapArray` (25 × {`CBuilding*`, new, old model}) / `InvisibilitySettingArray` (20 × `CEntity*`) | gta-reversed `TheScripts.h`; used by `CTheScripts::Save` 0x5D4C40 / `Load` 0x5D4FD0 (ReVa) |
| 0x481140 / 0x481200 | `AddToBuildingSwapArray` / `AddToInvisibilitySwapArray` | gta-reversed (relocated bodies 0x1569840 / 0x156DF60 per ReVa; *verify* the callable entries) |
| 0x43F9B0 | `CEntryExitManager::SetEnabledByName` → `SetEntryExitFlag(name, 0x4000, on)` | bytes (`07FB` handler 0x4730A8); ReVa |
| 0xC81318 / 0x72A4E0 / 0x72A510 | `CWeather::ForcedWeatherType` / `ForceWeather` / `ReleaseWeather` | plugin-sdk (*verify*) |
| 0x468560 | `CMissionCleanup::Process` (run by `00D8`, call at 0x469471): ped/car density 0x8D2530/0x8A5B20 = 1.0; 0xC0FCB2–0xC0FCB4 (no random gang members, only gang members, no random cops) = 0; 0x8D2534 (all random peds of one type) = −1; `NoResprays` = 0; `CCullZones::bMilitaryZonesDisabled` 0xC87ACD = 0; `FindPlayerWanted(-1)`+0x14 = 1.0; `DisableRandomTrains(0)` 0x6F5DB0, `SwitchAmbientPlanes(1)` 0x6CCC50, `DisableCopBikes(0)` 0x407D10, `ReleaseWeather` | bytes; ReVa (the names of 0xC0FCB2–4 and 0x8D2534 are *verify*) |
| 0xA8B42C, +0xDC | `CTheScripts::pActiveScripts`, `CRunningScript::m_bIsMission` | existing code (`CPlayerStateSave.cpp`) |
| 0x53C70B | existing `RedirectCall` on `GenericLoad` | existing code (`SaveLoadHooks.cpp`) |

`CPedType` acquaintance storage (W13) is not checked yet; WS-7 finds it in gta-reversed `PedType.cpp` and checks
it in the exe.

### 3.4 Hooks

None new. The registry runs from `Main.cpp`'s per-frame `gameProcessEvent` (where `CCityUnlockSync::Process`
runs today), learns about joins and host changes from the existing packet handlers, about restarts from
`CTimer::m_FrameCounter` going back (the way `CPlayerStateSave` does), and about loads from one call added to the
existing `GenericLoad` hook at 0x53C70B. Engine functions are called on the game thread, as their script commands
call them. No `RedirectCall`, `RedirectJump` or `Nop` site is added; the audit baseline does not change.

### 3.5 Packets and server relay

One generic packet, `WORLD_STATE`, on the `EVENT` channel (reliable, ordered). It takes the enum position of
`UPDATE_CITY_UNLOCKED`, which it replaces (D2), so the numbers of the packet types after it do not move:

| Field | Type | Meaning |
|---|---|---|
| `entry` | u8 | entry id (3.2) |
| `kind` | u8 | 0 FULL, 1 RECORDS |
| `hostMissionsPassed` | u16 | the host's int stat 147 (story guard) |
| `size` | u16 | payload size, ≤ 8000 (client and server serialize into 10 KB buffers, `CPacketFactory.cpp`) |
| `data` | bytes | the blob, or {u16 record index, record bytes} pairs |

An entry that ever needs more than 8000 bytes splits its FULL into numbered parts, and the receiver uses it only once
every part has arrived. None does today: the largest is zones, at most 380 × 12 bytes plus the header (about
4.6 KB).

Server (`server/src/PacketHandlers/world.cpp`): relay `WORLD_STATE` to everyone but the sender, only when the sender
is the host (the pattern of `UPDATE_ALL_TAGS` and today's `UPDATE_CITY_UNLOCKED`). No cache: the host's live state
is the only source. A packet still in flight from a player who stopped being host is dropped by this check.

Clients with an older build are refused at connect ("Version mismatch"), so the wire change needs the version bump
of the next package, and the friend needs that package.

### 3.6 Lifecycle

- **Join.** The host's `PLAYER_CONNECTED` handler (host branch, where `CCityUnlockSync::SyncCurrentState()` is
  called today) marks every entry for FULL. The FULLs go to everyone; the players already in apply the same values
  again, which changes nothing. The joiner usually receives them before its own new-game scripts run; those undo
  some of it, and the re-apply restores it once the joiner's `INTRO` is over.
- **Host change.** On `PLAYER_ASSIGN_HOST` for this player: drop all desired blobs (this game is the authority now)
  and mark every entry for FULL. For another player: drop the blobs last sent and the desired blobs, and wait for
  the new host's FULLs.
- **Disconnect.** Drop desired blobs and blobs last sent; the engine state stays as it is (no rollback). A reconnect
  is a join.
- **Host restart (new game or load).** Detected by the frame counter or the load call; every entry is marked for
  FULL once the host's game has run for 2 s, so the loaded or reset world goes out complete. Loading an older save
  rolls the friend's world back with it, which is "host wins".
- **The friend restarts.** Desired blobs stay; the gate waits for the friend's `INTRO`, then the re-apply puts the
  host's world back.
- **Story guard (D8).** A game that is not the host applies nothing while `hostMissionsPassed` is clearly below its
  own stat 147 (by more than a margin, about 5), and says once in chat that the host's story is behind its own.
  Stat 147 rises with every `register_mission_passed` (`0318`); that command is in `COpCodeSync`'s list, so in synced
  missions the friend's count rises with the host's, and a friend who started a new game this session stays below
  the host, whose save holds every pass. The margin covers the friend's own passes: `STUNT` (2) and `CPRACE` races
  (`HOTR`, `KICKS` run on every client) use the same command. Without the guard, a friend who connects first becomes
  host and overwrites the story owner's world (today this already happens to the city unlock), and a save made then
  keeps the wrong world.

### 3.7 Bandwidth and cost

A join or host restart sends about 7 KB once (zones 4.6 KB, roads up to 1.8 KB, the rest under 0.6 KB), in the
frame after the request. Changes are rare and small: a mission end sends tens to hundreds of bytes (`LA1FIN2`'s
zone changes are about 60 records, under 1 KB). Polling compares about 10 KB of memory per second in total.

### 3.8 Rules and conflicts

Rules: the registry writes engine state only on games that are not the host, never a script variable, never MAIN,
never a file. The host's save is unchanged, so saves stay loadable both ways. A save the friend makes has their stale
story and the mirrored world; it was never usable as the host's game (`docs/mission-porting.md`, saves). With the
story guard, a wrong host cannot overwrite the story owner's world.

How "host wins" is enforced without breaking the friend's own progression: their progression is per-player state,
which the registry does not touch, and their MAIN flags, which it does not write, so the friend's MAIN keeps running
on its own flags. Where the friend's MAIN turns its flags into world changes, the host's MAIN runs the same code on
newer flags and its result reaches the friend anyway. The conflicts:

| Source on the friend's side | Effect | Handling |
|---|---|---|
| New game: MAIN init, `INITIAL`, `INITIL2`, `INTRO` | put most entries back to the start of the story (barriers, bridge roads, zones, shops, garages, max wanted 4, city 0) | gate waits for the friend's missions to end, then re-apply; W1/W2 at once |
| `OPENUP` (MAIN) | reads `$Return_cities_passed`, which MAIN copies from stat 181 every frame (`MAIN.txt` 408), so with W1 it opens Badlands and desert properties for sale, and the clothes-shop doors (gated by stale flags for San Fierro and Las Venturas) | the doors are host-won through W10; the for-sale pickups are D4 |
| `GYM`, `ENTEXT`, `COLLS`, `PSAVE1`, `BUY_PRO` (MAIN) | gym doors, delayed door switches, tag rewards, save pickups, purchases | doors through W10; `COLLS` gives the same reward as on the host; save pickups per player; purchases D4 |
| `MOB_LA1`, `MOB_RAN`, `FLOW` (started on every client) | every call and gate waits on mission counters that stay 0 on the friend; nothing fires, no strand starts | none needed |
| Friend's own side missions (`ODDVEH` taxi/vigilante/…, `BURGJB`) | change W5 types and W12 settings for their own mission | entries suspend while the friend runs any mission; the host's values return afterwards |
| Friend's engine: gang wars, dealer drift, garage doors, EnEx transition bits, weather between syncs | would diverge | W7 holds the war triggers; `DealerStrength`, doors and the EnEx bits are not mirrored; forced weather in W12 |
| Opcode sync replays (`01EB`, `03DE`, `06D0`, `0A24`, `03C7`, `01F0`) | the same values as the registry | harmless; they act at once during cutscenes, the registry covers resets, joins and loads |

### 3.9 Alignment with the mirror layer

Both designs send host state to the other games. Pieces they should share, for the architect to reconcile:

1. **Host-only relay** on the server: one helper for "from the host to everyone else" and "from the host to one
   player" (the per-player print packets already do the second).
2. **One lifecycle source** on the client, used by both: authenticated, disconnected, host changed (local or not),
   player joined (host side), game restarted (new game or load, both sides), the host's mission started/ended (the
   host's `$onmission` edge; `ON_MISSION_FLAG_SYNC` on the others), and the helpers "the local game is ready" and
   "a mission of this game runs". The registry needs all but the mission edge.
3. **Join snapshot.** Both push full state from the host's `PLAYER_CONNECTED` handler; world state first, then the
   mission view. On different channels their relative order is not guaranteed; nothing in the registry depends on
   it.
4. **Mission end.** The engine's own end-of-mission reset (`CMissionCleanup::Process`) is world state and belongs to
   one owner. Recommended: the registry mirrors the host's live values (W12, D5); the mirror layer clears what it
   drew (HUD, blips, texts, checkpoints). Upstream's cleanup in the `ON_MISSION_FLAG_SYNC` handler stays.
5. **The host leaves.** The registry stops enforcing and keeps the world; the mirror layer must clear the mission
   view. Both react to `PLAYER_ASSIGN_HOST`.
6. **Wire.** Either one envelope type with a domain byte for both designs, or separate types; packet ids: `WORLD_STATE`
   reuses `UPDATE_CITY_UNLOCKED`'s position, new mirror-layer types go after `PLAYER_STATE_RESTORE`. If both ship in
   one package, one version bump.
7. **Story guard.** Worth sharing: a game that refuses the host's world (3.6) should not show that host's mission
   view either.
8. **Diagnostics.** The same `--coopdiag` conventions: a bounded event log next to the game executable when
   something happens (like `CoopAndreas_ped_sync.log`), and one summary line in `CoopAndreas_diag.log`.
9. **Script objects.** Neither design mirrors objects (2.3). A door or gate a mission opens is mission view if the
   mirror layer takes it on; otherwise it waits for an object-sync design (D11).

## 4. The barriers and the city unlock

### 4.1 What the original does

- **New game (every client).** `INTRO` sets stat 181 to 0, requests `BARRIERS1` and `BARRIERS2`, and switches the
  car roads off in 34 boxes: the Gant Bridge and the other San Fierro bridges, the roads from Los Santos into Flint
  County and Red County, and the bridges into Bone County and Las Venturas (`INTRO.txt` lines 17–58; again at lines
  1220–1227 in the original intro body, which upstream's skip never reaches).
- **#28 The Green Sabre, mission passed.** Stat 181 = 1 (`LA1FIN2.txt` 2962): no more four stars in San Fierro
  (`CGameLogic::SetPlayerWantedLevelForForbiddenTerritories` 0x441770), Las Venturas still forbidden. Zone
  strengths reset for most of Los Santos, gang wars off, `MOB_CAT` and `BCRASH` started.
- **#28, cleanup (pass and fail).** `RemoveIpl("BARRIERS1")` (`LA1FIN2.txt` 3234): the 8 Los Santos roadblocks go.
  The road boxes stay off: ambient traffic avoids the bridges, players drive through.
- **#61 Yay Ka-Boom-Boom, end.** `RemoveIpl("BARRIERS2")`, stat 181 = 2, 33 of the 34 boxes switched back on (a
  small one at the south end of the Gant Bridge, x −2670…−2663, y 1203…1238, stays off) and one new box in San Fierro
  switched off (x −1762…−1751, y 508…532), `CRACK` removed, the factory models swapped through visibility.

### 4.2 What the friend gets

- **W1** (stat 181): no four stars where the host has opened the city.
- **W3**: the host's `BARRIERS1` turns ABSENT at the end of #28; the friend's game calls `RemoveIplAndIgnore` for
  that slot, which streams the IPL out at once (its 8 buildings are deleted in that call) and keeps dynamic streaming
  off, the state the host is in. The roadblocks disappear for the friend within about a second of the host's (poll
  interval plus network). At #61 the same happens to `BARRIERS2`.
- **W4**: at #61 the friend's list becomes the host's; loaded areas switch at once, the others when they stream in.
- **Late joiner, or the friend restarts.** Their `INTRO` requests both groups and switches the boxes off; the gate
  waits for `INTRO` to end, the re-apply removes the groups again and replays the host's list.
- **The host loads a save from before #28.** `CIplStore::Load` requests the groups again on the host; the next poll
  sees PRESENT and the friend calls `RequestIplAndIgnore`, which queues the IPL for streaming with keep-in-memory
  (no `LoadAllRequestedModels`, the same as the script command); the roadblocks appear within frames.
- **Streaming and timing.** Removal is synchronous and allowed at any distance (the host's own mission does it
  wherever the player is); a request is asynchronous. Neither IPL has car generators, so removal deletes no parked
  cars. A player standing on a roadblock when it goes falls a few centimetres. Nothing depends on the friend being
  near the bridges.

### 4.3 City unlock

`CCityUnlockSync` becomes entry 0 (D1): the same logic (host sends on change and on join, the others keep the host's
value, reset on disconnect), carried by `WORLD_STATE` and covered by the story guard. Its packet type's position is
reused (D2). Behaviour for the players does not change.

## 5. Risks and open questions

- **Identity by index.** Zone-info indexes, garage indexes, IPL slots and building pool slots must be the same in
  both games. They are when both run the same package on 1.0 US (the save file makes the same assumption); the layout
  header turns a mismatch into a disabled entry and a log line.
- **Wrong host.** Without the story guard the registry overwrites the story owner's world (as the city unlock sync
  already does). The guard is D8.
- **The host is alt-tabbed when the friend joins.** Its game handles `PLAYER_CONNECTED` only when it is back in
  front (`CNetworkKeepAlive` keeps the packet), so the join snapshot waits. A server-side cache would remove the wait
  at the cost of server state; not proposed for the first version.
- **Gang war kills by the friend.** Whether the friend's kills of the host's attack waves count in the host's
  `CGangWars` is untested (they are network peds owned by the host).
- **`FillZonesWithGangColours`** asks `CanPlayerStartAGangWarHere` per zone, which may depend on the local player;
  the radar colours could differ slightly between the games.
- **EnEx full-word copy.** Upstream's `Receive` writes all 16 flag bits; the adapter keeps the friend's bits 0x0008
  and 0x2000 on re-apply. Whether `Receive` should mask them too is D7.
- **Building lookup.** W11 finds buildings by model within 0.5 m of the host's position; both factories share one
  position, so the model is part of the key.
- **Not verified in the exe yet:** the rows marked *verify* in 3.3, and the `CPedType` storage for W13.
- **Unknowns about the original:** nothing requests `TRUTHSFARM` outside `TRUTH2`, so the farm should exist only
  during that mission (no effect on the design); whether 1.0's `0776` with slot −1 (`FUKDCRAK`) writes outside the
  IPL pool is vanilla behaviour the registry does not repeat.
- **Gang war HUD.** The host's war messages and its war blip are not mission HUD and not world state; neither design
  covers them.
- **Nothing here is tested in game.**

## 6. In-game test plan (two players)

**oyunda doğrulanmadı.** For the user (host) and the friend, after the work packages of section 7, with the package
that contains them. Missions are played in story order; tests tied to a mission run when the story reaches it.
Both start the game with the `CoopAndreas (tani)` shortcut so `--coopdiag` writes the registry log.

A. No mission needed (after WS-1, WS-3, WS-4):

1. Host: start the server, connect first, load the co-op save. Friend: connect, start a new game.
2. Friend: after the intro, drive to Loco Low Co and to the Los Santos Transfender (open since #10): the doors open
   and the mod menu works. Visit Binco and the barber: the doors work.
3. Friend: get two stars, then three; check that a fourth is possible only as on the host (max wanted 4 before the
   Badlands call). Lose them.
4. Friend: quit to the menu, start a new game while still connected; after the intro repeat step 2.
5. Friend: disconnect and reconnect; repeat step 2.
6. Host: load the same save again from the pause menu. Friend: repeat step 2; the registry log shows FULLs received.
7. Story guard (if D8 is accepted): both quit. Host: start the server and the tunnel only. Friend: start the game
   and connect first (the friend becomes host). Host: start the game, load the co-op save, connect second. Host: a
   chat line says the host's story is behind; the shops, garages and the four-star cities stay as in the host's
   game. Both quit without saving.
8. Host: start the next mission in story order and fail it on purpose once it has lowered the traffic (after its
   first cutscene). Friend: traffic and pedestrians come back when the mission has ended.

B. At the story point:

9. #17 Robbing Uncle Sam passed: friend recruits or meets Grove members; they carry the new weapons.
10. #25 Doberman passed: friend's map shows the gang territories like the host's. Friend: kill Ballas in a Ballas
    zone: no gang war starts. Host: start a war; friend helps; after the host wins, the friend's map shows the new
    territory.
11. #28 The Green Sabre passed (or failed): the roadblocks on the roads out of Los Santos into Flint County are gone
    for the friend as for the host. After a pass: friend drives into San Fierro: no four stars. Friend: restart their
    game and check again.
12. The `MOB_CAT` call after the Badlands strand: friend can reach five stars. #69 Verdant Meadows: six.
13. #42 Deconstruction passed: friend sees the finished site at the Doherty hole.
14. #61 Yay Ka-Boom-Boom passed: friend sees the destroyed crack factory, the San Fierro and Las Venturas roadblocks
    are gone, traffic uses the bridges, no four stars in Las Venturas.
15. #99 Riot: friend sees the riots in Los Santos; after #103 they stop.

## 7. Work packages

All for `gtasa-coder-high`, in this order (WS-2 to WS-4 can run in parallel after WS-1). Every package keeps
CoopAndreas' style (new files, private-build comments), cites each address it uses, passes `scripts/build.ps1`, adds
its policy cases to `tests/world_state_test.cpp` (compiled with `/W4 /WX` like the existing tests), adds no hook
site, and lists the in-game tests of section 6 it needs. Each is a protocol change for the next package.

| WP | Content | Ready before |
|---|---|---|
| WS-1 Registry core | `CWorldStateRegistry`, entry base, policy header and unit test; `WORLD_STATE` packet replacing `UPDATE_CITY_UNLOCKED` and its host-only relay; lifecycle (join, host change, disconnect, restart, load call), gate, story guard (D8), `--coopdiag` log and summary line; entries 0 `CityUnlock` (removes `CCityUnlockSync.*`) and 1 `MaxWantedLevel`; version bump | the next package (group 2) |
| WS-2 Barriers and roads | entries 2 `IplGroups` and 3 `RoadSwitches` (procedure of 3.2, list logic in the unit test) | the package with #28; earlier is better (mission road switches in group 1) |
| WS-3 Doors and garages | `EntryExits` adapter (host poll, masked re-apply, D7), entry 4 `Garages`; *verify* rows of the garage functions | the next package (fixes #10's mod shops and shops after a friend restart) |
| WS-4 Mission settings | entry 8 `MissionSettings` with the fields of 3.3 and forced weather; *verify* rows; finds the state variables behind `DisableRandomTrains`, `SwitchAmbientPlanes` and `DisableCopBikes` (or leaves them out) | the next package |
| WS-5 Gangs | entries 5 `Zones`, 6 `GangWars` with the war hold on the other games, 7 `GangWeapons` | the package with #25 (#17's weapons are cosmetic) |
| WS-6 Map objects | entries 9 `BuildingSwaps`, 10 `HiddenObjects`; finds the lookup the `03B6`/`0363` handlers use and `ReplaceWithNewModel` in the exe | the package with #42 |
| WS-7 Riots and relationships | entries 11 `LaRiots`, 12 `PedRelationships` (finds and checks the `CPedType` storage) | the package with #99 (relationships optional) |

Optional, only on a decision: blocking property purchases on the friend's game (D4), mirrored reward pickups (D6).

## 8. Decisions for the architect or the user

| # | Decision | Recommendation |
|---|---|---|
| D1 | `CCityUnlockSync`: move into the registry, or keep it and wrap it | Move it (entry 0): one mechanism, the story guard covers it, no second packet |
| D2 | Wire: one generic `WORLD_STATE` packet at `UPDATE_CITY_UNLOCKED`'s position, or one packet type per entry | Generic, reusing the position: one server handler, no renumbering, new entries without new types |
| D3 | Gang wars: only the host's game runs them (the friend sees the territories and joins the fights), or both games | Host only: two games changing the same zones would need a second authority |
| D4 | Properties: host-owned (the friend's purchases are not saved anywhere and their money is gone), per player, or blocked on the friend's game | Host-owned; tell the friend in the test notes not to buy; block on the friend's game later only if it happens |
| D5 | Owner of the engine's end-of-mission reset (traffic, military zones, wanted multiplier…) on the friend's game | The registry, by mirroring the host's live values (W12); the mirror layer clears what it draws |
| D6 | Story reward pickups (spray can, Emmet's pistol, territory cash) | No entry for now; the friend gets mission weapons from the ports |
| D7 | EnEx: keep upstream's packet and server cache and add the adapter, or replace them with a registry entry; mask bits 0x0008/0x2000 in `Receive` too | Keep and adapt; mask in `Receive` as well (a two-line change in an upstream file) |
| D8 | Story guard on stat 147, a server setting naming the story host, or nothing | The guard: client only, no configuration, also protects the city unlock that ships today |
| D9 | Testing the barriers before the story reaches #28 | Not in the co-op save (story order rule); rely on the unit test and logs, test at #28. A debug toggle on a throwaway game needs the user's approval |
| D10 | Map exploration | Per player |
| D11 | Persistent script objects (doors, gates, cabins, the Carter wall; 2.3) | A separate object-sync design, ready before the batch with #60 (doors a mission opens); the mirror layer may cover objects a mission moves while it runs |
