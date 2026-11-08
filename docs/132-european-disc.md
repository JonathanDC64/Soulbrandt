# 132. The European disc (BLES00932)

The port runs the European disc, BLES00932 version 1.00, as well as the US disc, BLUS30443. There is one program
with every feature on both discs. The European disc adds four more languages: French, German, Spanish and Italian.
Issue #323 asked for it.

## What differs between the two discs

- **EBOOT.BIN.** It is the same program, but most functions and data sit at slightly different addresses.
- **PARAM.SFO.**
- **The extra languages:** 211 more files for the four other languages.

Every other file (the 26 `.sdat` archives, the shaders, the SPU images) is the same, byte for byte.

## How the port tells them apart

- **Which disc.** The split build loads the game DLL that setup lifted from the player's EBOOT. At load,
  `des_game_host.cpp` looks for the 63 lifted functions the port calls by name: at their US addresses and at their
  European ones. Whichever set is complete is the disc. It is set before any guest code or port hook runs
  (`g_des_region`, `des_region.h`).
- **Guest addresses.** The port's guest addresses are written as the US disc's. `DES_AT(0x...)` gives the running
  disc's, from `port/regions/BLES00932.inc`.
  - An address missing from the table is a compile error at its `DES_AT`.
  - Registered functions use `DES_REGISTER_FUNCTION(us, fn)`.
- **What stays a US address:**
  - **Hook keys.** These are the `case 0x...:` and `addr == 0x...` tests in the instruction hooks. Setup lifts the
    European EBOOT with each hook at its European address, but calls it with its US address (`ppu_lifter.py`'s
    `ADDR=SYMBOL@KEY`).
  - **Lifted functions the port calls by name** (`func_XXXXXXXX`). The split build resolves them at the European
    address when the DLL loads.
- **Single-exe builds** (not `DES_SPLIT`) link the US names directly, so they are US only.

## Setup

- `game_table.json` lists both discs. The European one is described as the files that differ from the US disc's.
- The game check, the RPCS3 route and the lift read the disc from the game folder's `PARAM.SFO`.
- **For the European disc, the lift gets** (`scripts/des_region.py`):
  - the hook list at its addresses (`work/region/insn_hooks.txt`);
  - the overrides at its addresses;
  - `apply_boot_patches.py --title BLES00932`.
- **The SPU images** are found by their contents (`build_spu.py`), so their place in the EBOOT does not matter. The
  two job blocks have a `vaddr_eu` too.
- **The pipeline index** is the US disc's: the shaders are the same.

## What the player sees

- **Language.** It is set by Settings > Game > Language (`DES_LANGUAGE`), shown only for the European disc. The
  default is the PC's language when the disc has it, else English. It needs a restart.
  - The port sets `PS3_SYS_LANG` from it at start: English (UK), French, German, Spanish, Italian, Japanese (`ja`),
    Chinese (Traditional, `zh-tw`) or Korean (`ko`).
  - The game and the trophy names follow it. Japanese, Chinese and Korean change the text only: the European trophy
    set has no names in them, so those stay English.
  - `PS3_SYS_LANG` given at start wins. Japanese, Chinese or Korean given that way show English, as on a PS3.
  - The US disc is English only and stays English (US).
- **Saves.** The disc names its save folder: `BLES00932DEMONSS005`, and `BLUS30443DEMONSS005` for the US disc. A US
  save copied under the European name loads.
