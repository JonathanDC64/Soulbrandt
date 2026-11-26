// setup_folder.cpp on its own: where setup builds, the usual folder or the one the player chose on setup's screens
// (docs/88 section 3, "The build folder"). The folder is found in this order: DES_SETUP_DATA, the choice in
// <usual>/setup-folder.txt, a portable install's build in the shared folder from before, the usual folder. A choice is
// kept in full, or relative in a portable install, and goes when the usual folder is chosen again. A folder picked
// that holds other things gets a folder of setup's own in it, and the program's own folders are refused. And how
// setup's own files can be missing beside the program (issue #431). And game-folders.txt, the game folders the game
// ran from, and the offer and the copy of an earlier folder's saves (save_copy.cpp, docs/164 section 5). And whether a
// folder's drive can hold the build (issue #673): on Linux, CI mounts a FAT drive and a noexec one and names them in
// SETUP_CHECK_FAT_FOLDER and SETUP_CHECK_NOEXEC_FOLDER. And path_utf8.hpp: a name that isn't valid UTF-16 as text,
// never a throw (setup crashed at its start on one).
//
// Each case runs in made-up folders under the system's temporary folder, removed at the end. Prints one line a case
// and "setup_folder_check: N passed, M failed"; exit 1 on a failure. The same on every platform (CI runs it on Windows
// and Linux, host.yml). Built on demand: cmake --build <build> --target setup_folder_check.
#include "setup_folder.hpp"
#include "save_copy.hpp"

#include "path_utf8.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace ui::setup;

