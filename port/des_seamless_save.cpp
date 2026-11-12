// Written for Demon's Souls. Seamless co-op's own save (des_seamless.h): where it is, and bringing one of the
// player's normal characters into it, once.
//
// The game's save folder (the disc's TITLE_ID and DEMONSS005: BLUS30443DEMONSS005) holds four character slots and a
// slot list:
//   <g><kk>USER.DAT   slot k's character. kk is "" for slot 0, else "01" to "03"; g is the generation, "", "1" or
//                     "2". The game keeps two generations and loads the newer.
//   <g>04USER.DAT     the slot list, which the game's Profile List shows. Per slot: a name at 0x21C + k * 0x140
//                     (UTF-16, 16 characters) and the soul level 0x24 after it; and at 0x20C + k, one byte that is 1
//                     when the slot holds a character (a deleted character keeps its name there, with a 0).
// Bringing slot k copies that slot's files as they are, the slot list with only k marked, and the folder's other
// files (PARAM.SFO, the pictures), each with its time. Checked in the game: the Profile List then shows that one
// character, with "No profile data found." in the other three slots, and the character loads and saves as usual.
// The normal save is only read.
#include "des_seamless.h"

#include "des_region.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace des::seamless {
namespace {

namespace fs = std::filesystem;

constexpr const char* kNormalRoot = "gamedata/dev_hdd0/home/00000001/savedata";
constexpr const char* kCoopRoot = "gamedata/dev_hdd0/home/00000001/savedata-coop";
constexpr int kSlots = 4;
constexpr int kListSlot = 4;   // the slot list's files are named as a fifth slot's
constexpr size_t kListUsed = 0x20C, kListName = 0x21C, kListStride = 0x140, kListLevel = 0x24, kNameChars = 16;
constexpr uint32_t kListKind = 7, kCharacterKind = 0x19;   // the files' first word

fs::path upath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string utf8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string slot_file(int g, int k) {
    static const char* const kGen[] = {"", "1", "2"};
    char kk[4] = "";
    if (k) std::snprintf(kk, sizeof kk, "%02d", k);
    return std::string(kGen[g]) + kk + "USER.DAT";
}

bool read_file(const fs::path& p, std::vector<uint8_t>* out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

uint32_t be32(const std::vector<uint8_t>& b, size_t at) {
    if (at + 4 > b.size()) return 0;
    return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3];
}

bool given_root() {
    const char* v = std::getenv("PS3_SAVEDATA_ROOT");
    return v && *v;
}

// The game's save folder in a save root, for the running disc.
fs::path save_folder(const fs::path& root) {
    return root / (std::string(des_region_title_id(des_region())) + "DEMONSS005");
}

bool has_save(const fs::path& root) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        std::error_code e2;
        if (e.is_directory(e2) && fs::exists(e.path() / "PARAM.SFO", e2)) return true;
    }
    return false;
}

// The slot list the game reads: of the generations there, the newer of the two it keeps (it writes them in turn,
// "" then "1" then "2", and drops the oldest).
bool read_list(const fs::path& dir, std::vector<uint8_t>* out) {
    std::vector<uint8_t> b[3];
    bool ok[3];
    for (int g = 0; g < 3; g++)
        ok[g] = read_file(dir / slot_file(g, kListSlot), &b[g]) && be32(b[g], 0) == kListKind &&
                b[g].size() >= kListName + kSlots * kListStride;
    for (int g = 0; g < 3; g++)
        if (ok[g] && !ok[(g + 1) % 3]) {   // g's next isn't there: g is the newer of the two
            *out = std::move(b[g]);
            return true;
        }
    for (int g = 0; g < 3; g++)
        if (ok[g]) {
            *out = std::move(b[g]);
            return true;
        }
    return false;
}

bool has_character(const fs::path& dir, int k) {
    for (int g = 0; g < 3; g++) {
        std::vector<uint8_t> b;
        if (read_file(dir / slot_file(g, k), &b) && be32(b, 0) == kCharacterKind) return true;
    }
    return false;
}

std::string list_name(const std::vector<uint8_t>& b, int k) {
    std::string name;
    const size_t at = kListName + k * kListStride;
    for (size_t i = 0; i < kNameChars; i++) {
        const uint32_t c = uint32_t(b[at + 2 * i]) << 8 | b[at + 2 * i + 1];
        if (!c) break;
        if (c < 0x80) {
            name += char(c);
        } else if (c < 0x800) {
            name += char(0xC0 | c >> 6);
            name += char(0x80 | (c & 0x3F));
        } else {   // (no surrogate pairs in the game's names)
            name += char(0xE0 | c >> 12);
            name += char(0x80 | (c >> 6 & 0x3F));
            name += char(0x80 | (c & 0x3F));
        }
    }
    return name;
}

