// Mods (mods.hpp, docs/143): the mods folder, each mod's files and kind, and PS3_VFS_MODS at start.
#include "mods.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
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

constexpr const char* kNoGameFolders =
    "Nothing in it is laid out like the game's own files (folders such as menu or param), so the game wouldn't find "
    "it.";

// The folder in a mod folder that holds what USRDIR holds: a USRDIR within five levels (a PS3_GAME\USRDIR, or one
// in a wrapper folder or a version's folder), else the shallowest folder that holds one of the game's folders. Backup
// folders are passed over. Two candidates at the same depth: none (Problem::TwoCopies). No game folder at all: none,
// and the mod is loose (Problem::NoGameFiles until plan() finds where its files go).
fs::path find_root(const fs::path& modFolder, Mod& m) {
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
            m.why = Problem::TwoCopies;
            m.problem = "It holds two versions or options of the mod (" + utf8(two[0].lexically_relative(modFolder)) +
                        " and " + utf8(two[1].lexically_relative(modFolder)) +
                        "), and the game can only use one.";
            return {};
        }
        if (!usr.empty()) return usr.front();
        if (!game.empty()) return game.front();
        level = std::move(next);
    }
    m.why = Problem::NoGameFiles;
    m.problem = kNoGameFolders;
    m.loose = true;
    return {};
}

bool note_file(const std::string& low) {
    for (const char* ext : kNoteExtensions)
        if (ends_with(low, ext)) return true;
    return false;
}

bool junk_file(const std::string& lowRel, const std::string& lowName) {
    return one_of(lowName, kJunkNames) || lowRel.find("__macosx/") != std::string::npos;
}

// A file locked as on the PS3 (an NPD container, as the game's .sdat files are). This version reads unlocked files
// only: setup unlocks the game's own, never a mod's.
bool locked_file(const fs::path& p, const std::string& low) {
    if (!ends_with(low, ".sdat") && !ends_with(low, ".edat")) return false;
    std::ifstream f(p, std::ios::binary);
    char magic[4] = {};
    return f.read(magic, 4) && std::memcmp(magic, "NPD", 4) == 0;
}

// The game's files by name (lowercase), each with its paths below USRDIR (the game's case, '/'): where a mod's loose
// file goes. Read once per game folder (PS3_VFS_ROOT), under sGameLock.
std::mutex sGameLock;
const std::map<std::string, std::vector<std::string>>& game_files() {
    static std::string sFor = "\n";
    static std::map<std::string, std::vector<std::string>> sFiles;
    const char* root = std::getenv("PS3_VFS_ROOT");
    const std::string key = root ? root : "";
    if (key == sFor) return sFiles;
    sFor = key;
    sFiles.clear();
    if (key.empty()) return sFiles;
    const fs::path r = from_utf8(key);
    for (const fs::path& usr : {r / "PS3_GAME" / "USRDIR", r / "USRDIR"}) {
        if (!is_dir(usr)) continue;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(usr, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            std::string rel = utf8(it->path().lexically_relative(usr));
            std::replace(rel.begin(), rel.end(), '\\', '/');
            sFiles[lower(utf8(it->path().filename()))].push_back(rel);
        }
        break;
    }
    return sFiles;
}

