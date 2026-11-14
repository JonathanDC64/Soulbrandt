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
If a code went further than you meant, choose **New code**: the old one stops working, and players already in your
game stay. The game makes a new code by itself only when someone has tried many wrong ones; Co-op then says so.

**To join:**

1. Copy your friend's invite code.
2. Open **Co-op**, choose **Join**, then **Paste and join**, or **Type code** to type it (with a keyboard or
   the controller). The game starts again on your co-op characters.

**To leave:** **Co-op** > **Leave co-op**. A friend's game first takes them back to their own world, where it saves
what the group did; the host's leave sends its friends back to theirs. Then the game starts again on your normal
characters.

**If the host's game closes,** a friend sees the game's own "The session has ended." about 13 s later and goes back
to their own world. **Co-op** then says the connection was lost. The friend's game keeps trying to reach the host's:
after a network drop, with the host still hosting, it connects again, goes home as the game ends the session, and
joins again by itself from there. If the host starts hosting again, its invite code is a new one. The friend's
game then says "The host's game did not accept this invite code. Ask the host for it again." and stops trying:
**Leave co-op**, then join with the new code.

**Co-op characters** are kept in their own save folder (`savedata-coop`, beside the normal one). Co-op never writes
your normal characters.

**Bringing a character along.** The first time you host or join, while co-op has no characters yet, the game asks
whether to bring one of your normal characters. Co-op gets a copy, and your normal character stays as it is. Nothing
from co-op comes back to it. Or choose **Start with none** and make a new character in co-op. The question comes only
once: after that, co-op has characters of its own.

**Up to 4 players** (the host and three friends). The host can take fewer: Settings > Network > **Players when you
host** (2, 3 or 4). A change counts at once, and players already in stay. A friend who comes when the game is full
waits: Co-op says so, and the friend's game joins as soon as a place frees up.

**In the game:**

- **Joining needs no sign.** Once the host stands in an area, a friend's game joins by itself, wherever the friend
  is. The friend loads the host's area and arrives beside the host as a blue phantom, facing the way the host faces. A friend watching a cutscene
  in its own world (an area's opening one, say) goes when the cutscene ends: the game's load waits for it.
- **Body form or soul form.** A friend joins in the form it has. In the host's world it is a blue phantom either way,
  with its form's health: full in body form, half in soul form (as the game gives a summoned soul). A friend in body
  form who dies is a soul from then on. A boss kill revives every friend in soul form, as it revives the host: at
  once if it stands, or when it comes back if it is dead. Each friend goes home in the form it has then.
- **The Stone of Ephemeral Eyes** works during co-op for any player in soul form, the host too: it brings the player
  back in body form, as it does alone. A friend stays a blue phantom. In body form it can't be used, as in the game.
- **Friends follow the host.** When the host travels (an Archstone, the Nexus, a death), its friends go too: one
  loading screen, then they stand beside the host again.
- **Archstones:** the host can use them during co-op, and the group travels with it. A friend still gets "You can't
  return during multiplayer.", at the Nexus's stones too.
- **The White Eye Stone** takes a friend home as its own trip home does (in body form, its own world back); co-op
  then joins the host again a few seconds later. Leave co-op is the way out of co-op.
- **Items are each player's own.** A friend sees the items lying in the area and gets drops from dead enemies, as the
  host does. Each player has its own copy: one player's pickup leaves the others' where it was. An item a friend
  takes is also gone in its own world.
- **Light walls** (the fog you press Cross at and walk through, outside boss rooms) are gone for everyone once
  one player walks through.
- **Boss kills keep everyone together:** no rating screen, nobody sent home, and the boss room's walls drop. The
  host's room stays open, during a boss fight and after it, so a friend can always join again.
- **A friend who dies** comes back where the host last arrived (the start of the area) and stays in the session.
  Once it is back, the others see it again, with its rolls, attacks and other moves.
- **In a boss fight,** a friend who dies first watches until the fight ends. Once the boss is dead, it comes back at
  the area's start. If the host dies too, it goes back with everyone.
- **Bosses get more health with friends,** as in the original game: half as much again with one friend, double with
  two. Three friends get double too (the original has nothing for three, so a boss had its solo health).
- **When the host dies,** everyone goes back to the start together.
- **Progress is everyone's.** A boss killed, or a door, lever, elevator, shortcut, bridge or fog wall opened while
  friends are in the host's world counts in each friend's own world too.
- **A boss's Demon's Soul is everyone's.** When one player takes it at the boss's Archstone, every other player in
  the session gets it too, unless it already took that soul in its own world. A friend can take it itself as well.
- **So are keys and the items only the host could take:** 3-1's prison keys, and the items pulled from the ground or
  a corpse at a prompt (1-1's sword, the glowing corpse in 5-2 and the others like them). Whoever takes one, the
  others get it too. A player who joins later and doesn't have it yet still finds it there, in the host's world and
  in its own.
- **Your own world stays yours.** In a friend's world you see it as it is there. Your own world's doors, items and
  people stay as they were, and what the group did is added when you go back.
- **Going back:** when the host leaves, its friends go back to their own worlds, where they stood when they joined,
  and their games save there, each in the form it has then (above).
- **The Nexus is shared.** Friends stand in the host's Nexus and see its people as the host does, not as phantoms,
  and can talk to them.
- **The Nexus's cutscenes** (the one after a boss, the Monumental's story) play for each player who hasn't seen them
  in their own world yet, even when the host has. So does an area's opening, such as the dragon's flight at 1-1's
  start.
- **Skipping a cutscene** (START, then Cross) works for everyone and skips only your own: the others' go on.
- **The Monumental's question:** whoever talks to the Monumental answers it. Once the other Archstones open, they open
  in every player's world. His story plays only for the one who hears it: when a friend hears it first, the host then
  gets his later lines and his question, not the story.

### If your friends can't connect

The host's router has to let friends in on one port (36100, TCP and UDP).

- The game asks the router to open it (UPnP). Co-op shows whether that worked.
- **The router's forward lasts an hour** and the game renews it every 30 minutes while it hosts. Closing the game
  removes it. A forward left behind by a crash ends by itself within the hour. A router that only takes permanent
  forwards gets one, which closing the game removes. If the router's internet address changes during a session,
  Co-op shows new codes.
- **The address in the code.** It is the router's internet address when UPnP gives one. When it doesn't (no UPnP,
  or a router behind another one), the game asks Google's STUN server (`stun.l.google.com:19302`, one request, a
  few hundred bytes) what address its traffic comes from, and uses that. Google sees the host's IP address (it is
  in PRIVACY.md). `DES_SEAMLESS_STUN=0` turns this off, and `DES_SEAMLESS_STUN=host:port` names another server. The
  address says nothing about whether friends get through: the port still has to be open.
