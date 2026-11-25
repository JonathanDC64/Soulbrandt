// Where setup builds the game: the usual folder or the player's choice (see setup_folder.hpp). One fact differs by
// platform, in one small function: whether two file names that differ only in case are the same name.
#include "setup_folder.hpp"
#include "path_utf8.hpp"

#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <cwctype>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ui::setup {
namespace {

std::string utf8(const std::filesystem::path& p) {
    return path_utf8(p);  // (never throws: path_utf8.hpp)
}

std::filesystem::path from_utf8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

// ---- What differs by platform ----

#ifdef _WIN32
// Windows' file names ignore case.
bool same_name(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::towlower(wint_t(a[i])) != std::towlower(wint_t(b[i]))) return false;
    return true;
}
#else
bool same_name(const std::string& a, const std::string& b) {
    return a == b;
}
#endif

// ---- The same everywhere ----

// The path without "." and "..", and without a separator at its end (but a root's).
std::filesystem::path tidy(const std::filesystem::path& p) {
    std::filesystem::path out = p.lexically_normal();
    if (!out.has_filename() && out.has_relative_path()) out = out.parent_path();
    return out;
}

// `path` is `root` or in it, by their names.
bool inside(const std::filesystem::path& path, const std::filesystem::path& root) {
    const auto rel = tidy(path).lexically_relative(tidy(root));
    return !rel.empty() && *rel.begin() != "..";
}

// In the program's folder, a name an update replaces: the engine's folder, the update's own, and the names the
// release lists in setup/release-files.txt (des_setup/update.py's program files).
bool program_name(const std::filesystem::path& program, const std::filesystem::path& top) {
    for (const char* name : {"setup", ".update", ".previous", ".previous.old"})
        if (same_name(top.native(), std::filesystem::path(name).native())) return true;
    std::ifstream f(program / "setup" / "release-files.txt", std::ios::binary);
    for (std::string line; std::getline(f, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (same_name(top.native(), from_utf8(line.substr(0, line.find('/'))).native())) return true;
    }
    return false;
}

}  // namespace

std::filesystem::path chosen_folder(const std::filesystem::path& usual) {
    if (usual.empty()) return {};
    std::ifstream f(usual / kChoiceFile, std::ios::binary);
    for (std::string line; std::getline(f, line);) {
        if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);  // (a byte order mark: a file saved by Notepad)
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        const auto p = from_utf8(line.substr(start));
        return tidy(p.is_absolute() ? p : usual / p);
    }
    return {};
}

Folders find_folders(const std::filesystem::path& given, const std::filesystem::path& shared,
                     const std::filesystem::path& portableProgram) {
    if (!given.empty()) return {given, {}, false};
    const auto usual = portableProgram.empty() ? shared : portableProgram / "data" / "setup-data";
    if (usual.empty()) return {};
    if (const auto chosen = chosen_folder(usual); !chosen.empty() && !same_folder(chosen, usual)) {
        std::fprintf(stderr, "[setup] setup's folder: %s, chosen on setup's screens (%s in %s)\n", utf8(chosen).c_str(),
                     kChoiceFile, utf8(usual).c_str());
        return {chosen, usual, true};
    }
    // A portable install (issue #364, docs/45 section 4.4) that was built in the shared folder before (0.1.0 put it
    // there) keeps using it until it is moved here, rather than build the game again.
    std::error_code ec;
    if (!portableProgram.empty() && !std::filesystem::exists(usual / "setup", ec) && !shared.empty() &&
        std::filesystem::exists(shared / "setup" / "ready.txt", ec)) {
        std::fprintf(stderr,
                     "[setup] portable install, built in %s before: setup's folder stays there (to take it along, "
                     "move it to %s)\n",
                     utf8(shared).c_str(), utf8(usual).c_str());
        return {shared, usual, false};
    }
    return {usual, usual, false};
}

bool same_folder(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (a.empty() || b.empty()) return a.empty() && b.empty();
    std::error_code ec;
    if (std::filesystem::exists(a, ec) && std::filesystem::exists(b, ec)) {
        const bool same = std::filesystem::equivalent(a, b, ec);
        if (!ec) return same;
    }
    return same_name(tidy(a).native(), tidy(b).native());
}

bool holds_setup(const std::filesystem::path& folder) {
    std::error_code ec;
    return std::filesystem::exists(folder / "setup" / "ready.txt", ec) ||
           std::filesystem::exists(folder / "setup" / "journal.json", ec);
}

