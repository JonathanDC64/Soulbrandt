# 143. Mods

Players can put mods in a folder and turn each one on or off in Settings > Mods. The game then reads a mod's files in
place of its own. Nothing is copied into the game's files, so turning a mod off gives the game back as it was.

## 1. The mods folder

- **Where:** `mods` in the data folder, beside `settings.ini`. In a portable install that is `data\mods` beside the
  program. Settings > Mods > Open the mods folder creates it and opens it.
- **One folder per mod.** The folder's name is the mod's name in the list.
- **What goes in a mod's folder:** the game's folders it changes (`menu`, `param`, `parts`...), laid out as on the
  disc below `PS3_GAME\USRDIR`. A mod's folder can also hold:
  - a `USRDIR` or `PS3_GAME\USRDIR` (as many PS3 and RPCS3 mods ship);
  - one wrapper folder, as archives often unpack (`My Mod\My Mod v1.2\menu\...`).

  The game looks up to five folders deep for a `USRDIR`, else for the shallowest folder that holds one of the game's
  folders. Folders whose names start with `Original`, `Backup`, `Vanilla`, `Uninstall` or `Unmodded` are passed over:
  mods ship the game's own files in them to undo the mod. Two candidates at the same depth (two versions of the mod)
  is a problem, and the row names both.
- **Left out:** the mod's notes and pictures beside its game folders (`.txt`, `.md`, `.pdf`, `.jpg`...) and what file
  managers leave (`desktop.ini`, `Thumbs.db`, `.DS_Store`, `__MACOSX`).
- **Can't be used:**
  - a mod with an `EBOOT.BIN`: Soulbrandt runs its own program, so a mod that changes the game's program does nothing
    here;
  - a folder with no game files, or more than 200,000 files;
  - a name with `|` in it (Linux only; Windows names can't have one).

## 2. On, off and the order

- **New mods are off.** Turning one on takes effect the next time the game starts. A row whose value differs from this
  session's is marked as changed until then.
- **The setting:** `DES_MODS` in `settings.ini`, the names of the mods that are on, separated by `|`.
- **The order:** the folders' names, A to Z. When two mods that are on have the same file, the one that comes first
  gives it. To change which one wins, rename the folders (`1 Textures`, `2 Fonts`).
- **Same files:** when mods that are on share files, a "Same files" row lists them with the mods that have each one,
  the winner first.
- **Look again** reads the folder again after you add, remove or rename a mod.

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
- Settings > Mods > This session > Online play says the same.

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
- **Script mods** (`.luabnd` and friends): a script file that is plain (not encrypted) is read as it is. These are
  being tested with real mods.
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
- **Not through the mods:** what the port itself reads from the game's files outside the game (the cursor's sword
  model, the window's icon).
- **Problem reports** have a "Mods:" line with the mods in use, each with its kind and file count.

## 6. For tests

| Variable | What it does |
|---|---|
| `DES_MODS=<name>\|<name>` | The mods that are on, as the setting. Set in the environment, the Mods rows can't change it. |
| `DES_MODS_DIR=<folder>` | Another mods folder (a test's). |
| `PS3_VFS_MODS=<folder>\|<folder>` | The mods' game-file folders, given directly. `DES_MODS` is then not read, and the Mods tab says so. |
| `PS3_FSLOG=1` | Logs every file the game opens, to see what a mod would need to replace. |

`port/ui/tests/mods_check.cpp` (CI, Windows and Linux) checks the look and gameplay rules, where a mod's files are
found, the unusable cases, the order, shared files and `PS3_VFS_MODS`. ps3recomp's
`libs/filesystem/tests/test_vfs_path.c` checks the path parts the runtime uses.

## 7. Known limits

- **Linux:** names are case-sensitive, so a mod's folders and files must be named as the game names them.
- **Memory** for very large texture mods (section 4).
- **Folder listings** are not merged (section 5).