- **"No router answered"**, or UPnP is off on the router: forward port 36100 (TCP and UDP) to the host's PC in the
  router's settings. The code already has the address from the STUN server; if it couldn't be reached, choose
  **Public address** in Co-op and give the address the internet sees (the router's status page shows it, or a "what
  is my IP" website). The invite code is made from it at once, and the game keeps it for the next time. An address
  given this way wins over the STUN server's.
- **"Behind another router"**: the router that opened the port isn't the one facing the internet. This happens with a
  second router (often your own router plugged into the provider's box), or a provider that shares one address
  between many homes (carrier-grade NAT).
  - The game only reaches the first router, even when both have UPnP on: the search for routers stays on the PC's
    own network.
  - Best: put one of the two in access point (bridge) mode, so only one router is left.
  - Or forward port 36100 (TCP and UDP) on the outer router to the first router's internet-side address. The code
    has the outer address from the STUN server.
  - With carrier-grade NAT, ask the provider for a public address.
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
- **Nothing else is reached,** apart from the STUN request above, from a host whose router gives no address. No name
  the game looks up (`c.demons-souls.com` and the rest) resolves, and no sign-in happens. A gameplay mod keeps co-op off, as it keeps online off (docs/143).
- **Which discs:** every disc plays with every other. The US and European discs are one program; the Japanese and
  Asian discs' 1.04 program is the same code at other addresses (`port/regions/`), and all four have the same game
  data. Each signs in with its own communication ID (US NPWR00881_00, European NPWR01249_00, Japanese and Asian
  NPWR00245_00), so the host's room server keeps the others' players in the US disc's rooms
  (`psnr_host_config.same_game`, as the game server's `sameGames`). Without that, a European friend's join of a US
  host failed with "no such room". Tested on this PC, each copy from setup's own lift of this branch:
  - a US host with a European friend;
  - a US host with a Japanese friend, and a Japanese host with a US friend: the join, a follow through the host's
    warp to the Nexus, and the Japanese friend's death and respawn;
  - a US host with an Asian friend: the join.

### The invite code

16 letters and digits in groups of four, from Crockford's base 32: no I, L, O or U, so no two characters look alike.
Reading a code back ignores case, dashes and spaces, and takes O as 0 and I or L as 1.

It holds 80 bits: the host's IPv4 address (32), the port (16), the key (28) and a check (4). The check catches most
typos ("That code has a typo").

**Who can join.** The room server (ps3recomp's `psnr_host`) is the only thing the router opens. The game's own
online server runs on this PC's loopback only. Every connection must say `HELLO` with the key, as a 32-byte blob
made from it, within 10 s; without it, nothing else is answered. So:
- A scan of the internet can find that something answers on the port, but can't join, list rooms or send anything
  to the players without the key.
- Guessing the key: one address is ignored for a minute after 5 wrong keys. After 100 wrong keys from any
  addresses, the key is spent: no one else gets in, players already in stay, and the Co-op window asks the host for
  a new code. A guess gets 100 tries a code, 1 in 2.7 million of finding a 28-bit key.
- One address may have only 2 connections that haven't said `HELLO` yet, and each gets 10 s, so connections that
  never send a key can't take the players' places.
- The UDP side (the probes and the relay) answers only a player's id with its token, from its `HELLO`. A probe's
  answer is smaller than the probe.
- **New code** gives the room server a new key at once. The host's own game keeps the key it started with, from this
  PC only.

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

The facing goes with the place. A summoned reload takes its facing from a rotation beside each place (`gm+0x30`,
`+0xA50`, `+0xAF0`: x, y, z, w, with y the yaw), which the game left at zero, so a friend always arrived facing one
fixed way. The host's `REDY` carries its yaw (from the player's transform, `*(*(character + 0x20) + 0x10) + 4`)
with each place, and the load step writes it there with the position. The game then turns the friend and puts its
camera behind it, as for any arrival. (Setting the transform's yaw after the arrival doesn't last: the game turns
the player back within 30 frames.)

**Placing the players again.** A load drops the other players' characters, and the game places a player only when it
joins the room. So after each load, each game puts the other players back on its list of players to place, as the
game's own join does.

**The host keeps its session.** The game's leaves on a load are skipped on the host. Its session ends with co-op.

**A player back after a death.** A dead phantom leaves the game's sessions, so the game never brings its character
back: on the other games that character stays in its death animation, faded out, and plays none of its moves after.
So a friend back in the world after its respawn says so (`BACK`). Each other game then takes that character out, as
the game does when a member leaves, and puts the player back on its list of players to place. The game makes a new
character for it, as after a load.

**Messages between the games** go through the room server (`psnr_app_send`, ps3recomp):

| Tag | From | Body | Meaning |
|---|---|---|---|
| `ROOM` | host | the room's data | join with this |
| `ROM?` | friend | none | send the room's data |
| `LEAD` | host | its load count | the host's load started: follow |
| `REDY` | host | load count, area, where it arrived and where it stands (each with its facing), its area's dead characters | after each load, every 2 s, and when asked |
| `HER?` | friend | none | send a `REDY` now (a friend's respawn) |
| `BACK` | friend | its player id | back in the world after its respawn: make its character again |
| `FLGS` | host | area, offset, up to 1,000 bytes of its event flags | the host's flags, in three parts, before each `REDY` |
| `FLAG` | host | flag, area | a progress flag was set in the host's world |
| `FLAG` | friend | flag, area | the Archstones' seal was opened by this friend: the host's world takes it |

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
  `NotNetMessage_begin` and `_end`, so each player's prompt is its own. A friend who takes it keeps the taken flag
  in its own world, so its own world doesn't offer it again.
- **The others get it too** (`pickups_for_all`). The prompt's handler (`OnEvent_<id>`) has a branch for "another
  player did it" (`Luafunc_PlaySynchroAnimation`); after it the taker now sends `LuaCallStart(<id>, 77)`, a network
  event, as the boss rooms' notices are sent. Every game registers `LuaCall(<id>, 77, "SEAMLESS_Grant_<id>")` as the
  area starts. On a message from another player (`param:IsNetMessage()`) with its own flag clear, it removes its
  prompt (`DeleteEvent`) and runs the handler without that branch (`SEAMLESS_Give_<id>`): the same items, flag and
  steps after, with no pickup animation. A player whose flag is set (it took the soul in its own world) gets
  nothing.
- **Whose flag.** On a friend the flags in play are the host's, so as an area's script is read (before the area
  starts) each of these pickup flags is made the friend's own world's (`des_seamless_area_read`). A friend that hasn't
  taken a soul sees the prompt even where the host has; one that has sees none, and gets no second one.
- **Keys.** 3-1's prison keys (`OnDistanceActionPlusAttribute(<id>, ..., ATTR_LIVE + ATTR_GREY, once)` with a
  pickup branch and an item in the handler) were for the host and players in their own world. They get the same
  treatment: each player's own prompt (`NotNetMessage`, `+ ATTR_WHITE`), the grant, and the flag mirror.
- **The host's item prompts** (`gifts_for_all`): the items a prompt offers the host alone (`possible_chr_type`, at
  `ItemBoxDist_A`), with their own pickup steps that end in `OnEvent_<id>_1`, which gives the items and sets flag
  `<id>`: 1-1's sword, 2-3's, 4-1's, 5-3's and the Nexus's, and 5-2's glowing corpse. The prompt is offered to a
  friend too (`+ ATTR_WHITE`). `OnEvent_<id>_1` first sends `LuaCallStart(<id>, 78)` (before the pickup's own
  messages, one of which removes the others' prompt), and each game's `SEAMLESS_Gift_<id>` gives the same items and
  sets the flag unless its flag is set. The flag is mirrored as the souls' are.
- **The Stone of Ephemeral Eyes** (goods 1021). Its row (`EQUIP_PARAM_GOODS_ST`, param table 3) allows it only in
  soul form in one's own world: `enable_gray` (`+0x19`) 1, `enable_white` (`+0x1A`) and `enable_multi` (`+0x1C`) 0.
  Each frame of a session the row gets `enable_multi` 1 (the host), and `enable_white` 1 while this player is a friend
  in soul form; outside a session it is the disc's again. The revival it gives (behaviour 3021, SpEffect 3024) turns
  a friend's blue phantom (type 1) into an alive player of its own world (type 0), which the rest of co-op doesn't
  count as a friend: the next frame sets type 1 again (`SetChrType`'s native, US 0x00445F30) and body form, as a
  boss kill revives it.
- **Items.** A phantom's game makes no items: the area's items (lying on corpses, made at each load, US 0x0025CAE8)
  and a dead enemy's drop (rolled by each game when the enemy dies, 0x002B2900) are made only when the player type
  the area was loaded for is body form (0) or soul form (8), and a friend is a blue phantom (1). Both reads of that
  type are hooked (0x0025CB6C, 0x002B2984), and on a friend they read 0. Nothing about these items goes over the
  network: each game has its own list, so a pickup takes the item from that player's game only, and each player's
  drops are its own rolls. Whether an area item was already taken is its flag; on a friend the flags in play are
  the host's, so that read (0x0025CC34) gives the friend's own world's instead. A friend sees the items it hasn't
  taken, whether the host has or not. One it takes is kept in its own world (the item list's flag set, from
  0x0025C5EC), as a boss's soul is: its own world doesn't offer it again.
- **The Nexus's people.** A visitor's game (a phantom's) sets up each person whose character row has byte `0xE8` set
  in a mode of its own (4): drawn as a phantom. The three places that choose it (a person's setup, US 0x002999C8, and
  its controller's two, 0x002C2588 and 0x002C3018) are hooked after they read that byte, and on a friend they read 0.
- **Talking to them.** The talk scripts ask whether the player is a phantom (their function 19), and give a phantom no
  talk prompt. The answer comes from the player's type, read through 0x0026F378 in two places (US 0x004C9600 and
  0x004C9C18): any type but 0 (alive) or 8 (soul form) is a phantom. Both calls are hooked, and on a friend they
  return 0.
- **The Nexus's cutscenes.** Each cutscene in the Nexus's script is a `RequestRemo(scene, REMO_FLAG, event, 1)` (and
  the Monumental's story a `RequestPlayMovie`), started once the event's flag is clear. A friend plays with the host's
  flags, so it would never see one the host has seen. When a friend's game reads the Nexus's script, the program
  clears each of those flags that the friend hasn't set in its own world, and the cutscene plays there. Seeing it sets
  the flag in the friend's own world, as any other. The script sets it as the area starts, before the friend counts
  as arrived, so a cutscene's flag is kept from then on.
- **An area's opening.** The same goes for the cutscene an area's start plays: its script's `Initialize` calls the
  event's handler itself (1-1's dragon, event 2:
  `if proxy:IsCompleteEvent( 2 ) == false then ... OnEvent_2(proxy,param); end`).
  Only those: an area's other cutscenes start from something a player does there (a fog walked through, a place
  reached), and play in every player's game as they did.
- **The Archstones' seal.** The events whose header names the seal (要石の封印), and the flags their handlers set, are
  the seal's. When one is set on a friend, it is kept in the friend's own world and sent to the host (`FLAG`), whose
  world sets it through the game's own flag setter. The host's own, as other progress, goes to its friends.

**The scripts' edits,** in memory, as each script is read (the game's Lua reader and Lua's string reader, hooked at
their first instruction):
- `global_event.lua`, all or nothing: if one edit doesn't fit, the file stays as it is.
  - A boss kill keeps a blue phantom in the world. It gets no load wait, no trip-home animation, no "Objective met"
    recall and no leaving announcement, and nothing is rolled back. The multiplayer walls drop as at a session's end.
  - No rating screen after a kill.
  - A blue phantom's death keeps it in the session: a summoned reload instead of the trip home, and no recall
    (`PartyGhostDeath_1` goes on to `_2` after a second, even after a boss kill).
  - In a boss fight, `PartyGhostDeath_2` waits: it looks again each second while `SeamlessBossFight` is set and
    `ClearBoss` isn't. Two timer slots take turns, so a handler never sets up its own slot. Once the host has died
    (`HostDead` sets `SeamlessHostDown`), it asks for full health and leaves the load to the host's respawn, which
    takes everyone back.
  - The host's death doesn't send the phantoms home; the host's respawn takes them along.
  - `proxy:ClearBossGauge(` also clears `SeamlessBossFight`.
  - A boss's extra health: `Lua_MultiDoping` (and `Lua_MultiDopingm04_02`, for 4-2), at the boss room's first entry,
    gives each boss part effect 7500 (health x1.5) for one blue phantom and 7501 (x2) for two. The count is the
    session's blue phantoms (`GetWhiteGhostCount`: members of type 1), so three matched neither and got no extra
    health. `if whiteCount == 2` becomes `>= 2`.
  - No room lock at a kill (`BlockClear2`'s `LockSession`; see below).
  - The cutscenes keep their solo mode in a session. Every `RequestRemo(scene, REMO_FLAG, event, 1)` passes the
    global `REMO_FLAG` as the cutscene's mode. It is 0 when a map's scripts start, `OnEvent_4034` (the session
    connected) makes it 2, and `OnEvent_4035` (the session's end) makes it 0 again. A cutscene asked for in mode 2 (a
    boss's at its fog, any asked for after the session connected) can't be skipped on a friend's game, and the host's
    skip ends only the host's. Ones asked for before 4034, such as an area's opening, were already mode 0 in a session
    and each player skips its own. `OnEvent_4034` now sets 0, so every cutscene works that way.
- Each area's script: the boss rooms' party walls drop as in solo play; a guest doesn't play the warp-out pose at a
  boss's death; the host may use the Archstones in a session, and the Nexus's stones (which had no session check)
  give a guest the levels' "You can't return during multiplayer." (10010720); a blue phantom may take a boss's soul.
  A boss fight is
  marked from its health bar: every `proxy:SetBossGauge(` (a statement of its own in every area) sets
  `SeamlessBossFight`, and `proxy:ClearBossGauge(` clears it. All of a map's scripts share one set of globals, as
  the area scripts' calls to `global_event.lua`'s functions show.
- The wandering ghosts' sightings (a scripted phantom that walks by and fades, over and over: `OnWanderFade`) are
  each player's own. The tutorial's two (`m08_00`'s 14400, `m08_01`'s 14411) loop through timers registered between
  `RepeatMessage_begin` and `_end` with no `NotNetMessage`, as the tutorial has no co-op. In a session each pass is
  a network event: the other games run it too and send theirs back, so the timers multiply. A friend's went from 1
  to over 1,000 in a few minutes and filled its event heap (INGAME, 1 MB), and its game stopped ("Transaction
  failed") and left the session. The areas made for sessions run theirs between `NotNetMessage_begin` and `_end`
  (`m06_00`'s 124); the edit does the same here. It takes every function with `OnWanderFade`, the handlers of the
  timers they register and the functions they call, and the functions whose timers start them, and makes their
  `RepeatMessage` blocks, their `OnWanderFade` and the region events that start them local. Each player sees the
  ghost when it walks into the region itself.
- No room lock: every `proxy:LockSession();` (a boss room's, as a player walks into its fog, and `BlockClear2`'s at a
  kill) becomes a comment. The lock is a byte of the session (`sm+0x2CA`) that stays set until the host's game leaves
  its room (`UnLockSession` is an empty function): the original ends the session after a boss. A locked host's game
  kicks every new join (the member handler at `0x0053D8C0`, then `KickoutRoomMember`). A friend that follows the host
  keeps its place in the room, so it never mattered there. A friend back from a trip of its own needs a new join: at
  the tutorial's end each game takes its own player to the Nexus, and the host, still in its locked room, kicked the
  friend's joins. (The friend's game then kept leaving the room it was kicked from, and the runtime took down each
  room it joined after that; fixed in ps3recomp, see "A friend kicked out of the room" below.)
- The light walls a player walks through (press Cross, a walk, then the wall is gone) are gone in every player's game.
  In the other games the wall's handler plays the walk on the walker's character and marks the wall passed
  (`Luafunc_PlaySynchroAnimation`'s true branch, then a return). The areas made for sessions drop the wall for them
  through the game's wall-delete sync (`LuaCallStart`). The tutorial's (`m08_01`'s 14291) drops it with a direct call
  in the walker's game alone, as the tutorial has no co-op: the others lost the fog, the prompt and the event, but
  kept the wall itself, so they couldn't pass. The edit makes the same call in the true branch, between
  `NotNetMessage_begin` and `_end`, wherever a handler drops its wall by a direct call to a function with
  `SetColiEnable(..., false)` and has no `LuaCallStart`; only 14291 matches.
- The Nexus's timers and checks (`OnKeyTime2`, `OnRegistFunc`) are each player's own. No phantom reaches the Nexus in
  the original game, so its script registers them outside `NotNetMessage_begin` and `_end`, and in a session each one
  is a network event: a step that fires in one game runs in the others too, where it registers the next step again.
  Each game runs its own chains from its own flags (the first visit's messages and the Nexial Binding at their end,
  the Maiden's revival, the clear items), so when two ran at once in a session each step multiplied: a friend got
  three Bindings after the tutorial's end. The edit registers all 29 between `NotNetMessage_begin` and `_end` (not
  the ones in a comment). The Nexus's other network events (the Archstones, the NPCs' events) stay as they are.

The edited copies are cached by the original's size and hash, so a script read again costs nothing.

## Settings for developers and tests

| Variable | What it does |
|---|---|
| `DES_SEAMLESS=host` / `join` | Co-op mode for this start (the Co-op window sets it when it starts the game again). |
| `DES_SEAMLESS_CODE=<code>` | The code to join. |
| `DES_SEAMLESS_PORT=n` | The host's port. Default 36100. |
| `DES_SEAMLESS_UPNP=0` | The host leaves the router alone (the port forwarded by hand). |
| `DES_SEAMLESS_ADDRESS=ip` | The host's public IPv4 address, for the code, when the router doesn't give it. Co-op's Public address keeps it in settings.ini; a report shows only whether it is set. |
| `DES_SEAMLESS_PLAYERS=n` | The most players a host takes, itself included: 2 to 4 (4 when unset). Settings > Network keeps it in settings.ini. One more connects but waits: the host answers its ask for the room with FULL, and it joins once the host answers with the room again (the room server also refuses a join past the limit, `max_players`). |
| `DES_SEAMLESS_STUN` | Unset: a host whose router gives no public address asks Google's STUN server (`stun.l.google.com:19302`) for it. `0`: never. `host:port`: that server instead. |
| `DES_SEAMLESS_CODE_FILE=path` | Tests: the host writes its codes there (public, then local); a joining game with no `DES_SEAMLESS_CODE` waits for the file and takes the code from it. |

The runtime's knobs that co-op sets: `PS3_NET_ONLINE=1`, `PS3_NP_ONLINE_ID`, `PS3_NET_P2P_PORT`, `PSNR_SERVER`,
`PSNR_KEY`, `PS3_HTTP_LOCAL` (the in-process server's port), `PS3_NET_HOSTS=localhost=127.0.0.1` (so no other name
resolves) and `PS3_SAVEDATA_ROOT` (the co-op save). A value set in the environment wins, as for the Network rows.

**A friend's leave** does what the friend's game does when the host's room is gone (`global_event.lua`'s
`OnRoomDisappeared`, then `OnLeave_Limit`), through the same natives:
1. leave the session (`LeaveSession`), and ask for the player's own flags back at the next load
   (`SetFlagInitState(2)`);
2. once out of the session: back to where the player stood before the join (`SetSummonedPos`,
   `SetDefaultMapUid(-1)`), in the form it had before the join (`SetChrTypeDataGreyNext`), with a load
   (`WarpNextStageKick`);
3. at home, the game saves by itself. The Co-op window waits for a file written in the co-op save after the arrival
   and for the game's save to finish: no save asked for (GameMan `+0xA97`, `+0xA98`) and its save task idle
   (`+0xAA4` back at 0). A save writes PARAM.SFO first and the data after, so a new file alone is too early. Then
   (or after 2 minutes, or on **Leave now**) it starts the game again. Back or Esc doesn't close that window.
   Scripted, with keys sent to the window as a friend in the host's Nexus: Esc there kept it, and the game started
   again once, after the save; three presses of Leave now within 0.2 s started it once.

A Leave outside a session:
- **During a load,** for example while the game is taking the friend home itself after the host's game closed, it
  waits for the player in the world. A load counts from the moment the game manager's next area (`+0x08`) is set,
  until the player stands in the world again. Quit Game to the title sets no next area.
- **Back from a session with no save since,** it waits for that save, as its own trip home does.
- **Otherwise** (the title screen, say) it is done at once.

The room server passes the host's progress flags to every game connected to it, in its session or not. A friend
takes only the ones that come while it is in the session, once the join has kept its own world apart. Flags from a
host's boss kill while the friend was away don't count in the friend's world.

**Going home** (a friend's leave, or the host gone):
- **To where the friend stood.** The game's trip home reloads the area in play: a phantom of the game's own is always
  summoned into its own area. A friend here may have joined from anywhere, so at the end of `WarpNextStageKick` (out of
  a session, before the trip home) the load is pointed at the area and place the friend had at the join (the game
  manager's next area, `+0x08`, and summoned place, `+0x20`).
- **The flags that load puts in play** come from the kept banks: the swap took the new world's from a bank older than
  the kept one.
- **The form it goes home in.** The session's start records the player's form (GameMan `+0xA90`: 0 body form, 8 soul
  form), and on the trip home `SetChrTypeDataGreyNext` gives it back (from a blue phantom: soul form for 8, body form
  for 0). That word is the friend's form during the session: a death in body form (its HP at 0) sets 8, and a boss
  kill sets 0.
- **The host's game closed:** the friend sees the game's own "The session has ended." once, and goes home. Two
  things keep it to that one box:
  - The program doesn't join again while the room server (the host's game) can't be reached: the room data and the
    host's place it holds are stale. Each such join failed with two more boxes ("Failed to join the session.", "The
    session has ended."). Once np_psnr reaches the host's game again, the friend asks it for its room as at the
    start.
  - ps3recomp answers the game's `LeaveRoom` as left when the room's connection is lost (the room server has let the
    friend go). Before, the call failed with no connection, and the game tried again every 15 s, each time with the
    box.
- **A friend kicked out of the room** (`0x1103`): the game leaves that room, again every 15 s while that fails, each
  time with "The session has ended.". The room server has taken the friend out already, so it answers "not in the
  room". ps3recomp answers a leave of a room the friend was kicked out of as left, and a leave takes down only the
  room it names. Before, the leaves never stopped, and each one took down the room the friend had joined since, so
  the host's game, which never heard from the friend there, gave up and left; at every new room the same.

**Forms in the host's world:**
- **Joining in body form.** The game summons only soul-form players. Each frame of a joined guest's session (state
  6), the session update (`0x0053E578`) reads the player's type (`0x0053EA2C`). If it is body form (0), the game
  raises `OnFailedJoinSession` ("Failed to join the session") and leaves the room. On a friend the program reads it
  as a phantom (1), so the join goes on in body form. The hook first checks that the instruction there is that load
  (`lwz r0,0x8C(r9)`), as it is on all four discs.
- **A blue phantom either way.** The summoned load makes a blue phantom (1) only of soul form: the type at the load's
  request (the next load's record, `+0x8C`) must be 8, or the friend arrives in body form, not as a phantom. A friend
  in body form goes to that load as 8 (at the load step).
- **Health.** A load in soul form gives soul form's effect (SpEffect 8: the most HP x0.5) a little after the arrival.
  The scripts' effect ids are behaviours: `0x00595B80` finds the id in BehaviorParam, and the row's `+0x14` names the
  effect. Behaviour 101 is SpEffect 8, which the game's revivals erase (`EraseEventSpecialEffect(10000, 101)`). A
  friend in body form has it erased whenever its effect list (`chr+0x304`: each entry's `+0x18` is its effect, `+0x20`
  the next) holds it, then a full recover. A friend in soul form keeps it, as the game's summoned souls do (their most
  HP is full at the arrival and half once the effect comes).
- **A boss kill** (a flag of the kill's handlers, from the host) revives a friend in soul form as `BlockClear2` and
  `BlockClear2_1` revive a soul-form host: soul form's effect erased and a full recover, with no animation. The host's
  handler queues "The Demon Was Destroyed" and then "You Revived" (`SetTextEffect` 1, then 3); a blue phantom's
  queues only the first. So the friend's revival waits for its own game's "The Demon Was Destroyed" (a hook at the
  start of `SetTextEffect`'s native, `mr r3,r4` on all four discs) and queues "You Revived" after it. The kill's flag
  comes first. One that is dead at the kill is in body form from its respawn; if a load comes before the text, it
  is revived at the arrival, without the text.
- **"The Demon Was Destroyed" once.** On a friend an area's boss-death handler runs twice: for its own copy of the
  boss, and as the host's message (1-1's `OnEvent_4096`). Each starts the boss clear, so the text came twice. A
  friend shows it once per load: at the same hook a second one becomes type 0, which the text queue (`0x00722170`)
  ignores.
- An earlier version took soul form for the join (`ChangeGreyGhost`) and gave the body form back at home; a friend
  that closed its game while it waited could start again as a soul. Before that, a friend went home revived after a
  boss kill (`BlockClear2_3`'s steps at the end of `SetChrTypeDataGreyNext`).

**Bringing a character** (`des_seamless_save.cpp`). The game's save folder (the disc's TITLE_ID and `DEMONSS005`)
holds four character slots and a slot list:

| File | What it is |
|---|---|
| `<g><kk>USER.DAT` | Slot k's character (k from 0 to 3; the Profile List's first slot is 0). `kk` is empty for slot 0, else `01` to `03`; `g` is the generation (empty, `1` or `2`). The game keeps two generations, loads the newer, and on each save writes the next one and deletes the oldest. |
| `<g>04USER.DAT` | The slot list, which the game's Profile List shows. Per slot: the name at `0x21C + k * 0x140` (UTF-16, 16 characters) and the soul level `0x24` after it. At `0x20C + k`, one byte that is 1 when the slot holds a character. A deleted character keeps its name there, with a 0. |

The copy takes that slot's files as they are, the slot list with only that slot marked, and the folder's other files
(`PARAM.SFO`, the pictures), each with its time. `PARAM.PFD` (the runtime's list of secure files) keeps only the
files copied. It is made in a `.bringing-` folder and renamed into place when complete. Checked in the game: the
Profile List shows that one character, with "No profile data found." in the other three slots. The character loads
the newer generation, and saves as usual.

## Not done yet

- Checked by hand so far: a boss kill with a friend, each taking the boss's soul, the friend's Leave, and its rejoin.
  Scripted: a progress flag that a friend from the Nexus set in the host's 1-1 through the game's own setter (flag 201,
  new to the friend) was in the friend's save, among its flags for the Boletarian Palace, after it went home, saved and started
  again. Also scripted: a friend pulled 1-1's tower lever (event 10, the door beside the start) with the action
  button at the lever's own pull point. The game's event set the lever's and the door's flags (10, 11) in both
  games, the host's world sent flag 10 on as progress, and the friend kept it for its own world. (The test host had
  pulled that lever before, so a test knob cleared both flags and reloaded the area first.) Not yet by hand.
  Also scripted: a host past the first demon took the Nexus's Boletaria Archstone (put at the stone's own touch
  point, then its prompt and the "Move to Another World" menu, Boletarian Palace). The game's leave at the stone
  was held back ("the host stays in its session"), and the friend followed into 1-1 and stood beside it. A friend's
  own touch of a Nexus stone, scripted: it used to open the menu, and its warp took it out of the session to the
  area alone (co-op brought it back once that area's opening cutscene had ended). Now it shows "You can't return
  during multiplayer." and the friend stays, on the Japanese and Asian discs too. The host's trip, run again with
  the change, is as before.
- Forms, scripted (US disc): a friend in body form joined as a blue phantom with full health, in the Nexus and in
  1-1. Killed in the boss's arena, it was a soul, and the boss's kill brought it back in body form at its respawn. A
  friend in soul form standing at a boss kill was revived on the spot (the revival's glow, then full health). One
  that died with no boss killed came back with soul form's health. Each went home in the form it had then. By hand
  (two copies on one PC): a body-form join, a death, the revival at Phalanx's kill and Leave home all as described;
  that test found the texts' order and the spawn-in animation, both fixed since (scripted: "The Demon Was
  Destroyed", then "You Revived", once each). Not run on the other discs.
- What a friend's talk changes (a conversation heard, a choice made, an item given) stays in its copy of the host's
  world: it is neither kept in the friend's own world nor sent to the host. Only the Archstones' seal is shared.
- The Archstones' seal from a friend: checked through its four flags (scripted), and by hand. Host and friend
  killed Phalanx together, went back to the Nexus, and the friend talked to the Monumental first: his story played
  on the friend's game only, and the host could use the other Archstones afterwards. The host's own talk with him
  then gave his later lines and his question (the friend's answer stays in the friend's copy of the host's world).
- A friend's talk is checked as far as the Crestfallen's prompt and first line. Scripted, with the Maiden in Black:
  - with a host who hadn't met the Monumental yet, her prompt and early lines ("The Monumental awaits thee, above.");
  - with a host past the first demon, her level up. The friend answered YES to "Seek Soul Power?", then chose Seek
    Soul Power and Vitality in her menus and confirmed. Its Vitality went from 11 to 12 and its souls down by the 724
    shown. After a Leave, at home and saved, the friend's save file holds Vitality 12.

  Scripted with the storage and the smith (same host):
  - Stockpile Thomas: the friend took 96 Half Moon Grass from his storage, and after a Leave, at home, it still
    carried them and his list still showed 6.
  - Boldwin: his Forge and Repair screens listed the friend's own equipment. No forge was done.
- Closing the game window as a friend: the session is lost and nothing from it is saved (the game only saves at
  home).
- The tutorial's demon (the Vanguard, `m08_01`) offered its soul only outside a session (`ATTR_NOSESSION`; the
  game has no co-op there), so with a friend nobody got it. Its prompt is now offered with or without a session, to
  each player, like the other bosses'. Checked on the script (both of its registrations change, and only those); not
  yet in the game. (An earlier note here called it the final area's soul: `m08` is the tutorial.)
- Items for a friend: the area's items and enemy drops (above). Not covered: a drop that an item lot allows only once
  (its flag is read from the host's flags in play), and the scripted pickups (the Demonbrand, the keys) that an
  area's script offers to the host and to players in their own world only, as the boss souls were.
- The final area under the Nexus (King Allant, `m01_00`) was made for sessions: its soul prompts are the host's, as
  in the levels, so the soul edit above gives a friend its own. The solo clear bonus is skipped in a party, and only
  the host's game sets the clear state and plays the ending. Not tested.
- Watching a boss fight after dying, scripted: host and friend were put in 1-1's boss arena, and the fight started
  for both (the boss's health bar on both screens). The friend was then killed. It stayed down and watched for the
  half minute until the host killed the boss, then came back at the area's start, and the host made its character
  again. A death outside a boss fight comes back within seconds. With the host killed too, half a minute after the
  friend, the host's respawn took the friend along: both at the start, the friend standing with full health. Not
  yet by hand.
- The Japanese and Asian discs (with their 1.04 update) play with the others ("Which discs" above). What is checked
  beside the runs:
  - Co-op's rows in `port/regions/BCJS30022.inc` and `BCAS20071.inc` come from `tools/region/region_table.py find`
    on the 1.04 programs (91-100% of the instructions). The earlier rows were of the discs' first programs, which
    the port doesn't run (`WarpNextStageKick`, for one, is 0x0044A9E0 on the Japanese 1.04 program, not 0x0045B630).
  - Each function co-op calls and each hook site matches the US one instruction for instruction, with the same
    field offsets and the same registers. The one difference near them is a read the hooks don't use.
  - The fixed offsets co-op reads behind its TOC slots are the same: the session manager's `-0x7F08`, GameMan's
    `-0x7FFC`, the flags' `-0x8000`, the world's `-0x7FFC` and the sign code's `-0x7F98` and `-0x7F90`. (The code
    that loads each slot uses the same offsets from it on all three programs.)
  - The save fields a Leave waits on are the same too: all 18 byte loads and stores of GameMan's `+0xA97` and
    `+0xA98` in each program, and the save task's (`+0xAA4`) code, instruction for instruction.
  - The saves are the same files under another folder name, so one test character serves every disc.
