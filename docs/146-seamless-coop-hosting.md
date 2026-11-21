# 146. Seamless co-op: hosting and joining

Seamless co-op lets friends play through the game together. One player's game hosts; the others join it with an
invite code. **The game goes only between the friends' games.** The one server involved is this project's, and only
to introduce them: for a room code it tells the two games each other's addresses, so they can connect without an open
port (docs/162). With it off (`DES_SEAMLESS_INTRO=0`), the direct code needs no server at all.

This page covers three parts:
- **Part 1, connecting:** how a game hosts, how a friend joins, and what each game does with the network.
- **Part 2, the session in the game:** joining with no sign from anywhere, following the host with one loading screen,
  boss kills and deaths.
- **Part 3, progress for everyone:** what the group does together counts in each friend's own world too, each player
  takes their own Demon's Soul, and a friend's own world is kept apart while they play in the host's.

## For players

**Seamless co-op is still being tested.** The first time a player chooses **Host** or **Join** on a PC, a
notice says so: expect some problems, report them with **Report an issue** (friends too). It also says that the
games tell this project's server their internet addresses to find each other, and that anyone with an invite code can
join and learn the host's internet address, so codes are shared only privately. The player ticks "I understand" and
chooses **Continue**; **Cancel** leaves things as they were. The answer is kept in the data folder
(`coop_notice_ack_2.txt`, as Report's own first question keeps `report_zip_ack.txt`), so the notice shows once per PC.
(`coop_notice_ack.txt` was the answer to the notice before room codes, which didn't mention the server: a player who
ticked that one is asked again.)

**The name in the menus** is **Seamless co-op** (the start screen's button, the F1 menu's entry and its window's
title), so it isn't taken for the game's own co-op with summon signs.

**To host:**

1. Open **Seamless co-op** (on the start screen, or F1 > Seamless co-op) and choose **Host**. The game starts
   again on your co-op characters.
2. Windows may ask whether the game may use the network. Choose **Allow**, or your friends can't connect.
3. Open **Seamless co-op** again. It shows your **invite code**, like `K7Q2M-9XD0R-TB4HW` (the room code, 15
   letters and digits), once this project's server has your room: a second or so. **Copy code** puts it on the
   clipboard. Send it to your friends. When your router opened the port, or you gave a public address, or the
   server can't be reached, Co-op also shows the **direct code** under it, like `7KQ2-M9XD-0RTB-4HWE` (16): it
   works without the server, but needs the port open. Each code shown (the code for your own network too) has a
   small **Copy** button beside it, for the mouse; it says "Copied" for 2 s.

**Send the code only to the friends you play with.** Anyone with it can join you. Under the room code, Seamless
co-op says just that: the room code holds no address. The direct code holds your internet address, so under it the
window adds "Don't post it in public". The code for your own network needs no warning.
If a code went further than you meant, choose **New code**: the old one stops working, and players already in your
game stay. The game makes a new code by itself only when someone has tried many wrong ones; Seamless co-op then says
so.

**To join:**

1. Copy your friend's invite code.
2. Open **Seamless co-op**, choose **Join**, then **Paste and join**, or **Type code** to type it (with a keyboard or
   the controller). Either kind of code works. The game starts again on your co-op characters.
3. Until it gets in, Co-op says how the join goes ("Finding the host's game...", or what is in the way, below), and
   the game keeps trying: after 10 s, 30 s, then every minute.

**To leave:** **Seamless co-op** > **Leave co-op**. A friend's game first takes them back to their own world, where
it saves what the group did; the host's leave sends its friends back to theirs. Then the game starts again on your
normal characters.

**If the host's game closes,** a friend sees the game's own "The session has ended." about 13 s later and goes back
to their own world. **Seamless co-op** then says the connection was lost. The friend's game keeps trying to reach the
host's: after a network drop, with the host still hosting, it connects again, goes home as the game ends the session, and
joins again by itself from there. If the host starts hosting again, its invite codes are new ones. With a room code,
the friend's game then says "No game is hosting with this code. Ask the host for its code again." and stops trying
after 2 minutes; with a direct code, "The host's game did not accept this invite code. Ask the host for it again." at
once. **Leave co-op**, then join with the new code.

**Co-op characters** are kept in their own save folder (`savedata-coop`, beside the normal one). Co-op never writes
your normal characters.

**Bringing a character along.** The first time you host or join, while co-op has no characters yet, the game asks
whether to bring one of your normal characters. Co-op gets a copy, and your normal character stays as it is. Nothing
from co-op comes back to it. Or choose **Start with none** and make a new character in co-op. The question comes only
once: after that, co-op has characters of its own.

**Up to 4 players** (the host and three friends). The host can take fewer: Settings > Network > **Players when you
host** (2, 3 or 4). A change counts at once, and players already in stay. A friend who comes when the game is full
waits: Co-op says so, and the friend's game joins as soon as a place frees up.

**Mods.** Co-op works with mods on (online play doesn't, with mods that change how the game plays). **Host** or
**Join** while any mod is on first shows how many are on ("3 mods are on"). Its first line, larger and in the warning
colour: when something goes wrong in co-op, turn every mod off (Mods, in the menu), restart and try again before
reporting it. Then: co-op with mods will probably have problems (games that don't match, missing enemies or items,
crashes), and everyone should have the same mods that change how the game plays. Last, the mods that are on: up to
three by name, else two and how many more. **Play with mods** goes on; **Back** leaves things as they were. It asks each time, unless you tick "Don't ask again
for these mods": a different set of mods asks again (`coop_mods_ack.txt` in the data folder keeps the answer and the
set).

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
- **The tutorial's trips are the host's too.** At the end of each part of the tutorial, and when the dragon god ends
  it, a friend goes on with its host: one that gets there first waits there for the host. A friend killed by the
  Vanguard comes back where it fell; when the host dies there, the group goes to the Nexus. Before, a friend whose game
  got there first left co-op, and its join back stalled.
  The tutorial's end makes every player a soul: a friend that goes on to the Nexus with its host arrives in soul form,
  with the tutorial over, as the host does (2026-11-15; before, it arrived in body form).
- **Archstones:** the host can use them during co-op, and the group travels with it. A friend still gets "You can't
  return during multiplayer.", at the Nexus's stones too. No multiplayer wall closes the way back to an Archstone
  (in 1-2, the Phalanx room's wall to 1-2), so the host is never shut in with the Archstone after a death.
- **The White Eye Stone** takes a friend home as its own trip home does (in body form, its own world back); co-op
  then joins the host again a few seconds later. Leave co-op is the way out of co-op.
- **Items are each player's own.** A friend sees the items lying in the area and gets drops from dead enemies, as the
  host does. Each player has its own copy: one player's pickup leaves the others' where it was. An item a friend
  takes is also gone in its own world.
- **Light walls** (the fog you press Cross at and walk through, outside boss rooms) are gone for everyone once
  one player walks through.
- **Boss kills keep everyone together:** no rating screen, nobody sent home, and the boss room's walls drop. The
  host's room stays open, during a boss fight and after it, so a friend can always join again.
- **A friend who joins during a boss fight** fights the boss too. Before, in 1-1, Phalanx wasn't in its game: it
  saw only the host fighting, and its blows did nothing.
- **A friend who dies** comes back where the host last arrived (the start of the area) and stays in the session.
  After a death in a boss fight, it comes back at its own bloodstain instead (below).
  Once it is back, the others see it again, with its rolls, attacks and other moves.
- **Bloodstains.** A player who dies drops its souls in a bloodstain where it died, a friend too. Only that player
  sees and touches its own stain. A friend can take its souls back in the host's world, or, if it goes home first,
  at the same spot in its own world. Dying again before that loses them, as in the game. A stain a friend left in its
  own world shows in the host's world too when it is in the same area.
- **Your glow.** A friend shows as a blue phantom in the host's world. Settings > Network > Your glow (on by
  default) decides whether the others see your glow. Off: they see you as you look in your own world, and so do
  you. A change counts within 2 seconds.
- **Your glow colour.** Settings > Network > Your glow colour picks the colour of your glow: blue (the game's),
  one of eight named colours, or Custom, set by hue in the row below it. The others see that colour on you, and so
  do you. Each player picks its own, so in a party of four each friend can have its own. Soul form stays the game's.
  Cutscenes that show you (the tutorial's opening, for one) show your glow and its colour as you chose them too.
- **In a boss fight,** a friend who dies first watches until the fight ends. Once the boss is dead, it comes back at
  its own bloodstain in the boss's room, where it can take its souls back at once. Before, it came back at the area's
  start. If the host dies too, it goes back with everyone. If the connection to the host is lost while it
  watches, it goes home (with the game's own message for a lost session) and joins again from there.
- **Bosses get more health with friends,** as in the original game: half as much again with one friend, double with
  two. Three friends get double too (the original has nothing for three, so a boss had its solo health).
- **When the host dies,** everyone goes back to the start together. There the friends see the host's rolls, attacks
  and other moves again. Before, a friend that was alive could see the host stuck in a weapon swap after that.
- **Enemies stay where they belong after a load.** Before, an enemy that stood near a player as the group moved to
  another area could move the new area's enemy with the same number to its place. In the tutorial that put the
  Vanguard outside its hall: it fell, and "The Demon Was Destroyed" showed before anyone saw it.
- **Souls from enemies.** Every player gets an enemy's full souls when it dies, whoever killed it. (The original game
  gives a summoned phantom half.)
- **Souls from bosses.** Every player gets a boss's full souls (Phalanx's 1,270), a few seconds after the kill and
  after a soul's revival, as a player alone does. (The original game gives the host three quarters and a summoned
  phantom a quarter.) Before, they came only at each player's next load.
- **Progress is everyone's.** A boss killed, or a door, lever, elevator, shortcut, bridge or fog wall opened while
  friends are in the host's world counts in each friend's own world too.
- **A boss's Demon's Soul is everyone's.** When one player takes it at the boss's Archstone, every other player in
  the session gets it too, unless it already took that soul in its own world. A friend can take it itself as well.
- **1-2's castle gate opens for friends too.** When the host's world has the demon of another world dead that the
  gate needs, each player sees the gate's cutscene as it walks up to the gate, and then the gate is open in its
  game. Before, if the Tower Knight died during the visit, the gate opened for the host alone, and stayed shut for a
  friend until the next load.
- **3-1's flight to the next part works in a session.** After the Fool's Idol, when any player walks onto the
  flight's spot, every player's game plays its cutscene and takes that player to the landing (each friend to a spot of
  its own beside the host's, so nobody is pushed off the ledge). Before, nobody in a session could take it: its handler
  did nothing in a party, as the original ends the session at the kill. Then (#575 to 11-16) only the walker saw it,
  and the others were taken along without it.
- **Boletaria's clear counts in a friend's own world too** (1-4's flag 8010), so its own Nexus can open the way down
  once all five worlds are cleared there.
- **So are keys and the items only the host could take:** 3-1's prison keys, the items pulled from the ground or a
  corpse at a prompt (1-1's sword, the glowing corpse in 2-2, the sword after the final boss and the others like
  them), and the items the host's game alone made takeable in the moment (1-1's hanging corpse, 3-1's iron
  maidens). Whoever takes one, the others get it too. A player who joins later and doesn't have it yet still finds it
  there, in the host's world and in its own.
- **The final boss and the endings are everyone's.** Friends get the prompt at King Allant's fog in the Old One, as
  at every other boss. The cutscene after him (the Maiden's prayer) plays for everyone, whoever walks up first. The
  ending one player starts, good or bad, is everyone's: every game plays it, and every player goes on to New Game+ in
  their own world. A friend's game plays its own credits and NG+ start, then joins the host again in the NG+ Nexus.
- **Your own world stays yours.** In a friend's world you see it as it is there. Your own world's doors, items and
  people stay as they were, and what the group did is added when you go back.
- **World tendency.** In the host's world a friend plays in the host's world tendency, as a summoned phantom does in
  the original game. The friend's own tendency doesn't change, and it is in play again when the friend goes back.
- **Going back:** when the host leaves, its friends go back to their own worlds, where they stood when they joined,
  and their games save there, each in the form it has then (above).
- **The Nexus is shared.** Friends stand in the host's Nexus and see its people as the host does, not as phantoms,
  and can talk to them. The people there and in the areas (Ostrava in 1-1, for one) are the host's, as in the game's
  own co-op. The tutorial's wandering ghosts (white phantoms on the way and in the demon's hall) are ghosts for everyone.
- **The Nexus's cutscenes** (the one after a boss, the Monumental's story) play for each player who hasn't seen them
  in their own world yet, even when the host has. So does an area's opening, such as the dragon's flight at 1-1's
  start. Of the ones the Nexus plays as you arrive (your first visit, the Maiden's return, the seal lifting once all
  five worlds are cleared), a friend sees only the newest one the host has seen, so its Nexus is the host's. The
  final area's cutscenes play for everyone when they happen, never again later.
- **Skipping a cutscene** (START, then Cross) works for everyone and skips only your own: the others' go on.
- **The Monumental's question:** whoever talks to the Monumental answers it. Once the other Archstones open, they open
  in every player's world. His story plays only for the one who hears it: when a friend hears it first, the host then
  gets his later lines and his question, not the story.

### If your friends can't connect

**With the room code** most friends get in with no open port: the server tells the two games each other's addresses,
both send to each other at once, and each router then lets the other's packets in. It can't when both routers are
strict (a router that gives each connection a new port, as some mobile networks and shared provider addresses do, on
one side, and one that only lets in what it sent to, or another such router, on the other). Co-op then says so:
- the host's: "A friend's game found yours but couldn't get through. Open port 36100 (TCP and UDP) on your router, or
  let that friend host.";
- the friend's: "The host's game was found, but your two networks block a direct connection. ..." with the same ways
  out. The friend's game keeps trying.

When the server can't be reached, or has no place, the host's Co-op says so and shows the direct code; the friend's
says "Can't reach the server that finds the host's game. If the host's port is open, ask for its direct code."

**With the direct code**, or a room code that can't get through, the host's router has to let friends in on one port
(36100, TCP and UDP).

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
- **Players on the same network** can use the room code too: the server sees both games come from one internet
  address and sends the friend's game to the host's address at home. With the direct code shown, Co-op also shows a
  **local code** below it.

### Reporting a co-op problem

Right after it happens, each player picks **Report an issue** in the menu, and says who was the host. Each report
holds that player's co-op log: what happened in the session (joins, leaves, loads, the game's own session events),
each line with its time, so the host's and a friend's reports line up. A report made after restarting the game still
has the last session's.

With mods on, first turn every mod off, restart and try again: report it only if it still happens. The co-op log
names the mods that were on.

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
  and only the host's router is involved. With a room code, that port is the one the two games punched open in the
  routers (below), not one forwarded.
- **The room code** (docs/162). While it hosts, the host's game registers a room with the endpoint on this project's
  server (psnr's UDP listener, port 36100), from its room server's own UDP port, and again every 20 s. A friend's game
  looks the room up there and gets the host's addresses (the one the endpoint sees, and the one at home); the
  endpoint sends the host's game the friend's. Both then send to each other over UDP (a cookie first, so nothing is
  answered for an address that didn't ask), and the friend's game tries TCP to the host's port at the same time: the
  first that opens carries the `HELLO`, the other is closed. Over UDP the room session runs on ps3recomp's
  `psnr_rudp` (in order, resent, kept alive), and the game's P2P traffic goes through the host as with a direct code.
  The endpoint keeps nothing on disk, logs only counts, and never sees the key or the game. A host the endpoint
  introduced to a friend who then never connected (15 s) shows the "couldn't get through" line above.
- **The key.** Each time a game hosts, it draws a new key. The code carries it, and the room server turns away any
  game without it. An address that sends wrong keys is ignored for a minute after five.
- **The game's own online server** (messages, signs, ghosts, world tendency, login) is answered inside each game, by
  `des_seamless.cpp`, with nothing in it: no messages, no signs, no ghosts, and a neutral world tendency. The answers
  follow the open-source dessego server's.
- **World tendency:** each player's own, as offline. Online, the game mixes the server's tendency into the player's
  at a load into the Nexus (half each). With only the neutral answer above, that pulled every area halfway to neutral
  on each such trip (0.8 to 0.4 in a test). In co-op the game now skips the server's values (`des_qwc_local`, the
  Network tab's "Yours only", docs/69). Each game still plays the shared area with its own player's tendency: the
  game's own message that gives a phantom the host's tendency (0x17) doesn't come in seamless co-op (docs/69).
- **Nothing else is reached,** apart from the endpoint (above) and the STUN request, from a host whose router gives
  no address. No name
  the game looks up (`c.demons-souls.com` and the rest) resolves, and no sign-in happens.
- **Mods** don't keep co-op off, as they keep online play off (docs/143): co-op's game goes only between the
  friends' games, so a mod can't spoil anyone else's. Host or Join warns first while any mod is on (For players,
  Mods), and the co-op log names the mods that were on (`[coop] mods on, accepted: ...`).
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

### The invite codes

Both kinds are letters and digits from Crockford's base 32: no I, L, O or U, so no two characters look alike. Reading
a code back ignores case, dashes and spaces, and takes O as 0 and I or L as 1. Its length tells which kind it is.

**The room code:** 15 characters in groups of five, 75 bits: the room number (40), the key (28) and a check (7). The
room number names the room at the endpoint; the key goes only to the host's game, in the `HELLO`. The check caught
459 of the 465 one-character typos of a test code.

**The direct code:** 16 characters in groups of four, 80 bits: the host's IPv4 address (32), the port (16), the key
(28) and a check (4). The check catches most typos ("That code has a typo").

A friend's game rejects anything else with "That isn't an invite code: it has 15 or 16 letters and digits."

**Who can join.** The room server (ps3recomp's `psnr_host`) is the only thing the router opens. The game's own
online server runs on this PC's loopback only. Every connection must say `HELLO` with the key, as a 32-byte blob
made from it, within 10 s; without it, nothing else is answered. So:
- A scan of the internet can find that something answers on the port, but can't join, list rooms or send anything
  to the players without the key.
- Guessing the key: one address is ignored for a minute after 5 wrong keys. After 100 wrong keys from any
  addresses, the key is spent: no one else gets in, players already in stay, and the Seamless co-op window asks the host for
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

**The old area's enemy places.** The game keeps tables of the area's characters' states, by their number in the
area's list (`chr+0x70`). It fills them from the other players' messages:
- P2P 3 and 0x21 from each member: places and facings (at a table's `+8`), and states (`+0xC`);
- P2P 4 and 0x26 from the session's host: states (`+0x10`), and places and facings.

Each connection keeps a queue of each message, and a member's are read only while this game has a character for
that player (0x00532108). The game's own sessions end with the area. A seamless session doesn't, and after a load the
others' characters are placed as soon as this game arrives. So what another player sent from the old area before its
own load was read into the new area's tables, onto the character with the same number.

In the tutorial (11-15), `m08_00`'s no. 4, an enemy 16 m from the Archstone, put `m08_01`'s no. 4, the Vanguard, at
its place, outside the hall. It fell from there when its logic woke (a player in its hall), and the game showed "The
Demon Was Destroyed". This needs a player near an enemy of the old area as the load starts, which is why it doesn't
happen every time.

So from the start of each host load (on the host, its own start; on a friend, its follow), what the five reads of
these messages get is dropped (hooks after 0x002C9AEC, 0x002C98C4, 0x002C9740, 0x002C9600, 0x002C96A8). This lasts
until each other player in the session has said it arrived for that load (`HERE`). Then every connection's queues of
the four are emptied once more, at the next read, on the readers' thread. A `HERE` (through the room server) is sent
once its sender's load has ended, so what that player sent from the old area (peer to peer) left a load's time
before it.

The players waited on are the game's members with a slot (`+0x38`) that are still in the room. A player that left
during a game's load used to stay in that game's member list (each game now takes such a leave at once, below), but
the room library had dropped it: its members (`sm+0x11C` to `+0x120`, 0x30 bytes each) start with the word a member
has at `+8`. Before, the host's game waited on such a friend until it joined again.

**Who moves each character after a load.** A game moves only the area's characters it owns. Each game asks the
area's sync object (the character manager's `+0x1C0`) whether a character is its own: `0x002955B8` and `0x002CA418`
look in the object's list at `+0x58`.
- **A character the game owns** gets its AI controller (`0x002D2270`, the character's `+0x4A0`) and runs its state
  script.
- **Another game's character** gets the network controller (`+0x4A4`) and waits for that game's messages. Until they
  come, an NPC stands in its default pose.

The host shares the characters out every 5 s (`0x002CE2B0`; the timer at `+0x90` starts at 0 when the area is set
up). The host's share goes into its own list, and each friend's goes out as message `0x15`. A game alone in its
session uses its claims (`+0x6C`, built in `0x002CDD38`) as its list.

In the game, the host's list carries over from before anyone joined. A seamless load sets up a new area while the
session is open, so for the area's first 5 s no game owned any character. In the Nexus (11-20) the NPCs stood for
5.0 s in the host's game and 5.4 s in a friend's, then sat down.

Now, from the area's set-up (`0x002CB178`) until its first share-out (the timer's reset at `0x002CE90C`), the host's
game also counts its claims as its own (a hook at the lookup's end, `0x002CA45C`), as it would if it were alone. From
the first share-out on, the game shares the characters out as before. In the host's game the Nexus's NPCs now sit
from the first frame (their state script ran 8 ms after the area's start).

**A friend gets each character's state again once it reads them.** The game sends a character's animation state
(message `0x21`) only when it changes:
- A character's AI step (`0x00308430`) compares the state it runs with its copy in the sync object's table at `+0xC`.
- On a change it writes the new state there and queues the character for the frame's send (`0x002CBB10`).

An NPC that sat down stays sitting, so a game that missed that one message never gets it. A friend drops what the others
sent before its load ended (the hold above). With the host's NPCs sitting in its first frames, the friend was often
still loading, so the NPCs stood for good in its world. That happened in a test (11-20): the Nexus, then 1-1, then back.

Now a game that has emptied those queues says so (`SYNC`). Each other game then sets its copies of the states of the
characters it runs to -1: its list, and in the host's game before the first share-out its claims. The game's own
check then sends each state again on the next frame. This happens only while nothing is queued for the send (the list
at `+0x84` empty, checked by a hook after the send, `0x002CE440`), so a -1 is never sent. Places (message 3) go out
all the time, so they need nothing.

The game has a catch-up of its own for a member that joins: `0x0053D288` calls `0x00297508`, which calls `0x002CD380`.
The host then sends message `0x26`, each character's place and state, one character a frame. That message sends the
table as it is, and the table holds 0 for a character no game runs yet, so it isn't used here.

**A friend that leaves during the host's load.** The session's event dispatch (`0x0053DFD8`) takes a member's leave
through the game's member-left step (`0x0053B7F8`) only while a byte the game sets during its loads is clear
(`bl 0x001A2028`). While it is set, the dispatch drops the event. The room library drops the member anyway, and the id
the game's member shares with the room entry is freed. So the host's game kept such a friend as a member, and when the
friend joined again, the step ran with the freed id and found nothing: the join stopped half way, with the friend
outside the host's world until the host's session ended. Another friend's game, following that load, did the same:
it kept the old member, and the friend that joined again never came back there. That check now reads 0 on every game
(a hook after the `bl`), so the leave is taken at once. With no friend left, the host's session ends there and it
opens a new room, as when a friend's game closes during the load, and the friend's next join goes like a first one.
With another friend still there, the session goes on. A friend's game never gets the host's leave: the room server
runs in the host's game, so when the host goes, the room goes too.

**A player back after a death.** A dead phantom leaves the game's sessions, so the game never brings its character
back: on the other games that character stays in its death animation, faded out, and plays none of its moves after.
So a player back in the world after a death says so (`BACK`): a friend after its respawn or after a load it followed
while dead, and the host after its death's load. Each other game then takes that character out, as the game does when
a member leaves, and puts the player back on its list of players to place. The game makes a new character for it, as
after a load. A dead character outlives a load on the others' games (the placement makes none while one with that
player's id is there), so a `BACK` waits until the game that gets it is in the world after its own load. Only a
character that game saw dead (its game data's HP at 0) is made again; one made since is left alone. Before, only a
friend's own respawn sent it: when the friend and the host died within seconds, each game kept the other's dead
character (the friend unseen on the host's screen, the host stuck in its death animation on the friend's).

A `BACK` can also come before its player has a character on the game that gets it. A friend that followed the host's
death load gets the host's `BACK` right after its own load, and its placement makes the host's character a moment
later. That character is made from the game data the friend's game keeps for the host, which can still hold the HP of
the death (0), and a character made then starts out dead too: it moves, but never plays its moves right (the host
stuck in a weapon swap on the friend's screen). So a `BACK` that finds no character waits for it. A character made
with that HP above 0 is kept; one seen dead is made again once that HP is back, and the new one starts out
alive.

**Messages between the games** go through the room server (`psnr_app_send`, ps3recomp):

| Tag | From | Body | Meaning |
|---|---|---|---|
| `ROOM` | host | the room's data | join with this |
| `ROM?` | friend | none | send the room's data |
| `LEAD` | host | its load count | the host's load started: follow |
| `REDY` | host | load count, area, where it arrived and where it stands (each with its facing), its area's dead characters | after each load, every 2 s, and when asked |
| `HER?` | friend | none | send a `REDY` now (a friend's respawn) |
| `BACK` | any | its player id | back in the world after a death: make its character again |
| `HERE` | any | the host's load count | in the world after that load: the area's syncs from this player are current again |
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
  - the flags of a handler that a kill's handler names in quotes (a timer's, `OnKeyTime2(..., "<name>", ...)`), when
    that handler counts the cleared worlds (`GetBossFlagCount(`). That is 1-4's `OnEvent_4481`, which sets
    Boletaria's clear (8010) for the host alone; the other worlds' clears (8011-8014) are in their kill handlers
    already. Wider (every named handler) would also take Ostrava's flags, the warp-out poses' and the boss music's;
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
- **What the soul's handlers kept for the host** (`soul_steps_for_all`, before `pickups_for_all` copies them): a block
  under `if proxy:IsClient() == false then` in a soul's handlers (`OnEvent_<id>`, `OnEvent_<id>_<k>`) becomes
  `if true then`. In the original a phantom goes home after the boss, so only the host's world went on from there. A
  friend in a seamless session stays. The one such block (every area's script checked) is 1-2's: the Tower Knight's
  soul (4252, `OnEvent_4252_1`) starts the watch on the castle gate, `OnRegionJustIn(570, 10000, 2250, "OnEvent_570",
  everytime)` between `NotNetMessage_begin` and `_end`. At the gate, with flag 8035 set (a demon of another world dead,
  from those worlds' scripts and the Nexus's; not a world's own flag, so on a friend it is the host's), `OnEvent_570` plays the gate's cutscene (scene 20101) and
  sets flag 571, and the cutscene's end opens the gate (`EndAnimation(1270, 2)`). Without the edit, a friend's game
  never watched the gate when the Tower Knight died during the visit, and its gate stayed shut beside the host's open
  one (11-15). The area's start watches it for every player (Tower Knight dead, flag 571 clear), or opens it (571
  set), so a load put that right. A friend's game sets 571 in the host's flags in play only: it isn't a progress
  flag, and its own world keeps its own gate.
- **Treasures the host's game alone turned on** (`treasures_for_all`): a block under `if proxy:IsClient() == false
  then` whose code is only `TreasureDispModeChange` and `EnableObjTreasure` becomes `if true then`. Seven blocks match:
  1-1's hanging corpse (`OnEvent_280_1`) and 3-1's iron maidens (`OnEvent_492`, 496, 497, 501, 502 and 503; 493-495
  and 498-500 are in a block comment). A friend saw the item and couldn't take it until the next load, whose area start
  turns it on from the flag. Items are each game's own, so nobody gets one twice.
- **A handler that does nothing in a party** (`party_gates`): a handler whose first statement is `if
  proxy:IsInParty() == true then return; end` loses that line. One matches, 3-1's flight to the next part
  (`OnEvent_410`). The original ends the session at the Fool's Idol's kill, so the host is no longer in a party when it
  walks there; a seamless session stays, and the flight did nothing for anyone. (The Nexus's `OnEvent_450_1`, a bonus
  for a solo clear, and `global_event.lua` don't match: neither starts with it.)
- **A watch the host's game alone starts after a kill** (`kill_watches_for_all`): an `if proxy:IsClient() == false
  then` block whose code is only `NotNetMessage_begin`, `OnKeyTime2(<n>, "<handler>", ...)` and `NotNetMessage_end`,
  where that handler registers an `OnRegion*` watch, becomes `if true then`. One matches, the Fool's Idol's
  `OnEvent_5123_1` (its timer starts the flight's watch, `OnEvent_410_1`).
- **A cutscene that takes its player on, for everyone** (`flights_for_all`): 3-1's flight. `OnEvent_410`, the
  walker's own region watch at the altar, asks for the cutscene (40001) and sets flag 410. The cutscene's start
  (`OnEvent_410_RemoStart`) puts the player at the landing (point 2031), and its end removes the watch. The game
  registers those two handlers once each, as network events, so the walker's start ran in every game: the others
  were put at the landing without the cutscene, and their own handlers were used up, so a player who later played the
  cutscene itself stayed below (11-16, with the host taken along by its friend's walk). The game's own cutscene for a
  party (4-2's Patches, 1165) does it this way: the start tells the others, each game plays the cutscene, and the
  start and end handlers are each game's own. Now:
  - the start and end handlers are registered between `NotNetMessage_begin` and `_end`;
  - the walker's handler runs once per visit (`SeamlessFlight410`; the script starts anew at each load) and tells the
    others (`LuaCallStart(410, 79)`) before it asks for the cutscene;
  - each other game's `SEAMLESS_Flight_410` (`LuaCall(410, 79)`, registered with the start's handler) plays the same
    cutscene and sets flag 410, unless that game played it already. Players who walk in together each see it once;
  - the landing is a single point, 0.2-0.3 m from the edge of a walkway about 2 m wide (3-2's navmesh). Two players
    put there pushed each other off. Both at once fell to their death (the first test, below), and with a friend
    landing 2 s after the host, the friend still pushed the host off (the user's run, 11-16). So a friend's start, right
    after its warp, tells co-op its spot (`SEAMLESS_FlightBeside_410`: `SetEventFlag(19990 + spot - 1)`, past the
    game's last flag, as the ending's marker). The spot comes from its player number (`GetLocalPlayerId`; the first
    friend's is 2): 2, 3 and 4 take spots 1, 2 and 3. Co-op (`beside_landing`) then moves that warp to a spot of the
    friend's own on the walkway. The script's `Warp` (0x00428028 -> 0x002E7808) doesn't move the player at once: it
    leaves its target in the character's mover (`*(chr + 0x20)`): the position at +0xC0, the facing at +0xD0, and
    +0xB8 = 1. The game moves the player there at its next update, so co-op changes the waiting target's position, and
    the facing stays the landing's. (Moving the player at once, as the scripts' `LoadChrPos` does, was undone by the
    waiting warp a moment later.) The three spots have floor all round to 0.7, 0.6 and 0.4 m, and are 1.3 m or more
    from the landing and from each other. Only a waiting warp to within 1.5 m of the landing is changed.

  So every player sees the cutscene, and each one's own start takes that player to the landing. The edit applies to a
  `REMO_START` handler outside `NotNetMessage` that warps `LOCAL_PLAYER`, with a `LOCAL_PLAYER` region watch of the
  same ID whose handler asks for that ID's cutscene. Only 3-1's matches. The spots beside the point are for 3-1's
  landing alone (`kBesideLandings` in the scripts' edit, `kBeside` in co-op).
- **The sword after the final boss** (141, item lot 10730): the Nexus's script runs `gifts_for_all` too. Before, it
  returned before that step, so the sword stayed the host's.
- **The final area and the endings** (`final_area_for_all`, the Nexus's script, all or nothing). In the original only
  the host (and players in their own world) go in: the fog has no phantom's entry, the kill's last steps and the
  endings run in the host's game alone, and a phantom goes home at the kill. Now:
  - The fog (`OnPlayerActionInRegionAttribute(61, 2895, ...)`, each player's own prompt) takes `+ ATTR_WHITE`.
  - The King's death (`OnEvent_70`) runs once per game. Each game ran it for its own copy of the King and again for
    the other games' message, and what it set up came twice: the sword's pickup steps (`OnEvent_141_WAIT`), so one
    pull gave the host two swords (11-16). The first call runs, whichever it is: the guard is a script value
    (`SeamlessKingDead`), not flag 70, which can reach a friend from the host's world before its own call. The
    pickup's last step (`OnEvent_141_1`) runs once too.
  - A friend gets "The Demon Was Destroyed" after the King's last words, its boss health bar cleared, and flag 450
    at the text's end, as the host does (`OnEvent_450`'s live branch). The cutscene after the kill waits for 450.
    The handler is registered once (`CustomLuaCall(450, ..., once)`), and each game's own call comes after the
    King's last words (9054) in that game. The host's call, a message in a friend's game, came first, used up the
    friend's registration and returned, so the friend's own call found none (11-16: no text on the friend, and the
    King's health bar stayed; in three scripted runs the friend's game set 9054 but never 450). A friend's game now
    runs the first of the two, once (`SeamlessDemon450`). In a party the handler's next step (`OnEvent_450_1`) does
    nothing.
  - That cutscene (71, the Maiden's prayer; region 2082 once the King's last words, 9054, and 450 are set) plays in
    every game: its start sends `LuaCallStart(71, 79)`, and each game's `SEAMLESS_Scene_71` plays its own. Its end
    is then each game's too: the Maiden praying, the endings' watches, and the player put beside her (point 2083).
    Before, a friend's game never played it (9054 and 450 weren't set there), so the Maiden didn't pray there and
    no ending could start.
  - The good ending's prompt (81, region 2010) takes `+ ATTR_WHITE`. The ending's step (`OnEvent_80`, the Maiden
    killed; `OnEvent_81_2`, the prompt) was for the host's game alone (`IsClient() == false`): the clear state, the
    clear count, the items the clear takes (`LastRemoveEquip`) and the end flow (`OnEndFlow`), which resets every
    flag for NG+. It now runs in every game, once (`SeamlessEnding`, so the first ending started is everyone's), and
    sends `LuaCallStart(<id>, 79)`, whose `SEAMLESS_End_<id>` runs it in the others. Each game plays the ending's
    cutscene once (`SeamlessEndScene`).
  - Checked in a game alone (11-16, the user's save before the final area, the bad ending): the ending's step clears
    every flag, the world banks too, and takes the clear count from 0 to 1, before the ending's cutscene, the
    credits and the NG+ load.
- **A friend's NG+.** The ending resets the flags in play, which on a friend are the host's, and the friend's own
  world was kept apart (`keep_own_world`). The ending's step tells co-op that it started in this game: it sets flag
  19999 (`SetEventFlag(19999, true)`), past the game's last flag. `SetEventFlag`'s native lets ids up to 0x4E1F
  through, and the flags' set (0x005086F8) returns for any id above 0x427F, so the game ignores it, and only the
  co-op flag hook sees it (`ending_started`). The clear count can't tell: its object
  (`*(*(*(TOC - 0x788C) - 0x8000))`) is the world in play's, which on a friend is the host's, and a join can read a
  stale one. From then on nothing is kept apart: the game's flags, reset for NG+, are its own world's, and its trip
  home puts nothing back. It keeps its glow and colour until its game leaves the session, after the ending's
  cutscene (before, they went to the game's blue at once). It doesn't follow the host's next
  load: its game plays its own ending, its credits and its NG+ start, which leaves the session (the host's game keeps
  its own, "the host stays in its session"), and it joins the host again from its own NG+ Nexus. Before, the friend
  only followed the host's NG+ load, its own world still in the first playthrough (fewer souls from each enemy: each
  game pays its own player by its own clear count).
- **Whose flag.** On a friend the flags in play are the host's, so as an area's script is read (before the area
  starts) each of these pickup flags is made the friend's own world's (`des_seamless_area_read`). A friend that hasn't
  taken a soul sees the prompt even where the host has; one that has sees none, and gets no second one.
- **Keys.** 3-1's prison keys (`OnDistanceActionPlusAttribute(<id>, ..., ATTR_LIVE + ATTR_GREY, once)` with a
  pickup branch and an item in the handler) were for the host and players in their own world. They get the same
  treatment: each player's own prompt (`NotNetMessage`, `+ ATTR_WHITE`), the grant, and the flag mirror.
- **The host's item prompts** (`gifts_for_all`): the items a prompt offers the host alone (`possible_chr_type`, at
  `ItemBoxDist_A`), with their own pickup steps that end in `OnEvent_<id>_1`, which gives the items and sets flag
  `<id>`: the Nexus's, 1-1's sword, 2-3's, 4-3's, 5-1's, and 2-2's glowing corpse (5-2's has no such flag and
  stays the host's). The prompt is offered to a friend too (`+ ATTR_WHITE`). `OnEvent_<id>_1` first sends
  `LuaCallStart(<id>, 78)` (before the pickup's own messages, one of which removes the others' prompt), and each
  game's `SEAMLESS_Gift_<id>` gives the same items and sets the flag unless its flag is set. The flag is mirrored
  as the souls' are.
- **The Stone of Ephemeral Eyes** (goods 1021). Its row (`EQUIP_PARAM_GOODS_ST`, param table 3) allows it only in
  soul form in one's own world: `enable_gray` (`+0x19`) 1, `enable_white` (`+0x1A`) and `enable_multi` (`+0x1C`) 0.
  Each frame of a session the row gets `enable_multi` 1 (the host), and `enable_white` 1 while this player is a friend
  in soul form; outside a session it is the disc's again. The revival it gives (behaviour 3021, SpEffect 3024) turns
  a friend's blue phantom (type 1) into an alive player of its own world (type 0), which the rest of co-op doesn't
  count as a friend: the next frame sets type 1 again (`SetChrType`'s native, US 0x00445F30) and body form, as a
  boss kill revives it.
- **Bloodstains.** The game keeps one per character, in its save: the record at the game data's `+0x1C` (its souls
  at `+0x1C`, -1 once taken back), and a "shown" byte at the game data's `+0x18`. A friend's death runs
  `UpDateBloodMark` (in the replaced `PartyGhostDeath_1`), as `SoloPlayDeath_2` does for the host, and the touch
  event (`g_Initialize`'s `OnSelfBloodMark`) is registered for blue phantoms too (`+ ATTR_WHITE`). Each load's
  `InGameStart` runs `ParamInitialize` (US 0x0044A660), which shows the stain only for a player of type 0. It hides it
  (`InvalidMyBloodMarkInfo`) for a phantom, and for a soul-form player with others in its world, so in a session it
  would stay hidden after every load. `stain_tick` shows it again as `ParamInitialize` does for type 0
  (`0x00419670(manager, player, 0)`) whenever the byte is cleared and the stain still holds souls.
- **Each player's glow.** A character's draw update (US 0x002DD920) passes its type to `0x000D7C58`, which turns
  the draw object's (`+0x6A4`) phantom material on (`+0x78`) for types 1 to 4, 8, 10 and 11; `0x000D81D0` then
  picks the material from the type (`0x0007FD38`: 2 for a blue phantom, 1 for soul form). Each game sends its own
  choice (`DES_SEAMLESS_GLOW`) with its player id (the character's `+0x498`) every 2 s in a session (GLOW). The
  hook after the type's read (US 0x002DDAA8) gives 0 for a blue phantom whose player turned it off, itself
  included, so it is drawn as an alive player; the type and everything else that reads it stay a blue phantom's.
- **Each player's glow colour.** The phantom material (kind 2) holds two colours: the edge's at `+0x2A8` and the
  body's at `+0x2B8` (float4 each). For each draw of a phantom, the ghost colour upload (US 0x00080E88, called by
  0x0006C850) takes the material from the ghost manager's table by kind (`+0x68 + kind * 4`), and sends its colours
  to the shader when they differ from the last ones sent. A player with a colour (`DES_SEAMLESS_GLOW_COLOR`, sent
  in GLOW) gets its own copy of the material in that colour (the body colour as chosen, the edge 40% of the way to
  white). The hook at the upload's start (0x00080E88) puts that copy in kind 2's place when the drawn object (`r31`)
  is that player's: its handle (`+0x8`) is the character's draw object's (`*(chr + 0x6A4) + 0x4`). The hook after
  0x0006C850's call (0x0006C860) puts the material back. Other phantoms, and every other draw, read the game's.
  The ghost model draw (0x000E8C48) takes its shader constants (0x0006DA20) before its own upload, so each model
  is drawn with the colours of the model drawn before it. With one colour for every phantom that can't be seen;
  with two players in different colours, parts of one phantom took the other's. So while any player has a colour,
  the hook at 0x000E9650 (after the draw's `bl 0x0006C908`, `r31` the drawn object, `r26` its owner) writes that
  model's own edge and body colours into the constant block (`+0x11F0`: `+0x8C0`, `+0x8D0`, dirty bits 0x800000
  and 0x400000) and the manager's cache (`+0xC0`, `+0xD0`) first. With no colours chosen, nothing is written.
- **A cutscene's copies of the players.** A cutscene draws its own copy of a player, not the character (the
  tutorial's opening has two). When a cut first shows a copy, `0x006087B8` (from the cut change `0x005FCA10`,
  through `0x00603D28`) sets it up once: its draw object at `+0x70`, the character it copies at `+0xBC`, and the
  phantom material from that character's type (`0x0026F378`), as the character's own draw update does
  (`0x000D7C58` sets the draw object's `+0x78` on for types 1 to 4, 8, 10 and 11, and off for the rest). The copy's
  handle is not the character's, so it kept the game's blue whatever the player's colour, and its glow when the
  player turned it off. The hook after that type's read (US 0x00609338) keeps the copy (its handle and its
  character) for the length of the cutscene, and gives 0 for a blue phantom whose player turned its glow off.
  `glow_tick` gives each copy of a player with a colour that player's material copy, as it does the player.
  `glow_shown` finds this game's own player by the character in use (`player_chr()`), so a copy set up in the frame
  a load brings the character in gets the player's own choice too.
- **Souls from enemies.** Each game gives its own player an enemy's souls when the enemy dies (US 0x00272938: the
  NpcParam row's `+0x3C`, through `0x002D6C40` and the character's soul gain `0x002D8978`, which applies the player's
  own soul rate, SpEffect `+0xDC`). `0x002D6C40` halves them when `0x0052A610` says this game is a guest (session
  state 6). The hook after that call (0x00272A88) gives "not a guest" on a friend in seamless co-op, so it gets the
  full souls, as the host does. Online co-op without seamless mode keeps the game's half. A boss's own row has no
  souls, but what dies with it pays the same way (Phalanx's 38 blobs, 35 each, killed by `PhalanxDead`). A killing
  blow of at least 1.5 times the enemy's most HP (`0x00280DC8` sets a bit at `+0x318`) adds a fifth on the game that
  dealt it; the result is rounded up.
- **Souls from bosses.** A boss clear's `SetClearBonus` (US 0x0044A4B8) puts the boss's souls in the player's game
  data (`+0xD4` of `*(*0x01B4EF9C + 8)`). With anyone in the session (`0x0052A5B0` counts the members) it cuts them
  to 75% for the host (types 0 and 8) and 25% for a blue phantom (type 1). The hook after that call (0x0044A5E4) gives
  0 members in seamless co-op, so every player gets the full souls; online co-op without seamless mode keeps the cut.
  - **When they are paid:** `GetSoloClearBonus` pays the waiting souls and clears them, and so do the next load
    (`g_Initialize`'s `AddBlockClearBonus`) and a death (`UpDateBloodMark`).
  - A player alone is paid by `SoloBlockClear`, 3 s after `BlockClear2_1` (6 s in soul form, after its revival there).
    A party's way went through the rating screen, which seamless co-op leaves out, so its players were paid only at
    their next load. `BlockClear2_2` (0.5 s after `BlockClear2_1`, so after a soul's revival) now pays them.
  - **Once per boss.** On a friend the boss clear starts twice, and a second `SetClearBonus` after the payment would
    have the souls paid again at the next load. Two script values (`SeamlessBonusBoss`, `SeamlessBonusPaid`) let
    each step run once; each load clears them.
  - **The final boss.** The final area's clear (the Nexus's `OnEvent_450_1`) paid nothing in a party: its party
    branch only logs an error. It now takes the solo way for every player (`Clear_450`, 3 s later), and revives a
    host in soul form, as for a player alone. A player dead in the fight got nothing at all: its clear returns while
    it is dead (4047), and once it is back the area's start sees the King dead (70) and sets 450 itself, so neither
    450 nor 70 runs there again. The King's death (`OnEvent_70`, once per game) now sets the souls for a player dead
    then, and the load that brings it back pays them.
- **World tendency.** The game's QWC object (`*(*0x01B4EF9C + 0x24)`) holds the tendency in play at `+0x10`, two
  floats (white/black, left/right) for each of worlds 1-7, and the player's own at `+0x48`, which the save keeps
  (docs/69). In online co-op the host's game sends a phantom its values in play (P2P message 0x17, read at US
  0x00525750), and the phantom's game copies them to `+0x10`. That message never comes in seamless co-op, so each
  game would play the shared area in its own player's tendency, and the area scripts that read it on every game
  (`IsSuccessQWC`, in the 1-1 script's events 35, 61 and 62 for one) could differ between players. So the host's
  game sends its values in play before each READY (QWC, 14 words), and a friend's game writes them to `+0x10`:
  - before each summoned load's area scripts run;
  - when a new QWC comes in while it is in the host's world;
  - after the Nexus script's blend (the hook after `bl 0x009204A0` at US 0x004117E4, in `RecvGlobalQwc`'s
    0x004117C0), which sets `+0x10` from the player's own.

  When the session ends, the friend's own values are put back in play: each one that matched its own saved value at
  the first write follows that value as it is then, and any other is put back as it was. The own values at `+0x48`
  are never written.
- **Items.** A phantom's game makes no items: the area's items (lying on corpses, made at each load, US 0x0025CAE8)
  and a dead enemy's drop (rolled by each game when the enemy dies, 0x002B2900) are made only when the player type
  the area was loaded for is body form (0) or soul form (8), and a friend is a blue phantom (1). Both reads of that
  type are hooked (0x0025CB6C, 0x002B2984), and on a friend they read 0. Nothing about these items goes over the
  network: each game has its own list, so a pickup takes the item from that player's game only, and each player's
  drops are its own rolls. Whether an area item was already taken is its flag; on a friend the flags in play are
  the host's, so that read (0x0025CC34) gives the friend's own world's instead. A friend sees the items it hasn't
  taken, whether the host has or not. One it takes is kept in its own world (the item list's flag set, from
  0x0025C5EC), as a boss's soul is: its own world doesn't offer it again.
- **The people (the Nexus's and the areas').** A visitor's game (a phantom's) sets up each person whose character row
  has byte `0xE8` set in a mode of its own (4), chosen in three places, for two kinds of people:
  - a person with a player's body (`c0000`: Thomas, the Crestfallen, Ostrava): its setup, US 0x002999C8, gives the
    character's constructor mode 4;
  - an NPC with a model of its own (Stonefang's smith Ed, `c7131`, and the Dregling merchant, `c7100`): its
    constructor (0x002C2588, or its twin 0x002C3018) picks type 4 itself.

  Such a person has type 4, so the visitor sees a white phantom. On the host, and in the friend's own world, the same
  people are types 5 and 9.
  - A friend keeps that mode, as the game's own co-op does.
  - The character's draw update (US 0x002DD920) reads the type four times:
    - the phantom material on or off (0x000D7C58, the hook at 0x002DDAA8 above);
    - the material's kind (0x000D81D0);
    - the draw path (types 3, 4, 10 and 11 take another);
    - 0x0025D418.

    Types 5 and 9 read alike at all four. Each read is hooked (0x002DDAA8, 0x002DDADC, 0x002DDB48, 0x002DDB90). On a
    friend, such a person of type 4 reads 9, so the friend sees these people as the host does.
  - **The other kind's look.** That draw update is the first kind's only (its draw object is at `+0x6A4`). The
    second kind's constructor, in its visitor branch, also writes the row's `+0xEC` (7105 for the Dregling merchant,
    7135 for Ed) to `+0x24` of the character's `+0x18`, where a body's game leaves -1. That value, not the type, draws
    them as white phantoms: on a friend they were white phantoms in Stonefang from 11-14, when the mode was kept
    (below), to the 11-19 playtest. With the value kept at -1 they are drawn as on the host while their type still
    reads 4. The store's value is hooked (after `extsh r0,r0`, 0x002C2848, and 0x002C32D8 in the twin): on a friend
    it becomes the value already there, so the store changes nothing.
  - **Only these people.** Some characters are set up with mode 4 in every game: the tutorial's developer ghosts
    (`m08_00`'s 400, and `m08_01`'s 401 to 404, white phantoms that walk through the demon's hall and fight each
    other) are type 4 on the host too. Until 11-15 every non-player of type 4 read 9 on a friend, so the friend saw the ghosts as living
    people. The person's setup is now hooked twice:
    - after the byte's read (0x00299B74), where `r24` still holds the mode the setup came with;
    - after the new character's allocation (0x00299BB0).

    A character whose mode the rule changed (byte set, mode not 4 already) is noted, and only the noted ones read 9.
    Each new person at the same address replaces the note, and the notes go when the session ends.
  - **Their faces.** A character's face is made once, when its draw objects are built (US 0x002E0E60). Only a
    character that isn't of a phantom's type (3, 4, 10, 11) gets one (at `+0x664`), made from its own face data. So
    on a friend these people had no face: a hole where it goes, with the hair and ears around it (Thomas and the
    Crestfallen in the Nexus, 11-15). The read compared with 4 there is hooked too (0x002E1000), and on a friend it
    reads 9 by the same rule. The type kept at `+0x68` is still 4.
  - Until 11-14 the three places were hooked to give a body's mode instead, which made these people the friend's own.
    In 1-1, Ostrava then died in the friend's copy of the world within 20 s of the group reaching him, and his death
    (a network event, `OnCharacterDead(300, 624)`) reached the host's world, so he was gone in both. With the mode
    kept, he lives in both.
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
  Not the final area's (40 and 60, the trip down; 71, after the King; 80 and 81, the endings): they play in every
  game when they happen, and their flags set up the way down, the Old One and the Maiden as the area starts, so a
  friend's game keeps them as the host has them. One cleared to be seen again took the way down, the King and the
  Maiden's place out of a friend's Nexus while the host's had them.
- **An area's opening.** The same goes for the cutscene an area's start plays: its script's `Initialize` calls the
  event's handler itself (1-1's dragon, event 2:
  `if proxy:IsCompleteEvent( 2 ) == false then ... OnEvent_2(proxy,param); end`).
  Only those: an area's other cutscenes start from something a player does there (a fog walked through, a place
  reached), and play in every player's game as they did. Of the opening's, a friend's game plays only the newest the
  host has seen. The start plays one a visit, the first one not seen (the Nexus's 30, the first visit; 31, the
  Maiden back after the first demon; 39, the seal over the way down lifted once five worlds are cleared). With more of
  them cleared, it played an older one, and the newer ones, cleared to be played later, left the area as it was
  before them: a friend whose own world hadn't seen 39 stood in its host's Nexus with the seal still there (11-16).
  The older ones stay as the host has them.
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
  - A death in a boss fight comes back at the bloodstain. `PartyGhostDeath_1` notes whether `SeamlessBossFight` was
    set at the death (`SeamlessFightDeath`). If so, `PartyGhostDeath_2` sets flag 19993 just before its reload (past
    the game's last flag, as 19999 for the ending). The flag hook then reads the stain's record, which the death's
    `UpDateBloodMark` (US `0x00447590` -> `0x00419670(manager, player, 1)`) has just filled. The record is the game
    data's `+0x1C`: its place at `+0x0` (x, y, z), the player's rotation at `+0xC` (`+0x10` the yaw) and its map
    at `+0x20`, copied from the character's `+0x640` (`+0x24`, `+0x30`, `+0x3C`). The reload's load step puts the
    player right there, facing the way it died, instead of 1 m beside where the host arrived. A stain in another
    map than the host's falls back to that.
  - Out of the session (`proxy:IsSession() == false`: the host's game gone or out of reach), `PartyGhostDeath_2`
    starts the trip home as `OnRoomDisappeared` does: its message (20001000), then `OnLeave_Limit` 5 s later. The
    game's own handlers for a lost session (`OnRoomDisappeared`, `OnIrregularLeaveSession_1`) skip it while flag 4047
    (a reload under way) is set, and the phantom holds 4047 from its death until it is back. Before, a session lost
    during the watch left the phantom dead out of the session for good, and co-op doesn't join again until 4047 is
    clear ("on its way home: no join until it is there"; playtest 11-14, the second Tower Knight fight).
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
  - No multiplayer wall. When a session connects past an area's first part, `OnEvent_4034` has the host's game close
    the way back to that part's Archstone (`Lua_MultiWall`). The Archstone stands in the last boss's room, and the
    wall is that room's exit, 1996 minus the block (1-2 is block 1: 1995, the Phalanx room's way to 1-2). Only
    `OnEvent_4035` (the session's end) opens it. A session stays across a load, so after a death, or a trip to that
    Archstone, it connected again with the host at the Archstone, and the wall shut the host in the boss room
    (playtest 11-14, after a death at the Tower Knight; reproduced with two copies). A friend's game raised none: its
    wall comes from its own flags 3001-3003, which it doesn't set. `Lua_MultiWall` is now empty, as the host may use
    the Archstones in a session anyway. Checked with two copies: the Phalanx dead, the host to 1-2 and killed there,
    then the respawn at the 1-2 Archstone. The host's way to 1-2 is open, as the friend's is (before: the wall, on the
    host's game only).
- Each area's script: the boss rooms' party walls drop as in solo play; a guest doesn't play the warp-out pose at a
  boss's death; the host may use the Archstones in a session, and the Nexus's stones (which had no session check)
  give a guest the levels' "You can't return during multiplayer." (10010720); a blue phantom may take a boss's soul.
  A boss fight is
  marked from its health bar: every `proxy:SetBossGauge(` (a statement of its own in every area) sets
  `SeamlessBossFight`, and `proxy:ClearBossGauge(` clears it. All of a map's scripts share one set of globals, as
  the area scripts' calls to `global_event.lua`'s functions show. The Nexus's script (`m01_00`) gets this too, after
  its own edits: it holds the final area and its boss's health bar (`OnEvent_422`). Before, a friend who died in that
  fight didn't watch: it came back at once where the host had arrived, in the Nexus above. The King's fight ends at
  its death (`OnEvent_70`, in every game). At the other bosses the kill's `ClearBoss` ends a dead friend's watch, but
  this bar's clearing (`OnEvent_450`) comes only after the King's last words and skips a game whose player is dead
  (flag 4047), so a dead friend would have watched forever.
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
  the tutorial's end each game took its own player to the Nexus (now a friend goes with its host, see the next item),
  and the host, still in its locked room, kicked the friend's joins. (The friend's game then kept leaving the room it was kicked from, and the runtime took down each
  room it joined after that; fixed in ps3recomp, see "A friend kicked out of the room" below.)
- No join during the host's boss fight, as in the original, where that lock kept everyone out once the fight began.
  The scripts mark a fight with flag 19994 (past the game's last flag), set at the boss's health bar
  (`SetBossGauge`) and cleared at its clearing (`ClearBossGauge`); a load of the host's ends it too. The host's game
  tells its guests when its fight begins and ends (message `FGHT`, u32 1 or 0), and again with each ask for its room
  during one. A guest out of the session (a first join, or a rejoin after a drop) holds its join until the fight is
  over; the Co-op window says "The host is in a boss fight. You join as soon as it's over." Before this (11-20), a
  friend that rejoined during the Tower Knight's fight was in the room in the host's game but not in the fight in its
  own: no boss there.
- No trip of a friend's own: each `proxy:WarpNextStage(...);` in an area's script (not the Nexus's, whose stones have
  their own check above) runs only when `proxy:IsSession() == false or proxy:IsClient() == false`. In the game's own
  co-op such a trip ends a client's session: at the area's teardown, a client that didn't ask for a summoned reload
  leaves (docs/145). The tutorial's trips are each player's own:
  - at the end of `m08_00` and `m08_01` the player who got there goes on (`m08_01`'s 14241: "only the PC that
    arrived");
  - the dragon god's cutscene (`m08_02`'s region 2412, event 14350) plays in every game, whoever stepped in, and each
    then runs the same trip to the Nexus (`OnEvent_14381_3`);
  - a death in `m08_01` takes the player to the Nexus (`m08_01Death`, then `OnEvent_14300_3`, which revives it first).

  A friend whose game got there first (one that skipped the cutscene, say) left the session at its own trip, went to
  its own Nexus, and joined again. Its leave reached the host during the host's own load, and that join then stalled
  (see "Not done yet"). Now the friend waits, and the host's trip takes it along as a follow. A friend's death in
  `m08_01` leaves it revived where it fell. The levels' other trips are the boss Archstones' back to the Nexus, which
  a friend can't start in a session.
- The light walls a player walks through (press Cross, a walk, then the wall is gone) are gone in every player's game.
  In the other games the wall's handler plays the walk on the walker's character and marks the wall passed
  (`Luafunc_PlaySynchroAnimation`'s true branch, then a return). The areas made for sessions drop the wall for them
  through the game's wall-delete sync (`LuaCallStart`). The tutorial's (`m08_01`'s 14291) drops it with a direct call
  in the walker's game alone, as the tutorial has no co-op: the others lost the fog, the prompt and the event, but
  kept the wall itself, so they couldn't pass. The edit makes the same call in the true branch, between
  `NotNetMessage_begin` and `_end`, wherever a handler drops its wall by a direct call to a function with
  `SetColiEnable(..., false)` and has no `LuaCallStart`; only 14291 matches.
- A boss that wakes only for the player who walks through its fog also wakes for a player put in its room. Phalanx
  (1-1) is drawn and can be hit only after `PhalanxStandBy`, which the fog's handler (`OnEvent_110`) runs only for
  the player who walked through (its `param:IsNetMessage() == false` branch). In the game's own
  co-op every player walks in. A friend that joins during the fight arrives beside the host instead, so Phalanx
  wasn't in its game. The fight's start (`OnRegionJustIn(<id>, LOCAL_PLAYER, <room>, "OnEvent_<id>", once)`, the
  handler with `SetBossGauge`) runs in each game whose player arrives in the room, one put there included. It now
  runs the same stand-by first. For a player who walked in, that is a second run, which sets the same values again.
  The edit takes the functions a fog's handler (`HELPID_GO_TO_IN`) calls in that branch; only Phalanx's matches
  (checked on every area's script).
- The Nexus's timers and checks (`OnKeyTime2`, `OnRegistFunc`) are each player's own. No phantom reaches the Nexus in
  the original game, so its script registers them outside `NotNetMessage_begin` and `_end`, and in a session each one
  is a network event: a step that fires in one game runs in the others too, where it registers the next step again.
  Each game runs its own chains from its own flags (the first visit's messages and the Nexial Binding at their end,
  the Maiden's revival, the clear items), so when two ran at once in a session each step multiplied: a friend got
  three Bindings after the tutorial's end. The edit registers all 29 between `NotNetMessage_begin` and `_end` (not
  the ones in a comment). The Nexus's other network events (the Archstones, the NPCs' events) stay as they are.
- The kneel before the Maiden in Black at a level-up is the leveling player's own. The talk system starts two events
  through `LuaCall`: `OnEvent_90` turns the player to her, takes its control (`EnableLogic(10000, false)`) and kneels,
  and `OnEvent_91` stands it up and gives the control back. In a session that call reaches every game, where 10000 is
  that game's own player, so a friend knelt and couldn't move while the host leveled up. The edit makes any `LuaCall`
  handler in the Nexus that takes or gives back the player's control ignore a call from another game
  (`param:IsNetMessage()`); only 90 and 91 match. Checked with two copies: the friend held the stick through the
  host's level-up and moved the whole time (before: no movement for those 20 s).
- The Nexus's Archstones ignore another player's touch. A touch is a network event, so its handler ran in every
  player's game: when the host touched a stone, a friend's game said "You can't return during multiplayer." (the
  guest check above), and a friend's touch ran the host's handler. Each stone's handler now starts with
  `if param:IsNetMessage() == true then return; end`, as the levels' stones do.
- The poses a player's own script plays on that player show in the other players' games too: an Archstone's touch
  (8283), the trip home after it (8284) and a sword pulled out (8290: Stormbringer, the Nexus's last sword and three
  more). In the original no phantom saw a player use an Archstone, and a pull runs only in the puller's game, so the
  other games never played them; a friend saw the host stand still at the stone and then the loading screen
  (playtest 11-20). After each live `PlayAnimation(10000 or LOCAL_PLAYER, <pose>)` (70 in the areas' scripts) the edit
  sends `LuaCallStartPlus(4013, <area's key>, <pose>)`, unless the handler runs for another player's message. Each
  area's `SEAMLESS_Pose` (registered as it starts) plays it on the sender's character, `GetPlayID() + 10001`, as the
  game's own pose sync does, and does nothing in the sender's game. That sync (`Luafunc_PlaySynchroAnimation`) isn't
  used, as it would play the pose a second time in the sender's game, under the script's own `OnChrAnimEnd` watch of
  it. The send goes out from inside `NotNetMessage` blocks too: the call's network send checks `proxy+0x65`, and
  `NotNetMessage_begin` sets `proxy+0x64`. The key is the area's (7600 + 10 x world + block): the trip home's pose,
  sent just before the trip, reached the friend's game 2 s after its follow had loaded 1-1, and played on the host
  there as it arrived. No handler in the next area takes the old key, so the friend sees the touch and then the
  load. Checked with two copies at the Boletaria stone: the host's game sent both poses; the friend's game played the
  touch on the host's character (the stone's green light in its frames) and nothing in 1-1; the friend's own touch
  gave it the "can't return" message and nothing in the host's game. The swords' pulls weren't run.

The edited copies are cached by the original's size and hash, so a script read again costs nothing.

**The co-op log** (`logs\coop.log`, in every report; docs/74): the session's lines of the log, each with its time
(UTC). Co-op adds a line at each change of the session's state, with the members and the room's timer, and the name
of each session event the game raises (`OnRoomTimeOut`, `OnIrregularLeaveSession`, `OnLeave_Limit`, ...: a hook at
`0x00467628`, the function that calls a `global_event.lua` handler by name). Read the host's and the friends' side by
side by their times.

## Settings for developers and tests

| Variable | What it does |
|---|---|
| `DES_SEAMLESS=host` / `join` | Co-op mode for this start (the Seamless co-op window sets it when it starts the game again). |
| `DES_SEAMLESS_CODE=<code>` | The code to join. |
| `DES_SEAMLESS_MODS_OK=names` | Set by the Seamless co-op window: the mods the player accepted at Host or Join, for the co-op log. Unset with mods on (a test run), the log says no warning was shown. |
| `DES_SEAMLESS_PORT=n` | The host's port. Default 36100. |
| `DES_SEAMLESS_UPNP=0` | The host leaves the router alone (the port forwarded by hand). |
| `DES_SEAMLESS_ADDRESS=ip` | The host's public IPv4 address, for the code, when the router doesn't give it. Co-op's Public address keeps it in settings.ini; a report shows only whether it is set. |
| `DES_SEAMLESS_PLAYERS=n` | The most players a host takes, itself included: 2 to 4 (4 when unset). Settings > Network keeps it in settings.ini. One more connects but waits: the host answers its ask for the room with FULL, and it joins once the host answers with the room again (the room server also refuses a join past the limit, `max_players`). |
| `DES_SEAMLESS_GLOW=0` | The other players (and this one) see this player without its blue phantom glow in a session. Settings > Network > Your glow keeps it in settings.ini; a change counts within 2 s (GLOW). |
| `DES_SEAMLESS_GLOW_COLOR=RRGGBB` | This player's glow colour in a session, for the others and itself (unset: the game's blue). Settings > Network > Your glow colour keeps it in settings.ini; a change counts within 2 s (GLOW). |
| `DES_SEAMLESS_STUN` | Unset: a host whose router gives no public address asks Google's STUN server (`stun.l.google.com:19302`) for it. `0`: never. `host:port`: that server instead. settings.ini can set it too. |
| `DES_SEAMLESS_INTRO=host[:port]` | The endpoint that introduces players for room codes (port 36100 by default). Settings sets it from the Network tab's Server row: this project's server, or 127.0.0.1 for the local test server; any other row, `0`. A value in settings.ini (or the environment) wins. `0`: no room code, the direct code only, and nothing goes to the server; a room code can't be joined. |
| `DES_SEAMLESS_CODE_FILE=path` | Tests: the host writes its codes there (public, local, then the room code once the endpoint has the room); a joining game with no `DES_SEAMLESS_CODE` waits for the file and takes the code from it: the public one, else the local one. |
| `DES_SEAMLESS_CODE_PICK=room` | Tests: that joining game waits for the room code in the file instead, and joins with it. |

The runtime's knobs that co-op sets: `PS3_NET_ONLINE=1`, `PS3_NP_ONLINE_ID`, `PS3_NET_P2P_PORT`, `PSNR_SERVER`,
`PSNR_KEY`, `PS3_HTTP_LOCAL` (the in-process server's port), `PS3_NET_HOSTS=localhost=127.0.0.1` (so no other name
resolves) and `PS3_SAVEDATA_ROOT` (the co-op save). A friend's game sets `PSNR_BACKGROUND=1` (the join runs in a
thread and keeps trying), and for a room code `PSNR_ROOM`, `PSNR_VERSION` (co-op's protocol version, 1) and
`PSNR_SERVER` (the endpoint). A value set in the environment wins, as for the Network rows.

**A friend's leave** does what the friend's game does when the host's room is gone (`global_event.lua`'s
`OnRoomDisappeared`, then `OnLeave_Limit`), through the same natives:
1. leave the session (`LeaveSession`), and ask for the player's own flags back at the next load
   (`SetFlagInitState(2)`);
2. once out of the session: back to where the player stood before the join (`SetSummonedPos`,
   `SetDefaultMapUid(-1)`), in the form it had before the join (`SetChrTypeDataGreyNext`), with a load
   (`WarpNextStageKick`);
3. at home, the game saves by itself. The Seamless co-op window waits for a file written in the co-op save after the arrival
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
  kill sets 0. The tutorial's end sets 8 too: the player who dies there runs `m08_01`'s `OnEvent_14300_3` (revive,
  `Tutorial_end`, `ChangeGreyGhost`, warp to the Nexus), but a friend following the host there never died. Its load
  step, on a follow from `m08` to the Nexus (`m01`), sets 8 and makes `Tutorial_end`'s only write (its manager's
  `+0xA95` to 0, US `0x0020D3C0`).
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

- Room codes (docs/162) are tested on this PC only: the NAT matrix of ps3recomp's `test_psnr_punch` (virtual
  routers of six kinds, both ways), and two copies of the game through a local endpoint. Not yet across two real
  networks, and not against the project's server.
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

  The Maiden's level up, the storage and the smith were checked before the people were kept as the host's (11-14),
  and not run again since.
- The people kept as the host's (11-14, scripted, each player on an after-tutorial save):
  - In 1-1, Ostrava lived in both worlds at each check, up to 80 s after the group reached him. Before, he died in
    both within 20 s.
  - In the Nexus, the friend got Stockpile Thomas's and the Crestfallen's talk prompts and first lines.
  - The friend saw both as the host did: Thomas plain, and the Crestfallen with the sheen the host's game gives him
    too. Their faces were missing on the friend, though, which these checks didn't catch from where they looked.
- The people's faces (11-15, scripted, the same saves, the friend joining the host in the Nexus, each person seen
  from 2 m): before, Thomas had no face on the friend's game, with the wall showing through where it goes, as soon
  as the friend arrived in the Nexus and again after it died there. With the face's read hooked, Thomas and the
  Crestfallen (with his sheen) have their faces, both still have type 4, and the friend still gets Thomas's talk
  prompt. Ostrava's check in 1-1 wasn't run again: the hook changes which draw objects are built, not the stored
  type.
- The tutorial's ghosts (11-15, two copies, scripted, the pinned tutorial save, with a scratch log of the setups and
  of the draw update's read):
  - On the friend's game, each ghost's setup came with mode 4 already: `m08_00`'s 400, and `m08_01`'s 401 to 404
    after the host's load. None was noted.
  - With the friend in `m08_00`'s ghost region (2050), the ghost wandered, and the draw update read its type as 4.
    With the rule before this change (a scratch switch, same build), the same run read 9.
  - In the Nexus (the after-tutorial saves), the friend's game noted the 14 people the host's game sets up, so
    Thomas and the others still read 9.
  - The shots didn't catch the ghost itself on screen: it fades in for about 10 s at a time.
- Stonefang's merchant and smith (11-19, two copies, scripted, a save from the 11-15 co-op session, the friend
  following the host to 2-1, the merchant seen from 3 m by both):
  - Before (a scratch switch, same build), the friend saw the Dregling merchant as a white phantom. The value at
    `+0x24` of his `+0x18` was 7105 on the friend's game and -1 on the host's.
  - With the store's value kept, the friend saw him as the host does, with -1 there and his type still 4. Ed's store
    was kept the same way (7135 not written); he wasn't looked at.
  - Not checked with the change: the talk prompts of the two.
- Closing the game window as a friend: the session is lost and nothing from it is saved (the game only saves at
  home).
- The tutorial's demon (the Vanguard, `m08_01`) offered its soul only outside a session (`ATTR_NOSESSION`; the
  game has no co-op there), so with a friend nobody got it. Its prompt is now offered with or without a session, to
  each player, like the other bosses'. Checked on the script (both of its registrations change, and only those); not
  yet in the game. (An earlier note here called it the final area's soul: `m08` is the tutorial.)
- The Vanguard after the load into its area (11-15, two copies, scripted, the pinned tutorial save, god mode). Both
  players stood by `m08_00`'s Archstone, then the host loaded `m08_01`:
  - Before, the Vanguard was put at (99.3, -0.2, -21.9), outside its hall, on the host's screen in one run and on
    the friend's in another.
  - With the change, in three runs it stayed home (0.3, 19.2, -102.7) on both screens. Each run dropped one old
    message on the host's game, and up to two on the friend's (those at the last emptying).
  - With both players then put in its hall, the fight started on both screens. For the 90 s watched, the Vanguard
    stood in the same place on both games, within half a metre at each check.
  - A friend that left during the host's load (its own trip at the tutorial's end, the friend 1.5 s ahead, with that
    trip as it was before the friend went with its host): the host's game still listed it as a member after the
    load, while the room library had dropped it. Before, the host's hold waited until the friend joined again; with
    the room check it ended as the host arrived ("the 0 other player(s)").
- Items for a friend: the area's items and enemy drops (above). Not covered: a drop that an item lot allows only once
  (its flag is read from the host's flags in play), and the scripted pickups (the Demonbrand, the keys) that an
  area's script offers to the host and to players in their own world only, as the boss souls were.
- The final area under the Nexus (King Allant, `m01_00`) is the host's alone in the original. Its fog
  (`OnPlayerActionInRegionAttribute(61, 2895, ...)`, host, alive or soul form) has no second entry for the others: no
  `LuaCall(61, 5, ...)` is registered, and `OnEvent_61_3` would send `LuaCallStart(61, 5)` only while flag 61 is
  clear, which `OnEvent_61` has just set. So the edit gives everyone the first entry instead ("The final area and the
  endings" above). The boss has no soul prompt.
- Watching a boss fight after dying, scripted: host and friend were put in 1-1's boss arena, and the fight started
  for both (the boss's health bar on both screens). The friend was then killed. It stayed down and watched for the
  half minute until the host killed the boss, then came back at the area's start, and the host made its character
  again. A death outside a boss fight comes back within seconds. With the host killed too, half a minute after the
  friend, the host's respawn took the friend along: both at the start, the friend standing with full health. Not
  yet by hand.
- A friend put in Phalanx's room during the fight (two copies, scripted, the friend fighting by itself): before, the
  friend had Phalanx's health bar on its screen but no Phalanx and no hoplites, and 40 s of its blows took no health. With the
  edit, both games draw them, and in 40 s the friend killed 12 of the 38 hoplites in its game. Not yet by hand, and
  not run on the other discs.
- The tutorial's trips with a friend (11-15, two copies, scripted, the pinned tutorial save, god mode on the friend):
  - The dragon god's end, both players in `m08_02` and put into its region (2412), the friend 1.5 s before the host.
    Before, the friend's game ran its own trip to the Nexus first and left the session; its join back then stalled
    (as in the user's session). With the change, the friend followed the host's load, and in the Nexus each game had
    the other as a member. With the host 1.5 s first, the friend followed before the change too.
  - A friend killed in `m08_01`: it came back where it fell and stayed in the session. The host, killed next, went
    straight to the Nexus, and the friend followed.

  Not yet by hand, and not run on the other discs.
- 1-2's castle gate with a friend (11-15, two copies, scripted, the pinned tutorial save, god mode on both): flag
  8035 set on the host, both in `m02_01` (the friend had 8035 there too, from the host's flags), the Tower Knight
  killed by a test switch after the fight's start (4224 on both), the host's soul taken (the friend got its own, 4252),
  then both put into the gate's region (2250), the friend 3 s after the host. Before, only the host's game played the
  gate's cutscene (scene 20101) and set 571, as in the user's session. With the edit, both games played it and set
  571. Not yet by hand, and not run on the other discs.
- The rest of the game's scripts, checked for steps kept for the host (11-15): every area script's 218 `if` lines on
  the host and the client (83 at an area start, 135 in handlers), and the other ways a script tells them apart
  (`IsSession`, `ATTR_NOSESSION`, host-only prompts, `IsNetMessage`, `IsInParty`). Apart from the edits above, the
  host-only steps are "the host decides, every game does it" broadcasts, NPCs, the boss rooms' first entries and the
  bosses' thinking, all fine as they are. Two kinds are left as the original has them: the dragons' item drops in 1-2
  and 1-4 (`OnEvent_800_3_syncro_anime`, `HiryuOsaDead`, the host's alone, with a trophy) and the final boss (since
  11-16 everyone's: "The final area and the endings" above).
  Every area script through the edits before and after (a test program built from this file): only 1-1, 1-2, 1-3,
  1-4 (its progress flags), 2-3, 3-1 and the Nexus change, each as described.
- 3-1's flight with a friend (11-15, two copies, scripted, the pinned tutorial save, god mode on both): flag 5123
  set on the host, both loaded into `m04_00` (the friend following), then one player put on the flight's region
  (2030):
  - The host first: the host's game played the cutscene (scene 40001), and both stood at the landing (y 53.88) when
    it ended. Flag 410 was set in the host's game. (Read again on 11-16: the friend's game did not play it then; the
    friend was at the landing before its own walk, and its cutscene came from that walk, after which it stayed
    below, its start's handler used up.)
  - The friend first: the friend's game played the cutscene, and the host's game put the host at the landing
    without it, within 25 s. Flag 410 was set in the friend's game only; the host's watch was gone (the cutscene's
    end removes it everywhere), so the host walking back onto the spot did nothing until a load.
  - The host kept its session through both ("the host stays in its session": the warp's leave held back, as at the
    other trips).

  Without the edits the handler returns at once in a party, so nothing happens for anyone (read from the script, not
  run). Not yet by hand, and not run on the other discs.
- 3-1's flight in every game (`flights_for_all`, 11-16; the same setup as above, both copies at below-normal
  priority). Each run checks that:
  - each game opens scene 40001 once;
  - each player stands at the landing (y 53.88) 30 s after it lands;
  - the two are 1 m or more apart;
  - the friend's game logs its spot;
  - flag 410 is set in both.

  Results:
  - The friend first, the host first, and both at once (put on the spot 0.1 s apart): all pass, the same each time.
    Both games played the cutscene at once, the host landed on the point (247.15 53.88 -89.84), and the friend on
    spot 1 (248.15 53.88 -90.84), 1.41 m away. Neither moved in the 30 s after.
  - Without the spots, both players landed on the point at the same moment, then fell off the walkway within 5 s
    and died. Moving the friend at once with the call behind `LoadChrPos` (0x00273BE0) did the same: the waiting
    warp undid it.
  - In between, a friend's cutscene started 1.5 s × its player number later (the landings 2-3 s apart). It passed
    these tests, with the two pushed about 0.8 m apart. In the user's hand test with the scripted friend (11-16),
    though, the friend landed on the host, who hadn't moved, and pushed the host off the walkway.

  Every area script through the edits (the test program as above): only 3-1's changes. Not yet by hand with the
  spots, not with three or four players, and not run on the other discs.
- The final area and the endings (11-16, two copies, scripted, saves from just before the final area, god mode on
  both; the King and the Maiden killed by test switches, the host's trip down set by its flags). Twice, the same up
  to the ending:
  - In the Nexus the friend's game played only 39 and 303 of the host's cutscenes, and the seal over the way down
    was lifted. Before, it played 30 and 31 too, and the seal stayed (the user's run).
  - After the host's trip down and back (a load the friend followed), the friend kept 40.
  - The friend went through the fog by its own prompt (61), and the King died in both games.
  - The host pulled the sword once; each player got one (+1 each). Before, the host got two.
  - The cutscene after the kill (71) played in the friend's game too, and its Maiden prayed (16145).
  - The bad ending, from the host's game (the Maiden killed): the friend's game took the marker once, went through
    its own ending without following the host's NG+ load, left the session, and joined the host again from its own
    NG+ Nexus. Both clear counts 1, the same clear state.
  - The good ending, from the friend's game (its prompt at the way out, region 2010): the host's game ran it too
    (clear state 1 on both), the host stayed in its session through the ending's leave and loaded into NG+, and
    the friend went through its own and joined again from its own NG+ Nexus.
  - The user's playthrough of it (by hand, with the bot as the friend) found the friend without "The Demon Was
    Destroyed", the King's health bar left on its screen, and its colour gone to the game's blue at the Maiden's
    kill. After the change for the text (a third scripted run, the bad ending): the friend's game set 450 at the
    text's end, which it never did in the three runs before, and the other checks passed again. The colour is
    checked by hand only.

  The endings' cutscenes and credits were skipped in the scripted runs, and the other discs weren't run.
- A friend that leaves during the host's load and joins again isn't placed. The host's game keeps its old member
  through the load: so it was before the change above ("A friend that leaves during the host's load", 11-16). The
  change, checked with two copies (scripted, the pinned tutorial save, god mode on the friend, a test switch that
  brings back the friend's own trip at the tutorial's end, so that its leave comes during the host's load). Before
  it, twice: the friend joined the room again but was never placed (once through 150 s of the friend's Cross
  presses). With the change, six times (twice as a test switch): the host took the leave during its load, its
  session ended and it opened a new room, and the friend was back in the host's world, about a minute after the
  host's arrival where timed. A friend's game closed 0.5 s and 2 s into the host's load: both times the game took
  the leave itself (the byte not set yet, then the load over), and the host's session ended and it opened a new room.
  - Three copies, the host and two friends, one friend leaving during the host's load. With the change on the host
    alone (twice), the host's session went on with the other friend, who followed into the Nexus, and the friend
    that left joined again; but the other friend's game kept the old member, and after the rejoin it had no member
    for that friend (its list read once). With the change on every game, that friend's game took the leave during
    its own follow load, and after the rejoin it had the friend as a member again.
  - Four copies, the host and three friends, one friend leaving during the host's load (twice, the change on every
    game): the host and both other friends took the leave during their loads, the host's session went on, the
    friend joined again, and afterwards every game had the other three players as members.
  - The host's game closed 0.5 s, 2 s, 3 s and 8 s into a friend's follow (the Nexus's long load for 3 s and 8 s):
    the friend lost the room, went home and played on, with no member-left for the host. The same with the change on
    the host alone.

  Not run: a leave outside any load (the test trip's leave always came during one; the change does nothing then),
  a friend's Leave co-op just as the host travels, and the other discs.
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
