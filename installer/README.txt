CoopAndreas setup

WHAT YOU NEED
- GTA San Andreas installed in Steam (the Steam version).
- At least 7 GB of free space on the target drive.
- Internet for the first install: about 880 MB of patches are downloaded (not if Patches_v2.zip is in this folder).

INSTALL
1. Extract the whole zip into a folder.
2. Double-click "Install.bat".
   - It copies the Steam game to C:\Games\GTASA_Coop and downgrades the copy to GTA SA 1.0 US.
     Your Steam install is not changed, and every file is checked.
   - It installs CoopAndreas, CoopTunnel and the shortcuts.
   - To use another folder: Install.bat -GameDir D:\GTASA_Coop
3. If Windows says "Windows protected your PC": click "More info", then "Run anyway".

PLAY (the host runs the server, you connect to it)
1. The host starts the server and the tunnel first.
2. Start "CoopAndreas Tunnel - Join" on your desktop. It finds the host by itself; keep its window open.
3. Start "CoopAndreas" on your desktop > Start Game > Connect (127.0.0.1 : 6767 is filled in) > New Game.
   Your nickname must differ from the host's.
4. If the game shows a "Device Selection" window at start, confirm it.

NOTES
- Old saves can't be loaded in CoopAndreas; always start a New Game.
- When the host saves, your own state (weapons and ammo, health, armour, money, clothes, hair, tattoos, skills)
  is stored next to the host's save. It comes back when the host loads that save and you join with New Game.
  Keep your nickname: your state is matched by it.
- If your game crashes, wait until the host sees that you left (30-60 s), then join again with New Game;
  your last state comes back.
- To help find a problem, start the game with the "CoopAndreas (diagnostics)" shortcut in the game folder.
  Then send the host the CoopAndreas_*.log files from that folder, and the files in CoopAndreas_crashes if any.
- Alt+Tab pauses your game but keeps the connection. While the host is in Alt+Tab, the mission waits too.
- The version is shown in the bottom-left corner of the game and must be the same for both players.
  If you see "Version mismatch", install the latest package the host sent you.
- If the ping jumps around on Wi-Fi, use a cable or turn off the adapter's power saving.
- To update, run "Install.bat" from the new package. The game isn't copied again; only the mod files are
  replaced, and shortcuts and files with the names older packages used are removed.
- SOURCE folder: CoopAndreas is licensed under GPL-3.0; the source of the changes in this build is there.