// Of the game's paths for a file's name, the one whose folders match most of the folders the mod's file is in, from
// the name up ("mapstudio/m01_00_00_00.msb" -> "map/mapstudio/m01_00_00_00.msb"). "" when two match as well.
std::string best_place(const std::string& lowRel, const std::vector<std::string>& paths) {
    const std::vector<std::string> mine = split(lowRel, '/');
    std::string best;
    std::size_t bestScore = 0;
    bool tie = false;
    for (const std::string& p : paths) {
        const std::vector<std::string> theirs = split(lower(p), '/');
        std::size_t score = 0;
        while (score < mine.size() && score < theirs.size() &&
               mine[mine.size() - 1 - score] == theirs[theirs.size() - 1 - score])
            ++score;
        if (score > bestScore) {
            best = p;
            bestScore = score;
            tie = false;
        } else if (score == bestScore) {
            tie = true;
        }
    }
    return tie ? std::string() : best;
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
        // (notes and pictures anywhere: the game has no file of those kinds)
        if (junk_file(low, name) || note_file(low)) continue;
        const bool top = low.find('/') == std::string::npos;
        if (top) {
            if (low == "eboot.bin") {
                m.why = Problem::Program;
                m.problem = "It replaces the game's program (EBOOT.BIN). This PC version runs its own program "
                            "instead, so a mod like this can't work here.";
                continue;
            }
        }
        if (++m.files > kMaxFiles) {
            m.why = Problem::TooManyFiles;
            m.problem = "It has more than 200,000 files, far more than the game itself has.";
            return;
        }
        const std::string area = top ? std::string("(top)") : low.substr(0, low.find('/'));
        if (std::find(m.areas.begin(), m.areas.end(), area) == m.areas.end()) m.areas.push_back(area);
        if (gameplay_file(low)) {
            m.kind = Kind::Gameplay;
            if (m.gameplay.size() < kGameplayShown) m.gameplay.push_back(generic);
        }
        if (m.locked.size() < kGameplayShown && locked_file(it->path(), low)) m.locked.push_back(generic);
    }
    std::sort(m.areas.begin(), m.areas.end());
    if (!m.usable()) return;
    if (m.files == 0) {
        m.why = Problem::Empty;
        m.problem = "There are no files in it.";
    } else if (!m.locked.empty()) {
        m.why = Problem::Locked;
        m.problem = "Some of its files are locked, like the files on the game disc. Setup unlocks the game's own "
                    "files, but not a mod's, so the game can't read these.";
    }
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

const char* problem_label(Problem p) {
    switch (p) {
    case Problem::None: return "";
    case Problem::Name: return "Rename its folder";
    case Problem::TwoCopies: return "Two versions";
    case Problem::NoGameFiles: return "No game files";
    case Problem::NotInPlace: return "Not in place";
    case Problem::Program: return "Changes the program";
    case Problem::TooManyFiles: return "Too many files";
    case Problem::Empty: return "Empty";
    case Problem::Locked: return "Locked files";
    }
    return "";
}

const char* problem_advice(Problem p) {
    switch (p) {
    case Problem::None: return "";
    case Problem::Name: return "Rename the folder without the '|', then refresh the list.";
    case Problem::TwoCopies:
        return "Keep the version you want in this folder. Move the other one out, or into a folder of its own next "
               "to this one, and it becomes a separate mod. Then refresh the list.";
    case Problem::NoGameFiles:
        return "The mod's notes should say where its files go. Lay them out that way in this folder (as below "
               "PS3_GAME\\USRDIR on the disc), then refresh the list.";
    case Problem::NotInPlace:
        return "Select it to move its files into the right folders (inside this mod's folder). Then you can turn it "
               "on.";
    case Problem::Program:
        return "Nothing to do here. If the mod also comes without an EBOOT.BIN, try that version.";
    case Problem::TooManyFiles: return "Check that this folder holds one mod, not several or a copy of the game.";
    case Problem::Empty: return "Put the mod's files in it, or delete the folder.";
    case Problem::Locked:
        return "Use a version of the mod whose files aren't locked, if its download page has one.";
    }
    return "";
}

std::vector<std::string> split_list(const std::string& v) {
    return split(v, '|');
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
        m.why = Problem::Name;
        m.problem = "Its folder's name has a '|' in it. The list of mods that are on uses '|' between names.";
        return m;
    }
    m.root = find_root(modFolder, m);
    if (m.root.empty()) return m;
    walk(m);
    return m;
}

