# 166. RecompLauncher (2026-11-20)

Issue #656 asks for a `recomplauncher.json`, so that RecompLauncher can set the game up and show its settings. This
doc is what the launcher asks for (its guide and schema, read 2026-11-20), what the file in this repository does, and
what is left open. Labels: **read** (the guide, the schema or our code), **measured** (a run or a test), **inferred**.

## Short answer

- **The launcher installs from our GitHub releases** and reads `recomplauncher.json` at the top of the download. It
  starts the program from it, and edits `settings.ini` before each start to show some settings on the game's page.
- **The file** (`recomplauncher.json` at the root, put in both downloads by `package.py`) starts the program with no
  arguments, shows 14 settings, names the save folder and turns the launcher's filters off.
- **It asks for no game copy, runs no setup step and downloads nothing.** Setup still does all of that with the
  player's own copy: the launcher's Play opens setup on a first start, as a double-click does.
- **A check** (`tools/release/recomplauncher.py check`, in CI) fails when the file and the menu's settings drift.
- **It waits for 0.2.1** (the maintainer's call, 2026-11-20): 0.2.1's single data folder (docs/164) moves
  `settings.ini` and the saves, and the file names both, so it ships with the new paths from the start. Section 6
  has the maintainer's decisions, section 7 the questions for the launcher's team.

## 1. What the launcher does (read)

| Part | What the guide says |
|---|---|
| Install | From the GitHub releases, nothing through the launcher's servers. One file per system, picked by the words in its name: the system and the processor (`-Windows-x64.zip`, `-Linux-x64.AppImage`). A zip or 7z on Windows, an AppImage or `.tar.gz` on Linux. One folder on top is unwrapped. Files with `debug`, `symbols`, `pdb` or `source` in the name, and `.sha256`, `.sig` and `.asc` files, are never taken. |
| Checks | GitHub's SHA-256 of each uploaded file, before it installs. |
| Updates | The latest full release. It unpacks over the game's folder, so what the game wrote stays. Pre-releases are betas the launcher's team can open. |
| Start | `start.program` (the first path that exists), with `start.args` and `start.env`. Players can add their own launch options after ours. Every start gets `RECOMPLAUNCHER=1`, `RECOMPLAUNCHER_VERSION`, `RECOMPLAUNCHER_MODE` (`bigpicture` or `desktop`) and `RECOMPLAUNCHER_PAD` (`xbox` or `playstation`). |
| Settings | Rows of choices. Each choice writes keys into our settings file (TOML, INI or JSON, edited line by line), or adds arguments. The row shows the first choice whose keys all match the file; `defaults` stand in for keys the file does not have yet. |
| Game copy | `game` asks for the player's disc image or folder and checks it; `prepare` runs our own setup step with no window; `dumps` compares the copy with known good ones. `inputs` asks for key files (other consoles) or downloads NVIDIA's DLSS file. |
| Saves | Copied after every session (the newest ten stay), and sent to the player's account if they sign in. Without `saves`, the launcher watches which files the game writes and takes those. |
| Looks | Filters on ReShade, downloaded from reshade.me. While the game runs, `dxgi.dll` is put beside the program (or a Vulkan layer is added) and taken away when it closes. |
| Crashes | The newest `.log` in `logs` (or in the game's folder) is shown and goes with a report the player can send. `exit_codes` show our message instead. |
| Mods | A Mods tab for Thunderstore and GameBanana mods, with three ways of putting them in place. |

So the launcher downloads, on the player's PC: our release; ReShade, if the player picks a look; and NVIDIA's DLSS
file, only if a file asks for it.

## 2. How it fits this port (read)

| Launcher part | This port | In the file |
|---|---|---|
| Releases | `DemonsSoulsRecomp-X.Y.Z-windows.zip` and `-linux.tar.xz`, one folder on top, beside `SHA256SUMS`, its signature, the source zip and the symbols zip. No processor in the names (docs/105 section 10 keeps them). | nothing (questions 7.1 to 7.4) |
| Start | `Soulbrandt.exe`, `Soulbrandt` on Linux. Any argument that is not `--username` or `--psnr` is taken as the game file to boot (`elf_given` in des_ui.cpp, `main_elf` in des_options.cpp). | `start.program`, no arguments, no variables |
| Settings | `settings.ini`, `KEY = value`, no sections, every key written by the menu (settings.cpp's `save`). A variable in the environment wins and locks the row (`load_and_apply_env`). | `config`, 14 `settings` |
| Game copy | Setup asks for a folder with `PS3_GAME`, has RPCS3 unlock the encrypted files (or takes files the player decrypted), and builds the game. It cannot read an ISO yet. | nothing |
| Saves | `<install>/gamedata/dev_hdd0/home/00000001/savedata`. The game works in its own folder (`work_in_exe_folder`). | `saves` |
| Online key | `online-key.bin` in the data folder: the install's private key, which owns the online name (docs/69). | left out of `saves` on purpose |
| Filters | The game has its own anti-aliasing, scaling filter and HDR. | `looks` off |
| Crashes | The game's own crash window and issue report. | nothing |
| Mods | The game's own mods folder and order file (docs/143), none of the launcher's kinds. | nothing |

### 2.1 Where settings.ini is

The data folder (`report::data_dir`): `<install>/data` with `portable.txt` beside the program, else
`%APPDATA%\DemonsSoulsRecomp\DemonsSouls` on Windows and `~/.local/share/DemonsSoulsRecomp/DemonsSouls` on Linux
(`$XDG_DATA_HOME` first). The launcher's words for those are `{install}`, `{config}` (AppData\Roaming, or `~/.config`)
and `{local}` (AppData\Local, or `~/.local/share`). Its file list takes the first entry whose `if` exists, so:

1. `{install}/data/settings.ini`, if `{install}/portable.txt` exists.
2. `{local}/DemonsSoulsRecomp/DemonsSouls/settings.ini`, if `{install}/Soulbrandt` exists: the Linux download's
   program (the Windows one is `Soulbrandt.exe`).
3. `{config}/DemonsSoulsRecomp/DemonsSouls/settings.ini`: Windows.

So on Linux a pick made in the launcher before the game's first start goes to the file the game reads, not to
`~/.config`. A program renamed by PRODUCT_NAME needs the `if` to follow it.

### 2.2 The program starts itself again

Play at the end of setup, Restart after a restart-only setting, co-op's Host and Join, an update and going back all
start a new process of the program and end the first one (`crash_report::start_again`). The launcher started the
first one, so it sees the game close then (inferred): its play time stops there, it copies the saves then, and Big
Picture mode may come back over the game. The game itself goes on. Question 7.5.

## 3. The file

`recomplauncher.json` at the root:

- `name`, and `start.program` `["Soulbrandt.exe", "Soulbrandt"]`. No `args` and no `env`.
- `config`: `settings.ini`, the three places of section 2.1. No `always`, `ensure` or `new`, so the launcher writes a
  key only when the player picks a choice (inferred).
- `saves`: the save folder only.
- `looks`: off, "The game has its own anti-aliasing, scaling filter and HDR in its Settings."
- `system`: Windows 10 with Direct3D 12; Linux with glibc 2.35 and Vulkan 1.3 (the README's needs). The launcher
  tells a player whose PC is older, and never stops the game.
- `settings`, in four groups:

| Group | Row | Key | Choices |
|---|---|---|---|
| Display | Display mode | `DES_FULLSCREEN` | Window, Borderless, Exclusive (Windows only; Linux reads it as Borderless) |
| | Aspect ratio | `DES_ASPECT` | 16:9, 16:10, three 21:9 sizes, 32:9, Match the display |
| | HDR | `RSX_HDR` | Off, On |
| Frame rate | Frame rate | `DES_FPS` | 30 (original), 30 real frame time, 60, 120, 144, 165, 240, Unlocked |
| | V-sync | `DES_VSYNC` | On, Even frames, G-Sync / FreeSync, Off |
| Picture | Render resolution | `RSX_SCALE` | 50% to 300%, as in the menu |
| | Anti-aliasing | `RSX_AA` | Off, FXAA, SMAA |
| | Scaling filter | `RSX_OUTFILTER` | Bilinear, Sharp |
| | Anisotropic filtering | `RSX_ANISO` | Original (4x), Off, 2x to 16x |
| | Motion blur, Depth of field | `DES_MOTION_BLUR`, `DES_DOF` | On, Off |
| Start | Start screen | `show_start_screen` | Show, Skip (F1 still opens the menu) |
| | Boot logos | `DES_SKIP_INTRO` | Show, Skip, Last logo only |
| | Opening movie | `DES_SKIP_INTRO_MOVIE` | Play, Skip |

Left out on purpose:

- **Renderer** (`RSX_BACKEND`): its default is D3D12 on Windows, Vulkan on Linux and Vulkan on older GeForce cards
  (settings.cpp's `default_of`). One `defaults` cannot say that.
- **Language**: its choices depend on the disc.
- **A custom frame rate**: a number the menu writes (Frame rate's Custom) matches no choice, so the row shows none of
  them until the player picks one.
- Everything online, the controls and the HUD: they need the game's own pages.

## 4. The check

`python tools/release/recomplauncher.py check` (CI's Scripts job; unit tests in `tools/release/tests`). It reads
settings.cpp's `kDefs` and settings_catalog.cpp's rows as text and fails when:

- a key is not in `kDefs`, is a developer build's, or is per session (never read from settings.ini);
- a default is not the one in `kDefs`, or a key has a different default on another system;
- a value is not one its row writes, or a value the row writes has no choice;
- two rows change one key, or one row's choices set different keys;
- the file has a field the check does not know. Adding `game`, `prepare`, `inputs`, `mods` or the like means adding
  its checks first, so nothing that handles game files or downloads comes in unnoticed;
- `start` has arguments or variables, or the name and program are not PRODUCT_NAME's.

It does not check the places (settings.ini, the saves) or the help texts. `check_product_name.py` leaves the file to
it.

The release dry run checks that the file is at the top of both downloads. It is in `release-files.txt` too, so our
own update replaces it with the program.

## 5. Keeping it in step

- A new setting in the menu needs nothing here. A change to a listed row's values or default fails the check until
  the file follows.
- **The data folder for 0.2.1** (docs/164) puts settings and saves of new installs in one new folder. `config.file`
  and `saves` must then list the new places first and the old ones after. The check does not see this.
- **The download's new name** for 0.2.1 (`Soulbrandt-X.Y.Z-windows.zip`, with the old name kept beside it) puts two
  Windows zips in one release (question 7.2).

## 6. Decisions

All settled on 2026-11-20.

1. **Updates: the launcher's, not ours, in a copy it installed.** The launcher updates the game itself: it installs
   the latest full release, and each release reaches players the next time it checks. It unpacks the new version over
   the folder (read: the guide's "Releases that install cleanly").
   - Two updaters on one install would fight: the launcher's idea of the installed version goes wrong after our
     update or our Go back. After the launcher's update, setup shows "This version needs a rebuild" and rebuilds, as
     after ours (setup_engine's `launch_state`: Outdated).
   - **How the game knows** (`updates::from_launcher`, `port/ui/src/updates.cpp`):
     - The launcher sets `RECOMPLAUNCHER=1` for each game it starts (read: the guide's table of variables). The
       program's own restarts (setup's Play, Restart, co-op) keep the variables it started with.
     - The first such start leaves `setup/recomplauncher.txt` in the install. The launcher's updates keep it, since
       they unpack over the folder. A later start from the exe itself (a shortcut, a double-click) finds it, so it
       leaves the updates to the launcher too.
     - The file is left only in a player's install (`setup/release-info.txt` beside the program). A developer's build
       that the launcher's "Add a build" starts gets none.
   - **What changes then:**
     - no check at the start;
     - Settings > About's Check now is greyed out;
     - Settings > About has no Go back: it would put another version in place, as our update would;
     - the Updates line reads "RecompLauncher brings each new version.";
     - its help says how to undo it: delete the file, and start the game without the launcher.
   - **Measured** (the branch's host in a copy shaped like a player's install, a missing update feed):
     - started with `RECOMPLAUNCHER=1`: the file is left and no check runs;
     - started again without it: no check in 40 s;
     - without the file: the check runs (and ends in "got no answer", the missing feed).
2. **Downloads from our releases: yes.** That is how the launcher installs: straight from GitHub, the same files a
   player downloads by hand, checked by GitHub's SHA-256.
3. **Game files and keys: none, by design.** The file asks for no copy, no key and no setup step. Setup asks for the
   player's own copy as before. The check fails on any new field until it is reviewed.
4. **Where the file lives: both places, as built.** At the root, like `.recomp.json`, and at the top of both
   downloads, where the launcher looks first.
5. **The downloads stay as they are.** The guide asks for the processor in the name (`-windows-x64.zip`) and names an
   AppImage or a `.tar.gz` for Linux. Ours are `-windows.zip` and `-linux.tar.xz`. They are the only file for each
   system, so they should be taken. Questions 7.1 and 7.3 confirm it. Nothing changes unless the answer is no.
6. **Security.**
   - **Accepted:** the launcher checks GitHub's SHA-256, not our release signature (docs/105 section 7). A release
     published from a stolen account would reach launcher players, where our update refuses it. Releases stay drafts
     until they are signed, so this is the same risk as a download by hand.
   - `saves` names the save folder, so the launcher's copies and its account uploads take only the saves. Without it
     the launcher would guess from what the game writes, and could take `online-key.bin`, the key that owns the
     player's online name. Recommended: keep `saves`.
   - No `logs` folder is named, so the launcher sends none of our logs with its reports (inferred: it then looks in
     the game's folder, and our logs are in the data folder). Recommended: leave it out; our own report says what it
     holds before it is sent.
   - Looks would load a DLL from the program's folder into the game. Recommended: off, as built.

## 7. Questions for the launcher's team

1. Is a Windows file named `-windows.zip`, with no processor in the name, taken as x64?
2. 0.2.1 may have two Windows zips with the same program (the new name and the old one). Which is taken?
3. Is a Linux `.tar.xz` taken?
4. Which release counts as the latest: GitHub's Latest label, or the newest release? Our compiler downloads
   (`llvm-*`) are full releases too, one with a Windows zip. A workflow (release-latest.yml) keeps Latest on the
   newest `vX.Y.Z`.
5. The program ends and starts itself again (section 2.2). Does the launcher follow the new process?
6. settings.ini has no sections. Is a plain key like `DES_FPS` read and written as one, and is an empty value (the
   original 30 fps) matched?

## 8. Later

- `RECOMPLAUNCHER_MODE=bigpicture` could open the game in fullscreen. The button icons already follow the last
  controller used.
- A launch option added on the launcher's page breaks the start today (it is taken as the game file). Unknown
  arguments could be ignored instead.
- `game` and `prepare`, once setup reads an ISO and can run without a window, printing `progress` lines. The player's
  copy would then be picked once, in the launcher.
- A player who moves to the launcher's install starts with no saves there: they stay in the old folder's `gamedata`.
  0.2.1's offer to copy them (docs/164) covers that.

## 9. Tests

- **Measured:** the check passes on the file; its 23 unit tests pass (made-up sources: a value missing, a value too
  many, a wrong default, developer and per-session keys, a default per system, start arguments, unknown fields). A
  package dry run with stand-in programs put `recomplauncher.json` at the top of the Windows zip and the Linux
  tarball and in `release-files.txt`, the same bytes as committed.
- **Not tested:** the launcher itself. It is not installed here, and installing it needs asking.
