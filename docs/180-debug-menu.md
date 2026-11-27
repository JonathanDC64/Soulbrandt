# 180. The game's debug menus

The retail game still has the debug menus its developers used. The port can show them again:

| Knob | What it shows |
|---|---|
| `DES_DBGMENU=1` | the runtime debug menu, toggled with Select |
| `DES_DBGMENU_TITLE=1` | the developers' step list, in place of the title menu |

Both are environment variables only. There is no Settings row. They are read once, at start.

## 1. No cheating

With either knob set:
- **The session is offline.**
  - At start, before anything connects, the network knobs are cleared and the runtime is locked offline
    (`des_ui_net_off`).
  - That rules out online play, seamless co-op and Pantheon records.
  - The Network tab says why: "Off: the debug menu is on".
  - Setting a network knob later changes nothing.
- **The game saves to a copy.**
  - At start, the save folder is copied to `<save folder>-debug`, for example
    `gamedata/dev_hdd0/home/00000001/savedata-debug`. The game saves there for the whole session.
  - The normal saves are only read. So nothing the menus change (items, flags, World Tendency, the map) reaches the
    save that a later online session uses.
  - The copy is made again at each start, so a debug session always begins from the normal saves.
  - If the copy can't be made, the menus stay off, and the session is still offline.
- **The window's caption ends in "(debug menu)".**

## 2. The runtime debug menu (`DES_DBGMENU=1`)

**Select** cycles the menu:
1. shown, with the pad: the game ignores your buttons;
2. shown while you play;
3. hidden.

| Button | What it does |
|---|---|
| D-pad up/down | moves the cursor; ▽ at the bottom means more below |
| Cross | opens a folder, or runs an item marked "(Push Enter)" |
| Circle | goes back up a level |

Each folder shows how many items it holds, as `[NAME](count)`. The top level is:
- **SYSTEM:** memory, CPU and GPU timings, the renderer (FPS, screenshots, gamma), network settings, the pads,
  trophies, the menus (event flags, menu and text tests) and particles.
- **RESOURCE:** the loaded textures, materials, params, messages and maps.
- **FILE:** the loading queues.
- **SOUND:** the sound engine's counters and volumes.
- **GAME:**
  - the map's instances, objects and collision;
  - saving and loading, the item drop tables and the game's options;
  - World Tendency and network packets;
  - one folder per character;
  - AI, rendering switches and the cameras;
  - messages, bloodstains and signs, and cutscenes.

The texts are the developers' own, in English and Japanese. The menu's font has the Japanese characters.

## 3. The title debug menu (`DES_DBGMENU_TITLE=1`)

The title menu becomes a numbered list. Up/Down move, Cross opens, Circle goes back, and Left/Right turn a list's
pages. The entries:
- **00 Model Viewer:** the model parts, then the viewer.
- **02 Walk Through:** the map files, 10 pages. Cross loads one; `m01_00_00_00` is the Nexus.
- **05 SFX test.**
- **06 Network.**
- **08 FaceGen test.**
- **09 TitleList.**
- **10 and 11:** the title with the developers' old servers. They are long gone, and the session is offline anyway.
- **01, 03, 04 and 07:** empty ("---").

Walk Through can list maps that don't load: test maps, and Northern Limit, which needs more memory than the game
gives it.

## 4. How it works

The retail EBOOT kept the menus' code, their items, the debug font and the debug pad. Only two things were cut:
- the menu's root: `create_debug_menu` (0x00015A58) leaves it at 0;
- the body of the menu's per-frame call: 0x00015A50 is a bare `blr`.

`port/des_dbgmenu.cpp` restores both through three instruction hooks (`des_dbgmenu_hook` in `port/insn_hooks.txt`).
Addresses are BLUS30443's; the region tables have the other discs'.

| Hook | When | What the port does |
|---|---|---|
| 0x00020F44 | `create_debug_menu` has returned the menu | makes the root with the game's own label function and text. The game asks for the root right after (0x00020F4C) and adds its items to it; so do the other places that add items, later |
| 0x0016F42C | the `cmpwi` on the title step's new memory | runs the debug title step's constructor (0x0015C878) on it, and takes the branch past the normal one (0x0018B7D0) |
| 0x0001BB38 | after the emptied call, once a frame | the per-frame work below |

**The per-frame work:**
- read the debug pad's keys;
- on Select, cycle the state;
- while the menu has the pad, run the root's input;
- update the window and its timer, stepped by the frame's time;
- while shown, draw the window and the tree.

**The menu object** (0x14 bytes): +4 the root, +8 the debug pad, +0xC the window, +0x10 the state (0 hidden, 1 with
the pad, 2 shown). The title step makes the pad and the window.

**Taking the pad:**
- The game's pad device has four sources, each with a count at +0x20. The game ignores a source while its count is
  above 0.
- 0x001C67A8 (0, source) raises the count and 0x001C6760 lowers it.
- The menu's own pad is source 2. So while the menu has the pad, sources 0, 1 and 3 are off: the character doesn't
  move, attack or change items while you use the menu.

## 5. Tested

Tested on the US disc (BLUS30443) only:
- **The runtime menu,** in the tutorial and in the Nexus:
  - each top folder opened;
  - Select's three states;
  - a 2-second walk and D-pad Right did nothing while the menu had the pad, and the same walk moved the character
    once the game had it back.
- **The title menu:**
  - the model list;
  - Walk Through's pages;
  - loading the Nexus, and the runtime menu working there.

**Not run yet:**
- the EU, JP and Asia discs. Their addresses were found with `tools/region/region_table.py find`, and the EU ones pass
  `verify`;
- the items that change values;
- most of the "(Push Enter)" items;
- the other title steps.
