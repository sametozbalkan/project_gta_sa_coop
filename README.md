**English** · [Türkçe](README.tr.md)

# San Andreas Story Co-op <sub>(working title)</sub>

Play the whole GTA: San Andreas story together: two players, one save, every mission rebuilt for co-op, one by one.

![status: alpha](https://img.shields.io/badge/status-alpha-orange)
![version: 0.1.2-alpha](https://img.shields.io/badge/version-0.1.2--alpha-blue)
[![license: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-green)](LICENSE)
<!-- Buy Me a Coffee badge goes here once the account exists. -->

<!-- Screenshot or GIF goes here. -->

## What it is

A two-player co-op mod for the PC version 1.0 (US) of GTA: San Andreas; the installer makes a 1.0 US copy of your
Steam game. The host's game runs the story and its missions, and the other player takes part in every scene. Story
missions are rebuilt for co-op one at a time, strictly in story order: missions unlock exactly as in the original
game, and none is skipped. There is one save, the host's, and every release is checked so that it keeps loading.

The project is in alpha, so expect bugs and crashes. The first ten missions are rebuilt and being tested, and
anyone with the game can help test them (see [Testing and feedback](#testing-and-feedback)).

## Progress

✅ played through in two-player tests · 🧪 rebuilt, test pending · 🛠️ next group · ⏳ planned

| Region | Missions | ✅ | 🧪 | 🛠️ | ⏳ |
|---|---|:---:|:---:|:---:|:---:|
| Los Santos | 28 (#1–#28) | – | 10 | 7 | 11 |
| Badlands | 11 (#29–#39) | – | – | – | 11 |
| San Fierro | 26 (#40–#65) | – | – | – | 26 |
| Desert | 9 (#66–#74) | – | – | – | 9 |
| Las Venturas | 18 (#75–#92) | – | – | – | 18 |
| Return to Los Santos | 11 (#93–#103) | – | – | – | 11 |
| **Total** | **103** | **–** | **10** | **7** | **86** |

Missions ship in groups, and the next group starts only after the previous one has been tested. #1–#6 were
played through on 0.3.102; in 0.1.0 all of #1–#10 moved to the mission mirror layer, and they are being tested again
from a New Game ([docs/test-plan.md](docs/test-plan.md)). Mission numbers follow [docs/missions.md](docs/missions.md),
which also lists each mission's script, co-op risks and test history.

<details>
<summary><b>Los Santos</b> (#1–#28)</summary>

| # | Mission | Status | Note |
|---:|---|:---:|---|
| 1 | In the Beginning | 🧪 | The intro cutscene and bike ride are skipped; the story starts right after them. |
| 2 | Big Smoke | 🧪 | |
| 3 | Ryder | 🧪 | |
| 4 | Tagging Up Turf | 🧪 | |
| 5 | Cleaning The Hood | 🧪 | |
| 6 | Drive-Thru | 🧪 | |
| 7 | Nines And AK's | 🧪 | Retest pending for the fix (0.3.103) of the host vanishing on the other player's screen. |
| 8 | Drive-By | 🧪 | The other players' games crashed here in the 0.1.1 test; fixed in 0.1.2, retest pending. |
| 9 | Sweet's Girl | 🧪 | |
| 10 | Cesar Vialpando | 🧪 | |
| 11 | OG Loc | 🛠️ | Group 2 |
| 12 | Running Dog | 🛠️ | Group 2 |
| 13 | Wrong Side of the Tracks | 🛠️ | Group 2 |
| 14 | Just Business | 🛠️ | Group 2 |
| 15 | Home Invasion | 🛠️ | Group 2 |
| 16 | Catalyst | 🛠️ | Group 2 |
| 17 | Robbing Uncle Sam | 🛠️ | Group 2 |
| 18 | Life's a Beach | ⏳ | |
| 19 | Madd Dogg's Rhymes | ⏳ | |
| 20 | Management Issues | ⏳ | |
| 21 | House Party | ⏳ | |
| 22 | High Stakes, Low Rider | ⏳ | |
| 23 | Burning Desire | ⏳ | |
| 24 | Gray Imports | ⏳ | |
| 25 | Doberman | ⏳ | |
| 26 | Los Sepulcros | ⏳ | |
| 27 | Reuniting The Families | ⏳ | |
| 28 | The Green Sabre | ⏳ | |

</details>

<details>
<summary><b>Badlands</b> (#29–#39)</summary>

| # | Mission | Status |
|---:|---|:---:|
| 29 | Badlands | ⏳ |
| 30 | First Date | ⏳ |
| 31 | Tanker Commander | ⏳ |
| 32 | Body Harvest | ⏳ |
| 33 | Local Liquor Store | ⏳ |
| 34 | Against All Odds | ⏳ |
| 35 | Small Town Bank | ⏳ |
| 36 | King in Exile | ⏳ |
| 37 | Wu Zi Mu | ⏳ |
| 38 | Farewell, My Love... | ⏳ |
| 39 | Are You Going To San Fierro? | ⏳ |

</details>

<details>
<summary><b>San Fierro</b> (#40–#65)</summary>

| # | Mission | Status |
|---:|---|:---:|
| 40 | Wear Flowers In Your Hair | ⏳ |
| 41 | 555 WE TIP | ⏳ |
| 42 | Deconstruction | ⏳ |
| 43 | Back to School | ⏳ |
| 44 | Air Raid | ⏳ |
| 45 | Supply Lines... | ⏳ |
| 46 | New Model Army | ⏳ |
| 47 | Photo Opportunity | ⏳ |
| 48 | Jizzy | ⏳ |
| 49 | T-Bone Mendez | ⏳ |
| 50 | Mike Toreno | ⏳ |
| 51 | Outrider | ⏳ |
| 52 | Snail Trail | ⏳ |
| 53 | Ice Cold Killa | ⏳ |
| 54 | Pier 69 | ⏳ |
| 55 | Toreno's Last Flight | ⏳ |
| 56 | Mountain Cloud Boys | ⏳ |
| 57 | Ran Fa Li | ⏳ |
| 58 | Lure | ⏳ |
| 59 | Amphibious Assault | ⏳ |
| 60 | The Da Nang Thang | ⏳ |
| 61 | Yay Ka-Boom-Boom | ⏳ |
| 62 | Zeroing In | ⏳ |
| 63 | Test Drive | ⏳ |
| 64 | Customs Fast Track | ⏳ |
| 65 | Puncture Wounds | ⏳ |

</details>

<details>
<summary><b>Desert</b> (#66–#74)</summary>

| # | Mission | Status |
|---:|---|:---:|
| 66 | Monster | ⏳ |
| 67 | Highjack | ⏳ |
| 68 | Interdiction | ⏳ |
| 69 | Verdant Meadows | ⏳ |
| 70 | Learning to Fly | ⏳ |
| 71 | N.O.E. | ⏳ |
| 72 | Stowaway | ⏳ |
| 73 | Black Project | ⏳ |
| 74 | Green Goo | ⏳ |

</details>

<details>
<summary><b>Las Venturas</b> (#75–#92)</summary>

| # | Mission | Status |
|---:|---|:---:|
| 75 | Fender Ketchup | ⏳ |
| 76 | Explosive Situation | ⏳ |
| 77 | You've Had Your Chips | ⏳ |
| 78 | Don Peyote | ⏳ |
| 79 | Architectural Espionage | ⏳ |
| 80 | Key To Her Heart | ⏳ |
| 81 | Dam And Blast | ⏳ |
| 82 | Cop Wheels | ⏳ |
| 83 | Up, Up and Away! | ⏳ |
| 84 | Intensive Care | ⏳ |
| 85 | The Meat Business | ⏳ |
| 86 | Fish in a Barrel | ⏳ |
| 87 | Misappropriation | ⏳ |
| 88 | Madd Dogg | ⏳ |
| 89 | Freefall | ⏳ |
| 90 | High Noon | ⏳ |
| 91 | Saint Mark's Bistro | ⏳ |
| 92 | Breaking the Bank at Caligula's | ⏳ |

</details>

<details>
<summary><b>Return to Los Santos</b> (#93–#103)</summary>

| # | Mission | Status |
|---:|---|:---:|
| 93 | A Home In The Hills | ⏳ |
| 94 | Vertical Bird | ⏳ |
| 95 | Home Coming | ⏳ |
| 96 | Cut Throat Business | ⏳ |
| 97 | Beat Down on B Dup | ⏳ |
| 98 | Grove 4 Life | ⏳ |
| 99 | Riot | ⏳ |
| 100 | Los Desperados | ⏳ |
| 101 | End Of The Line (1) | ⏳ |
| 102 | End Of The Line (2) | ⏳ |
| 103 | End Of The Line (3) | ⏳ |

</details>

## Installing and updating

You need GTA: San Andreas bought on Steam (PC), about 7 GB of free disk space and, for the first install, an
Internet connection: about 880 MB of downgrade patches are downloaded.

1. On the [Releases](https://github.com/sametozbalkan/project_gta_sa_coop/releases) page, download
   `CoopAndreas-Setup.zip` from the newest pre-release (`CoopAndreas-Kurulum.zip` in 0.1.2 and earlier). The
   `.zip.sha256` file next to it holds the zip's SHA-256.
2. Extract the whole zip into a folder and run `Install.bat` (`Kur.bat` in 0.1.2 and earlier). If Windows says it
   protected your PC, choose More info, then Run anyway.
3. The installer copies your Steam game to `C:\Games\GTASA_Coop` (`Install.bat -GameDir D:\GTASA_Coop` for another
   folder), converts that copy to version 1.0 US with every file checked, and installs the mod, the Steam tunnel and
   the shortcuts. Your Steam installation stays untouched. `README.txt` in the package (`OKU-BENI.txt` in 0.1.2 and
   earlier) has the details.

To update, run the new package's `Install.bat` the same way: on an existing install it only refreshes the mod files.
Every player, the host included, needs the same release. The version shows in the bottom-left corner of the game,
and players with different versions get "Version mismatch" when they connect.

## Playing

One player hosts: their PC runs the server and their game runs the story. Over the Internet everyone connects
through the Steam tunnel, so Steam must be running on every PC and the other players must be the host's Steam
friends.

**The host**

1. Start `CoopAndreas Server` in the game folder and `1-Host.bat` in its `CoopTunnel` folder (`1-Host-ac.bat` in
   0.1.2 and earlier). Keep both windows open.
2. Recommended, once: in `server\server-config.ini`, set `story-host = <your nickname>` so that you stay the host
   even when someone else connects first (without it, the first player to connect is the host; in the nickname,
   characters other than letters, digits, `_`, `[` and `]` become `_`). When you play only over the tunnel, also set
   `bind = 127.0.0.1` so that nothing else on your network reaches the server. Restart the server after a change.
3. Start the game with the `CoopAndreas` shortcut.

**The other players**

1. Start the desktop shortcut `CoopAndreas Tunnel - Join` (`CoopAndreas Tünel - Bağlan` in 0.1.2 and earlier) and
   keep its window open; it finds the host by itself. To test the connection without the game, run
   `2-Test-connection.bat` in the `CoopTunnel` folder (`2-Baglanti-testi.bat` in 0.1.2 and earlier).
2. Start the game with the `CoopAndreas` shortcut.

**Everyone:** in the game menu, Start Game opens "Connect to a server". The IP `127.0.0.1` and port `6767` are
already filled in; use a nickname that differs from the other players' and keep it every session. Choose Connect,
then start a **New Game**. Missions start at their usual markers, in story order, and only the host saves (at a
safehouse, outside a mission). `USAGE.txt` in the `CoopTunnel` folder (`KULLANIM.txt` in 0.1.2 and earlier)
explains the tunnel.

## Testing and feedback

Testers are welcome. [docs/test-plan.md](docs/test-plan.md) lists what to check in the current release, step by
step and mission by mission, and which files to send; packages include a copy as `TEST-PLAN.md` (`TEST-LISTESI.md`,
in Turkish, in 0.1.2 and earlier). Start the game with the `CoopAndreas (diagnostics)` shortcut in the game folder
(`CoopAndreas (tani)` in 0.1.2 and earlier) so that it writes diagnostic logs.

Report problems as [GitHub issues](https://github.com/sametozbalkan/project_gta_sa_coop/issues) with the bug report
template: the version, whether you hosted, how many players took part, the mission, what happened, and the logs
(after a crash, the crash log too). Leave out personal data such as Steam names and IP addresses.

## What works in co-op

### Synced between the two games

- **Players:** movement and fighting on foot, name tags with health and armour bars, clothes, haircuts and
  tattoos, weapon skills, each other's position and waypoint on the map, text chat.
- **Weapons:** the weapon in hand, aiming, every shot, explosions, and thrown or fired projectiles such as
  grenades and rockets.
- **Vehicles:** driving, getting in and out, passenger seats, drive-by shooting, damage, colours, paint jobs,
  tuning parts and door locks.
- **NPCs and the world:** traffic and pedestrians from both games, with their movement, driving, weapons, shots
  and speech; time and weather follow the host; going in and out of interiors.
- **Missions:** in every mission the host plays, the title, objective and dialogue texts, help boxes, "Mission
  passed", markers on vehicles, characters and destinations, the yellow mission markers, timers and counters reach
  the other player automatically, and the pass reward (money and respect) goes to both; bets and costs inside a
  mission stay with the host. A rebuilt mission adds cutscenes, fades, camera moves, seats and positions. Mission
  dialogue audio plays in both games, and if either player dies or gets busted, the mission fails.

### Added by this project

- **Steam relay:** play over the Internet through Steam's relay network with no port forwarding, also behind
  CGNAT. By default, only the host's Steam friends can connect.
- **Installer:** copies your Steam game to a separate folder, converts that copy to version 1.0 US with every
  file checked, and installs the mod and the tunnel. The Steam installation stays untouched; running the
  installer again only updates the mod files.
- **Alt+Tab:** switching to another window or minimizing the game no longer drops you from the server, and the
  keys you held are released on the other player's screen.
- **Radio:** in the same car, the passenger hears the driver's radio station.
- **Tags:** a gang tag sprayed by either player counts for both (Tagging Up Turf).
- **Wanted levels from missions:** when the host's mission raises or clears the wanted level or makes the police
  ignore the player, the change applies to both players; stars from your own crimes stay yours.
- **Mission mirror layer:** what the host's mission shows on screen reaches the other player without extra lines
  in the mission scripts, also for a player who joins mid-mission.
- **World state:** unlocked cities, the highest wanted level, garages and shop doors, and the traffic, police and
  weather settings of the host's missions follow the host's game, also after a failed mission or when the other
  player restarts.
- **Story host:** the server can name the player who is always the host (`story-host`), so the story stays in
  the right game even if the other player connects first or the host reconnects.
- **NPC sync:** fixes for a crash on the other player's game when NPCs are synced.
- **Vehicle and NPC sync:** every car has one game that drives it, a car a mission deletes goes on both screens,
  every way of leaving a car shows on the other screen, and after a New Game or a load during a session the
  other game's NPCs and their cars come back (with the host's mission characters).
- **A player who leaves mid-mission:** the host's game doesn't crash and the mission goes on without them;
  story missions start only on the host's game.
- **Widescreen:** the video mode dialog at startup also lists widescreen (16:9) resolutions.
- **Frame limiter:** always on; without it, a New Game could stay on a black screen.
- **Saved player state:** the other player's weapons, armour, money, clothes, skills and respect are saved with
  the host's save.
- **Player recovery:** a player who vanishes from the other player's screen is restored automatically, and the
  other player gets control back if the host leaves during a cutscene.

### Privacy

No telemetry, no Discord integration, no crash-report upload and no serial key. Crash logs and diagnostic logs
are written only to your own PC. What the game sends, and to whom: [docs/privacy.md](docs/privacy.md).

## Known limitations

- Saves from the single-player game don't load in co-op: start a New Game.
- Only the host saves, at a safehouse and outside a mission: the story advances only in the host's game.
- The other player's saved state is matched by nickname: keep the same nickname every session, different from the
  host's.
- Alt+Tab pauses that player's own game; if it is the host, the mission waits too. The other player sees the away
  player, and the characters that player's game controls, frozen in place (a car they drive coasts on). The
  connection stays up.
- Some parts of a mission stay with the host: solo minigames (the other player watches), objects only the mission
  script knows about (such as the bottle practice in Nines And AK's), and the mission's own wanted-level checks,
  which read the host's stars.
- Messages meant only for the host (such as "get back in the car") show on the other player's screen too.
- Missions that aren't rebuilt yet run as the host's mission; the other player sees its texts, markers and timers
  and can help, but without the cutscenes, seats and positions a rebuild adds.
- A player who leaves during a mission and comes back sees its texts and markers but takes part again only from the
  next mission. If the host's own connection drops during a mission, some missions can hang (a fix is planned).
- Only the host should buy properties: another player's purchase isn't saved anywhere and the money is gone.
- Not synced yet: trains, mission objects (crates, doors, gates), fire, gang wars and territories, trailers, RC
  vehicles, parachutes and picking up the jetpack.

## How it works

Both games connect to a small server that relays their traffic; over the Internet, the project's tunnel carries
that traffic through Steam. The host is the player named in the server's `story-host` setting (by default the
first player to connect), and the host's game holds the story: mission scripts run only there. Vehicles and
characters are shared between the two games and kept in sync. The mission mirror layer copies what the host's
mission shows (texts, markers, timers, rewards) to the other player, and the world-state registry keeps the
rules of the host's world the same in both games. A rebuilt mission also sends selected script commands
(cutscenes, fades, camera, wanted levels) and puts the other player where each scene needs them. A save is the
host's normal save; a small side file also keeps the other players' state.

## Roadmap

- **Rest of Los Santos:** group 2 (#11–#17, Big Smoke's and Ryder's missions), group 3 (#18–#22, the music
  missions and Cesar) and group 4 (#23–#28, Burning Desire to The Green Sabre). Then, in story order, Badlands,
  San Fierro, the desert, Las Venturas and the return to Los Santos.
- **Before group 2:** trains and mission objects (crates, doors, gates) for two players; the designs are in
  `docs/design/`.
- **Rest of the world state, before the missions that need it:** race checkpoints (#22), gang territories and
  wars (#25), the bridge barriers and roads (#28), map objects (#42), the riots (#99), and object sync for doors
  and gates that missions open (#60).
- **Game systems that later missions need:** fire (#23), gang wars (#25, #98), trailers (#31), RC vehicles
  (#44–#46), parachutes (#72, #81, #89) and the jetpack (#73), plus more script space for Reuniting The
  Families (#27).
- **After the story:** side content (races, schools, vehicle side missions) and co-op pool, with the two players
  playing each other.

The detailed work plan and the open issues are in [BACKLOG.md](BACKLOG.md).

## Building from source

Requirements, all on Windows:

- Visual Studio 2026 with C++ (MSVC, x86)
- xmake 3.1.1
- Sanny Builder 4.2 with the mod's Sanny Builder mode (in its `sdk` folder), for the mission scripts
- .NET 10 SDK, for the Steam tunnel
- PowerShell 7, for packaging

The scripts expect a local, git-ignored `.tools/` folder (xmake, Sanny Builder); [CLAUDE.md](CLAUDE.md)
describes what goes in it.

- `scripts/build.ps1` builds the client DLL, server, proxy and launcher (`-Mode debug` for a debug build).
- `scripts/compile-scm.ps1` compiles the mission scripts with Sanny Builder 4 and rejects a result that
  overflows the game's script space or would break existing saves (`-OutDir` for a scratch folder; it reads
  model names from a 1.0 US game folder, `-GameDir`).
- `scripts/deploy.ps1` installs the build into a 1.0 US game folder (`-GameDir`; `-WhatIf` shows what it would
  do).
- `scripts/publish-tunnel.ps1` publishes the tunnel as one self-contained exe into `dist/`.
- `scripts/package.ps1` assembles the installer package together with the source of its changes
  (PowerShell 7).

Mission work: [docs/mission-porting.md](docs/mission-porting.md) explains how a mission is rebuilt for co-op,
and [docs/missions.md](docs/missions.md) lists every mission with its status and risks.

## Repository layout

- `CoopAndreas/` — the mod: client, server, proxy, launcher, mission scripts.
- `tunnel/` — the Steam relay tunnel (C#, .NET 10) and its launchers.
- `installer/` — the installer (Windows PowerShell 5.1).
- `scripts/` — build, mission compile, deploy, tunnel publish and packaging scripts.
- `tools/` — Python debugging helpers and the Ghidra names pipeline.
- `docs/` — the mission list, the porting guide, the test plan and the designs (`docs/design/`).
- `.github/` — the bug report template.
- Top level: `CLAUDE.md` (development notes and rules), `BACKLOG.md` (work plan and open issues), `LICENSE`.

## Legal

This is an unofficial modification of Grand Theft Auto: San Andreas and requires a legitimate copy of the game.
This repository contains no original game files, no game assets and no downgrade patches. The project is not
affiliated with or endorsed by Rockstar Games or Take-Two Interactive. All trademarks belong to their respective
owners.

## License and credits

Licensed under the GNU General Public License v3.0; see [LICENSE](LICENSE). Every package includes the matching
source in its `SOURCE` folder (`KAYNAK` in 0.1.2 and earlier).

Built on the open-source [CoopAndreas](https://github.com/Tornamic/CoopAndreas) networking core (GPL-3.0, by
Tornamic and contributors) and developed independently since.

| Third-party component | Used for | License |
|---|---|---|
| plugin-sdk (modified copy) | game structures and hooks for the 1.0 US executable | zlib (its Hooking.Patterns: MIT) |
| ENet 1.3.18 | networking between the games and the server | MIT |
| Dear ImGui | the client's debug windows | MIT |
| inih / INIReader | the server's config file | BSD-3-Clause |
| serialize (Mas Bandwidth LLC) | packet serialization | BSD-3-Clause |
| UTF8-CPP | UTF-8 text conversion | Boost Software License 1.0 |
| Facepunch.Steamworks 2.3.3 (NuGet) | the Steam relay tunnel | MIT |

The tunnel build also contains Valve's `steam_api64.dll`, which comes with the Facepunch.Steamworks package. The
source tree keeps the upstream Discord RPC SDK (with RapidJSON, MIT), which this build doesn't link into the
client. Sanny Builder 4 compiles the mission scripts; it is a build tool and isn't shipped.

_A personal project: test builds only, no support._
