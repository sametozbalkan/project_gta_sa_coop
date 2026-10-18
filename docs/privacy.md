# Privacy

This build talks only to the players you play with. It has no crash-report upload, no telemetry and no Discord
integration.

## What leaves your PC

- Game traffic to the server you connect to and, through it, to the other players: your nickname, your player's
  position and actions, vehicles, shots and the like, as in any multiplayer game. Over the Steam tunnel this traffic
  goes through Steam's relay network to the host.
- If you aren't the host: your own player state (weapons and ammo, health, armour, money, clothes, fighting style,
  perks, body and skill stats), sent to the host while you play so that the host's save can keep it for you.

## What doesn't

- No crash reports: the uploader and the lookup of a report server are compiled out (`COOP_NO_CRASH_UPLOAD`),
  whatever `report-crashlogs` says in `coopandreas.ini`.
- No telemetry or analytics of any kind.
- No Discord Rich Presence or any other Discord integration (`COOP_NO_DISCORD_RPC`).
- No serial key or account check (`COOP_NO_SERIAL_CHECK`).

## What stays on your PC

- Crash logs in the `CoopAndreas_crashes` folder of the game folder (registers, stack, loaded modules with their
  paths, active scripts) and a crash dump if you press Save Dump in the crash dialog. Share them only if you want
  to; they show your folder paths.
- With `--coopdiag` (the `CoopAndreas (diagnostics)` shortcut): `CoopAndreas_diag.log` and the other
  `CoopAndreas_*.log` files in the game folder. Attach them to a bug report only if you want to.
- On the host's PC, next to each co-op save: `<save>.coop` with the other players' state from that session,
  matched by nickname. Deleting it only means those players start that save without their items.
