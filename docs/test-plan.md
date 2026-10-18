# Test plan

For **0.1.2-alpha**, the current release. Anyone can test: you need two or more PCs, each with GTA: San Andreas from
Steam, and Steam accounts that are friends with each other. Tick a box only after you have seen it on at least two
PCs. Nothing in this plan is confirmed in game yet unless the results table says so; anything else you notice is
welcome too (see [Reporting](#reporting)).

The **host** is the player whose game runs the story: the player named by `story-host` in the server's config, or
the first player to connect. Every other player is a **guest**.

## Setup

1. **Install or update.** Everyone, the host included, installs the same package from the
   [Releases](https://github.com/sametozbalkan/project_gta_sa_coop/releases) page: download `CoopAndreas-Setup.zip`
   (`CoopAndreas-Kurulum.zip` in 0.1.2 and earlier) and its `.zip.sha256`, compare the zip's SHA-256
   (`Get-FileHash <zip>` in PowerShell) with it, extract the whole zip and run `Install.bat` (`Kur.bat` in 0.1.2 and
   earlier). The first install copies the Steam game to `C:\Games\GTASA_Coop` and converts that copy to 1.0 US; on an
   existing install it only refreshes the mod files.
2. **Same version.** The bottom-left corner of the game shows the version: `0.1.2-alpha` for everyone. If the chat
   shows `Version mismatch`, don't start testing.
3. **Host.** Start `CoopAndreas Server` in the game folder and, over the Internet, `1-Host.bat` in its `CoopTunnel`
   folder (`1-Host-ac.bat` in 0.1.2 and earlier); keep both windows open. So that you stay the host even when a guest
   connects first, put `story-host = <your nickname>` in `server\server-config.ini` and restart the server; with it
   empty, connect first yourself.
4. **Guests.** Start the desktop shortcut `CoopAndreas Tunnel - Join` (`CoopAndreas Tünel - Bağlan` in 0.1.2 and
   earlier) and keep its window open.
5. **Diagnostics.** Everyone starts the game with the **`CoopAndreas (diagnostics)`** shortcut in the game folder
   (`CoopAndreas (tani)` in 0.1.2 and earlier), which writes the logs listed under [Reporting](#reporting). Delete the
   old `CoopAndreas_*.log` files in the game folder first.
6. **Connect.** In the game menu, Start Game opens "Connect to a server": IP `127.0.0.1` and port `6767` are filled
   in; every player uses a different nickname and keeps it. Connect, then start a **New Game**; never load a
   single-player save.
7. **Story order.** Start missions at their normal markers, in story order; don't skip anything with cheats. Only the
   host saves, at a safehouse and outside a mission. Sections A3, B and D need #3, #4, #6, #8 and #9: use co-op saves
   from just before them if the host has them, otherwise play there from a New Game.

Session: date `____` · number of players `____` · package SHA-256 `____` · commit in `manifest.json` `____`.

## What changed

### 0.1.2 (fixes for the 0.1.1 test)

- **Drive-By crash on the guests' games:** the mission creates the Ballas' cars and gives their drivers the order to
  get out in the same frame, before the cars have network ids, so the order reached the other games without its car,
  and they crashed when a Balla got out. The order now goes out when it is carried out, with its car; a game skips a
  command for a car or character it doesn't have, and a get-out task without its car no longer crashes.
- **A dead player when a mission starts:** if a player was dead or arrested when the host started a mission, the
  host's game could crash. Now it doesn't; the mission may fail right after it starts.
- The network format is unchanged, but 0.1.1 and 0.1.2 don't connect to each other: everyone, the host included,
  needs 0.1.2. Co-op saves from 0.1.0 and 0.1.1 still load.

### 0.1.1

- **Vehicle sync:** each car has one game that drives it, so no car should jitter because two games drive it or
  freeze because none does. A car the host's mission deletes goes from every screen, and a guest inside is left on
  foot. A car one game deleted on its own comes back with the next update of the game that drives it. The server
  counts passenger seats right, and a host mission car driven by a player who leaves goes back to the host. Every way
  of leaving a car shows on the other screens: the exit key, the mission taking you out, being dragged out.
- **Restarting during a session:** when a player loads a save or starts a New Game while connected, the other games'
  NPCs and the cars they drive come back; during the host's mission, the mission characters (Sweet, Smoke, Ryder)
  come back with their blips. Before, they vanished or stayed behind as frozen cars without drivers.
- **Clock:** the games' shared clock no longer steps back, so vehicle and NPC packets are handled in order.
- **A player who leaves mid-mission:** when a guest quits, crashes or loses the connection during a mission, the
  host's game doesn't crash and the mission goes on. A guest who reconnects sees the mission's texts, blips and
  counters, but takes part again only from the next mission.
- **Story missions start only on the host:** story markers start nothing on a guest's game; a guest's own side
  activities (shops, taxi, gym) work.
- **Shopkeepers:** a barber or shop assistant created on a guest's game no longer disappears on the host when that
  guest leaves.
- **Alt+Tab:** the keys a player held are released on the other screens: their character stops walking, their car
  stops accelerating.
- **Server:** accepts only the story characters the game's scripts load.

## A. Outside missions

### A1. Driving together

- [ ] **Driver and passenger:** the host drives, a guest rides along; drive around for a few minutes. No jitter or
  teleporting.
- [ ] **Swapping seats:** the host gets out, the guest stays in the passenger seat. Then the guest gets out and takes
  the driver's seat, the host the passenger seat, and the guest drives. Both screens show one smooth car with everyone
  in the right seat.
- [ ] **Four doors:** the guest gets into a four-door car with G through the front right, rear left and rear right
  doors in turn, and once presses G next to the driver's door. Each time the host sees the guest in the right seat.
- [ ] **Motorbike:** the guest gets on behind the host on a motorbike with G; the host sees them sitting there.
- [ ] **Radio:** in the same car the passenger hears the driver's station and follows a change within about 1.5 s
  (Radio Off at once); the passenger's own station keys do nothing. In separate cars everyone hears their own radio.
- [ ] **Traffic:** drive side by side, then in different places, for 5–10 minutes. No doubled, frozen or jumping
  traffic cars. Blow up a car and drive away: the wreck goes from both screens together. Every player's
  `CoopAndreas_vehicle_sync.log` has a `destructor-hooks hooked=5/5` line.
- [ ] **The driver gets out, the passenger takes over:** the host drives, the guest rides along. The host gets out
  with F and walks about 150 m away; the guest waits a little, moves to the driver's seat and drives. Swap roles and
  repeat. As the driver gets out, the car doesn't freeze or roll on by itself, and it is in the same place on both
  screens.
- [ ] **Dragged out:** while the guest drives, the host pulls them out through the driver's door; then swap roles. On
  both screens the driver is dragged out and the other player sits down; the car doesn't jump back and forth.
- [ ] **Entry given up:** the guest tries to get into a locked car and gives up. 10 s later the server window shows
  `has been on foot for 10 s … is free again`.
- [ ] **Joining in heavy traffic:** while the host drives through heavy traffic, the guest connects and goes to the
  host: no frozen cars on the guest's screen.
- [ ] **Upside down:** turn a car upside down inside an open garage and wait 30 s. The car doesn't keep vanishing and
  coming back (at most 3 times in 30 s); no crash.

### A2. Alt+Tab

"A" alt-tabs, "B" watches; each of you is A once.

- [ ] **On foot:** A runs holding W, alt-tabs and stays on the desktop for 10 s. On B's screen A's character stops at
  once. Back in the game, A's character moves normally.
- [ ] **Driving:** A accelerates holding W, alt-tabs and waits 10 s. On B's screen the car doesn't accelerate; it
  coasts and slows down as if off the gas (it may hit something). When A is back, the car may jump to its real place;
  that is expected.
- [ ] **Aiming and firing:** A aims with an automatic weapon (Tec-9, AK; right mouse button) and keeps firing (left
  button), then alt-tabs. On B's screen the firing stops at once and A lowers the weapon. Try it while only aiming,
  too.
- [ ] (Optional) Alt+Tab during a drive-by as a passenger: the firing stops on B's screen.
- [ ] **Two minutes away:** A stays alt-tabbed (or minimized) for 2 minutes while B drives around and writes in the
  chat: nobody is dropped, and when A is back (closing the pause menu) B is where B really is and the chat lines are
  there.

### A3. Restarting during a session

- [ ] **The host loads a save:** wait side by side for a minute on a busy street in Ganton. The host loads a co-op
  save (Esc → Load Game). On the host's screen the guest's NPCs drive their cars on; no frozen car without a driver.
  The server window shows `started over: N peds sent again`.
- [ ] **A guest starts a New Game:** the same, with the guest choosing Esc → New Game. The host's traffic and
  pedestrians come back on the guest's screen.
- [ ] **During the host's mission:** first the guest saves their own game in the house on Grove Street. The host
  starts #4 Tagging Up Turf; while Sweet is on screen, the guest loads that save (or starts a New Game). On the guest's
  screen Sweet is where the host sees him, moving, with his radar blip.
- [ ] (Optional) In #6's chase, while driving their Greenwood, the guest starts a New Game: the server window shows
  `… of the host's mission goes back to … started over`; the Greenwood stays on the host and the chase goes on. When
  the guest's game is back, the Greenwood is where it was left, as one copy.

### A4. Story markers and side activities

- [ ] **Story marker:** while the host is away, a guest walks into a story marker alone (Sweet's house, Ryder or
  Smoke). No mission starts. It starts when the host walks in.
- [ ] **Side activities:** on their own, the guest buys clothes at Binco or gets a haircut at Reece's, starts the
  taxi side mission (key 2 in a taxi) and, if it is open, works out at the Ganton gym. All of it works.

### A5. Trains

- [ ] **Same train at Unity Station:** wait together at Unity Station until a train arrives. Both screens show the same
  train (Brown Streak or Freight) with the same number of carriages, stopping at the platform and leaving together.
- [ ] **Riding as a passenger:** when a Brown Streak stops at Unity Station, a guest gets into a passenger carriage with
  G, then the host gets into the locomotive or another carriage. Both ride to Market Station and get out there; neither
  player stands frozen at Unity Station on the other screen.
- [ ] **Skip to the next station:** while a guest rides inside a passenger carriage, they press the skip button shown in
  the help box: the train skips to the next station for everyone aboard.
- [ ] **A guest drives a train:** a guest takes the driver's seat of a locomotive and drives it along the track (and
  can start the Freight side mission with key 2); the host sees the train move and stop smoothly. When the host drives a
  freight train too fast into a curve until it derails, the guest sees the carriages derail too.
- [ ] **A guest rides a train far from the host:** while the host stays at Grove Street, a guest boards a train at
  Unity Station and rides it past Market Station (> 300 m from the host): the train doesn't vanish under the guest.
- [ ] **Reconnecting while a train is nearby:** while a train stands or moves near the host, a guest disconnects and
  reconnects: the train appears on the guest's screen.

## B. In missions

When a guest leaves during a mission, the host's `CoopAndreas_ped_sync.log` shows `stand-in=kept`, and when the
mission ends, `stand-in=released`.

### B1. #3 Ryder: a guest quits at the barber's

- [ ] Both players go into Reece's barber shop, the **guest first** (so that the barber is created on the guest's
  game).
- [ ] While the host gets a haircut, the guest quits with **Quit Game** in the menu.
- [ ] Expected: no crash on the host, the barber stays, the haircut finishes. The server window shows
  `ped … goes to …, who claimed it`.

### B2. #3 and #4: the mission takes you out of the car

- [ ] At the pizza place in #3 Ryder and at the first tag in #4 Tagging Up Turf, the mission takes the players out of
  the car: on every screen both get out with the normal animation, the exit doesn't play twice and the car doesn't
  drive off by itself. The logs show `exit-notice` lines.

### B3. #4 Tagging Up Turf: quitting from the menu

- [ ] Start the mission; both get into Sweet's car, the host drives.
- [ ] On the way to Idlewood the guest quits with **Quit Game**.
- [ ] Expected: no crash on the host, the chat shows `disconnected`, the guest's seat is free. The mission goes on
  for the host and can be passed.
- [ ] The guest reconnects: they see the host's mission texts, blips and markers; the mission doesn't wait for them.

### B4. #6 Drive-Thru: the guest's car at the end

On the first run (finish the mission normally):

- [ ] When the mission ends, the guest keeps driving their own Greenwood: it doesn't vanish under them.
- [ ] Then the guest gets out and walks away. If the car disappears, it disappears from both screens together; no
  frozen ghost car stays on the host's screen.
- [ ] (Optional) The same with the BMX in #2 Big Smoke.

### B5. #6 Drive-Thru: a guest's game closes during the chase

On a second run (from the save before #6):

- [ ] During the chase, while driving their own Greenwood, the guest ends the game in Task Manager (`gta_sa.exe`,
  End task).
- [ ] Expected: on the host's screen the guest stands frozen for 30–60 s, then the chat shows `disconnected`. No
  crash on the host; the Greenwood doesn't vanish but stays where it was; the chase goes on and the mission can be
  passed. The server window shows `[Vehicle] : vehicle … of the host's mission goes back to …`.
- [ ] The guest reconnects and sees the mission's texts and blips.

### B6. #9 Sweet's Girl: the mission deletes the guest's car

A forced case: the mission normally deletes only its own cars.

- [ ] After the attackers at the car park are dealt with, the guest takes one of the Ballas' Sentinels and drives to
  Grove Street with the host.
- [ ] As the last cutscene starts, the mission deletes that car (unless the host is in it).
- [ ] Expected: the car goes from both screens at once, the guest is left on foot, no crash. The guest's
  `CoopAndreas_vehicle_sync.log` shows `removal-applied`, the host's `host-mission-delete`.

## C. In every mission and in free roam

- [ ] The guests see the story characters (Sweet, Smoke, Ryder, Emmet …). The server window shows no yellow warning
  starting with `tried to spawn a ped with special model`; if it does, report the name in that line.
- [ ] No doubled, frozen or floating cars, and none that vanish by themselves.
- [ ] **Long session:** at the end, every player's `CoopAndreas_vehicle_sync.log` is under 2 MB and was written until
  the end, mostly with the once-a-minute `routine` lines. The `clock:` line in `CoopAndreas_diag.log` shows a small
  `ignored` count.

## D. The 0.1.2 fixes

### D1. #8 Drive-By: the Ballas get out of their cars

- [ ] The host and one or two guests play #8 Drive-By and drive the Greenwood to the first group of Ballas.
- [ ] Expected: the Balla in the Voodoo gets out on every screen; no game crashes.
- [ ] Go on to the second group with a Voodoo, then finish the mission.
- [ ] After the mission, the guests' `CoopAndreas_ped_sync.log` has no `replay-dropped` line with `opcode=05CD`.

### D2. Mission characters getting out of cars

- [ ] Through #1–#10, a mission character who gets out of a car does so on every screen. If one doesn't, or freezes
  in the car, note the mission and the moment; the `leave-car-no-vehicle` and `sequence-task-left-out` lines in
  `CoopAndreas_ped_sync.log` help.

### D3. A dead player when a mission starts (if you can)

- [ ] While a guest is dead (the Wasted screen is still on), the host starts a story mission.
- [ ] Expected: the host's game doesn't crash. The mission may fail at once; the host can start it again.

## E. Host, saves and the host's world

### E1. Story host and reconnecting

With `story-host` set on the server:

- [ ] **A guest connects first:** the server window shows `No host until the story host … connects`; the guest's chat
  shows no host line.
- [ ] **The host connects:** the host sees `You are the host now`, the guests `<host> is the host now`, and the host's
  contact markers appear on the guests' maps.
- [ ] **A guest leaves and reconnects:** the server window shows `stays the host`.
- [ ] **The host quits mid-mission,** during a cutscene or while the controls are off: on the guests' screens the
  cutscene ends within about a second, camera and controls come back, the traffic is normal again, and the mirrored
  blips, markers and counters go; no crash.
- [ ] **The host comes back:** wait until the server window shows `[Game] : N Disconnected.` (30–60 s), then the host
  reconnects and is the host again; the guests get the blips back. Reconnecting earlier gives `nickname taken`: close
  the game and wait.

### E2. A guest's state in the host's save

When the host saves, each guest's weapons, health, armour, money, clothes, fighting style, perks, stats and respect
(from missions, kills and clothes) go next to the save as `GTASAsf<n>.b.coop`. They are matched by nickname, so keep
yours. Wait about 3 s after the last change before the host saves. Over #2–#10 a guest's mission respect adds up to
**34** (#2 3, #3 3, #4 3, #5 3, #6 5, #7 4, #8 6, #9 7, #10 0).

- [ ] **Across sessions:** the guest buys a weapon, armour and clothes and has some money. The host saves at a
  safehouse (`Kept the state of 1 other player(s)`). Everyone quits; the host loads that save, the guest connects with
  a New Game. The guest's chat shows `Your saved player state is back (mission respect N, respect R, clothes C)`, and
  their things and respect are back. The next mission's contact shows for everyone; no old mission blips.
- [ ] **After a crash:** the guest changes something, waits 3 s and closes the game. Once the host sees
  `<name> disconnected` (30–60 s), the guest connects with a New Game and gets their last state.
- [ ] **Rollback:** the host saves (the guest's state is X). The guest changes their state (Y). The host loads that
  save while the guest stays connected; once on foot, the guest is back to X.
- [ ] **Saving while a guest is away:** the guest reaches state Z, waits 3 s and quits. The host saves. In the next
  session the guest connects and gets Z.
- [ ] **A save without guest state:** loading it shows `This save keeps no other player's state`. With a co-op save
  from 0.3.103, the guest's chat shows `no respect in this older save`: their things come back, their respect stays as
  it is.

### E3. The host's world

- [ ] **Mod garages:** after #10 a guest can drive into Loco Low Co (Willowfield, with a lowrider) and the Los Santos
  Transfender (Temple, any car), and the mod menu works. The other mod garages stay closed.
- [ ] **A guest restarts:** while connected, the guest starts a New Game; after the intro the Binco and barber doors
  and the two mod garages are still open.
- [ ] **Traffic after a failed mission:** in #2 the traffic thins out when the BMX ride starts. Fail the mission on
  purpose after that point: on the guests' screens too, cars and pedestrians come back within 1–2 s.
- [ ] **Wanted level cap:** outside a mission a guest commits crimes: they can't get more stars than the host's cap
  (4 at this point in the story).

### E4. Startup resolution

- [ ] The Device Selection dialog at startup lists your display's widescreen (16:9) modes, such as 2560×1440×32
  where the display supports it; you can pick one and reach the game menu, the picture is right in game, and you can
  pick it again after a restart.

## Missions #1–#10

Play them in order from a New Game. In each mission, fail it on purpose **before** passing it, then start it again
and pass it.

In every mission:

- [ ] The start, the cutscenes and the fades finish on every screen; no guest is stuck inside, in a car or on a black
  screen, and control comes back when it should.
- [ ] The guests see the mission title (bottom right) and every objective text, dialogue line and help box **once**;
  the blips on characters, cars and destinations in the same colour and size, changing and going away together (nobody
  sees a blip on themselves; a new character's blip may show on the radar only for up to half a second); the yellow
  mission markers in the same places (but not on the street while the host is inside a building).
- [ ] Pass: "Mission passed" and its tune on every screen; money and respect for everyone, the host's not doubled.
  **Not every mission pays money.**
- [ ] Fail: "Mission failed" and the reason on every screen; the help box goes. A player's death or arrest has no
  reason text of its own. A guest who dies or gets busted fails the mission too.
- [ ] After a pass or a fail, no blip, marker, counter, locked camera or controls, or black screen is left; a failed
  mission can be started again from its normal start.
- [ ] Once during a mission, a guest closes the game and connects again with a New Game: they get the current
  objective, the blips, the marker and the counters. Once, a guest alt-tabs for 30 s: back in the game the texts and
  blips are current.

### #1 In the Beginning (`INTRO`)

- [ ] A New Game reaches the start on Grove Street. The airport cutscene and the first BMX ride are skipped; that is
  normal.

### #2 Big Smoke (`INTRO1`)

- [ ] Start together. Each guest gets their own BMX with a blue blip (they also see the blip on the host's BMX; that
  is normal); getting on and off it brings and removes their own blip and the "get on the bike" text. Dialogue and
  objectives once, one marker on Grove Street. The ride and the way home finish without getting stuck.
- [ ] A guest more than 60 m behind Sweet gets a "too far" warning; it goes when they catch up.
- [ ] Pass: respect for everyone (+3). Do E3's traffic check in this mission.

### #3 Ryder (`INTRO2`)

- [ ] The barber and pizza parts, the restaurant scene and the way back to Grove finish; the guests get control back.
  Play the scene where the restaurant worker switches to a shotgun once watching it and once skipping it: no crash or
  freeze.
- [ ] The guest's Greenwood has a blue blip; "drive to the red marker" and "take Ryder home" follow the guest's own
  position. One marker at the barber's and at the house.
- [ ] Pass: respect (+3).

### #4 Tagging Up Turf (`SWEET1`)

- [ ] A guest finishes one Grove tag alone, and the mission goes on without the host spraying it again. Share the
  other tags; spraying the same tag together doesn't set it back. With two players, ride in Sweet's car together.
- [ ] One blip on Sweet's car. The guest's "go with Sweet" and "get back in Sweet's car" follow the guest's own state;
  on the roof the help about the fence reaches the guest too.
- [ ] Pass: **$200**, respect (+3) and spray paint (1,000) for everyone.

### #5 Cleaning The Hood (`SWEET1B`)

- [ ] Fight together outside and in the house; the placement going in and out is right, and control is free at the
  end.
- [ ] Each objective once; one blip or marker on each target.
- [ ] A guest more than 30 m from Ryder gets "too far"; back within 10 m, the current objective shows again.
- [ ] Pass: respect (+3).

### #6 Drive-Thru (`SWEET3`)

- [ ] After the first cutscene each guest has their own **Greenwood** with a blue blip. The seats in the host's car
  belong to CJ, Sweet, Ryder and Smoke; a guest who tries to get in is taken out.
- [ ] The Cluckin' Bell target and the blip on Sweet's car show once; in their own car a guest has a marker of their
  own in the exit lane. In the drive-thru scene the guest's car is moved to the exit lane, and the cars don't end up on
  top of each other.
- [ ] In the chase each guest gets a **Tec-9 with 300 rounds** and two drive-by help texts, sees one red blip on the
  Ballas' car and can damage it. "Kill the Ballas" shows once.
- [ ] Fail: lose the host's mission car or let a homie die.
- [ ] Pass: **$200** and respect (+5) for everyone. The guest keeps the Tec-9; that is normal.

### #7 Nines And AK's (`SWEET2`)

On 0.3.102 the host vanished from the guest's screen in this mission and the guest's game crashed at the Binco door;
the fix (0.3.103) hasn't been tested in game yet.

- [ ] Play #7 from start to end. The guest sees the host **all the time**, with the name tag above the host on foot.
  If the host vanishes, they should be back within about a second; note the time.
- [ ] At Binco the host goes in and out while the guest watches from outside: no crash; the host is invisible inside
  and visible again on the way out. Outside the mission the host also goes through two or three other doors (Sweet's
  house, a Cluckin' Bell): the same.
- [ ] The guest gets into the **rear right seat** of Smoke's car. One blip on Smoke's car, one marker at Emmet's; a
  guest out of the car for more than 1.5 s gets "get back in Smoke's car".
- [ ] At Emmet's the guest must be within about 10 m of the target too; while they are away, the mission waits.
- [ ] The host does the bottle practice. For blowing up the car, the guest is moved to a shooting spot of their own
  with a pistol and a target; the guest's shots can blow up the car and move the mission on. Don't go near the car as
  it explodes.
- [ ] One marker at Binco; the shop part is the host's, the guest waits outside.
- [ ] Fail: lose Smoke, Emmet or Smoke's car in their phase; the reason shows once, and the guest's pistol drops to 5
  rounds.
- [ ] Pass: respect (+4); no money.

### #8 Drive-By (`SWEET4`)

- [ ] Each guest gets their own **Greenwood and a Tec-9 with 1,000 rounds**; the host's car is for CJ and three
  homies. (Do D1 in this mission.)
- [ ] While a guest is on foot, the first drive doesn't go on; if a guest falls behind, the host waits, and the
  mission goes on once they are close.
- [ ] One red blip on each Balla; the guest gets "cruise the neighbourhood" and helps by shooting from their own car.
  Later the guest gets 2 stars too; the wanted and Pay 'n' Spray texts show once.
- [ ] The health bar of the host's car shows on the guests' screens and its value changes.
- [ ] In the police phase the mission's wanted level applies on every game; Pay 'n' Spray goes by the host's stars.
- [ ] Fail: let a homie die or lose the host's mission car.
- [ ] Pass: **$500** and respect (+6) for everyone; the health bar and the blips go.

### #9 Sweet's Girl (`HOODS5`)

- [ ] The guest is moved next to the host and gets a pistol; the rescue target, and one red blip on each attacker.
- [ ] Sweet's health bar shows on the guests' screens and its value changes.
- [ ] In a four-door car, Sweet and his girlfriend take the front right and rear left seats and the guest the **rear
  right** one; a guest already sitting in one of those seats is moved to the right one without being deleted.
- [ ] One blue blip on whoever falls behind (Sweet or his girlfriend).
- [ ] Back at Grove the guest gets out and is placed on foot for the handshake scene.
- [ ] Fail: let Sweet or his girlfriend die.
- [ ] Pass: respect (+7); no money.

### #10 Cesar Vialpando (`SWEET6`)

- [ ] The garage text shows once (not twice in a row); one marker at the garage and at the meeting. The mission waits
  until the guest is within about 10 m of the garage. During the mission the host uses the mod garage alone; the guest
  waits outside.
- [ ] After the tuning the guest rides in the lowrider's back seat or follows it; at the meeting they must be within
  about 15 m.
- [ ] Only the host plays the bet and the rhythm game. **The guest's money doesn't change during the bet.** The
  contest's dialogue and fail texts show once.
- [ ] Fail: the host loses a round; the result text reaches the guest, and camera and keys go back to normal.
- [ ] Pass: the money on the "Mission passed" screen goes to everyone (twice the bet, though the guest didn't bet).
- [ ] After the mission, do E3's mod garage check.

The plan stops at #10: the missions from #11 on aren't rebuilt for co-op yet.

## Known limits

- A guest who comes back during a mission takes no part in it: they see its texts and blips and join normally from
  the next mission.
- If the host's own connection drops during a mission, some missions (#2–#5, #7) can hang while they wait for a
  vehicle (a fix is planned).
- A guest who starts getting into a car and gives up counts as sitting in that seat on the server for 10 s.
- A task a mission gives a character at the moment it creates it doesn't play on the other screens (an old gap, no
  known effect in #1–#10).
- Mission objects (crates, doors, barrels) aren't synced yet (`docs/design/mission-objects.md`). Standing on top of a
  moving train without script attachment (`T3`, #74), race checkpoints (#22), gang territories and wars (#25) and the
  bridge barriers (#28) come in later packages.
- Messages meant only for the host (such as "get in Sweet's car") show on the guests' screens too.
- A guest's gym texts can disappear while the host's mission shows texts.
- Guests shouldn't buy properties: their purchase isn't saved anywhere and the money is gone.
- Alt+Tab pauses that player's game; if it is the host, the mission waits too.
- A player whose game crashed can connect again only after the server has dropped the old connection (30–60 s).

## Reporting

Report each problem as a GitHub issue with the
[bug report template](https://github.com/sametozbalkan/project_gta_sa_coop/issues/new?template=bug_report.md), one
problem per issue. Give:

- the version (bottom-left corner), **whether you hosted** and **the number of players**;
- the mission and its phase (or free roam), what you expected and what happened, and the steps to make it happen
  again;
- **the time** by your PC's clock, so that it can be found in the logs;
- the logs, from every player if you can. With the diagnostics shortcut the game writes these files into the game
  folder (`C:\Games\GTASA_Coop` unless you chose another one):
  - `CoopAndreas_diag.log`: a status line every 2 s (frame, game time, network, running scripts); it keeps going when
    the game hangs;
  - `CoopAndreas_session.log`: connecting, host changes, missions starting and ending;
  - `CoopAndreas_mirror.log`: what the host's mission shows and what reaches the guests;
  - `CoopAndreas_world_state.log`: the host's world (city unlock, garages, shop doors, mission settings);
  - `CoopAndreas_ped_sync.log`: characters and players (kept, recreated, removed);
  - `CoopAndreas_vehicle_sync.log`: vehicles (which game drives them, removals, exits);
  - `CoopAndreas_train_sync.log`: trains and vehicle attachments (carriages, copies, derailments, attachments);
- after a crash, the newest file in `CoopAndreas_crashes\` in the game folder (it is written with every shortcut);
- the last lines of the host's server window around the problem.

Leave out personal data: no Steam names and no IP addresses. Nicknames show in the chat and the server window and are
often Steam names, so replace them with "host", "guest 1" and so on before you post.

## Results

| Check | Result | Notes |
|---|---|---|
| A1 Driving together | | |
| A2 Alt+Tab | | |
| A3 Restarting during a session | | |
| A4 Story markers, side activities | | |
| A5 Trains | | |
| B1 #3 barber | | |
| B2 #3, #4 taken out of the car | | |
| B3 #4 quitting from the menu | | |
| B4 #6 the guest's car at the end | | |
| B5 #6 game closed during the chase | | |
| B6 #9 the guest's car deleted | | |
| C In every mission | | |
| D1 #8 Ballas getting out | | |
| D2 Characters getting out of cars | | |
| D3 Dead player at a mission start | | |
| E1 Story host, reconnecting | | |
| E2 A guest's state in the save | | |
| E3 The host's world | | |
| E4 Startup resolution | | |

| Mission | Pass | Fail and retry | Clean-up | Notes |
|---|---|---|---|---|
| #1–#5 | | | | |
| #6 Drive-Thru | | | | |
| #7 Nines And AK's | | | | |
| #8 Drive-By | | | | |
| #9 Sweet's Girl | | | | |
| #10 Cesar Vialpando | | | | |
