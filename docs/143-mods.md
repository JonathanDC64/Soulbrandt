# 143. Mods

Players can put mods in a folder and turn each one on or off in the Mods window. The game then reads a mod's files in
place of its own. Nothing is copied into the game's files, so turning a mod off gives the game back as it was.

**The Mods window** opens from the menu bar (F1, or the pad's menu buttons), beside Settings and Trophies, and from
the start screen's Mods button. It is made for players who have never used a mod, top to bottom:

- **A summary:** how many mods are on, "Restart the game to use your changes" when the list differs from what this
  session runs with, and "Playing offline" when a gameplay mod is on.
- **Two buttons:** Open the mods folder, and Refresh the list (after you add, remove or rename a mod's folder).
- **Your mods:** one card per mod, in their order, with arrows to move it, a big check box, its name, a sentence on
  what it changes ("Changes how the game looks or sounds", or "Changes how the game plays. You play offline while
  it's on.") and On or Off.
  - Pressing a card (Cross, Enter or a click) turns it on or off.
  - Its arrows, the shoulder buttons (L1 and R1) or Page Up and Page Down move it up or down. The higher mod wins
    when two change the same file.
  - A card that is on is green, and one that differs from this session says "after a restart".
  - When a mod higher in the list replaces some of this one's files, the card says so: "Move this mod up to use its
    own."
- **Needs attention:** the folders the game can't use as they are (below), each saying what is wrong and what to do.
- **Good to know:** a few lines on saves, online play, the order and big texture mods.

Everything is in one list, so Up and Down reach it all with a pad.

## 1. The mods folder

- **Where:** `mods` in the data folder, beside `settings.ini`. In a portable install that is `data\mods` beside the
  program. Mods > Open the mods folder creates it and opens it.
- **One folder per mod.** The folder's name is the mod's name in the list.
- **What goes in a mod's folder:** the game's folders it changes (`menu`, `param`, `parts`...), laid out as on the
  disc below `PS3_GAME\USRDIR`. A mod's folder can also hold:
  - a `USRDIR` or `PS3_GAME\USRDIR` (as many PS3 and RPCS3 mods ship);
  - one wrapper folder, as archives often unpack (`My Mod\My Mod v1.2\menu\...`).

  The game looks up to five folders deep for a `USRDIR`, else for the shallowest folder that holds one of the game's
  folders. Folders whose names start with `Original`, `Backup`, `Vanilla`, `Uninstall` or `Unmodded` are passed over:
  mods ship the game's own files in them to undo the mod. Two candidates at the same depth (two versions of the mod)
  is a problem, and the card names both.
- **Left out:** the mod's notes and pictures, wherever they are (`.txt`, `.md`, `.pdf`, `.jpg`...; the game has no
  file of those kinds), and what file managers leave (`desktop.ini`, `Thumbs.db`, `.DS_Store`, `__MACOSX`).
- **Loose files.** Some mods ship their files without the game's folders: a lone `c0000.anibnd.dcx`, a
  `mapstudio\m01_00_00_00.msb`, drawparam files in a wrapper folder. The game's own files (in the game folder the
  game reads) say where each one goes: the file's name, and the folders it is in when two of the game's files share
  the name. The card is then listed under Needs attention with **Fix it**; pressing it moves the files inside the
  mod's folder and removes the folders left empty, and the mod moves up to Your mods. A file the game doesn't have,
  or one that could go in more than one place (a font each language has, with no folder to tell which), stops this,
  and the card names the file.
- **Needs attention.** A folder the game can't use as it is goes in its own part of the list, below the mods that
  can be turned on. Its card says what is wrong and what to do, and on the right:

  | The card says | Why | What to do |
  |---|---|---|
  | Fix it | Loose files, and the game's own files show where each goes | Press it |
  | No game files | None of the game's folders, and its files can't be put in place (the card names the file) | Lay its files out as its notes say, as below `PS3_GAME\USRDIR` |
  | Two versions | The game's folders in two places: usually two versions or options in one download | Keep one; move the other into a folder of its own, where it becomes a mod of its own |
  | Changes the program | An `EBOOT.BIN`. This version runs its own program, built from the game's, so a changed one has nothing to change | Nothing; a version of the mod without it may work |
  | Locked files | Files locked as on the PS3 (an `NPD` `.sdat` or `.edat`). Setup unlocks the game's own files, not a mod's | A copy of the mod with the files unlocked, if there is one |
  | Empty | Its folders hold no files | |
  | Too many files | More than 200,000 | Check it is one mod |
  | Rename its folder | A `\|` in its name (Linux only; Windows names can't have one) | Rename it |

## 2. On, off and the order

- **New mods are off,** at the bottom of the list. Turning one on, or moving it, takes effect the next time the game
  starts. A card that differs from this session says "after a restart" until then, and the summary says to restart
  (also when only the order changed).
- **The order file:** `mods\load_order.txt` holds the order and which mods are on (section 2a). The Mods window writes
  it at every change; the game reads it at start.
- **The order:** the file's, then the folders it doesn't name, A to Z. When two mods that are on have the same file,
  the higher one gives it.
- **Same files:** when mods that are on share files, the card of each mod that loses some says which mod is higher in
  the list and wins for those (`mods::overlaps`).
- **Refresh the list** reads the folder again after you add, remove or rename a mod.
- **From before the order file:** `DES_MODS` in `settings.ini` named the mods that were on. At the first start with a
  mods folder and no order file, those mods go into a new order file (on, the folders A to Z as before), and
  `DES_MODS` is emptied. A `DES_MODS` in the environment still sets the mods for a test run (section 6).

## 2a. For mod managers: the mods folder is the contract

A mod manager (issue #404) installs, updates, removes, orders and turns mods on or off by changing the mods folder
alone. The game never writes anywhere else for mods, and never changes a mod's files, except when the player presses
**Fix it** on a mod with loose files.

- **Where:** `<data folder>\mods`, where `<data folder>` holds `settings.ini`: `%APPDATA%\DemonsSoulsRecomp\DemonsSouls`
  on Windows, `$XDG_DATA_HOME/DemonsSoulsRecomp/DemonsSouls` (else `~/.local/share/...`) on Linux, and `data` beside
  the program in a portable install. `DES_MODS_DIR` points the game elsewhere.
- **A mod:** one folder, its name the mod's name. Inside, the game's folders as below the disc's `PS3_GAME\USRDIR`
  (section 1 lists what else is found). Installing is unpacking into a new folder; updating is replacing that
  folder; rolling back is putting the old folder back.
- **The order file,** `mods\load_order.txt`, UTF-8 (a byte order mark is allowed), one line per mod, the first one
  first:

  ```
  # The mods, the first one first. When two mods that are on change the same file, the higher one wins.
  on HD Textures
  off Old Fonts
  on Rebalanced Weapons
  ```

  - Each line is `on` or `off` (any case), spaces or a tab, then the folder's name exactly as on disk. Spaces inside
    the name are kept; spaces at the end of a line are not.
  - Lines starting with `#`, blank lines and lines that start with any other word are passed over.
  - A name with no folder is passed over (and dropped the next time the game writes the file). A name listed twice
    counts once, at its first line.
  - Folders the file doesn't name come after, A to Z, off.
  - The game writes the file whole, through `load_order.txt.new` renamed over it, with three `#` lines at the top.
    It keeps only `on` and `off` lines, so a manager's own comments or extra lines don't survive the Mods window.
- **When:** the game reads the file at start, so change it while the game is closed. The Mods window writes it at
  every change.
- **Online play** is decided from the files, not from anything a mod says about itself (section 3). A mod that changes
  how the game plays keeps the session offline, whatever the manager or the mod's own notes say.
- **Not read:** a manager's manifest or any other file in the mods folder (other than mod folders and the order
  file).

## 3. Mods and online play

A mod that changes how the game plays keeps the whole session offline, so that players with and without such mods
never meet. Mods that only change how the game looks or sounds keep online play.

**Look only** (the rest is gameplay):

| Files | Why |
|---|---|
| Textures anywhere (`.tpf`, `.tpf.dcx`, `.dds`, `.png`) | pictures only |
| `facegen`, `font`, `item`, `menu`, `movie`, `msg`, `mtd`, `parts`, `remo`, `sfx`, `shader`, `sound` | faces, fonts, item and equipment models, menus, movies, text, materials, effects, cutscenes, sound |
| `param/drawparam/` | lighting, fog and depth of field |
| `map/` models (`.flver`) | how the maps look |

**Gameplay:** the game's parameters (`param/gameparam`...), the maps' layouts (`map/mapstudio`), collision and
navigation, characters (`chr`, except textures), objects (`obj`), scripts (`script`), and any file the list does not
know, including loose files beside the game's folders.

**What happens when a gameplay mod is on:**
- Online stays off for the session, whatever Settings > Network says, and the runtime's network is locked off (also
  for network settings given in the environment).
- If Online is on in the settings, a notice says so at start: "Playing offline: gameplay mods are on", with the mods'
  names and how to play online again.
- Settings > Network > Online says why in its help, and This session > Network reads "Off: gameplay mods are on".
- The Mods window's summary says "Playing offline", with the mods' names.

**What this protects, and what it doesn't.** The check keeps honest players apart. It does not stop someone who edits
the game's own files in place, or changes the program. That is the server's job: reports, bans and server-side checks.

**Planned:**
- A check at start of the gameplay folders against the disc's known checksums, to catch files changed in place.
- A separate online world for modded players on the project's server (rule "c"), so modded players can still play
  together.

## 4. Which mods work

- **Mods for the PS3 game or for RPCS3 that replace the game's files** work as they are: textures, fonts, text, models,
  parameters.
- **Mods that change the game's program** don't: an `EBOOT.BIN`, or RPCS3 patches (`patch.yml`).
- **Script mods** (`.luabnd.dcx.sdat` and the talk scripts): a plain script file works. A published mod's
  replacement for the tutorial's script loaded and the level ran as usual. A script locked again for the PS3
  can't be read (section 1).
- **Very large texture mods** can run out of the game's memory and crash, as on a PS3 and in RPCS3. Raising the
  game's memory limits is the next step (docs/59, items M1-M4).

## 5. How it works

- **At start** (`port/ui/src/mods.cpp`, called from `settings.cpp`'s `load_and_apply_env` before the network rows):
  the mods that are on and usable become `PS3_VFS_MODS`, their game-file folders separated by `|`, the first one first.
  The log says which mods are on, which of their files make them gameplay mods, and which were left out and why.
- **In the runtime** (ps3recomp `runtime/syscalls/sys_fs.c`, `ps3_vfs_ps3game_fallback`): every path the game opens
  below the game's folder (`PS3_VFS_ROOT`) or its update (`PS3_VFS_UPDATE`) is looked up in each mod folder in order.
  The first mod that has the file gives it; otherwise the update, then the disc. Paths outside the game's folder (saves,
  the system cache) are never redirected. The log names each file a mod gives, up to 100.
- **The game's cache** (`/dev_hdd1`) is emptied at every start, so a mod turned on or off applies at the next start.
- **Not merged:** folder listings. The game opens its files by name and was not seen listing folders, so a mod can
  replace files but not add files the game would have to find by listing.
- **Not through the mods:** what the port itself reads from the game's files outside the game: the cursor's sword
  model, the window's icon, and the button icons' pages. The button icons find the game's button page by its
  contents, so a mod that redraws that page keeps its own buttons, and the Xbox, Nintendo and keyboard icons don't
  replace them.
- **Problem reports** have a "Mods:" line with the mods in use, each with its kind and file count.

## 6. For tests

| Variable | What it does |
|---|---|
| `DES_MODS=<name>\|<name>` | The mods that are on, in place of the order file's on and off (the order stays the file's). The Mods cards can't change them then, and the summary says so. |
| `DES_MODS_DIR=<folder>` | Another mods folder (a test's), with its own order file. |
| `PS3_VFS_MODS=<folder>\|<folder>` | The mods' game-file folders, given directly, in that order. Neither `DES_MODS` nor the order file is read, and the Mods window says so. |
| `PS3_FSLOG=1` | Logs every file the game opens, to see what a mod would need to replace. |
| `DES_UI_OPEN=mods` | Opens the Mods window at start (for screenshots, with `DES_UI_KEYS` and `DES_UI_SHOT`). |

`port/ui/tests/mods_check.cpp` (CI, Windows and Linux) checks the look and gameplay rules, where a mod's files are
found, the unusable cases and their reasons (locked files among them), every reason's card text and advice, loose
files planned and put in place (against a made-up game folder), the order file (read with a byte order mark, CRLF,
comments, unknown lines and missing folders; written over itself; `DES_MODS` taken over once), shared files and which
mod covers which, and `PS3_VFS_MODS` in the order file's order. ps3recomp's `libs/filesystem/tests/test_vfs_path.c` checks the path parts the runtime uses.

## 7. Known limits

- **Linux:** names are case-sensitive, so a mod's folders and files must be named as the game names them.
- **Memory** for very large texture mods (section 4).
- **Folder listings** are not merged (section 5).