bool copy_with_time(const fs::path& from, const fs::path& to, std::error_code& ec) {
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    fs::last_write_time(to, fs::last_write_time(from, ec), ec);
    return !ec;
}

}  // namespace

const char* coop_save_root() { return kCoopRoot; }

std::vector<Character> characters_to_bring() {
    std::vector<Character> out;
    // A given save root is co-op's too (start() leaves it), so there is nothing to bring; nor once co-op has its own.
    if (given_root() || des_region_asian() || has_save(kCoopRoot)) return out;
    const fs::path dir = save_folder(kNormalRoot);
    std::vector<uint8_t> list;
    if (!read_list(dir, &list)) return out;
    for (int k = 0; k < kSlots; k++) {
        if (list[kListUsed + k] != 1 || !has_character(dir, k)) continue;
        Character c;
        c.slot = k;
        c.name = list_name(list, k);
        c.level = static_cast<int>(be32(list, kListName + k * kListStride + kListLevel));
        if (c.name.empty()) c.name = "Slot " + std::to_string(k + 1);
        out.push_back(std::move(c));
    }
    return out;
}

std::string bring_character(int slot) {
    if (slot < 0 || slot >= kSlots) return "That isn't one of your characters.";
    if (given_root() || has_save(kCoopRoot)) return "Co-op already has its own characters.";
    const fs::path from = save_folder(kNormalRoot);
    const fs::path to = save_folder(kCoopRoot);
    const fs::path part = fs::path(kCoopRoot) / (".bringing-" + utf8(to.filename()));
    std::error_code ec;
    fs::remove_all(part, ec);   // (this function's own unfinished copy, from a game that ended during one)
    ec.clear();
    if (!fs::create_directories(part, ec) || ec) return "Could not make the co-op save folder.";

    std::vector<std::string> copied;
    bool character = false, list = false;
    std::string pfd;
    for (const auto& e : fs::directory_iterator(from, ec)) {
        std::error_code e2;
        if (!e.is_regular_file(e2)) continue;
        const std::string name = utf8(e.path().filename());
        if (name.empty() || name[0] == '.') continue;   // (the runtime's files being written)
        bool mine = false, slot_list = false, any_slot = false;
        for (int g = 0; g < 3; g++) {
            mine = mine || name == slot_file(g, slot);
            slot_list = slot_list || name == slot_file(g, kListSlot);
            for (int k = 0; k < kSlots; k++) any_slot = any_slot || name == slot_file(g, k);
        }
        if (name == "PARAM.PFD") {
            std::vector<uint8_t> b;
            if (read_file(e.path(), &b)) pfd.assign(b.begin(), b.end());
            continue;   // written last, with only the files copied
        }
        if (slot_list) {
            std::vector<uint8_t> b;
            if (!read_file(e.path(), &b) || be32(b, 0) != kListKind || b.size() < kListName + kSlots * kListStride)
                continue;
            for (int k = 0; k < kSlots; k++) b[kListUsed + k] = k == slot ? 1 : 0;
            const fs::path out = part / e.path().filename();
            {
                std::ofstream f(out, std::ios::binary | std::ios::trunc);
                f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
                if (!f.flush()) return "Could not write the co-op save.";
            }
            fs::last_write_time(out, fs::last_write_time(e.path(), e2), e2);
            list = true;
        } else if (mine) {
            if (!copy_with_time(e.path(), part / e.path().filename(), e2)) return "Could not copy your character.";
            character = true;
        } else if (any_slot) {
            continue;   // another character's
        } else if (!copy_with_time(e.path(), part / e.path().filename(), e2)) {
            return "Could not copy the save's files.";
        }
        copied.push_back(name);
    }
    if (ec) return "Could not read your save.";
    if (!character || !list) return "Your save doesn't have that character.";
    fs::remove(to, ec);   // (an empty folder of that name; one with files in it stays, and the rename below fails)
    ec.clear();
    if (!pfd.empty()) {   // the runtime's list of the save's secure files, one a line: the ones copied
        std::string kept, line;
        for (size_t i = 0; i <= pfd.size(); i++) {
            if (i < pfd.size() && pfd[i] != '\n') {
                if (pfd[i] != '\r') line += pfd[i];
                continue;
            }
            for (const std::string& c : copied)
                if (line == c) kept += line + "\n";
            line.clear();
        }
        std::ofstream f(part / "PARAM.PFD", std::ios::binary | std::ios::trunc);
        f << kept;
        if (!f.flush()) return "Could not write the co-op save.";
    }
    fs::rename(part, to, ec);
    if (ec) return "Could not finish the co-op save.";
    std::fprintf(stderr, "[coop] slot %d's character is now in the co-op save too (%zu files)\n", slot + 1,
                 copied.size() + (pfd.empty() ? 0 : 1));
    return "";
}

}  // namespace des::seamless
