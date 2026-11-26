// Written for Demon's Souls. Where setup builds the game (docs/88 section 3, "The build folder"): the usual folder, or
// one the player chose on setup's screens. The choice is a small file in the usual folder, setup-folder.txt, that
// names the chosen one, so that every later start (the game's own too) finds it. And whether setup's own files are
// beside the program. Only std::filesystem, so the same on every platform: setup_engine.cpp decides with these, and
// tests/setup_folder_check.cpp checks them on their own.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ui::setup {

// The choice's file, in the usual folder. des_setup reads the same file (pipeline.py CHOICE).
inline constexpr const char* kChoiceFile = "setup-folder.txt";
// des_setup's NAME: the folders' name before the one data folder (0.2.0 and earlier): the shared build folder
// (%LOCALAPPDATA%\DemonsSoulsRecomp) and the settings folder (%APPDATA%\DemonsSoulsRecomp\DemonsSouls). Still used
// where they hold something (below), and still known as setup's own when a folder by that name is picked.
inline constexpr const char* kFolderName = "DemonsSoulsRecomp";
// des_setup's ONE_NAME: the one data folder's name (docs/164), and the folder of its own that setup makes in a folder
// picked that holds other things. A fixed technical name, apart from the name people see (docs/130): a rename of the
// project never changes it, or players' files would be left behind.
inline constexpr const char* kOneFolderName = "Soulbrandt";  // (the fixed folder name)

// The folder that <usual>/setup-folder.txt names: its first line that is not empty or a comment ('#'), a full path or
// one relative to the usual folder. Empty when there is no such file or line.
std::filesystem::path chosen_folder(const std::filesystem::path& usual);

// Setup's folder, and the usual one that holds the choice.
struct Folders {
    std::filesystem::path dir;    // setup's data folder
    std::filesystem::path usual;  // where setup builds when nothing is chosen, and keeps the choice; empty when the
                                  // folder is given (DES_SETUP_DATA): nothing to choose then
    bool chosen = false;          // dir is the player's choice
};
// In this order: `given` (DES_SETUP_DATA) as it is; the folder chosen; then the usual one. The usual folder is
// `shared` (setup_shared_folder: the one data folder's setup-data, or an earlier version's folder), or in a portable
// install (`portableProgram`: the program's folder, when portable.txt is beside it; else empty) data/setup-data
// beside the program. A portable install that was built in the shared folder before, and chose nothing, keeps using
// it (issue #364).
Folders find_folders(const std::filesystem::path& given, const std::filesystem::path& shared,
                     const std::filesystem::path& portableProgram);

// The same folder: the same one on disk when both exist, else the same path (on Windows, in any case).
bool same_folder(const std::filesystem::path& a, const std::filesystem::path& b);
// Setup's work is in the folder: a build (setup/ready.txt) or part of one (setup/journal.json).
bool holds_setup(const std::filesystem::path& folder);
// The folder a pick in the folder dialog gives: the folder itself when it is new or empty, or setup's already (its
// work in it, a folder moved by hand; the usual folder, which holds the choice's file; or one named kOneFolderName or
// kFolderName); else setup's own folder in it (kOneFolderName), so that setup's folders (inputs, work, game, setup,
// ...) do not go among the player's files.
std::filesystem::path folder_for_pick(const std::filesystem::path& picked);
// Empty when setup can build in `folder`, else why not, for the player: it is the program's folder, or in one of the
// program's own folders, which an update replaces (setup, and the names in setup/release-files.txt); or setup cannot
// make it or write in it; or its drive cannot hold programs (cannot_hold_programs). It makes the folder. program: the
// program's folder (empty: not known).
std::string cannot_use(const std::filesystem::path& folder, const std::filesystem::path& program);
// Empty when the drive `folder` is on can hold what setup unpacks and runs there, else why not, for the player. On
// Linux setup's Python has 1,048 symbolic links (bin/python3 -> python3.13, ...), which a drive formatted FAT32 or
// exFAT cannot hold (tar -xf ends with exit 2, issue #673), and the build runs the programs it unpacks, which a drive
// mounted with noexec does not. Asked of the drive itself: a small program file and a link to it, made in the folder
// (or the nearest one above it there is) and removed at once. Empty when it cannot be asked (cannot_use's write check
// says that), and always on Windows, whose setup needs neither.
std::string cannot_hold_programs(const std::filesystem::path& folder);
// Keeps the choice of `folder` in <usual>/setup-folder.txt, or removes that file when `folder` is the usual one. The
// path is written relative to the usual folder when both are in the program's folder (a portable install, which may
// be moved or renamed as one folder), else in full. False, with why (for the player), when the file cannot be written.
bool keep_choice(const std::filesystem::path& usual, const std::filesystem::path& folder,
                 const std::filesystem::path& program, std::string& why);

