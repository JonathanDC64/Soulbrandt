#pragma once
// Mods (docs/143). A mod is a folder in the mods folder (<data folder>\mods) that holds files laid out as the game's
// USRDIR holds them (menu\, param\, ...), or a folder that holds such a folder (a USRDIR or PS3_GAME\USRDIR in it, or
// wrapper folders, as mod archives often unpack; a copy of the game's own files kept to undo the mod is passed over).
// The runtime reads an enabled mod's file in place of the game's: settings.cpp's load_and_apply_env calls apply(),
// which sets PS3_VFS_MODS (ps3recomp's runtime/syscalls/sys_fs.c). The Mods window (mods_window.cpp) shows them.
//
// The order and which mods are on: the mods folder's order file (load_order.txt, read_order). When two enabled mods
// have the same file, the one higher in it gives it. Folders it does not name come after, A to Z, off.
//
// Online play: a mod that changes how the game plays (gameplay_file) keeps the whole session offline; mods that only
// change how it looks or sounds do not. This file decides which; settings.cpp and des_ui.cpp act on it.
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ui::mods {

enum class Kind { Look, Gameplay };

// Why a mod can't be turned on. The Mods window shows problem_label on its row and problem_advice in its help, after
// Mod::problem (what is wrong, in this mod's words).
enum class Problem {
    None,
    Name,         // its folder's name has a '|'
    TwoCopies,    // the game's folders in two places: usually two versions of the mod in one download
    NoGameFiles,  // none of the game's folders, and its files can't be put in place
    NotInPlace,   // loose files that plan() found a place for: "Put its files in place" fixes it
    Program,      // an EBOOT.BIN
    TooManyFiles,
    Empty,
    Locked,       // files locked as on the PS3 (NPD .sdat/.edat)
};
const char* problem_label(Problem p);   // a row's value: "Locked files", "Two versions"...
const char* problem_advice(Problem p);  // what the player can do

// A loose file of a mod and where in its folder it goes (below the mod's folder, '/' separators).
struct Move {
    std::string from, to;
};

struct Mod {
    std::string name;              // its folder's name (UTF-8)
    std::filesystem::path folder;  // <mods folder>\<name>
    std::filesystem::path root;    // the folder in it that holds what USRDIR holds; empty when none was found
    Problem why = Problem::None;   // why it can't be turned on
    std::string problem;           // what is wrong, in a sentence or two ("" when it can be used)
    Kind kind = Kind::Look;
    std::size_t files = 0;              // files below root
    std::vector<std::string> areas;     // the game's folders it has files in ("menu", "param"), A to Z
    std::vector<std::string> gameplay;  // the first few of its files that make it a gameplay mod (below root, '/')
    std::vector<std::string> locked;    // the first few of its files that are locked as on the PS3 (NPD), unusable
    bool loose = false;                 // it has no game folders (problem says so); plan() may find where its files go
    std::vector<Move> moves;            // plan(): where each loose file goes, from the game's own files; place()
                                        // moves them
    bool enabled = false;               // "on" in the order file (or named in a test's DES_MODS)

    bool usable() const { return why == Problem::None; }
};

// DES_MODS's names, in its order ('|' between them).
std::vector<std::string> split_list(const std::string& list);

// The mods folder: DES_MODS_DIR, else <data folder>\mods; empty when nothing is kept (a test run's empty data folder).
std::filesystem::path folder(const std::filesystem::path& dataDir);

// The order file: <mods folder>\load_order.txt, UTF-8, one line per mod, the first one first:
//   on <folder name>
//   off <folder name>
// '#' starts a comment line; other lines, and names with no folder, are passed over. It belongs to the mods folder,
// so a mod manager installs, updates, orders and turns mods on or off by changing that folder alone (issue #404).
// The game reads it at start; the Mods window writes it.
inline constexpr const char* kOrderFile = "load_order.txt";
struct Entry {
    std::string name;
    bool on = false;
};
// The file's entries, in its order (each name once, its first line). Empty when there is no file; *found says so.
std::vector<Entry> read_order(const std::filesystem::path& modsFolder, bool* found = nullptr);
// Writes the file (through a temporary file, so a reader never sees half of it). False when it could not be written.
bool write_order(const std::filesystem::path& modsFolder, const std::vector<Entry>& order);
// The entries for these mods, in their order.
std::vector<Entry> entries(const std::vector<Mod>& mods);
// The mods that were on in settings.ini's DES_MODS (before the order file) go into a new order file, the folders A to
// Z as before. Nothing when the folder has an order file already, or none of its folders. True when it wrote the file.
bool adopt_setting(const std::filesystem::path& modsFolder, const std::string& enabledList);

// One mod folder as it is now. `name` is its folder's name.
Mod read(const std::filesystem::path& modFolder);

// The mods in the mods folder, in their order: the order file's, then the folders it does not name, A to Z. Each is
// enabled as the file says (a folder it does not name: off), or, with `enabledList` (a test's DES_MODS: names
// separated by '|'), when that names it.
std::vector<Mod> scan(const std::filesystem::path& modsFolder);
std::vector<Mod> scan(const std::filesystem::path& modsFolder, const std::string& enabledList);

// Whether a mod's file, given below its root with '/' separators ("param/gameparam/gameparamna.parambnd.dcx"), changes
// how the game plays. Textures, models, text, sound, movies, effects, menus and lighting do not; the game's parameters,
// maps' layouts and collision, characters, objects, scripts and anything the list does not know do.
bool gameplay_file(const std::string& rel);

// For a loose mod (files with no game folders, as some mods ship: "c0000.anibnd.dcx", "mapstudio\m01_00_00_00.msb"):
// where each file goes, from the game's own files (PS3_VFS_ROOT's USRDIR). Each file's name, and the folders it is in,
// must point to one of the game's files; otherwise moves stays empty and the problem names the file. With moves, the
// mod is Problem::NotInPlace.
void plan(Mod& mod);
// Moves a mod's loose files where the game reads them (Mod::moves), inside its own folder, and removes the folders
// that are left empty. "" when all moved, else what went wrong (the files moved so far stay moved).
std::string place(const Mod& mod);

// "file" lines for the files that two or more of these mods have, each with the mods that have it, the first
// (the one that gives the file) first. At most `max` lines.
std::vector<std::string> conflicts(const std::vector<Mod>& mods, std::size_t max = 20);

// The same, by mod: each mod some of whose files an earlier mod also has ("covered" by it, since the earlier one
// gives them), with the earlier mod and how many files. In the mods' order.
struct Overlap {
    std::string covered, by;
    std::size_t files = 0;
};
std::vector<Overlap> overlaps(const std::vector<Mod>& mods);

// At start, before the network knobs: the enabled mods become PS3_VFS_MODS, in their order, unless the environment
// set it (a test run: its folders are then read as mods are). The order file says which are on, or `enabledList` (a
// DES_MODS given in the environment) when there is one. Logs what is on.
void apply(const std::filesystem::path& dataDir, const std::optional<std::string>& enabledList = std::nullopt);

// This session's mods, as apply found them, in their order.
const std::vector<Mod>& in_use();
// A gameplay mod is in use: this session stays offline.
bool gameplay_in_use();
// PS3_VFS_MODS was set when the game started (a test run), so the order file was not read.
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
