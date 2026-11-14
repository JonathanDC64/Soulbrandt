// What Settings' rows (settings_catalog.cpp, settings_values.cpp) read or do outside settings.cpp: the gamepads, the
// game's swords, updates, the window's scale. settings_hooks.cpp forwards each to the UI; tests/settings_catalog_check.cpp
// has its own, so the table is checked without RmlUi, a window or a game.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ui::catalog::hooks {

std::string escape(std::string_view text);  // ui::escape: text made safe in RML
void open_url(const char* url);
void toast(const std::string& text, int seconds);

int region();  // des_region.h: 0 the US disc, 1 the European, 2 the Japanese, 3 the Asian

// Controls
std::string menu_chord_label();  // the gamepad buttons that open the menu ("L3 + R3")
struct PadSupport {
    std::string value, help;  // help: RML
};
PadSupport pad_support();
struct Pad {
    std::string id;    // PAD_CONTROLLER's value for it ("controller1")
    std::string name;  // what it is ("DualSense (USB)"); "" when it can't say
};
std::vector<Pad> connected_pads();
int stick_default_zone();  // the deadzone, in percent, the connected controller has without one set

// Interface
struct Sword {
    std::string name, label;
};
std::vector<Sword> swords();
void apply_scale();

// Network
void party_size_changed();  // Players when you host: a host takes the new size at once

// About
std::string update_status();
bool update_possible();
bool update_checking();
void update_check();
std::string update_found_version();  // "" when no newer release was found
void open_update_notes();
std::string update_kept_version();   // the version an update kept to go back to; ""
void ask_go_back(const std::string& version);
bool data_dir_known();  // logs and reports have a folder (report::data_dir)

}  // namespace ui::catalog::hooks
