# 140. The Japanese and Asian discs (BCJS30022, BCAS20071)

The port runs the Japanese disc (BCJS30022) and the Asian disc (BCAS20071), both **updated to 1.04**, as well as the US
and European discs. Issue #432 asked for them. They play online in one world with the US and European discs.

## Why the update is required

The discs on their own are an earlier build of the game, compiled differently: only 60% of their functions match the
US disc's. The 1.04 updates bring both programs in line with the US program. 99.6% of their functions are then identical
to the US ones, with their addresses moved, as on the European disc. All 26 encrypted data files (`.sdat`) of an updated
game have exactly the US contents.

So setup needs the update for these two discs, and only for them.

## What setup does

For these discs the title's entry in `tools/setup/des_setup/game_table.json` has an `update`: the version, the package's
size and SHA-256, and the 91 files it holds.

Setup looks for the update, in this order:

1. the path given (`--update`, or Choose the update on the setup screen);
2. RPCS3's `dev_hdd0/game` folder, where RPCS3 installs updates;
3. the game's folder and the two folders above it.

It accepts the package itself (`.pkg`) or an installed folder. A folder counts if its `PARAM.SFO` names this disc and
version 01.04. Either way each file is checked against the table's sizes and sums.

Setup has no key and no decryptor (docs/51), so it never opens a package itself. The player's RPCS3 installs it, with
`--headless --installpkg`, in a folder of setup's own that is removed afterwards. Nothing is written in the player's
RPCS3. RPCS3 exits with an error status in this mode whether or not it installed anything, so setup goes by the folder
and its files. Without an RPCS3 that setup can start, it asks the player to install the package in RPCS3 and choose the
folder.

The files go to `<setup data>/update/<TITLE_ID>`, and `ready.txt` gets `update=update/<TITLE_ID>`.

RPCS3 decrypts the 14 `.sdat` files as it installs them and keeps their names. Those plain files equal the table's
decrypted files, so setup accepts either form and does not decrypt them again. The runtime reads a plain `.sdat` as it
is (`edat_resolve`).

15 of the game's encrypted originals come from the update: `EBOOT.BIN` and 14 `.sdat` files. The other 12 `.sdat` files
come from the disc.

## At run time

The host sets `PS3_VFS_UPDATE` from `ready.txt`, so an update left over from another disc never affects this one.

The runtime (ps3recomp, `PS3_VFS_UPDATE`) gives the guest a file from that folder in place of the disc's, as on a PS3
that installed the update. It is applied in `ps3_vfs_ps3game_fallback`, the hook all three path translators call.

## Addresses

The scheme is the European disc's (docs/132): `port/regions/<TITLE>.inc`, with `DES_REGION_ADDR(us, other)` sorted by
the US address, and `DES_AT(us)` for the running disc's. `0xFFFFFFFF` (`kDesNotOnDisc`) is "this program has no such
thing". The two new tables have 231 addresses each, most found automatically by matching functions (`tools/region`), the few
where the code around them moved by hand.

What differs from the European disc:

- **The boot logos.** The Japanese and Asian programs have 4 (last type 3), and the logo dialog's fields sit one word
  further on (`des_options.cpp`).
- **Slots of the TOC.** The TOC (the program's table of globals) has one more entry on these two programs, so the slots
  the port reads are 4 bytes lower. The port names those slots by address in the tables, as it does everything else
  (`data ... a slot of the TOC`). Reading them as `r2 - 0x72F0` looks right and is wrong here.

  That one was found the hard way: the menu system's update got the wrong object, so the message box the server sends at
  sign-in was never drawn, and the game waited for its OK forever on a black screen. Offline has no such box, so
  offline played fine.

- **The online tag switch.** These programs have no `kOnlineTagCases`, so there is nothing to patch there.

The SPU images (`scripts/spu_table.json`) have `vaddr_jp` and `vaddr_asia` beside `vaddr` and `vaddr_eu`.

## Saves, trophies, shaders

- Save folders: `BCJS30022DEMONSS005` and `BCAS20071DEMONSS005`. A save from another disc also loads if its folder is
  renamed.
- Trophies: both discs use the set NPWR00245.
- The pipeline index (docs/97) is shared with the US disc (`game_tag` in `des_prework.cpp`).

## Languages

The programs pick their language from the PS3 system language themselves, so the port only sets that
(`des_region_language`).

- **The Japanese disc** is always Japanese, whatever the system language says. It has no Language row in Settings.
- **The Asian disc** has English, Traditional Chinese and Korean, and a Language row for them. It has Japanese text
  too, but its program never picks it: a PS3 set to Japanese gets English.

## Online

Everyone plays in the US disc's world, as with the European disc (docs/132).

- The Japanese and Asian programs ask for `/demons-souls/ss.info` and `/demons-souls-asia/ss.info` on
  `cmnap.scej-online.jp:18000`, a name the port's server list already maps. The server's bootstrap ignores the path.
- Both programs use one communication ID, NPWR00245. The server's psnr keeps its rooms and boards under the US disc's,
  NPWR00881 (server `internal/psnr/rooms.go`). Until that is deployed, Japanese and Asian players only meet each other.

## How to test

Setup and the region tables have tests:

```
python -m unittest discover -s tools/setup/tests
python tools/region/region_table.py check
python -m unittest discover -s tools/region/tests
```

In the game, check the disc's text, a level load, the trophy list and an online sign-in. At sign-in the server's message
box must appear and close with OK.