void plan(Mod& m) {
    m.moves.clear();
    if (!m.loose) return;
    m.why = Problem::NoGameFiles;   // (until a place is found for every file)
    m.problem = kNoGameFolders;
    std::lock_guard<std::mutex> lock(sGameLock);
    const auto& game = game_files();
    if (game.empty()) return;   // (no game folder known)
    std::vector<Move> moves;
    std::set<std::string> targets;
    std::string why;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(m.folder, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end && why.empty(); it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        std::string rel = utf8(it->path().lexically_relative(m.folder));
        std::replace(rel.begin(), rel.end(), '\\', '/');
        const std::string low = lower(rel), name = lower(utf8(it->path().filename()));
        if (junk_file(low, name) || note_file(low)) continue;
        if (moves.size() == 1000) {
            why = "it has more than 1,000 files";
            break;
        }
        const auto f = game.find(name);
        const std::string to = f == game.end() ? std::string() : best_place(low, f->second);
        if (f == game.end()) why = rel + " is not one of the game's files";
        else if (to.empty()) why = rel + " could go in more than one place";
        else if (!targets.insert(lower(to)).second) why = "two of its files go to " + to;
        else moves.push_back({rel, to});
    }
    if (!why.empty() || moves.empty()) {
        if (!why.empty()) m.problem = std::string(kNoGameFolders) + " Its files can't be put in place: " + why + ".";
        return;
    }
    m.moves = std::move(moves);
    m.why = Problem::NotInPlace;
    m.problem = "Its files aren't in the right folders yet, but the game's own files show where each one goes.";
}

std::string place(const Mod& m) {
    for (const Move& mv : m.moves) {
        const fs::path from = m.folder / from_utf8(mv.from), to = m.folder / from_utf8(mv.to);
        std::error_code ec;
        if (fs::exists(to, ec)) return mv.to + " is there already";
        fs::create_directories(to.parent_path(), ec);
        fs::rename(from, to, ec);
        if (ec) return "could not move " + mv.from + " (" + ec.message() + ")";
        // The folders it leaves empty, up to the mod's folder.
        for (fs::path d = from.parent_path(); d.has_relative_path() && d != d.parent_path(); d = d.parent_path()) {
            const std::string rel = utf8(d.lexically_relative(m.folder));
            if (rel.empty() || rel == "." || starts_with(rel, "..")) break;
            std::error_code e2;
            if (!fs::is_empty(d, e2) || e2) break;
            fs::remove(d, e2);
        }
    }
    std::fprintf(stderr, "[mods] %s: %zu files put in place\n", m.name.c_str(), m.moves.size());
    return {};
}

std::vector<Entry> read_order(const fs::path& modsFolder, bool* found) {
    std::vector<Entry> out;
    if (found) *found = false;
    if (modsFolder.empty()) return out;
    std::ifstream in(modsFolder / kOrderFile, std::ios::binary);
    if (!in) return out;
    if (found) *found = true;
    std::set<std::string> seen;
    std::string line;
    for (bool first = true; std::getline(in, line); first = false) {
        if (first && starts_with(line, "\xEF\xBB\xBF")) line.erase(0, 3);   // (a byte order mark, as Notepad may write)
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        std::size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos || line[a] == '#') continue;
        const std::size_t b = line.find_first_of(" \t", a);
        if (b == std::string::npos) continue;
        const std::string word = lower(line.substr(a, b - a));
        if (word != "on" && word != "off") continue;
        const std::size_t c = line.find_first_not_of(" \t", b);
        if (c == std::string::npos) continue;
        std::string name = line.substr(c);
        if (!seen.insert(name).second) continue;
        out.push_back({std::move(name), word == "on"});
    }
    return out;
}

bool write_order(const fs::path& modsFolder, const std::vector<Entry>& order) {
    if (modsFolder.empty()) return false;
    std::error_code ec;
    fs::create_directories(modsFolder, ec);
    const fs::path file = modsFolder / kOrderFile, part = modsFolder / (std::string(kOrderFile) + ".new");
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << "# The mods, the first one first. When two mods that are on change the same file, the higher one wins.\n"
               "# One line per mod: \"on\" or \"off\", a space, then the mod's folder name. Folders not listed here\n"
               "# come after them, off. The game reads this file when it starts; its Mods window writes it.\n";
        for (const Entry& e : order) out << (e.on ? "on " : "off ") << e.name << '\n';
        out.flush();
        if (!out) {
            out.close();
            fs::remove(part, ec);
            return false;
        }
    }
    fs::rename(part, file, ec);   // (replaces the old file)
    if (ec) {
        fs::remove(part, ec);
        return false;
    }
    return true;
}

std::vector<Entry> entries(const std::vector<Mod>& mods) {
    std::vector<Entry> out;
    for (const Mod& m : mods) out.push_back({m.name, m.enabled});
    return out;
}