namespace {

int g_passed = 0, g_failed = 0;
fs::path g_root;

std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

fs::path from_utf8(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string read(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// A case: its own folder under the root, made empty first.
void run(const char* name, const std::function<std::string(const fs::path&)>& body) {
    std::error_code ec;
    const fs::path dir = g_root / name;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    std::string problem;
    try {
        problem = body(dir);
    } catch (const std::exception& e) {
        problem = std::string("threw: ") + e.what();
    }
    if (problem.empty()) {
        ++g_passed;
        std::printf("ok    %s\n", name);
    } else {
        ++g_failed;
        std::printf("FAIL  %s: %s\n", name, problem.c_str());
    }
}

// "" when a and b are the same folder, else what differs.
std::string same(const char* what, const fs::path& got, const fs::path& want) {
    if (same_folder(got, want)) return {};
    return std::string(what) + " is " + utf8(got) + ", not " + utf8(want);
}

std::string all(std::initializer_list<std::string> problems) {
    for (const std::string& p : problems)
        if (!p.empty()) return p;
    return {};
}

}  // namespace

int main() {
    std::error_code ec;
    g_root = fs::temp_directory_path(ec) / ("setup_folder_check-" + std::to_string(getpid()));
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root, ec);

    // ---- The order ----
    run("given_wins", [](const fs::path& t) {
        write(t / "shared" / kChoiceFile, utf8(t / "chosen") + "\n");
        const Folders f = find_folders(t / "given", t / "shared", {});
        return all({same("dir", f.dir, t / "given"), f.usual.empty() ? "" : "a usual folder beside a given one",
                    f.chosen ? "given counted as chosen" : ""});
    });
    run("usual_without_choice", [](const fs::path& t) {
        const Folders f = find_folders({}, t / "shared", {});
        return all({same("dir", f.dir, t / "shared"), same("usual", f.usual, t / "shared"),
                    f.chosen ? "chosen with no choice" : ""});
    });
    run("full_choice", [](const fs::path& t) {
        write(t / "shared" / kChoiceFile, "# a note\n" + utf8(t / "D" / "build") + "\n");
        const Folders f = find_folders({}, t / "shared", {});
        return all({same("dir", f.dir, t / "D" / "build"), same("usual", f.usual, t / "shared"),
                    f.chosen ? "" : "not counted as chosen"});
    });
    run("relative_choice", [](const fs::path& t) {
        write(t / "shared" / kChoiceFile, "../build\n");
        return same("dir", find_folders({}, t / "shared", {}).dir, t / "build");
    });
    run("bom_crlf_spaces_comments", [](const fs::path& t) {
        write(t / "shared" / kChoiceFile, "\xEF\xBB\xBF# saved by Notepad\r\n\r\n   " + utf8(t / "x") + "  \r\n");
        return same("dir", chosen_folder(t / "shared"), t / "x");
    });
    run("only_comments", [](const fs::path& t) {
        write(t / "shared" / kChoiceFile, "# nothing\n\n");
        const Folders f = find_folders({}, t / "shared", {});
        return all({same("dir", f.dir, t / "shared"), f.chosen ? "chosen from comments" : ""});
    });
    run("the_usual_named", [](const fs::path& t) {
        write(t / "shared" / kChoiceFile, utf8(t / "shared") + "\n");
        const Folders f = find_folders({}, t / "shared", {});
        return all({same("dir", f.dir, t / "shared"), f.chosen ? "the usual folder counted as chosen" : ""});
    });
    run("no_shared_folder", [](const fs::path&) {
        const Folders f = find_folders({}, {}, {});
        return f.dir.empty() && f.usual.empty() ? std::string() : "a folder from nothing: " + utf8(f.dir);
    });

    // ---- A portable install ----
    run("portable_usual", [](const fs::path& t) {
        const Folders f = find_folders({}, t / "shared", t / "prog");
        return all({same("dir", f.dir, t / "prog" / "data" / "setup-data"),
                    same("usual", f.usual, t / "prog" / "data" / "setup-data")});
    });
    run("portable_choice", [](const fs::path& t) {
        write(t / "prog" / "data" / "setup-data" / kChoiceFile, "../../build\n");
        write(t / "shared" / kChoiceFile, utf8(t / "elsewhere") + "\n");  // (the shared folder's is not this one's)
        const Folders f = find_folders({}, t / "shared", t / "prog");
        return all({same("dir", f.dir, t / "prog" / "build"), f.chosen ? "" : "not counted as chosen"});
    });
    run("portable_built_in_shared_before", [](const fs::path& t) {
        write(t / "shared" / "setup" / "ready.txt", "format=1\n");
        const Folders f = find_folders({}, t / "shared", t / "prog");
        return all({same("dir", f.dir, t / "shared"), same("usual", f.usual, t / "prog" / "data" / "setup-data"),
                    f.chosen ? "counted as chosen" : ""});
    });
    run("portable_choice_over_shared_build", [](const fs::path& t) {
        write(t / "shared" / "setup" / "ready.txt", "format=1\n");
        write(t / "prog" / "data" / "setup-data" / kChoiceFile, utf8(t / "D") + "\n");
        return same("dir", find_folders({}, t / "shared", t / "prog").dir, t / "D");
    });

    // ---- Keeping the choice ----
    run("keep_in_full", [](const fs::path& t) {
        std::string why;
        if (!keep_choice(t / "shared", t / "D" / "build", t / "prog", why)) return "not kept: " + why;
        const std::string text = read(t / "shared" / kChoiceFile);
        if (text.find(utf8(t / "D" / "build")) == std::string::npos) return "not in full: " + text;
        return all({same("dir", find_folders({}, t / "shared", {}).dir, t / "D" / "build"),
                    fs::exists(t / "shared" / (std::string(kChoiceFile) + ".part")) ? "its .part file is left" : ""});
    });
    run("keep_relative_in_a_portable_install", [](const fs::path& t) {
        std::string why;
        const auto usual = t / "prog" / "data" / "setup-data";
        if (!keep_choice(usual, t / "prog" / "build", t / "prog", why)) return "not kept: " + why;
        const std::string text = read(usual / kChoiceFile);
        if (text.find("\n../../build\n") == std::string::npos) return "not relative: " + text;
        fs::create_directories(t / "prog" / "build");
        fs::rename(t / "prog", t / "prog moved");  // (the whole install renamed: the choice goes along)
        return same("dir", find_folders({}, t / "shared", t / "prog moved").dir, t / "prog moved" / "build");
    });
    run("keep_outside_a_portable_install_in_full", [](const fs::path& t) {
        std::string why;
        const auto usual = t / "prog" / "data" / "setup-data";
        if (!keep_choice(usual, t / "D", t / "prog", why)) return "not kept: " + why;
        return read(usual / kChoiceFile).find(utf8(t / "D")) == std::string::npos ? std::string("not in full")
                                                                                     : std::string();
    });
    run("the_usual_again_removes_the_file", [](const fs::path& t) {
        std::string why;
        if (!keep_choice(t / "shared", t / "D", {}, why)) return "not kept: " + why;
        if (!keep_choice(t / "shared", t / "shared", {}, why)) return "the usual folder not kept: " + why;
        return all({fs::exists(t / "shared" / kChoiceFile) ? "setup-folder.txt is still there" : "",
                    same("dir", find_folders({}, t / "shared", {}).dir, t / "shared")});
    });
    run("keep_a_name_in_other_letters", [](const fs::path& t) {
        std::string why;
        const auto folder = t / from_utf8("Jeux \xC3\xA9t\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC");  // "Jeux été 日本"
        if (!keep_choice(t / "shared", folder, {}, why)) return "not kept: " + why;
        return same("dir", find_folders({}, t / "shared", {}).dir, folder);
    });

    // ---- A pick ----
    run("pick_a_new_folder", [](const fs::path& t) { return same("folder", folder_for_pick(t / "new"), t / "new"); });
    run("pick_an_empty_folder", [](const fs::path& t) {
        fs::create_directories(t / "empty");
        return same("folder", folder_for_pick(t / "empty"), t / "empty");
    });
    run("pick_a_folder_with_things_in_it", [](const fs::path& t) {
        write(t / "Games" / "notes.txt", "mine\n");
        return same("folder", folder_for_pick(t / "Games"), t / "Games" / kOneFolderName);
    });
    run("pick_a_folder_moved_by_hand", [](const fs::path& t) {
        write(t / "moved" / "setup" / "ready.txt", "format=1\n");
        write(t / "part" / "setup" / "journal.json", "{}\n");
        return all({same("built", folder_for_pick(t / "moved"), t / "moved"),
                    same("part built", folder_for_pick(t / "part"), t / "part")});
    });
    run("pick_the_usual_folder_again", [](const fs::path& t) {
        write(t / "shared" / kChoiceFile, utf8(t / "D") + "\n");  // (and setup's leftovers beside it)
        write(t / "shared" / "setup" / "logs" / "run.log", "\n");
        return same("folder", folder_for_pick(t / "shared"), t / "shared");
    });
    run("pick_a_folder_named_for_setup", [](const fs::path& t) {
        write(t / "Games" / kOneFolderName / "tools" / "x", "\n");  // (a first check's Python, say)
        write(t / "Games" / kFolderName / "tools" / "x", "\n");     // (an earlier version's)
        return all({same("folder", folder_for_pick(t / "Games" / kOneFolderName), t / "Games" / kOneFolderName),
                    same("old", folder_for_pick(t / "Games" / kFolderName), t / "Games" / kFolderName)});
    });
    run("pick_a_drive", [](const fs::path& t) {
        const auto root = t.root_path();
        return same("folder", folder_for_pick(root), root / kOneFolderName);
    });

    // ---- The one data folder (docs/164): which place each part uses ----
    // (UserFolders as Windows has them; on Linux both are the same folder, as in old_folders_on_linux)
    const auto user = [](const fs::path& t) { return UserFolders{t / "Roaming", t / "Local"}; };
    const auto source = [](const char* what, FolderSource got, FolderSource want) {
        if (got == want) return std::string();
        return std::string(what) + " is " + source_words(got) + ", not " + source_words(want);
    };
    run("one_folder_and_the_old_ones", [&](const fs::path& t) {
        return all({same("one", one_folder(user(t)), t / "Local" / kOneFolderName),
                    same("old settings", old_settings_folder(user(t)), t / "Roaming" / kFolderName / "DemonsSouls"),
                    same("old shared", old_shared_folder(user(t)), t / "Local" / kFolderName),
                    one_folder({}).empty() ? "" : "a folder without the system's folders"});
    });
    run("settings_new_install", [&](const fs::path& t) {
        const Pick p = settings_folder(user(t), {});
        return all({same("settings", p.dir, t / "Local" / kOneFolderName),
                    source("place", p.source, FolderSource::One)});
    });
    run("settings_an_earlier_versions", [&](const fs::path& t) {
        fs::create_directories(old_settings_folder(user(t)));  // (empty: as if nothing had been kept there)
        const Pick empty = settings_folder(user(t), {});
        write(old_settings_folder(user(t)) / "settings.ini", "DES_FPS=60\n");
        const Pick p = settings_folder(user(t), {});
        return all({source("empty old folder", empty.source, FolderSource::One),
                    same("settings", p.dir, old_settings_folder(user(t))),
                    source("place", p.source, FolderSource::Old)});
    });
    run("settings_portable", [&](const fs::path& t) {
        write(old_settings_folder(user(t)) / "settings.ini", "DES_FPS=60\n");
        const Pick p = settings_folder(user(t), t / "prog");
        return all({same("settings", p.dir, t / "prog" / "data"), source("place", p.source, FolderSource::Portable)});
    });
    run("setup_shared_new_install", [&](const fs::path& t) {
        write(old_shared_folder(user(t)) / "tools" / "python" / "x", "\n");  // (a first check's Python only)
        write(old_shared_folder(user(t)) / "DemonsSouls" / "settings.ini", "\n");  // (Linux's old settings live here)
        return same("shared", setup_shared_folder(user(t)), t / "Local" / kOneFolderName / "setup-data");
    });
    run("setup_shared_an_earlier_versions", [&](const fs::path& t) {
        const auto old = old_shared_folder(user(t));
        std::string out;
        for (const char* f : {"setup/ready.txt", "setup/journal.json", kChoiceFile}) {
            std::error_code ec;
            fs::remove_all(old, ec);
            write(old / from_utf8(f), "\n");
            if (out.empty()) out = same(f, setup_shared_folder(user(t)), old);
        }
        return out;
    });
    run("setup_a_choice_kept_by_an_earlier_version", [&](const fs::path& t) {
        write(old_shared_folder(user(t)) / kChoiceFile, utf8(t / "D" / "build") + "\n");
        const Folders f = find_folders({}, setup_shared_folder(user(t)), {});
        return all({same("dir", f.dir, t / "D" / "build"), same("usual", f.usual, old_shared_folder(user(t))),
                    f.chosen ? "" : "not chosen"});
    });
    run("saves_new_install", [&](const fs::path& t) {
        fs::create_directories(t / "prog" / "gamedata" / "dev_hdd0" / "home" / "00000001" / "savedata");  // (empty)
        const Pick p = saves_folder(user(t), t / "prog", false);
        return all({same("saves", p.dir, t / "Local" / kOneFolderName),
                    source("place", p.source, FolderSource::One)});
    });
    run("saves_an_earlier_versions", [&](const fs::path& t) {
        const fs::path home = t / "prog" / "gamedata" / "dev_hdd0" / "home" / "00000001";
        std::string out;
        for (const fs::path& f : {home / "savedata" / "BLUS30443DEMONSS005" / "USER.DAT",
                                  home / "savedata-coop" / "BLUS30443DEMONSS005" / "USER.DAT",
                                  t / "prog" / "gamedata" / "trophies" / "BLUS30443.dat"}) {
            std::error_code ec;
            fs::remove_all(t / "prog", ec);
            write(f, "save\n");
            const Pick p = saves_folder(user(t), t / "prog", false);
            if (out.empty())
                out = all({same(utf8(f).c_str(), p.dir, t / "prog"), source("place", p.source, FolderSource::Old)});
        }
        return out;
    });
    run("saves_portable", [&](const fs::path& t) {
        const Pick p = saves_folder(user(t), t / "prog", true);
        return all({same("saves", p.dir, t / "prog"), source("place", p.source, FolderSource::Portable)});
    });
    run("shader_cache_new_old_portable", [&](const fs::path& t) {
        const Pick fresh = shader_cache_folder(user(t), t / "prog", false);
        fs::create_directories(t / "prog" / "shader_cache");
        const Pick old = shader_cache_folder(user(t), t / "prog", false);
        const Pick portable = shader_cache_folder(user(t), t / "other", true);
        return all({same("new", fresh.dir, t / "Local" / kOneFolderName / "shader_cache"),
                    source("new place", fresh.source, FolderSource::One),
                    same("old", old.dir, t / "prog" / "shader_cache"),
                    source("old place", old.source, FolderSource::Old),
                    same("portable", portable.dir, t / "other" / "shader_cache"),
                    source("portable place", portable.source, FolderSource::Portable)});
    });
    run("old_folders_on_linux", [&](const fs::path& t) {
        // One base for both (XDG_DATA_HOME): the old settings are inside the old shared folder, which stays unused for
        // the build until it holds one.
        const UserFolders u{t / "share", t / "share"};
        write(old_settings_folder(u) / "settings.ini", "\n");
        return all({same("settings", settings_folder(u, {}).dir, t / "share" / kFolderName / "DemonsSouls"),
                    same("shared", setup_shared_folder(u), t / "share" / kOneFolderName / "setup-data")});
    });

    // ---- Folders setup refuses ----
    run("can_use_a_folder", [](const fs::path& t) {
        const std::string why = cannot_use(t / "D" / "build", t / "prog");
        if (!why.empty()) return "refused: " + why;
        return all({fs::is_directory(t / "D" / "build") ? "" : "not made",
                    fs::is_empty(t / "D" / "build") ? "" : "its check's file is left"});
    });
    run("refuses_the_program_and_its_own_folders", [](const fs::path& t) {
        write(t / "prog" / "setup" / "release-files.txt", "des_host.exe\nlicenses/ffmpeg/LICENSE\nsetup/release-files.txt\n");
        for (const auto& f : {t / "prog", t / "prog" / "setup" / "x", t / "prog" / "licenses" / "y",
                              t / "prog" / ".previous" / "z"})
            if (cannot_use(f, t / "prog").empty()) return "not refused: " + utf8(f);
        for (const auto& f : {t / "prog" / "build", t / "prog" / "data" / "build", t / "prog" / kFolderName})
            if (const std::string why = cannot_use(f, t / "prog"); !why.empty()) return "refused " + utf8(f) + ": " + why;
        return std::string();
    });
    run("refuses_what_cannot_be_made", [](const fs::path& t) {
        write(t / "a file", "x");
        return cannot_use(t / "a file" / "build", {}).empty() ? std::string("not refused") : std::string();
    });
    run("refuses_a_relative_folder", [](const fs::path&) {
        return cannot_use("build", {}).empty() ? std::string("not refused") : std::string();
    });

    // ---- A drive that cannot hold the build (issue #673) ----
    run("holds_programs_here", [](const fs::path& t) {
        if (const std::string why = cannot_hold_programs(t); !why.empty()) return "refused: " + why;
        return fs::is_empty(t) ? std::string() : std::string("its check's files are left");
    });
    run("holds_programs_asks_the_folder_above", [](const fs::path& t) {
        if (const std::string why = cannot_hold_programs(t / "not" / "made"); !why.empty()) return "refused: " + why;
        std::error_code ec;
        return all({fs::exists(t / "not", ec) ? "a folder made" : "",
                    fs::is_empty(t) ? "" : "its check's files are left"});
    });
#ifndef _WIN32
    // Drives CI mounts on Linux (host.yml): a FAT one, as a player's USB stick or exFAT drive, which holds no links,
    // and one mounted with noexec. Both refused, with nothing of the check's left there.
    const auto refused = [](const char* variable, const char* word) {
        const char* at = std::getenv(variable);
        if (!at || !*at) return;
        run(variable, [at, word](const fs::path&) {
            const fs::path drive = from_utf8(at);
            if (const std::string why = cannot_hold_programs(drive); why.find(word) == std::string::npos)
                return std::string("not refused for '") + word + "': '" + why + "'";
            if (cannot_use(drive / "build", {}).empty()) return std::string("cannot_use took it");
            std::error_code ec;
            const bool left = !fs::is_empty(drive / "build", ec) || fs::exists(drive / ".setup-program-check", ec) ||
                              fs::exists(drive / ".setup-link-check", ec);
            fs::remove_all(drive / "build", ec);
            return left ? std::string("its check's files are left") : std::string();
        });
    };
    refused("SETUP_CHECK_FAT_FOLDER", "links");
    refused("SETUP_CHECK_NOEXEC_FOLDER", "noexec");
#endif

    // ---- Setup's own files beside the program (issue #431) ----
    const auto gone = [](EngineGone got, EngineGone want) {
        return got == want ? std::string() : "engine_gone is " + std::to_string(int(got)) + ", not " +
                                                 std::to_string(int(want));
    };
    run("engine_there", [gone](const fs::path& t) {
        write(t / "prog" / fs::path(kEngineEntry), "\n");
        write(t / "temp" / "Temp1_x.zip" / "x" / fs::path(kEngineEntry), "\n");  // (an archive tool took out all)
        return all({gone(engine_gone(t / "prog", t / "temp"), EngineGone::No),
                    gone(engine_gone(t / "temp" / "Temp1_x.zip" / "x", t / "temp"), EngineGone::No)});
    });
    run("engine_program_copied_alone", [gone](const fs::path& t) {
        write(t / "Desktop" / "game.exe", "\n");
        return all({gone(engine_gone(t / "Desktop", t / "temp"), EngineGone::NoSetupFolder),
                    gone(engine_gone(t / "Desktop", {}), EngineGone::NoSetupFolder)});
    });
    run("engine_from_inside_the_archive", [gone](const fs::path& t) {
        write(t / "temp" / "Temp1_x.zip" / "x" / "game.exe", "\n");  // (Explorer's folder for a zip's program)
        write(t / "temp" / "7zO1A2B.tmp" / "game.exe", "\n");        // (7-Zip's)
        fs::create_directories(t / "other");
        return all({gone(engine_gone(t / "temp" / "Temp1_x.zip" / "x", t / "temp"), EngineGone::FromArchive),
                    gone(engine_gone(t / "temp" / "7zO1A2B.tmp", t / "temp"), EngineGone::FromArchive),
                    // the temporary folder by another path to it (Windows' TEMP can give its short 8.3 name)
                    gone(engine_gone(t / "temp" / "7zO1A2B.tmp", t / "other" / ".." / "temp"),
                         EngineGone::FromArchive),
                    gone(engine_gone(t / "temp" / "7zO1A2B.tmp", {}), EngineGone::NoSetupFolder)});
    });
    run("engine_setup_folder_incomplete", [gone](const fs::path& t) {
        write(t / "prog" / "setup" / "release-files.txt", "game.exe\n");
        write(t / "temp" / "x" / "setup" / "tools" / "setup" / "des_setup" / "__init__.py", "\n");
        return all({gone(engine_gone(t / "prog", t / "temp"), EngineGone::Incomplete),
                    gone(engine_gone(t / "temp" / "x", t / "temp"), EngineGone::Incomplete)});
    });

    // ---- The game folders the game ran from (game-folders.txt, docs/164 section 5) ----
    run("game_folders_newest_first", [](const fs::path& t) {
        std::string text = game_folders_text("", t / "Game A", "2026-11-19T10:00:00Z");
        text = game_folders_text(text, t / "Game B", "2026-11-19T11:00:00Z");
        const auto got = game_folders(text);
        if (got.size() != 2) return "2 folders, not " + std::to_string(got.size());
        if (text.find("2026-11-19T11:00:00Z\t") == std::string::npos) return std::string("no stamp");
        return all({same("newest", got[0], t / "Game B"), same("older", got[1], t / "Game A")});
    });
    run("game_folders_once_each", [](const fs::path& t) {
        std::string text = game_folders_text("", t / "Game A", "1");
        text = game_folders_text(text, t / "Game B", "2");
        text = game_folders_text(text, t / "Game A" / ".", "3");   // (A again, written another way: up to the top)
        const auto got = game_folders(text);
        if (got.size() != 2) return "2 folders, not " + std::to_string(got.size());
        if (text.find("3\t") == std::string::npos || text.find("1\t") != std::string::npos)
            return std::string("A's old stamp kept");
        return all({same("newest", got[0], t / "Game A"), same("older", got[1], t / "Game B")});
    });
    run("game_folders_kept_at_most", [](const fs::path& t) {
        std::string text;
        for (int i = 0; i < kGameFoldersKept + 5; ++i)
            text = game_folders_text(text, t / ("Game " + std::to_string(i)), "x");
        const auto got = game_folders(text);
        if (int(got.size()) != kGameFoldersKept) return "kept " + std::to_string(got.size());
        return same("newest", got[0], t / ("Game " + std::to_string(kGameFoldersKept + 4)));
    });
    run("game_folders_bad_lines_go", [](const fs::path& t) {
        const std::string old = "# a comment\r\nnot a line\r\n\tno stamp\r\nstamp\trelative/folder\r\n2026\t" +
                                utf8(t / "Kept") + "\r\n";
        const auto got = game_folders(game_folders_text(old, t / "New", "now"));
        if (got.size() != 2) return "2 folders, not " + std::to_string(got.size());
        return all({same("newest", got[0], t / "New"), same("kept", got[1], t / "Kept")});
    });
    run("remember_game_folder_writes_only_its_file", [](const fs::path& t) {
        const fs::path data = t / "data";
        write(data / "settings.ini", "x=1\n");
        std::string why;
        if (!remember_game_folder(data, t / "Soul Game", "2026-11-19T12:00:00Z", why)) return "refused: " + why;
        if (!remember_game_folder(data, t / "Other", "2026-11-19T12:05:00Z", why)) return "refused: " + why;
        if (read(data / "settings.ini") != "x=1\n") return std::string("settings.ini changed");
        std::error_code ec;
        int files = 0;
        for (const auto& e : fs::directory_iterator(data, ec)) (void)e, ++files;
        if (files != 2) return std::to_string(files) + " files in the data folder, not 2";   // (no .new left)
        const auto got = game_folders(read(data / kGameFoldersFile));
        if (got.size() != 2) return "2 folders, not " + std::to_string(got.size());
        return all({same("newest", got[0], t / "Other"), same("older", got[1], t / "Soul Game")});
    });
    run("remember_game_folder_refuses", [](const fs::path& t) {
        std::string why;
        if (remember_game_folder({}, t / "Game", "now", why)) return std::string("no data folder taken");
        if (remember_game_folder(t / "data", "relative", "now", why)) return std::string("a relative folder taken");
        if (remember_game_folder(t / "data", t / "a\tb", "now", why)) return std::string("a tab taken");
        if (remember_game_folder(t / "data", t / "Game", "", why)) return std::string("no stamp taken");
        std::error_code ec;
        return fs::exists(t / "data" / kGameFoldersFile, ec) ? std::string("a file written") : std::string();
    });

    // ---- An earlier game folder's saves, offered and copied (docs/164 section 5) ----
    const auto save = [](const fs::path& game, const std::string& name, const std::string& text) {
        write(game / "gamedata" / "dev_hdd0" / "home" / "00000001" / "savedata" / "BLUS30443DEMONSS005" / name, text);
    };
    run("save_offer_newest_with_saves", [&](const fs::path& t) {
        save(t / "Old B", "USER.DAT", "b");
        save(t / "Old C", "USER.DAT", "c");
        fs::create_directories(t / "Old A" / "gamedata" / "dev_hdd0" / "home" / "00000001" / "savedata");  // (empty)
        const std::vector<fs::path> list = {t / "Old A", t / "Old B", t / "Old C"};
        return all({same("offer", save_offer(list, {}, t / "home"), t / "Old B"),
                    same("never B", save_offer(list, {t / "Old B"}, t / "home"), t / "Old C"),
                    save_offer(list, {t / "home"}, t / "home").empty() ? "" : "offered to a home answered Never",
                    save_offer(list, {t / "Old B", t / "Old C"}, t / "home").empty() ? "" : "offered a Never",
                    save_offer({t / "Old B"}, {}, t / "Old B").empty() ? "" : "offered its own folder"});
    });
    run("saves_owner_from_any_level", [&](const fs::path& t) {
        save(t / "Old", "USER.DAT", "x");
        const fs::path saveDir = t / "Old" / "gamedata" / "dev_hdd0" / "home" / "00000001" / "savedata" /
                                 "BLUS30443DEMONSS005";
        return all({same("game folder", saves_owner(t / "Old"), t / "Old"),
                    same("gamedata", saves_owner(t / "Old" / "gamedata"), t / "Old"),
                    same("a save", saves_owner(saveDir), t / "Old"),
                    saves_owner(t).empty() ? "" : "found saves above the game folder",
                    saves_owner(t / "nothing").empty() ? "" : "found saves in no folder"});
    });
    run("never_copy_list", [&](const fs::path& t) {
        std::string why;
        if (!never_copy_from(t / "data", t / "Old", "2026-11-26T12:00:00Z", why)) return "refused: " + why;
        if (!never_copy_from(t / "data", t / "home", "2026-11-26T12:01:00Z", why)) return "refused: " + why;
        const auto got = game_folders(read_text(t / "data" / kNeverCopyFile));
        if (got.size() != 2) return "2 folders, not " + std::to_string(got.size());
        return all({listed(got, t / "Old") ? "" : "Old not listed", listed(got, t / "home") ? "" : "home not listed",
                    listed(got, t / "Other") ? "Other listed" : "",
                    read_text(t / "missing.txt").empty() ? "" : "text from no file"});
    });

    // The copy (save_copy.hpp). The files a folder holds, as "<relative path>=<text>" lines, to compare.
    const auto files_of = [](const fs::path& dir) {
        std::vector<std::string> out;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file()) out.push_back(utf8(it->path().lexically_relative(dir)) + "=" + read(it->path()));
        std::sort(out.begin(), out.end());
        std::string s;
        for (const auto& f : out) s += f + "\n";
        return s;
    };
    run("copy_saves_copies_and_checks", [&](const fs::path& t) {
        save(t / "Old", "USER.DAT", std::string(300000, 'u'));
        save(t / "Old", "204USER.DAT", "system");
        write(t / "Old" / "gamedata" / "dev_hdd0" / "home" / "00000001" / "savedata-coop" / "BLUS30443DEMONSS005" /
                  "USER.DAT", "coop");
        write(t / "Old" / "gamedata" / "trophies" / "NPWR00881.json", "{}");
        write(t / "Old" / "gamedata" / "dev_hdd0" / "game" / "big.bin", "not a save");  // (not copied)
        const std::string before = files_of(t / "Old");
        const auto r = ui::saves::copy_saves(t / "Old", t / "home");
        if (!r.ok) return "not copied: " + r.why;
        if (r.files != 4) return std::to_string(r.files) + " files, not 4";
        std::error_code ec;
        if (fs::exists(t / "home" / ui::saves::kCopyFolder, ec)) return std::string("the copies' folder is left");
        if (fs::exists(t / "home" / "gamedata" / "dev_hdd0" / "game", ec)) return std::string("game data copied");
        if (files_of(t / "Old") != before) return std::string("the earlier folder changed");
        if (!holds_saves(t / "home")) return std::string("no saves in the new folder");
        const fs::path saves = fs::path("dev_hdd0") / "home" / "00000001";
        return all({read(t / "home" / "gamedata" / saves / "savedata" / "BLUS30443DEMONSS005" / "USER.DAT") ==
                            std::string(300000, 'u')
                        ? ""
                        : "USER.DAT differs",
                    read(t / "home" / "gamedata" / saves / "savedata-coop" / "BLUS30443DEMONSS005" / "USER.DAT") ==
                            "coop"
                        ? ""
                        : "co-op's save differs",
                    read(t / "home" / "gamedata" / "trophies" / "NPWR00881.json") == "{}" ? "" : "trophies differ"});
    });
    run("copy_saves_replaces_nothing", [&](const fs::path& t) {
        save(t / "Old", "USER.DAT", "old");
        save(t / "Old", "204USER.DAT", "old system");
        save(t / "home", "204USER.DAT", "this install's");
        const std::string before = files_of(t / "home");
        const auto r = ui::saves::copy_saves(t / "Old", t / "home");
        if (r.ok) return std::string("copied over a file there");
        if (r.why.find("is there already") == std::string::npos) return "why: " + r.why;
        std::error_code ec;
        return all({files_of(t / "home") == before ? "" : "the new folder changed",
                    fs::exists(t / "home" / ui::saves::kCopyFolder, ec) ? "the copies' folder is left" : ""});
    });
    run("copy_saves_into_an_empty_skeleton_and_over_leftovers", [&](const fs::path& t) {
        save(t / "Old", "USER.DAT", "old");
        fs::create_directories(t / "home" / "gamedata" / "dev_hdd0" / "home" / "00000001" / "savedata");  // (empty)
        write(t / "home" / ui::saves::kCopyFolder / "dev_hdd0" / "stale.bin", "an earlier try's");
        const auto r = ui::saves::copy_saves(t / "Old", t / "home");
        if (!r.ok) return "not copied: " + r.why;
        std::error_code ec;
        return all({fs::exists(t / "home" / ui::saves::kCopyFolder, ec) ? "the copies' folder is left" : "",
                    fs::exists(t / "home" / "gamedata" / "dev_hdd0" / "stale.bin", ec) ? "a leftover moved in" : "",
                    holds_saves(t / "home") ? "" : "no saves in the new folder"});
    });
    run("copy_saves_refuses", [&](const fs::path& t) {
        save(t / "Old", "USER.DAT", "old");
        fs::create_directories(t / "Empty" / "gamedata");
        const auto none = ui::saves::copy_saves(t / "Empty", t / "home");
        const auto self = ui::saves::copy_saves(t / "Old", t / "Old");
        const auto blank = ui::saves::copy_saves({}, t / "home");
        std::error_code ec;
        return all({none.ok ? "copied from a folder with no saves" : "", self.ok ? "copied into itself" : "",
                    blank.ok ? "copied from no folder" : "",
                    fs::exists(t / "Old" / ui::saves::kCopyFolder, ec) ? "wrote in the earlier folder" : ""});
    });

