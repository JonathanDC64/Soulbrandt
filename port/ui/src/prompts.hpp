// Written for Demon's Souls. The menus' prompts: which button does what, shown along the bottom ("✕: Open",
// "○: Back"). Here the buttons each device has for each prompt, as data; a page lists its prompts (Prompt), and the
// look's style sheet draws them. The buttons follow the game's own prompts (button_pages.cpp): a Nintendo pad reads
// A B X Y as an Xbox pad does, as SDL names its buttons.
#pragma once

#include <string>
#include <string_view>

namespace ui::prompts {

enum class Device { Keyboard, PlayStation, Xbox, Nintendo };

// What a prompt asks for. Compare and Hide are held.
enum class Key { Move, Change, Confirm, Back, Preview, Help, Compare, Hide, PrevTab, NextTab, Count };

// A button as a prompt shows it: its text (the label on the key or button), and its classes for the look's style
// sheet ("face ps-cross"), which may draw a picture in its place.
struct Glyph {
    std::string_view text;
    std::string_view classes;
};
Glyph glyph(Device device, Key key) noexcept;

// A prompt of a page's: the button and what it does ("Open").
struct Prompt {
    Key key;
    std::string label;
};

// The device the prompts show: the Button icons row's choice (DES_GLYPHS: auto, playstation, xbox, nintendo,
// keyboard), or with auto the one used last (cellPad's PAD_GLYPH_*: 1 the keyboard and mouse, 3 Xbox, 4 Nintendo;
// anything else is the game's own, PlayStation), as the game's prompts choose (button_icons.cpp).
Device resolve(std::string_view glyphs, int used) noexcept;

}  // namespace ui::prompts
