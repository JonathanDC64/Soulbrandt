// Written for Demon's Souls. See save_offer.hpp.
#include "save_offer.hpp"

#include "app.hpp"
#include "crash_report.hpp"
#include "path_utf8.hpp"
#include "report.hpp"
#include "settings.hpp"
#include "setup_engine.hpp"
#include "setup_folder.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <optional>
#include <vector>

namespace ui::saves {
namespace {

namespace fs = std::filesystem;

bool env_on(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && std::strcmp(v, "0") != 0;
}

// DES_SAVE_COPY_FROM as a path (UTF-8; on Windows read wide, so a name in other letters stays whole).
fs::path copy_from_env() {
#ifdef _WIN32
    const wchar_t* v = _wgetenv(L"DES_SAVE_COPY_FROM");
    return v && *v ? fs::path(v) : fs::path();
#else
    const char* v = std::getenv("DES_SAVE_COPY_FROM");
    return v && *v ? fs::path(std::u8string(v, v + std::strlen(v))) : fs::path();
#endif
}

std::string now_utc() {
    const std::time_t now = std::time(nullptr);
    char when[32] = "";
    if (const std::tm* t = std::gmtime(&now)) std::strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%SZ", t);
    return when;
}

std::vector<fs::path> never_list() {
    const fs::path data = report::data_dir();
    return data.empty() ? std::vector<fs::path>{} : setup::game_folders(setup::read_text(data / setup::kNeverCopyFile));
}

std::optional<CopyResult> sLaunchCopy;
fs::path sLaunchCopyFrom;

}  // namespace

fs::path home() {
    return report::saves_pick().dir;
}

bool missing() {
    if (settings::harness_run() || std::getenv("PS3_SAVEDATA_ROOT") || std::getenv("PS3_HDD0_ROOT") ||
        env_on("DES_KEEP_CWD"))
        return false;
    const setup::Pick pick = report::saves_pick();
    return !pick.dir.empty() && pick.source != setup::FolderSource::Old && !setup::holds_saves(pick.dir);
}

Offer offer() {
    Offer o;
    const fs::path data = report::data_dir();
    if (!missing() || data.empty()) return o;
    const std::vector<fs::path> never = never_list();
    const fs::path here = home();
    if (setup::listed(never, here)) return o;
    o.folder = setup::save_offer(setup::game_folders(setup::read_text(data / setup::kGameFoldersFile)), never, here);
    if (!o.folder.empty()) {
        o.kind = Offer::Kind::Folder;
        return o;
    }
    // An earlier version ran on this PC: its settings or its build are where it kept them. Its game folder may be one
    // that game-folders.txt never named (a version before 0.2.0 did not write it) or one that was moved.
    const bool earlier = report::settings_pick().source == setup::FolderSource::Old ||
                         setup::same_folder(setup::usual_data_dir(), setup::old_shared_folder(report::user_folders()));
    if (earlier) o.kind = Offer::Kind::Find;
    return o;
}

void never(const fs::path& folder) {
    std::string why;
    if (setup::never_copy_from(report::data_dir(), folder, now_utc(), why))
        std::fprintf(stderr, "[saves] never offered again: %s\n", path_utf8(folder).c_str());
    else
        std::fprintf(stderr, "[saves] Never not kept (%s)\n", why.c_str());
}

bool restart_to_copy(const fs::path& folder) {
    std::fprintf(stderr, "[saves] copy from %s: the game starts again to copy them before it boots\n",
                 path_utf8(folder).c_str());
    if (!crash_report::start_again({"DES_SAVE_COPY_FROM=" + path_utf8(folder)}, false)) {
        std::fprintf(stderr, "[saves] the game could not start again: nothing copied\n");
        return false;
    }
    if (app().quit) app().quit();
    return true;
}

void copy_at_launch() {
    const fs::path from = copy_from_env();
    if (from.empty()) return;
    if (!missing()) {
        std::fprintf(stderr, "[saves] DES_SAVE_COPY_FROM=%s: this install has saves, so nothing is copied\n",
                     path_utf8(from).c_str());
        return;
    }
    const fs::path to = home();
    std::error_code ec;
    fs::create_directories(to, ec);
    sLaunchCopyFrom = from;
    sLaunchCopy = copy_saves(from, to);
    if (sLaunchCopy->ok)
        std::fprintf(stderr, "[saves] copied %zu files (%llu bytes) from %s into %s, each checked\n",
                     sLaunchCopy->files, static_cast<unsigned long long>(sLaunchCopy->bytes),
                     path_utf8(from / "gamedata").c_str(), path_utf8(to / "gamedata").c_str());
    else
        std::fprintf(stderr, "[saves] not copied from %s: %s (nothing there changed)\n", path_utf8(from).c_str(),
                     sLaunchCopy->why.c_str());
}

const CopyResult* launch_copy() {
    return sLaunchCopy ? &*sLaunchCopy : nullptr;
}

const fs::path& launch_copy_from() {
    return sLaunchCopyFrom;
}

}  // namespace ui::saves
