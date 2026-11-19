// Written for Demon's Souls. The menus' prompts: which button does what, shown along the bottom ("✕: Open",
// "○: Back"). Here the buttons each device has for each prompt, as data; a page lists its prompts (Prompt), and the
// look's style sheet draws them. The buttons follow the game's own prompts (button_pages.cpp): a Nintendo pad reads
// A B X Y as an Xbox pad does, as SDL names its buttons, and so does a Steam Deck.
#pragma once

#include <string>
#include <string_view>

namespace ui::prompts {

enum class Device { Keyboard, PlayStation, Xbox, Nintendo, SteamDeck };

// What a prompt asks for. Compare and Hide are held. Right is the right arrow alone (a page's hint for where it goes:
// Mods' Restart now).
enum class Key { Move, Change, Confirm, Back, Preview, Help, Compare, Hide, PrevTab, NextTab, Right, Count };

// A button's picture, drawn the way the game's prompts look (button_art.hpp): one of a pad's buttons by its cellPad
// name (button_pages::kPadNames: "cross", "r1"), the D-pad with some of its ways lit, or a key.
struct Art {
    enum class Is : unsigned char { None, Button, Dpad, Key };
    Is is = Is::None;
    std::string_view name{};   // Button: its cellPad name; Key: the name on its cap ("Enter"; none on an arrow key)
    int code = 0;              // Dpad: the ways lit (bit 0 up, 1 right, 2 down, 3 left); Key: its virtual-key code
};

// A button as a prompt shows it: its picture (one, or two side by side: the keyboard's arrows), and as text its label
// on the key or button and its classes for the look's style sheet ("face ps-cross"). The look draws one or the other.
struct Glyph {
    std::string_view text;
    std::string_view classes;
    Art art[2]{};
};
Glyph glyph(Device device, Key key) noexcept;

// A prompt of a page's: the button and what it does ("Open"). Its kind, when it has one, is a class for the look's
// style sheet: "tabs", Settings' buttons that change its tab (a look that draws those at the tabs' ends hides these).
struct Prompt {
    Key key;
    std::string label;
    std::string_view kind{};
};

// The device the prompts show: the Button icons row's choice (DES_GLYPHS: auto, playstation, xbox, nintendo,
// steamdeck, keyboard), or with auto the one used last (cellPad's PAD_GLYPH_*: 1 the keyboard and mouse, 3 Xbox,
// 4 Nintendo; anything else is the game's own, PlayStation), as the game's prompts choose (button_icons.cpp). On a
// Steam Deck (deck: input.hpp's on_steam_deck), auto shows an Xbox pad as the Deck: its own controls reach the game as
// one, through Steam Input or SDL.
Device resolve(std::string_view glyphs, int used, bool deck = false) noexcept;

}  // namespace ui::prompts
