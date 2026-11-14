// Settings' rows on their own (settings_catalog.cpp, with settings.cpp and settings_values.cpp; settings_hooks.hpp is
// this file's): every row's setting exists, its choices and numbers are sound, its help is written as the window
// expects (no "(KEY)" line, no "(original)" in a choice), and each row's text, notes and buttons can be made. For each
// disc region, the Language section shows its one row (none, and no heading, on the Japanese disc). No window, no
// game: the defaults only, in a settings file of its own under the system's temporary folder.
//
// Prints each tab's rows, a line per failure and "settings_catalog_check: N rows, M failed"; exit 1 on a failure.
// Built on demand: cmake --build <build> --target settings_catalog_check, then settings_catalog_check.
#include "settings.hpp"
#include "settings_catalog.hpp"
#include "settings_hooks.hpp"
#include "../../des_seamless.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <regex>
#include <set>
#include <string>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

extern "C" void ps3_setenv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value && *value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

// What settings.cpp needs of report.cpp and privacy.cpp: no data folder.
namespace ui::report {
const std::filesystem::path& data_dir() {
    static const std::filesystem::path none;
    return none;
}
void set_data_dir(const std::filesystem::path&) {}
std::string this_pc() { return "test-pc"; }
std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }  // (ASCII paths only here)
std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += char(c);
    return s;
}
FILE* open_file(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
    return _wfopen(path.c_str(), std::wstring(mode, mode + std::strlen(mode)).c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}
bool replace_file(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return !ec;
}
}  // namespace ui::report

namespace ui::privacy {
int version() { return 1; }
void set_needed(bool) {}
}  // namespace ui::privacy

// What settings.cpp needs of seamless co-op (des_seamless.cpp): off.
namespace des::seamless {
Mode mode() { return Mode::Off; }
bool start(const std::string&, std::string*) { return false; }
void held(const std::string&) {}
}  // namespace des::seamless

namespace {
int g_region = 0;
}  // namespace

// settings_hooks.hpp, made up: two pads (one that can't say what it is), two swords, no updates.
namespace ui::catalog::hooks {
std::string escape(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}
void open_url(const char*) {}
void toast(const std::string&, int) {}
int region() { return g_region; }
std::string menu_chord_label() { return "L3 + R3"; }
PadSupport pad_support() { return {"Xbox, PlayStation", "Gamepads the game reads."}; }
std::vector<Pad> connected_pads() { return {{"controller1", "Xbox Wireless Controller"}, {"controller2", ""}}; }
int stick_default_zone() { return 24; }
std::vector<Sword> swords() { return {{settings::get("DES_CURSOR_SWORD"), "The default sword"}, {"other", "Another"}}; }
void apply_scale() {}
void party_size_changed() {}
std::string update_status() { return "Up to date."; }
bool update_possible() { return true; }
bool update_checking() { return false; }
void update_check() {}
std::string update_found_version() { return "9.9.9"; }
void open_update_notes() {}
std::string update_kept_version() { return "0.1.0"; }
void ask_go_back(const std::string&) {}
bool data_dir_known() { return false; }
}  // namespace ui::catalog::hooks

namespace {

namespace catalog = ui::catalog;
namespace settings = ui::settings;

int g_fail = 0;
std::string g_where;  // "Tab > Row"

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL %s: %s\n", g_where.c_str(), what.c_str());
        g_fail++;
    }
}

// The help must not say its own "(KEY)": the window adds it (settings_catalog.hpp).
const std::regex kKeyLine(R"(\(([A-Z][A-Z0-9_]+|[a-z][a-z0-9_]+)\)\s*$)");

void check_kind(const catalog::RowDesc& row, const catalog::ChoiceRow& kind) {
    check(!(kind.live && !kind.choices.empty()), "both fixed and live choices");
    const std::vector<catalog::Choice> list = kind.live ? kind.live(settings::get(row.key)) : kind.choices;
    check(!list.empty(), "no choices");
    std::set<std::string> values;
    int originals = 0;
    for (const catalog::Choice& c : list) {
        check(values.insert(c.value).second, "the value \"" + c.value + "\" twice");
        check(!c.label.empty(), "a choice without a label");
        check(c.label.find("(original)") == std::string::npos,
            "\"" + c.label + "\" says (original): mark it Choice::original instead");
        originals += c.original;
    }
    check(originals <= 1, "more than one original choice");
    if (!kind.live) {  // (the default must be one of the choices, or it shows as "Custom (...)")
        std::string def = settings::get(row.key);
        if (def.empty() && !kind.blank.empty()) def = kind.blank;
        check(values.count(def) == 1, "the default \"" + def + "\" is not one of the choices");
    }
    if (kind.custom) {
        check(!kind.custom->label.empty() && bool(kind.custom->value), "Custom without a label or a value");
        if (kind.custom->value) kind.custom->value();
    }
    if (kind.liveNote) {
        check(!kind.liveNote->note.text.empty() && kind.liveNote->visible, "a live note without text or a test");
        if (kind.liveNote->visible) kind.liveNote->visible();
    }
}