- **Trophies.** The set is NPWR01249 (38 trophies, the same list). Its unlocks are kept in their own file.
- **Boot logos.** The European disc has six logos and the US disc five. Boot logos "Last logo only" knows which.
- **Online.** Both discs play in one world. The server's bootstrap sends every language to the US game server, and
  psnr keeps the European disc's rooms (NPWR01249) under the US disc's ID (DemonsSoulsServer PR #20).

## Japanese, Chinese and Korean

The European disc carries the text and fonts for them: Japanese in the root of `msg/` and `font/`, Chinese and Korean
in `msg/tchinese`, `msg/korean`, `font/tchinese` and `font/korean`. Its program never picks them: on a PS3 set to one
of them it shows English (UK).

- **The language index.** The program keeps its language as one number:
  - 0 Japanese, 1 Asian English, 2 Korean, 3 Chinese, 4 US English;
  - 5-9 English (UK), German, French, Spanish, Italian.

  It fills the number at first use, from the PS3's language, and only ever with 5-9. The text, the fonts and the
  menus' language folder all follow it.
- **What the port does.** For Japanese, Chinese or Korean it sets the number (0, 3 or 2) before the game runs
  (`use_asian_text` in `des_region.cpp`). These are writes to the game's memory: no hook, no new lift.
- **Two choices that stay Western.** Two other places switch on the number. Their cases for 0-3 are pointed at a
  Western one, so only the text changes:
  - **The balance file.** 0-3 load `GameParam.parambnd`, the Japanese and Asian discs' balance. They load
    `GameParamNA.parambnd`, as English does. Weapons, enemies and the talk table stay the European disc's, and saves
    and online stay compatible.
  - **The server's tags.** 1-3 read the Asian tags, which the original servers sent to the Japanese world. They read
    UK English's. (Our server sends every language to one world anyway.)
- **The menus' pictures** (`menu.tpf`) are the same for every language, on the Asian disc too. Only French, German,
  Spanish and Italian have their own stay-menu pictures, so these three use the English ones, as English (UK) does.
- **What stays English.**
  - Japanese: the Japanese disc has menu headings in Latin letters, and the 22 lines the Western build added include
    10 with no Japanese (the area names on the map screen, `Ability`, and a few more).
  - Chinese and Korean: 19 talk lines of one character are missing, so they show no subtitle. The Asian disc's own
    talk table names the same lines.
- **PC's language.** A Japanese or Korean PC gets its language. A Chinese PC gets Chinese only when it is set to
  Traditional Chinese (Taiwan, Hong Kong, Macau).
- Asian English (1) is not offered.

## Adding a guest address

1. Write it as the US address, through `DES_AT` (data, return addresses, vtables) or `DES_REGISTER_FUNCTION`. A hook
   key stays as it is.
2. Find its European address. This needs both decrypted EBOOTs:

   `python tools/region/region_table.py find <US EBOOT.elf> <EU EBOOT.elf> 0x00ABCDEF`

   It matches code by its instructions, and data by its TOC slots, the code that builds it, or the words around it.
   It prints a table line only when every way it found agrees.
3. Add the line to `port/regions/BLES00932.inc`, sorted by US address.

**CI** runs `region_table.py check` (in the Scripts job). It fails when:
- a guest address in the port is not in the table;
- one is used without `DES_AT`;
- the table is not sorted, or has an unused line.

`region_table.py verify <US> <EU>` checks every line against both EBOOTs. Today 210 of the 225 agree, and none
differ. The other 15 were checked by hand:
- vtables;
- a float among zeros;
- the logo update, which the European disc changed;
- the language's two jump tables (the European ones have ten cases, the US ones five or six).

## Tested

The European disc ran through setup's own lift and compile and the split host:
- **Default language** (English (UK)): the regression route passes, and its four shots match the US disc's.
- **`DES_LANGUAGE=fr`:** French text in the game and French trophy names.
- **`DES_LANGUAGE=ja`, `zh-tw` and `ko`:** each opens its own fonts and text files, and the balance file stays
  `gameparamna`. The HUD's item and place names and the auto-save notice are in that language. French and the default
  leave the language word to the game and the two tables as they were. Not run in these three: the Start menu in the
  game (the test script's Start press did not open it), a conversation, and online play.
- **`DES_SKIP_INTRO=0` and `2`:** all six logos, and the last one only.
- **`DES_FPS=60`:** 60 fps, game time equal to real time.
- **Save:** a US save copied under the European folder name loads.
