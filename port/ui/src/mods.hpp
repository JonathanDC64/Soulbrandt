#pragma once
// Mods (docs/143). A mod is a folder in the mods folder (<data folder>\mods) that holds files laid out as the game's
// USRDIR holds them (menu\, param\, ...), or a folder that holds such a folder (a USRDIR or PS3_GAME\USRDIR in it, or
// wrapper folders, as mod archives often unpack; a copy of the game's own files kept to undo the mod is passed over). The runtime reads an enabled mod's file in place of the game's:
// settings.cpp's load_and_apply_env calls apply(), which sets PS3_VFS_MODS (ps3recomp's runtime/syscalls/sys_fs.c).
//
// The order: the mods' folder names, A to Z. When two enabled mods have the same file, the one whose name comes first
// gives it.
//
// Online play: a mod that changes how the game plays (gameplay_file) keeps the whole session offline; mods that only
// change how it looks or sounds do not. This file decides which; settings.cpp and des_ui.cpp act on it.
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace ui::mods {

enum class Kind { Look, Gameplay };

// A loose file of a mod and where in its folder it goes (below the mod's folder, '/' separators).
struct Move {
    std::string from, to;
};

struct Mod {
    std::string name;              // its folder's name (UTF-8)
    std::filesystem::path folder;  // <mods folder>\<name>
    std::filesystem::path root;    // the folder in it that holds what USRDIR holds; empty when none was found
    std::string problem;           // why it can't be used ("" when it can)
    Kind kind = Kind::Look;
    std::size_t files = 0;              // files below root
    std::vector<std::string> areas;     // the game's folders it has files in ("menu", "param"), A to Z
    std::vector<std::string> gameplay;  // the first few of its files that make it a gameplay mod (below root, '/')
    std::vector<std::string> locked;    // the first few of its files that are locked as on the PS3 (NPD), unusable
    bool loose = false;                 // it has no game folders (problem says so); plan() may find where its files go
    std::vector<Move> moves;            // plan(): where each loose file goes, from the game's own files; place()
                                        // moves them
    bool enabled = false;               // named in DES_MODS
};

// The mods folder: DES_MODS_DIR, else <data folder>\mods; empty when nothing is kept (a test run's empty data folder).
std::filesystem::path folder(const std::filesystem::path& dataDir);

// One mod folder as it is now. `name` is its name in DES_MODS.
Mod read(const std::filesystem::path& modFolder);

// The mods in the mods folder, in their order (A to Z by name), each marked enabled when `enabledList` (DES_MODS:
// names separated by '|') names it.
std::vector<Mod> scan(const std::filesystem::path& modsFolder, const std::string& enabledList);

// Whether a mod's file, given below its root with '/' separators ("param/gameparam/gameparamna.parambnd.dcx"), changes
// how the game plays. Textures, models, text, sound, movies, effects, menus and lighting do not; the game's parameters,
// maps' layouts and collision, characters, objects, scripts and anything the list does not know do.
bool gameplay_file(const std::string& rel);

// For a loose mod (files with no game folders, as some mods ship: "c0000.anibnd.dcx", "mapstudio\m01_00_00_00.msb"):
// where each file goes, from the game's own files (PS3_VFS_ROOT's USRDIR). Each file's name, and the folders it is in,
// must point to one of the game's files; otherwise moves stays empty and the problem names the file.
void plan(Mod& mod);
// Moves a mod's loose files where the game reads them (Mod::moves), inside its own folder, and removes the folders
// that are left empty. "" when all moved, else what went wrong (the files moved so far stay moved).
std::string place(const Mod& mod);

// "file" lines for the files that two or more of these mods have, each with the mods that have it, the first
// (the one that gives the file) first. At most `max` lines.
std::vector<std::string> conflicts(const std::vector<Mod>& mods, std::size_t max = 20);

// At start, before the network knobs: the enabled mods (DES_MODS) become PS3_VFS_MODS, unless the environment set it
// (a test run: its folders are then read as mods are). Logs what is on.
void apply(const std::filesystem::path& dataDir, const std::string& enabledList);

// This session's mods, as apply found them, in their order.
const std::vector<Mod>& in_use();
// A gameplay mod is in use: this session stays offline.
bool gameplay_in_use();
// PS3_VFS_MODS was set when the game started (a test run), so DES_MODS was not read.
bool from_environment();
// The gameplay mods in use, by name, separated by ", ".
std::string gameplay_names();
// One line for reports: "none", or "2: Texture Pack (look, 120 files), Rebalance (gameplay, 3 files)".
std::string summary();

// Text and names for the screens.
const char* kind_label(Kind kind);  // "Look only" / "Gameplay"
std::string join(const std::vector<std::string>& items, const char* sep);
std::string utf8(const std::filesystem::path& path);
std::filesystem::path from_utf8(const std::string& s);

}  // namespace ui::mods