std::vector<Mod> scan(const fs::path& modsFolder) {
    std::vector<Mod> out;
    if (modsFolder.empty() || !is_dir(modsFolder)) return out;
    std::vector<fs::path> subs = subfolders(modsFolder);   // (A to Z)
    std::vector<bool> taken(subs.size(), false);
    for (const Entry& e : read_order(modsFolder)) {
        for (std::size_t i = 0; i < subs.size(); ++i) {
            if (taken[i] || utf8(subs[i].filename()) != e.name) continue;
            taken[i] = true;
            Mod m = read(subs[i]);
            m.enabled = e.on;
            out.push_back(std::move(m));
            break;
        }
    }
    for (std::size_t i = 0; i < subs.size(); ++i)
        if (!taken[i]) out.push_back(read(subs[i]));   // (not in the file: new, off)
    return out;
}

std::vector<Mod> scan(const fs::path& modsFolder, const std::string& enabledList) {
    std::vector<Mod> out = scan(modsFolder);
    const std::vector<std::string> on = split(enabledList, '|');
    for (Mod& m : out) m.enabled = std::find(on.begin(), on.end(), m.name) != on.end();
    return out;
}

bool adopt_setting(const fs::path& modsFolder, const std::string& enabledList) {
    bool found = false;
    read_order(modsFolder, &found);
    if (found || modsFolder.empty() || !is_dir(modsFolder)) return false;
    const std::vector<std::string> on = split(enabledList, '|');
    std::vector<Entry> order;
    for (const fs::path& sub : subfolders(modsFolder)) {
        const std::string name = utf8(sub.filename());
        order.push_back({name, std::find(on.begin(), on.end(), name) != on.end()});
    }
    if (order.empty() || !write_order(modsFolder, order)) return false;
    std::fprintf(stderr, "[mods] the mods that were on in settings.ini (DES_MODS) are now in %s\n",
                 utf8(modsFolder / kOrderFile).c_str());
    return true;
}

namespace {

// Each game file the mods have (lowercase, below their roots), with the mods that have it, in the mods' order.
std::map<std::string, std::vector<std::string>> owners_of(const std::vector<Mod>& mods) {
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
            // (notes and such beside the folders or anywhere)
            if (low.find('/') == std::string::npos || note_file(low) ||
                junk_file(low, lower(utf8(it->path().filename()))))
                continue;
            owners[low].push_back(m.name);
        }
    }
    return owners;
}

}  // namespace

std::vector<std::string> conflicts(const std::vector<Mod>& mods, std::size_t max) {
    std::vector<std::string> out;
    for (const auto& [file, names] : owners_of(mods)) {
        if (names.size() < 2) continue;
        if (out.size() == max) {
            out.push_back("...");
            break;
        }
        out.push_back(file + ": " + join(names, ", "));
    }
    return out;
}

std::vector<Overlap> overlaps(const std::vector<Mod>& mods) {
    std::map<std::pair<std::string, std::string>, std::size_t> count;   // (covered, by) -> files
    for (const auto& [file, names] : owners_of(mods))
        for (std::size_t k = 1; k < names.size(); ++k) ++count[{names[k], names[0]}];
    std::vector<Overlap> out;
    for (const Mod& m : mods)   // (in the mods' order)
        for (const auto& [pair, files] : count)
            if (pair.first == m.name) out.push_back({pair.first, pair.second, files});
    return out;
}

void apply(const fs::path& dataDir, const std::optional<std::string>& enabledList) {
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
        const std::vector<Mod> all = enabledList ? scan(dir, *enabledList) : scan(dir);
        std::vector<std::string> named;   // (the mods said to be on)
        if (enabledList) named = split(*enabledList, '|');
        else
            for (const Entry& e : read_order(dir))
                if (e.on) named.push_back(e.name);
        std::vector<std::string> roots;
        for (const Mod& m : all) {
            if (!m.enabled) continue;
            if (!m.usable()) {
                std::fprintf(stderr, "[mods] %s: not used (%s): %s\n", m.name.c_str(), problem_label(m.why),
                             m.problem.c_str());
                continue;
            }
            roots.push_back(utf8(m.root));
            sInUse.push_back(m);
        }
        for (const std::string& name : named) {
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
