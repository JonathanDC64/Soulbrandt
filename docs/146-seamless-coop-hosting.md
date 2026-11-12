# 146. Seamless co-op: hosting and joining

Seamless co-op lets friends play through the game together. One player's game hosts; the others join it with an
invite code. **No online server is used**: not this project's, and not anyone else's. Only the friends' games talk
to each other.

This page covers the first part: how a game hosts, how a friend joins, and what each game does with the network.
Following the host from area to area, deaths and boss fights come in later parts.

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
2. Open **Co-op**, choose **Join**, then **Paste and join**. The game starts again on your co-op characters.

**To leave:** **Co-op** > **Leave co-op**. The game starts again on your normal characters.

**Co-op characters** are kept in their own save folder (`savedata-coop`, beside the normal one). Co-op never writes
your normal characters.

**Up to 4 players** (the host and three friends).

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

## Not done yet

- Joining in the game itself (the host's room made without a sign, the friend placed in the host's world), following
  the host, deaths and boss fights: tested in the spike (docs/145, section 6), not built into this part yet.
- Importing one normal character into the co-op save, once.
- Typing a code (the Join window takes it from the clipboard only). Off Windows, the clipboard is the game's own.
- The Japanese and Asian discs: allowed once a test with the others works.