// The engine's entry in a player's install, beside the program (tools/release/package.py: setup/ mirrors the source
// tree).
inline constexpr const char* kEngineEntry = "setup/tools/setup/run_setup.py";
// How the engine's files are missing beside the program, when they are (issue #431).
enum class EngineGone {
    No,             // the entry is there
    FromArchive,    // no setup folder, and the program is in the system's temporary folder: it was started from inside
                    // the download's archive, and the archive tool took out the program alone
    NoSetupFolder,  // no setup folder beside the program: the program was copied or moved on its own
    Incomplete,     // a setup folder without the entry: part of the download extracted, or files removed (an antivirus)
};
// program: the program's folder. temp: the system's temporary folder (empty: not known).
EngineGone engine_gone(const std::filesystem::path& program, const std::filesystem::path& temp);

// ---- The one data folder (docs/164) ----
//
// A new install keeps all its files in one folder, kOneFolderName in %LOCALAPPDATA% (on Linux in $XDG_DATA_HOME, else
// ~/.local/share): settings, the online key, mods, logs and reports in it; setup's build in its setup-data; the saves
// in its gamedata; the shader cache in its shader_cache. A portable install (portable.txt beside the program) keeps
// them in the program's folder instead. What an earlier version left is used where it is: each part goes to its old
// place when that already holds something, and nothing is ever moved or deleted. Each answer is what is on disk at
// the start, the same in every process of the program; given folders (DES_DATA_DIR, DES_SETUP_DATA, PS3_SHADER_CACHE,
// a save root) come before all of it and are the callers' to check.

// The system's folders under which the parts go: %APPDATA% and %LOCALAPPDATA% on Windows; on Linux both are
// $XDG_DATA_HOME, else ~/.local/share.
struct UserFolders {
    std::filesystem::path roaming, local;
};
// <local>/kOneFolderName.
std::filesystem::path one_folder(const UserFolders& u);
// The earlier versions' settings folder, <roaming>/kFolderName/DemonsSouls (SDL_GetPrefPath's), and their shared build
// folder, <local>/kFolderName.
std::filesystem::path old_settings_folder(const UserFolders& u);
std::filesystem::path old_shared_folder(const UserFolders& u);

// Which place a part uses, and why: its words for the log and the report.
enum class FolderSource { Portable, Old, One };
const char* source_words(FolderSource p);
struct Pick {
    std::filesystem::path dir;
    FolderSource source = FolderSource::One;
};
// The settings folder (settings.ini, the online key, mods, logs, reports): <program>/data in a portable install
// (portableProgram: the program's folder, else empty); the earlier versions' folder when it holds anything; else the
// one folder.
Pick settings_folder(const UserFolders& u, const std::filesystem::path& portableProgram);
// find_folders' `shared`: the earlier versions' shared folder when setup's work or a choice of folder is in it (a
// build, or part of one, or setup-folder.txt), else <one>/setup-data.
std::filesystem::path setup_shared_folder(const UserFolders& u);
// The folder that holds the saves' gamedata (the PS3 file system's /dev_hdd0: saves, co-op's own saves, trophies),
// where the game works (des_ui.cpp): the program's folder in a portable install, or when its gamedata holds saves
// (holds_saves); else the one folder.
Pick saves_folder(const UserFolders& u, const std::filesystem::path& program, bool portable);
// The shader cache (PS3_SHADER_CACHE): <program>/shader_cache in a portable install, or when the program's folder has
// one; else <one>/shader_cache.
Pick shader_cache_folder(const UserFolders& u, const std::filesystem::path& program, bool portable);
// `folder`/gamedata holds saves: a folder or file in gamedata/dev_hdd0/home/<user>/savedata (or savedata-coop, ...),
// or in gamedata/trophies.
bool holds_saves(const std::filesystem::path& folder);
// `folder` exists and has something in it.
bool holds_anything(const std::filesystem::path& folder);

// ---- The game folders the game ran from (docs/164 section 5) ----

// In the player's data folder (report::data_dir): the game folders the game ran from, newest first, one a line as
// "<when>\t<folder>" (UTF-8; when: ISO 8601, UTC). A game folder's saves are in its gamedata folder. Written at each
// player's start of the game (des_ui.cpp); a later version reads it to offer copying old saves into a new install, so
// that a player who unpacks it into another folder does not start without them. Nothing else reads it, and deleting it
// loses only that offer.
inline constexpr const char* kGameFoldersFile = "game-folders.txt";
inline constexpr int kGameFoldersKept = 20;
// `old` (the file's text, or empty) with `folder` first, stamped `when`, then the other folders of `old` in their order:
// each folder once (same_folder), at most kGameFoldersKept. Lines that are not "<when>\t<absolute folder>" go.
std::string game_folders_text(const std::string& old, const std::filesystem::path& folder, const std::string& when);
// The folders in the file's text, newest first.
std::vector<std::filesystem::path> game_folders(const std::string& text);
// Notes `folder` in <dataDir>/game-folders.txt: the new text is written beside it first, then put in its place, so the
// file is never half written. Nothing else is touched. False, with why, when it cannot be read or written (a file there
// that cannot be read is left as it is).
bool remember_game_folder(const std::filesystem::path& dataDir, const std::filesystem::path& folder,
                          const std::string& when, std::string& why);

}  // namespace ui::setup