void check_kind(const catalog::RowDesc& row, const catalog::ToggleRow&) {
    const std::string def = settings::get(row.key);
    check(def.empty() || def == "0" || def == "1", "an on/off default of \"" + def + "\"");
}

void check_kind(const catalog::RowDesc&, const catalog::NumberRow& kind) {
    check(kind.min < kind.max, "min " + std::to_string(kind.min) + " is not below max " + std::to_string(kind.max));
    check(kind.step >= 1 && (kind.max - kind.min) % kind.step == 0, "a step that does not reach max");
    check(bool(kind.get) && bool(kind.set), "no get or set");
    if (!kind.get) return;
    const int v = kind.get();
    check(v >= kind.min && v <= kind.max, "its default " + std::to_string(v) + " is out of range");
    if (kind.format) check(!kind.format(v).empty(), "an empty formatted value");
    check(kind.stick == -1 || kind.stick == 0 || kind.stick == 1, "a stick other than 0 or 1");
}

void check_kind(const catalog::RowDesc&, const catalog::TextRow& kind) {
    check(kind.maxLength > 0, "no maximum length");
}

void check_kind(const catalog::RowDesc&, const catalog::InfoRow& kind) {
    kind.value();
}

void check_kind(const catalog::RowDesc&, const catalog::ActionRow&) {}

int check_row(const catalog::TabDesc& tab, const catalog::RowDesc& row) {
    g_where = tab.title + " > " + row.label;
    check(!row.label.empty(), "no label");
    const bool sets = !std::holds_alternative<catalog::InfoRow>(row.kind) && !std::holds_alternative<catalog::ActionRow>(row.kind);
    if (sets) {
        check(!row.key.empty() && settings::find(row.key), "the setting \"" + row.key + "\" is not in settings.cpp's kDefs");
    } else {
        check(row.key.empty(), "a key on a row that changes nothing");
        check(!(row.flags & catalog::kHideKey), "kHideKey on a row without a key");
    }
    const std::string help = row.help();
    check(!help.empty(), "no help");
    check(row.key.empty() || help.find("(" + row.key + ")") == std::string::npos, "the help says (" + row.key + ")");
    check(!std::regex_search(help, kKeyLine), "the help ends in a (KEY) line");
    const std::string full = catalog::help_with_key(row);
    check(row.key.empty() || (row.flags & catalog::kHideKey) || full.ends_with("(" + row.key + ")"),
        "help_with_key adds no (KEY)");
    row.footnote();
    if (row.notes)
        for (const catalog::Note& n : row.notes()) check(!n.text.empty(), "an empty note");
    if (row.locked) {
        row.locked();
        check(!row.lockedValue().empty(), "locked, with no value to show then");
    }
    if (row.buttons)
        for (const catalog::Button& b : row.buttons()) {
            check(!b.text.empty() && bool(b.press), "a button without text or press");
            if (b.disabled) b.disabled();
        }
    std::visit([&](const auto& kind) { check_kind(row, kind); }, row.kind);
    return 1;
}

// The Game tab's Language rows shown on a disc of region r, and its heading.
void check_region(int r, int rows, bool heading) {
    g_region = r;
    g_where = "Game > Language, region " + std::to_string(r);
    for (const catalog::TabDesc& tab : catalog::tabs()) {
        if (tab.title != "Game") continue;
        for (const catalog::SectionDesc& s : tab.sections) {
            if (s.title != "Language") continue;
            check((!s.when || s.when()) == heading, std::string("the heading is ") + (heading ? "hidden" : "shown"));
            int shown = 0;
            for (const catalog::RowDesc& row : s.rows) shown += !row.when || row.when();
            check(shown == rows, std::to_string(shown) + " rows shown, not " + std::to_string(rows));
            return;
        }
    }
    check(false, "no Language section");
}

}  // namespace

int main() {
    // (a settings file of its own, never the player's)
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("settings_catalog_check_" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    ps3_setenv("DES_SETTINGS", (dir / "settings.ini").string().c_str());
    ps3_setenv("DES_DEV_SESSION", "");
    settings::load_and_apply_env();

    int rows = 0;
    std::set<std::string> tabs;
    for (const catalog::TabDesc& tab : catalog::tabs()) {
        g_where = tab.title;
        check(tabs.insert(tab.title).second, "a second tab of that name");
        std::set<std::string> sections;
        int n = 0;
        for (const catalog::SectionDesc& s : tab.sections) {
            check(s.title.empty() || sections.insert(s.title).second, "a second section " + s.title);
            check(!s.rows.empty(), "an empty section " + s.title);
            for (const catalog::RowDesc& row : s.rows) n += check_row(tab, row);
        }
        std::printf("%s: %d rows\n", tab.title.c_str(), n);
        rows += n;
    }
    check_region(0, 1, true);
    check_region(1, 1, true);
    check_region(2, 0, false);
    check_region(3, 1, true);

    std::filesystem::remove_all(dir, ec);
    std::printf("settings_catalog_check: %d rows, %d failed\n", rows, g_fail);
    return g_fail ? 1 : 0;
}
