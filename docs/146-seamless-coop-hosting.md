# 146. Seamless co-op: hosting and joining

Seamless co-op lets friends play through the game together. One player's game hosts; the others join it with an
invite code. **No online server is used**: not this project's, and not anyone else's. Only the friends' games talk
to each other.

This page covers three parts:
- **Part 1, connecting:** how a game hosts, how a friend joins, and what each game does with the network.
- **Part 2, the session in the game:** joining with no sign from anywhere, following the host with one loading screen,
  boss kills and deaths.
- **Part 3, progress for everyone:** what the group does together counts in each friend's own world too, each player
  takes their own Demon's Soul, and a friend's own world is kept apart while they play in the host's.

## For players

**To host:**

1. Open **Co-op** (on the start screen, or F1 > Co-op) and choose **Host**. The game starts again on your co-op
   characters.
2. Windows may ask whether the game may use the network. Choose **Allow**, or your friends can't connect.
3. Open **Co-op** again. It shows your **invite code**, like `7KQ2-M9XD-0RTB-4HWE`. **Copy code** puts it on the
   clipboard. Send it to your friends.

**Don't post the code in public.** It holds your internet address. Send it only to the friends you play with.

**To join:**

1. Copy your friend's invite code.
2. Open **Co-op**, choose **Join**, then **Paste and join**, or **Type code** to type it (with a keyboard or
   the controller). The game starts again on your co-op characters.

**To leave:** **Co-op** > **Leave co-op**. A friend's game first takes them back to their own world, where it saves
what the group did; the host's leave sends its friends back to theirs. Then the game starts again on your normal
characters.

**Co-op characters** are kept in their own save folder (`savedata-coop`, beside the normal one). Co-op never writes
your normal characters.

**Up to 4 players** (the host and three friends).

**In the game:**

- **Joining needs no sign.** Once the host stands in an area, a friend's game joins by itself, wherever the friend
  is. The friend loads the host's area and arrives beside the host as a blue phantom.
- **Body form or soul form.** A friend in body form joins in soul form, since the game summons only soul-form
  players, and gets its body form back when it goes home.
- **Friends follow the host.** When the host travels (an Archstone, the Nexus, a death), its friends go too: one
  loading screen, then they stand beside the host again.
- **Archstones:** the host can use them during co-op, and the group travels with it. A friend still gets "You can't
  return during multiplayer."
- **Boss kills keep everyone together:** no rating screen, nobody sent home, and the boss room's walls drop.
- **A friend who dies** comes back where the host last arrived (the start of the area) and stays in the session.
- **When the host dies,** everyone goes back to the start together.
- **Progress is everyone's.** A boss killed, or a door, lever, elevator, shortcut, bridge or fog wall opened while
  friends are in the host's world counts in each friend's own world too.
- **Each player takes their own Demon's Soul** from a boss, where the host takes theirs.
- **Your own world stays yours.** In a friend's world you see it as it is there. Your own world's doors, items and
  people stay as they were, and what the group did is added when you go back.
- **Going back:** when the host leaves, its friends go back to their own worlds, where they stood when they joined,
  and their games save there. A friend goes back in body form if it was in body form before the join, or was there
  when a boss died.

### If your friends can't connect

The host's router has to let friends in on one port (36100, TCP and UDP).

- The game asks the router to open it (UPnP). Co-op shows whether that worked.
- **"No router answered"**, or UPnP is off on the router: forward port 36100 (TCP and UDP) to the host's PC in the
  router's settings.
- **"Behind another router"**: the router that opened the port isn't the one facing the internet. This happens with a
  second router, or a provider that shares one address between many homes (carrier-grade NAT). Forward the port on
  the outer router too, or ask the provider for a public address.
- **Players on the same network** can use the **local code** that Co-op shows below the invite code.

## How it works

```
 host's game                                          a friend's game
 ┌──────────────────────────────────┐                ┌──────────────────────────────┐
 │ the game  ── 127.0.0.1 ──► room  │◄── the code's ─┤ the game                      │
 │ (NP, P2P)                  server│    address,    │ (NP, P2P)                     │
 │                         (port    │    one port    │                               │
 │ the game's own servers   36100)  │                │ the game's own servers        │
 │ (cellHttp) ─► answered here      │                │ (cellHttp) ─► answered there  │
 └──────────────────────────────────┘                └──────────────────────────────┘
```

- **The room server** is ps3recomp's `psnr_host` (its `docs/ONLINE.md`, "A game one player hosts"). It runs in a
  thread of the host's game, and speaks the same protocol as a psnr server, so the game's matchmaking code is the
  same as online. The host's own game connects to it at 127.0.0.1; friends connect to the address in the code.
- **One open port.** Every player's game traffic goes through the room server's port, so friends need no open port
  and only the host's router is involved.