    // ---- Names that aren't valid text (path_utf8.hpp) ----
    run("utf16_names_as_text", [](const fs::path&) {
        struct Case {
            std::u16string in;
            const char* want;
        };
        const Case cases[] = {
            {u"rpcs3.exe", "rpcs3.exe"},
            {std::u16string{u'a', char16_t(0xE9)}, "a\xC3\xA9"},
            {std::u16string{u'a', char16_t(0xD83D), char16_t(0xDE00)}, "a\xF0\x9F\x98\x80"},  // (a pair: one character)
            {std::u16string{u'a', char16_t(0xD800), u'b'}, "a\xEF\xBF\xBD" "b"},      // a high one alone
            {std::u16string{char16_t(0xDC00), u'c'}, "\xEF\xBF\xBD" "c"},            // a low one alone
            {std::u16string{u'd', char16_t(0xDBFF)}, "d\xEF\xBF\xBD"},              // a high one last
        };
        for (const Case& c : cases)
            if (const std::string got = ui::utf8_from_utf16(c.in.data(), c.in.size()); got != c.want)
                return "a name became " + got + ", not " + c.want;
        return std::string();
    });
#ifdef _WIN32
    // A file whose name holds an unpaired surrogate, as Windows allows: read back from the folder, its name is text
    // with U+FFFD (std::filesystem's u8string() throws on it).
    run("bad_file_name_read_back", [](const fs::path& t) {
        const fs::path bad = t / std::wstring{L'b', L'a', L'd', wchar_t(0xD800), L'.', L't', L'x', L't'};
        std::ofstream(bad, std::ios::binary) << "x";
        std::error_code ec;
        if (!fs::exists(bad, ec)) return std::string("could not make the file");
        std::string got;
        for (const auto& e : fs::directory_iterator(t, ec)) got = ui::path_utf8(e.path().filename());
        return got == "bad\xEF\xBF\xBD.txt" ? std::string() : "read back as " + got;
    });
#endif

    // ---- What differs by platform ----
    run("same_folder_by_case", [](const fs::path& t) {
        const bool same_case = same_folder(t / "Not There" / "Build", t / "not there" / "build");
#ifdef _WIN32
        return same_case ? std::string() : std::string("Windows' names differ by case");
#else
        return same_case ? std::string("names that differ by case taken as one") : std::string();
#endif
    });

    fs::remove_all(g_root, ec);
    std::printf("setup_folder_check: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
