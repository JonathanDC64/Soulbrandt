# 137. The Discord status

Date: 2026-11-07. Status: built and checked against a stand-in for Discord. It shows nothing until the project has
its Discord application (below).

## What it shows

On the player's Discord profile, while the game runs and the Discord app is open on the same PC:

- **Playing Soulbrandt (Demon's Souls)**: the name of the project's application on Discord's developer portal.
- **The area**: The Nexus, Boletarian Palace, Shrine of Storms, Tower of Latria, Valley of Defilement, Stonefang
  Tunnel, Tutorial. "At the title screen" before a game is loaded.
- **Online or Offline**: whether online play is on (DES_ONLINE).
- **The time played** this session (Discord counts it from the start).
- **A button, "Soulbrandt on GitHub"**, which opens the project's page. Other people see it on the profile; Discord
  does not show buttons to the player themselves.

The row: **Settings > Network > Discord status** (DES_DISCORD), on by default, live. Turning it off clears the status
at once.

## How it works

- Discord's app listens on a local pipe (`\\.\pipe\discord-ipc-0` to `-9` on Windows) or socket (`discord-ipc-0` to
  `-9` in `$XDG_RUNTIME_DIR`, `TMPDIR` or `/tmp`, and Flatpak's and Snap's folders on Linux). The port writes to it;
  nothing goes to the network from the port. Each message is 8 bytes (kind, length) and JSON:
  a handshake with the application id, then `SET_ACTIVITY` with the status.
- `port/ui/src/discord_ipc.cpp`: the pipe or socket, the messages and the JSON. `discord_presence.cpp`: a thread of its
  own that finds Discord (every 15 s while it is closed), sends the status when it changes (at most one every 4 s;
  Discord takes 5 in 20 s), answers Discord's pings, and clears the status when the row is turned off.
- The area comes from the game: the port sees which `script/mNN.luabnd` it opens (`des_options.cpp`).
- What is sent: the application id, the area name, Online or Offline, the session's start time, the button. Nothing
  about the player's character, account or PC.
- Test runs leave it off unless DES_DISCORD is given, so no test touches a real Discord.

## What the project needs

An application on Discord's developer portal (discord.com/developers/applications), made by the project's owner:

1. **New Application**, named **Soulbrandt (Demon's Souls)**. That name is what profiles show after "Playing".
2. Copy its **Application ID** into `kAppId` in `discord_presence.cpp`. The id is not a secret: every program with a
   Discord status sends its own in the clear.
3. Optional: **Rich Presence > Art Assets**, an image (512x512 or larger) under a key such as `logo`, shown beside the
   status. Use the project's own art, not the game's or the publisher's. Then set the key as the status's large image
   (today DES_DISCORD_IMAGE=<key> for a try).

Until step 2 there is no id, so the thread does nothing.

## What else it can do

Discord's status has more fields; none of these are built yet.

- **Clickable lines**: the area line and the image can open a link (Discord added these in 2025).
- **A second button**, for example the Discord server or the latest release.
- **A small picture** in the corner of the large one: body or soul form, or the character's class.
- **Party size** in co-op ("In a party (2 of 3)"), from the online code's session.
- **More detail**: Soul Level or World Tendency, read from the game's memory as the HUD code does.
- **Ask to Join**: Discord's invite button. It would need the online code to accept a join request (summoning through
  Discord). A large piece of work.

## Knobs

- DES_DISCORD: 1 (default) shows the status, 0 none. A test run needs DES_DISCORD=1 given to try it.
- DES_DISCORD_APP=<id>: another application's id (testing with a private one).
- DES_DISCORD_IPC=<path>: the pipe or socket to use, in place of Discord's (the check below).
- DES_DISCORD_IMAGE=<key>: the large image's asset key.

## Checks

`port/ui/tests/discord_check.cpp` (target `discord_check`, CI on Windows and Linux): the messages and JSON, then a
whole exchange with a stand-in for Discord on a pipe or socket of the test's own (handshake, READY, a status, a ping
and its pong, Discord's close with its reason).