- **The key.** Each time a game hosts, it draws a new key. The code carries it, and the room server turns away any
  game without it. An address that sends wrong keys is ignored for a minute after five.
- **The game's own online server** (messages, signs, ghosts, world tendency, login) is answered inside each game, by
  `des_seamless.cpp`, with nothing in it: no messages, no signs, no ghosts, and a neutral world tendency. The answers
  follow the open-source dessego server's.
- **Nothing else is reached.** No name the game looks up (`c.demons-souls.com` and the rest) resolves, and no
  sign-in happens. A gameplay mod keeps co-op off, as it keeps online off (docs/143).
- **Which discs:** the US and European discs play together (the same program and game data). The Japanese and Asian
  discs can't host or join yet: their 1.04 program is another build, not yet tried with the others.

### The invite code

16 letters and digits in groups of four, from Crockford's base 32: no I, L, O or U, so no two characters look alike.
Reading a code back ignores case, dashes and spaces, and takes O as 0 and I or L as 1.

It holds 80 bits: the host's IPv4 address (32), the port (16), the key (28) and a check (4). The check catches most
typos ("That code has a typo").

## How the session works

`des_seamless_game.cpp` drives the game's own summon code by address. `des_seamless_scripts.cpp` edits the area scripts as the game reads them. Nothing runs unless co-op
started, and no game file changes.

**Joining with no sign:**
- The host's game opens its room with the step a sign's touch uses. It then builds the room's data as the sign code
  does for a summon, and sends it to its friends through the room server.
- A friend's game joins with that data, as with a sign's answer. Its join is a summoned reload.

**One load for every arrival.** A friend's summoned reload is held at the game's load step (an instruction hook).
There, the map it loads is renamed to the host's area, in memory and with the same length. Its place is set beside the
host. So the same step serves:
- a join, wherever the friend stood;
- a follow: when the host's own load starts, the friends start a summoned reload, which waits on the loading screen
  until the host has arrived;
- a respawn after a friend's death.

**Placing the players again.** A load drops the other players' characters, and the game places a player only when it
joins the room. So after each load, each game puts the other players back on its list of players to place, as the
game's own join does.

**The host keeps its session.** The game's leaves on a load are skipped on the host. Its session ends with co-op.

**Messages between the games** go through the room server (`psnr_app_send`, ps3recomp):

