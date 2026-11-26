// Written for Demon's Souls. The keys as the F1 menu and its pages read them: one command per key, the same from the
// keyboard and the pads (input.cpp turns a pad's buttons into these keys). Worked out alone (no event, no document),
// so a check can run it (tests/input_map_check.cpp). The other documents keep ui.cpp's map_nav_event.
//
//   Command   Keyboard           PlayStation   Xbox
//   Confirm   Enter              Cross         A
//   Back      Esc                Circle        B
//   Menu      F1                 Start, chord  Menu, chord
//   Tabs      Q / E, PgUp/PgDn   L1 / R1       LB / RB
//   Preview   P                  Triangle      Y
//   Help      H                  Select        View
//   Compare   C (held)           Square        X
//   Hide      Shift (held)       R3            RS
//
// With Same buttons as the game on (input.hpp's menu_buttons and menu_key), the PlayStation and Xbox columns are the
// game's buttons wherever Change Buttons put them, and the keys for Cross and Circle (E and Space in the Souls layout)
// come in as Enter and Esc.
#pragma once

#include <RmlUi/Core/Input.h>

#include <vector>

namespace ui::menu {

enum class Command { None, Up, Down, Left, Right, Confirm, Back, Menu, PrevTab, NextTab, Preview, Help, Compare, Hide };

// Compare and Hide act while their key is down; the others as it goes down.
constexpr bool held(Command c) noexcept {
    return c == Command::Compare || c == Command::Hide;
}

// A key's command. typing: a text field has the focus, and Left, Right, the letters and Shift are its own.
Command command_for_key(Rml::Input::KeyIdentifier key, bool shift, bool typing) noexcept;

// The held commands, from their keys going down and up. Two keys can hold one command (either Shift); it stops when
// the last comes up.
class Holds {
public:
    // A key went down or up: whether its command's state changed.
    bool down(Rml::Input::KeyIdentifier key, Command c);
    bool up(Rml::Input::KeyIdentifier key);
    bool held(Command c) const noexcept;
    // Lets go of everything (the window lost the focus: keys held then may not come up). Whether anything was held.
    bool release_all() noexcept;

private:
    struct Held {
        Rml::Input::KeyIdentifier key;
        Command command;
    };
    std::vector<Held> mHeld;
};

}  // namespace ui::menu
