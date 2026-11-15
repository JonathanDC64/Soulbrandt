// Written for Demon's Souls. The menus' looks (docs/32, "Menu looks"). Every look draws the same documents; what it
// changes is in its own style sheet, res/rml/looks/<id>.rcss, inside @media (theme: <id>), so only the chosen look's
// rules apply. Every document gets every look's sheet (document.cpp), and choosing a look switches RmlUi's theme.
//
// Adding a look: a line in kLooks (released = false until it is finished, so only developer builds offer it) and its
// sheet. The Menu look row (Interface) lists the looks offered; tests/ui_style_check.cpp checks each sheet.
#pragma once

#include <string_view>

namespace ui::looks {

// What a look does that its sheet can't say. A trait is only for that: whatever RCSS can do stays in the sheet.
struct Look {
    std::string_view id;     // the ui_look setting's value, the RmlUi theme's name, and res/rml/looks/<id>.rcss
    std::string_view label;  // its choice in the Menu look row
    bool released;           // false: offered in developer builds only
    bool helpPane;           // Settings shows the help beside the rows; else a line under the band, and Help opens it
    bool listBeside;         // a value's list opens beside the value, at its row; else below the row (settings_page.cpp)
    bool menuHeight;         // Settings' frame is as tall as Interface > Menu height says; else as the sheet says
};

inline constexpr Look kLooks[] = {
    {"today", "Today", true, true, false, true},
    {"band", "Pause band", false, false, true, false},
};

inline constexpr std::string_view kDefault = "today";

// A developer build offers every look; a release, the released ones.
constexpr bool offered(const Look& look) {
#if DES_DEV
    return true;
#else
    return look.released;
#endif
}

// The look of that id, or null.
constexpr const Look* find(std::string_view id) {
    for (const Look& l : kLooks)
        if (l.id == id) return &l;
    return nullptr;
}

// How many looks this build offers: the Menu look row shows only when there is a choice.
constexpr int offered_count() {
    int n = 0;
    for (const Look& l : kLooks) n += offered(l);
    return n;
}

// The look the ui_look setting picks, when this build offers it; else the default.
const Look& current();

// Makes current() the look the menus draw: its theme on in the UI's context, the others off. At the UI's start, and
// when the setting changes.
void apply();

}  // namespace ui::looks
