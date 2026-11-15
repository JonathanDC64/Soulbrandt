// Written for Demon's Souls. What a Settings row shows and does, worked out from its description (settings_catalog.hpp)
// and the settings as they are now: the value it shows, whether Left and Right change it, what Enter does and lists,
// and the facts its help ends with. No RmlUi: the Settings page (settings_page.cpp) draws it in the look chosen, and
// tests/settings_catalog_check.cpp runs it over every row.
#pragma once

#include "settings_catalog.hpp"

#include <string>
#include <vector>

namespace ui::rows {

using catalog::RowDesc;

// The value a row shows.
struct Value {
    std::string text;       // "Vulkan", "On", "60 fps", "Custom (90)"
    bool original = false;  // the game's own choice, shown with "(original)"
    bool first = false;     // Left can't go further (the first choice, Off, the smallest number)
    bool last = false;      // nor Right
};
Value value(const RowDesc& row);

bool modified(const RowDesc& row);  // not at its default
bool locked(const RowDesc& row);    // set from outside (pinned), or locked by the row itself
bool waits(const RowDesc& row);     // restart-only, changed since the game started (settings::restart_pending)

// Left and Right change it: a choice, on or off, or a number, unless it is locked.
bool steps(const RowDesc& row);
// Left (-1) or Right (+1): whether the value changed (not at an end, not locked). Runs the row's changed().
bool step(const RowDesc& row, int dir);

// What Enter does: lists the choices (or a row's buttons), lets the value be typed, opens a page (Controls), or
// presses the row's one button (Open the website).
enum class Enter { None, List, Edit, Open, Press };
Enter enter(const RowDesc& row);
std::string enter_label(const RowDesc& row);  // its prompt: "List", "Type", "Open", or the button's text

// What Enter lists: a choice row's choices (with its Custom one), Off and On, or the row's buttons.
struct Item {
    std::string label;
    bool original = false;  // the game's own
    bool current = false;   // the value in use
    bool disabled = false;  // a button that can't be pressed now
    bool button = false;    // pressed, not chosen
    catalog::Sound sound = catalog::Sound::None;  // a button's
};
std::vector<Item> items(const RowDesc& row);
// The index-th of them chosen, or pressed: whether anything happened.
bool pick(const RowDesc& row, int index);

// Typing a value (Enter::Edit): the text the field starts with, and what was typed set (a number clamped to its
// range). Whether the value changed.
std::string edit_text(const RowDesc& row);
bool commit_edit(const RowDesc& row, const std::string& typed);
int edit_limit(const RowDesc& row);  // the most characters, -1 any

// Preview shows it on the game (catalog::kPreview), unless it is locked.
bool previewable(const RowDesc& row);
// Compare can show the value the Settings page opened with on the game: applied at once, not locked, not the window.
bool comparable(const RowDesc& row);

// The help's facts: when a change applies ("At once", "After a restart", "Set at start"; "" for a row with no
// setting) and why the row can't change ("" when it can).
std::string applies(const RowDesc& row);
std::string lock_note(const RowDesc& row);

}  // namespace ui::rows