std::filesystem::path folder_for_pick(const std::filesystem::path& picked) {
    const auto p = tidy(picked);
    if (p.empty()) return p;
    std::error_code ec;
    // Setup's already: its work in it, the usual folder (the choice's file in it), or one with setup's own name.
    if (holds_setup(p) || std::filesystem::exists(p / kChoiceFile, ec) ||
        same_name(p.filename().native(), std::filesystem::path(kFolderName).native()))
        return p;
    // A drive's root is never a folder of setup's own, empty or not.
    const bool root = p == p.root_path();
    if (!root && !std::filesystem::exists(p, ec)) return p;
    if (!root && std::filesystem::is_directory(p, ec) && std::filesystem::is_empty(p, ec) && !ec) return p;
    return p / kFolderName;
}

std::string cannot_use(const std::filesystem::path& folder, const std::filesystem::path& program) {
    if (folder.empty() || !folder.is_absolute()) return "Choose a folder on this PC.";
    if (!program.empty()) {
        if (same_folder(folder, program)) return "Choose a folder of its own, not the program's folder.";
        if (inside(folder, program)) {
            const auto rel = tidy(folder).lexically_relative(tidy(program));
            if (program_name(program, *rel.begin()))
                return "This folder is part of the program, which an update replaces. Choose another folder.";
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (!std::filesystem::is_directory(folder, ec)) return "Setup cannot make this folder. Choose another one.";
    // A file of its own written in it, and removed at once.
    const auto probe = folder / ".setup-write-check";
    bool wrote = false;
    {
        std::ofstream f(probe, std::ios::binary | std::ios::trunc);
        if (f.is_open()) {
            f << "ok\n";
            f.flush();
            wrote = f.good();
        }
    }
    std::filesystem::remove(probe, ec);
    if (!wrote) return "Setup cannot write in this folder. Choose another one.";
    return cannot_hold_programs(folder);
}

std::string cannot_hold_programs(const std::filesystem::path& folder) {
#ifdef _WIN32
    (void)folder;
    return {};
#else
    std::error_code ec;
    std::filesystem::path at = folder;  // (the folder may not be made yet: the nearest one above it, on its drive)
    while (!at.empty() && !std::filesystem::is_directory(at, ec) && at != at.parent_path()) at = at.parent_path();
    if (at.empty() || !std::filesystem::is_directory(at, ec)) return {};
    const auto program = at / ".setup-program-check";
    const auto link = at / ".setup-link-check";
    std::filesystem::remove(link, ec);
    bool wrote = false;
    {
        std::ofstream f(program, std::ios::binary | std::ios::trunc);
        if (f.is_open()) {
            f << "#!/bin/sh\n";
            f.flush();
            wrote = f.good();
        }
    }
    if (!wrote) {
        std::filesystem::remove(program, ec);
        return {};
    }
    // (chmod may be refused where the drive fixes the rights; access then says whether it runs)
    (void)::chmod(program.c_str(), 0755);
    const bool runs = ::access(program.c_str(), X_OK) == 0;
    std::filesystem::create_symlink(program.filename(), link, ec);
    const bool links = !ec && std::filesystem::is_symlink(std::filesystem::symlink_status(link, ec));
    std::filesystem::remove(link, ec);
    std::filesystem::remove(program, ec);
    if (!links)
        return "Setup cannot build on this drive: it cannot hold the links Linux programs are made of, as drives "
               "formatted FAT32 or exFAT cannot. Choose a folder on a drive formatted for Linux, such as one in your "
               "home folder.";
    if (!runs)
        return "Setup cannot build on this drive: programs cannot run from it (it is mounted with noexec). Choose a "
               "folder on another drive, such as one in your home folder.";
    return {};
#endif
}

bool keep_choice(const std::filesystem::path& usual, const std::filesystem::path& folder,
                 const std::filesystem::path& program, std::string& why) {
    why.clear();
    std::error_code ec;
    const auto file = usual / kChoiceFile;
    if (usual.empty()) {
        why = "Setup's folder is set for this PC, so it cannot be changed here.";
        return false;
    }
    if (same_folder(folder, usual)) {  // (back to the usual folder: no choice to keep)
        std::filesystem::remove(file, ec);
        if (!std::filesystem::exists(file, ec)) return true;
        why = "Setup could not change its file in " + utf8(usual) + ".";
        return false;
    }
    std::string named = utf8(tidy(folder));
    if (!program.empty() && inside(usual, program) && inside(folder, program)) {
        named = path_generic_utf8(tidy(folder).lexically_relative(tidy(usual)));
    }
    std::filesystem::create_directories(usual, ec);
    const auto part = usual / (std::string(kChoiceFile) + ".part");
    bool wrote = false;
    {
        std::ofstream f(part, std::ios::binary | std::ios::trunc);
        if (f.is_open()) {
            f << "# Setup builds the game in this folder, chosen on its screens (docs/88). Without this file it "
                 "builds here.\n"
              << named << "\n";
            f.flush();
            wrote = f.good();
        }
    }
    if (wrote) std::filesystem::rename(part, file, ec);
    if (!wrote || ec || !same_folder(chosen_folder(usual), folder)) {
        std::filesystem::remove(part, ec);
        why = "Setup could not save this choice in " + utf8(usual) + ".";
        return false;
    }
    return true;
}

EngineGone engine_gone(const std::filesystem::path& program, const std::filesystem::path& temp) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(program / std::filesystem::path(kEngineEntry), ec)) return EngineGone::No;
    if (std::filesystem::is_directory(program / "setup", ec)) return EngineGone::Incomplete;
    // In the temporary folder or below it, folder by folder on disk: Windows' TEMP can name it by its short 8.3 name,
    // and the program's path is the long one.
    if (!temp.empty())
        for (auto p = tidy(program); !p.empty(); p = p.parent_path()) {
            if (same_folder(p, temp)) return EngineGone::FromArchive;
            if (p == p.parent_path()) break;
        }
    return EngineGone::NoSetupFolder;
}