| Tag | From | Body | Meaning |
|---|---|---|---|
| `ROOM` | host | the room's data | join with this |
| `ROM?` | friend | none | send the room's data |
| `LEAD` | host | its load count | the host's load started: follow |
| `REDY` | host | load count, area, where it arrived, where it stands, its area's dead characters | after each load, every 2 s, and when asked |
| `HER?` | friend | none | send a `REDY` now (a friend's respawn) |
| `FLGS` | host | area, offset, up to 1,000 bytes of its event flags | the host's flags, in three parts, before each `REDY` |
| `FLAG` | host | flag, area | a progress flag was set in the host's world |

**The flags** (event flags: bit `n & 7` of byte `n >> 3`, 17,024 of them):
- **Each world has its own flags,** flags 0-3,199 and 8,192-12,287; the rest (bosses, for one) are shared by every
  world. A load into another world puts the old world's own flags in its bank and takes the new one's.
- **A blue phantom plays with the host's flags.** The join brings them, and each summoned reload puts them back. In
  co-op the host sends its flags before each `REDY`, so a follow or a respawn gets the host's world as it is now.
- **A friend's own world is kept apart.** At its join the friend's game keeps a copy of its own flags, and the
  program keeps its own world banks. A load into another world on a friend would put the host's flags in the
  friend's bank and the friend's own flags in play. So after each load in a session the program puts the friend's
  banks back and the host's flags in play. The trip home puts the friend's own flags back (the game's own step),
  and its save then holds its own world.
- **Progress for everyone.** From each area script as it is read, the program lists the progress flags:
  - every flag a boss kill's handlers set (the events whose handler starts the boss clear, `CustomLuaCallStart(4050)`);
  - the events whose header comment names a door, lever, elevator, shortcut, bridge or fog wall, and names no
    treasure, item, corpse, shop, talk, checkpoint, host-only wall or cutscene.

  When one is set on a friend in a session, or the host says one was set in its world (`FLAG`), it goes into the
  friend's own world: its own copy for shared flags and its home world's, its kept bank for another world's. Flags
  only move forward: a flag cleared is never sent or kept.
- **Demon's Souls.** A boss's soul is offered to the host and to players in their own world; the scripts' edit offers
  it to a blue phantom too. The prompt is a network event, so one player's pickup would also fire the others'
  (they see the pickup animation) and use up their once-only prompt. The edit registers it between
  `NotNetMessage_begin` and `_end`, so each player's prompt is its own. A friend who takes it keeps the taken flag in its own world, so its own world doesn't
  offer it again; one who doesn't can still take it at the same place in its own world.

**The scripts' edits,** in memory, as each script is read (the game's Lua reader and Lua's string reader, hooked at
their first instruction):
- `global_event.lua`, all or nothing: if one edit doesn't fit, the file stays as it is.
  - A boss kill keeps a blue phantom in the world. It gets no load wait, no trip-home animation, no "Objective met"
    recall and no leaving announcement, and nothing is rolled back. The multiplayer walls drop as at a session's end.
  - No rating screen after a kill.
  - A blue phantom's death keeps it in the session: a summoned reload instead of the trip home, and no recall.
  - The host's death doesn't send the phantoms home; the host's respawn takes them along.
- Each area's script: the boss rooms' party walls drop as in solo play; a guest doesn't play the warp-out pose at a
  boss's death; the host may use the Archstones in a session; a blue phantom may take a boss's soul.

The edited copies are cached by the original's size and hash, so a script read again costs nothing.

## Settings for developers and tests

| Variable | What it does |
|---|---|
| `DES_SEAMLESS=host` / `join` | Co-op mode for this start (the Co-op window sets it when it starts the game again). |
| `DES_SEAMLESS_CODE=<code>` | The code to join. |
| `DES_SEAMLESS_PORT=n` | The host's port. Default 36100. |
| `DES_SEAMLESS_UPNP=0` | The host leaves the router alone (the port forwarded by hand). |
| `DES_SEAMLESS_ADDRESS=ip` | The host's public IPv4 address, for the code, when the router doesn't give it. |
| `DES_SEAMLESS_CODE_FILE=path` | Tests: the host writes its codes there (public, then local); a joining game with no `DES_SEAMLESS_CODE` waits for the file and takes the code from it. |

The runtime's knobs that co-op sets: `PS3_NET_ONLINE=1`, `PS3_NP_ONLINE_ID`, `PS3_NET_P2P_PORT`, `PSNR_SERVER`,
`PSNR_KEY`, `PS3_HTTP_LOCAL` (the in-process server's port), `PS3_NET_HOSTS=localhost=127.0.0.1` (so no other name
resolves) and `PS3_SAVEDATA_ROOT` (the co-op save). A value set in the environment wins, as for the Network rows.

**A friend's leave** does what the friend's game does when the host's room is gone (`global_event.lua`'s
`OnRoomDisappeared`, then `OnLeave_Limit`), through the same natives:
1. leave the session (`LeaveSession`), and ask for the player's own flags back at the next load
   (`SetFlagInitState(2)`);
2. once out of the session: back to where the player stood before the join (`SetSummonedPos`,
   `SetDefaultMapUid(-1)`), in soul form (`SetChrTypeDataGreyNext`), with a load (`WarpNextStageKick`);
3. at home, the game saves by itself. The Co-op window waits for a file written in the co-op save after the arrival
   (2 minutes at most, or **Leave now**), then starts the game again.

**Going home** (a friend's leave, or the host gone):
- **To where the friend stood.** The game's trip home reloads the area in play: a phantom of the game's own is always
  summoned into its own area. A friend here may have joined from anywhere, so at the end of `WarpNextStageKick` (out of
  a session, before the trip home) the load is pointed at the area and place the friend had at the join (the game
  manager's next area, `+0x08`, and summoned place, `+0x20`).
- **The flags that load puts in play** come from the kept banks: the swap took the new world's from a bank older than
  the kept one.
- **After a boss kill** with the friend in the session, the friend goes home revived, as `BlockClear2_3` does for a
  blue phantom: at the end of `SetChrTypeDataGreyNext`, `SetAliveMotion`, `EraseEventSpecialEffect(10000, 101)`,
  `RequestFullRecover` and `RevivePlayerNext`.
- **A friend in body form before the join:** the game summons only soul-form players. A body-form friend's join fails
  (`OnFailedJoinSession`, "Failed to join the session"), so the program calls the game's `ChangeGreyGhost` 10
  frames before the join. Going home, it gets its body form back the same way, without the revival animation.

## Not done yet

- Checked by hand so far: a boss kill with a friend, each taking the boss's soul, the friend's Leave, and its rejoin.
  Not yet: a door or shortcut opened by a friend, and that progress in the friend's save after going home.
- Closing the game window as a friend: the session is lost and nothing from it is saved (the game only saves at
  home).
- A friend in body form who leaves co-op after taking soul form for a join that never completed stays in soul form.
- The final area's Demon's Soul is offered only outside a session (`ATTR_NOSESSION`; the game has no co-op there).
- In a boss fight, a dead player watching until the fight ends. Today it comes back at the area's
  start like any other death.
- Importing one normal character into the co-op save, once.
- The Japanese and Asian discs: allowed once a test with the others works.
