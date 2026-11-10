// Mods (mods.hpp, docs/143): the mods folder, each mod's files and kind, and PS3_VFS_MODS at start.
#include "mods.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <system_error>

extern "C" void ps3_setenv(const char* name, const char* value);  // boot_main.cpp: through the getenv cache

namespace ui::mods {

namespace fs = std::filesystem;

namespace {

std::vector<Mod> sInUse;
bool sGameplay = false;
bool sFromEnv = false;

// The game's top folders (a disc's USRDIR): a folder holding one of them holds what USRDIR holds.
constexpr const char* kGameFolders[] = {"chr", "facegen", "font", "item", "map", "menu", "movie", "msg", "mtd", "obj",
    "other", "param", "paramdef", "parts", "remo", "script", "sfx", "shader", "sound", "testdata", "tropdir"};

// Folders whose files only change how the game looks or sounds: menus and fonts, text, faces, effects, materials and
// shaders, sound, movies, equipment and item models, cutscenes.
constexpr const char* kLookFolders[] = {
    "facegen", "font", "item", "menu", "movie", "msg", "mtd", "parts", "remo", "sfx", "shader", "sound"};

// A mod archive's own files, never the game's: its notes and pictures, and what file managers leave.
constexpr const char* kNoteExtensions[] = {".txt", ".md", ".pdf", ".rtf", ".nfo", ".html", ".htm", ".url", ".jpg",
    ".jpeg", ".gif", ".webp", ".bmp", ".doc", ".docx"};
constexpr const char* kJunkNames[] = {".ds_store", "desktop.ini", "thumbs.db"};

constexpr std::size_t kMaxFiles = 200000;   // a folder this big is not a mod (or not one to read at every start)
constexpr std::size_t kGameplayShown = 5;

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

bool ends_with(const std::string& s, const char* tail) {
    const std::size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

bool starts_with(const std::string& s, const char* head) {
    return s.rfind(head, 0) == 0;
}

template <std::size_t N>
bool one_of(const std::string& s, const char* const (&list)[N]) {
    for (const char* x : list)
        if (s == x) return true;
    return false;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t a = 0;
    while (a <= s.size()) {
        const std::size_t b = s.find(sep, a);
        const std::string part = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!part.empty()) out.push_back(part);
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

bool is_dir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

// The subfolders of a folder, A to Z by name.
std::vector<fs::path> subfolders(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code e2;
        if (it->is_directory(e2)) out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool holds_game_folder(const fs::path& dir) {
    for (const fs::path& sub : subfolders(dir))
        if (one_of(lower(utf8(sub.filename())), kGameFolders)) return true;
    return false;
}

// A copy of the game's own files that a mod ships beside its own, to undo it ("Original\PS3_GAME\USRDIR"): never the
// mod.
bool backup_folder(const fs::path& dir) {
    const std::string name = lower(utf8(dir.filename()));
    for (const char* word : {"original", "backup", "vanilla", "uninstall", "unmodded"})
        if (starts_with(name, word)) return true;
    return false;
}

// The folder in a mod folder that holds what USRDIR holds: a USRDIR within five levels (a PS3_GAME\USRDIR, or one
// in a wrapper folder or a version's folder), else the shallowest folder that holds one of the game's folders. Backup
// folders are passed over. Two candidates at the same depth: none, and *problem says why.
fs::path find_root(const fs::path& modFolder, std::string* problem) {
    std::vector<fs::path> level{modFolder};
    for (int depth = 0; depth <= 5 && !level.empty(); ++depth) {
        std::vector<fs::path> usr, game, next;
        for (const fs::path& d : level) {
            if (depth > 0 && lower(utf8(d.filename())) == "usrdir") usr.push_back(d);
            else if (holds_game_folder(d)) game.push_back(d);
            for (const fs::path& sub : subfolders(d))
                if (!backup_folder(sub)) next.push_back(sub);
        }
        if (usr.size() > 1 || (usr.empty() && game.size() > 1)) {
            const std::vector<fs::path>& two = usr.size() > 1 ? usr : game;
            *problem = "It holds the game's folders in more than one place (" +
                       utf8(two[0].lexically_relative(modFolder)) + ", " + utf8(two[1].lexically_relative(modFolder)) +
                       "): keep only the one you want in this folder.";
            return {};
        }
        if (!usr.empty()) return usr.front();
        if (!game.empty()) return game.front();
        level = std::move(next);
    }
    *problem = "No game files found. A mod's folders (menu, param, parts...) go in this folder.";
    return {};
}

// A mod's files below its root: the count, the game folders they are in, and the first few that make it a gameplay
// mod. Notes, pictures and file managers' leftovers beside the game's folders are not the mod's.
void walk(Mod& m) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(m.root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        std::string generic = utf8(it->path().lexically_relative(m.root));
        std::replace(generic.begin(), generic.end(), '\\', '/');
        const std::string low = lower(generic);
        const std::string name = lower(utf8(it->path().filename()));
        if (one_of(name, kJunkNames) || low.find("__macosx/") != std::string::npos) continue;
        const bool top = low.find('/') == std::string::npos;
        if (top) {
            bool note = false;
            for (const char* ext : kNoteExtensions) note = note || ends_with(low, ext);
            if (note) continue;
            if (low == "eboot.bin") {
                m.problem = "It has an EBOOT.BIN: Soulbrandt runs its own program, so a mod that changes the game's "
                            "program can't be used.";
                continue;
            }
        }
        if (++m.files > kMaxFiles) {
            m.problem = "It has more than 200,000 files.";
            return;
        }
        const std::string area = top ? std::string("(top)") : low.substr(0, low.find('/'));
        if (std::find(m.areas.begin(), m.areas.end(), area) == m.areas.end()) m.areas.push_back(area);
        if (gameplay_file(low)) {
            m.kind = Kind::Gameplay;
            if (m.gameplay.size() < kGameplayShown) m.gameplay.push_back(generic);
        }
    }
    std::sort(m.areas.begin(), m.areas.end());
    if (!m.problem.empty()) return;
    if (m.files == 0) m.problem = "It has no files.";
}

const char* kind_word(Kind kind) {
    return kind == Kind::Gameplay ? "gameplay" : "look";
}

}  // namespace

std::string utf8(const fs::path& path) {
    const auto s = path.u8string();
    return std::string(s.begin(), s.end());
}

fs::path from_utf8(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string join(const std::vector<std::string>& items, const char* sep) {
    std::string out;
    for (const std::string& s : items) {
        if (!out.empty()) out += sep;
        out += s;
    }
    return out;
}

const char* kind_label(Kind kind) {
    return kind == Kind::Gameplay ? "Gameplay" : "Look only";
}

fs::path folder(const fs::path& dataDir) {
    if (const char* given = std::getenv("DES_MODS_DIR"); given && *given) return from_utf8(given);
    if (dataDir.empty()) return {};
    return dataDir / "mods";
}

bool gameplay_file(const std::string& relIn) {
    const std::string rel = lower(relIn);
    // Textures and pictures, wherever they are.
    if (ends_with(rel, ".tpf") || ends_with(rel, ".tpf.dcx") || ends_with(rel, ".dds") || ends_with(rel, ".png"))
        return false;
    const std::size_t slash = rel.find('/');
    if (slash == std::string::npos) return true;   // a file beside the game's folders: not known
    const std::string top = rel.substr(0, slash);
    if (one_of(top, kLookFolders)) return false;
    if (starts_with(rel, "param/drawparam/")) return false;   // lighting, fog and depth of field
    if (top == "map") {
        // The maps' models are looks; their layouts (mapstudio), collision (.hkx), navigation (.nvm, .mcp, .mcg)
        // and the rest are not.
        if (starts_with(rel, "map/mapstudio/")) return true;
        return rel.find(".flver") == std::string::npos;
    }
    return true;
}

Mod read(const fs::path& modFolder) {
    Mod m;
    m.name = utf8(modFolder.filename());
    m.folder = modFolder;
    if (m.name.find('|') != std::string::npos) {
        m.problem = "Its name has a '|': rename the folder.";
        return m;
    }
    m.root = find_root(modFolder, &m.problem);
    if (m.root.empty()) return m;
    walk(m);
    return m;
}

std::vector<Mod> scan(const fs::path& modsFolder, const std::string& enabledList) {
    std::vector<Mod> out;
    if (modsFolder.empty() || !is_dir(modsFolder)) return out;
    const std::vector<std::string> on = split(enabledList, '|');
    for (const fs::path& sub : subfolders(modsFolder)) {
        Mod m = read(sub);
        m.enabled = std::find(on.begin(), on.end(), m.name) != on.end();
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<std::string> conflicts(const std::vector<Mod>& mods, std::size_t max) {
    std::map<std::string, std::vector<std::string>> owners;
    for (const Mod& m : mods) {
        if (m.root.empty()) continue;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(m.root, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            std::string rel = utf8(it->path().lexically_relative(m.root));
            std::replace(rel.begin(), rel.end(), '\\', '/');
            const std::string low = lower(rel);
            if (low.find('/') == std::string::npos) continue;   // (notes and such beside the folders)
            owners[low].push_back(m.name);
        }
    }
    std::vector<std::string> out;
    for (const auto& [file, names] : owners) {
        if (names.size() < 2) continue;
        if (out.size() == max) {
            out.push_back("...");
            break;
        }
        out.push_back(file + ": " + join(names, ", "));
    }
    return out;
}

void apply(const fs::path& dataDir, const std::string& enabledList) {
    sInUse.clear();
    sGameplay = false;
    sFromEnv = false;
    if (const char* given = std::getenv("PS3_VFS_MODS"); given && *given) {
        // From the environment (a test run): those folders, each read as a mod's root.
        sFromEnv = true;
        for (const std::string& part : split(given, '|')) {
            Mod m;
            m.root = m.folder = from_utf8(part);
            m.name = utf8(m.folder.filename());
            m.enabled = true;
            walk(m);
            sInUse.push_back(std::move(m));
        }
    } else {
        const fs::path dir = folder(dataDir);
        if (dir.empty()) return;   // (no data folder kept: no mods folder)
        const std::vector<Mod> all = scan(dir, enabledList);
        std::vector<std::string> roots;
        for (const Mod& m : all) {
            if (!m.enabled) continue;
            if (!m.problem.empty()) {
                std::fprintf(stderr, "[mods] %s: not used: %s\n", m.name.c_str(), m.problem.c_str());
                continue;
            }
            roots.push_back(utf8(m.root));
            sInUse.push_back(m);
        }
        for (const std::string& name : split(enabledList, '|')) {
            const bool there = std::any_of(all.begin(), all.end(), [&](const Mod& m) { return m.name == name; });
            if (!there) std::fprintf(stderr, "[mods] %s is on, but the mods folder has no such folder\n", name.c_str());
        }
        if (!roots.empty()) ps3_setenv("PS3_VFS_MODS", join(roots, "|").c_str());
        if (sInUse.empty()) {
            if (!all.empty())
                std::fprintf(stderr, "[mods] %zu in the mods folder, none on\n", all.size());
            return;
        }
    }
    for (const Mod& m : sInUse) sGameplay = sGameplay || m.kind == Kind::Gameplay;
    std::fprintf(stderr, "[mods] on, the first one first: %s\n", summary().c_str());
    for (const Mod& m : sInUse)
        if (m.kind == Kind::Gameplay)
            std::fprintf(stderr, "[mods]   %s changes how the game plays (%s)\n", m.name.c_str(),
                         join(m.gameplay, ", ").c_str());
    if (sGameplay) std::fprintf(stderr, "[mods] a gameplay mod is on: this session stays offline\n");
}

const std::vector<Mod>& in_use() {
    return sInUse;
}

bool gameplay_in_use() {
    return sGameplay;
}

bool from_environment() {
    return sFromEnv;
}

std::string gameplay_names() {
    std::vector<std::string> names;
    for (const Mod& m : sInUse)
        if (m.kind == Kind::Gameplay) names.push_back(m.name);
    return join(names, ", ");
}

std::string summary() {
    if (sInUse.empty()) return "none";
    std::vector<std::string> parts;
    for (const Mod& m : sInUse)
        parts.push_back(m.name + " (" + kind_word(m.kind) + ", " + std::to_string(m.files) + " files)");
    return std::to_string(sInUse.size()) + ": " + join(parts, ", ");
}

}  // namespace ui::mods