// ---- The game folders the game ran from ----

namespace {

constexpr const char* kGameFoldersHeader =
    "# The game folders this PC's game ran from, newest first: when (UTC), a tab, the folder. Each one's saves are in\n"
    "# its gamedata folder. A later version reads this to offer copying old saves into a new install.\n";

struct Noted {
    std::string when;
    std::filesystem::path folder;
};

std::vector<Noted> noted(const std::string& text) {
    std::vector<Noted> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) continue;
        std::filesystem::path folder = from_utf8(line.substr(tab + 1));
        if (!folder.is_absolute()) continue;
        out.push_back({line.substr(0, tab), std::move(folder)});
    }
    return out;
}

}  // namespace

std::string game_folders_text(const std::string& old, const std::filesystem::path& folder, const std::string& when) {
    std::string out = std::string(kGameFoldersHeader) + when + "\t" + utf8(folder) + "\n";
    std::vector<std::filesystem::path> kept{folder};
    for (const Noted& n : noted(old)) {
        if (int(kept.size()) >= kGameFoldersKept) break;
        bool again = false;
        for (const auto& k : kept) again = again || same_folder(n.folder, k);
        if (again) continue;
        out += n.when + "\t" + utf8(n.folder) + "\n";
        kept.push_back(n.folder);
    }
    return out;
}

std::vector<std::filesystem::path> game_folders(const std::string& text) {
    std::vector<std::filesystem::path> out;
    for (const Noted& n : noted(text)) out.push_back(n.folder);
    return out;
}

bool remember_game_folder(const std::filesystem::path& dataDir, const std::filesystem::path& folder,
                          const std::string& when, std::string& why) {
    const std::string name = utf8(folder);
    if (dataDir.empty() || !folder.is_absolute() || name.find_first_of("\t\r\n") != std::string::npos ||
        when.empty() || when.find_first_of("\t\r\n") != std::string::npos) {
        why = "no folder to note";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);
    const std::filesystem::path file = dataDir / kGameFoldersFile;
    std::string old;
    if (std::filesystem::exists(file, ec)) {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            why = utf8(file) + " cannot be read";   // (left as it is: rewriting it would lose its folders)
            return false;
        }
        std::stringstream s;
        s << in.rdbuf();
        old = s.str();
    }
    std::filesystem::path next = file;
    next += ".new";
    {
        std::ofstream out(next, std::ios::binary | std::ios::trunc);
        out << game_folders_text(old, folder, when);
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(next, ec);   // (this function's own file)
            why = utf8(next) + " cannot be written";
            return false;
        }
    }
    std::filesystem::rename(next, file, ec);   // (replaces the file: on Windows MoveFileEx's REPLACE_EXISTING)
    if (ec) {
        why = utf8(file) + " cannot be replaced: " + ec.message();
        std::error_code ec2;
        std::filesystem::remove(next, ec2);
        return false;
    }
    return true;
}

}  // namespace ui::setup
