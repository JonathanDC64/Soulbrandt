// Written for Demon's Souls. A pad's buttons as the menus' keys, worked out alone (RmlUi's key names only, no clock),
// so a check can run it (tests/input_map_check.cpp). input.cpp turns what it holds into key presses, with repeat.
//
// - D-pad or left stick: the arrows (the stick with some give, so it doesn't flicker at the threshold); Cross: Enter;
//   Circle: Escape; R1/R2 and L1/L2: the next and previous tab; Start or the menu chord: F1.
// - Triangle: Preview, Square: Compare (held), Select: Help, R3: Hide (held).
// - A button of the menu chord (L3 + R3, or Select + Start) does its own thing only alone: R3 hides while L3 isn't
//   down, and stops once the chord is made; Select's Help is a tap, given as Select comes up, unless Start went down
//   while it was held. What the chord made stays done until its buttons come up.
// - With Same buttons as the game on (input.hpp's menu_buttons), the buttons come in as the game reads them through
//   Change Buttons (through_map), so each of these is the game's button of that name, wherever the player put it.
#pragma once

#include <RmlUi/Core/Input.h>

#include <array>
#include <bitset>

namespace ui::input {

struct PadState;

// CELL_PAD_CTRL_* (cellPad.h)
namespace cell {
constexpr unsigned kSelect = 1u << 0, kL3 = 1u << 1, kR3 = 1u << 2, kStart = 1u << 3;
constexpr unsigned kUp = 1u << 4, kRight = 1u << 5, kDown = 1u << 6, kLeft = 1u << 7;
constexpr unsigned kL2 = 1u << 8, kR2 = 1u << 9, kL1 = 1u << 10, kR1 = 1u << 11;
constexpr unsigned kTriangle = 1u << 12, kCircle = 1u << 13, kCross = 1u << 14, kSquare = 1u << 15;
}  // namespace cell

enum PadKey {
    kPadUp,
    kPadDown,
    kPadLeft,
    kPadRight,
    kPadConfirm,
    kPadCancel,
    kPadNext,
    kPadPrev,
    kPadMenu,
    kPadPreview,
    kPadHelp,
    kPadCompare,
    kPadHide,
    kPadKeyCount
};

using PadKeys = std::bitset<kPadKeyCount>;

// What pad_keys keeps between frames.
struct PadKeysState {
    bool stickX[2] = {}, stickY[2] = {};  // left/right, up/down held
    bool r3Spent = false;      // R3 made the chord: no Hide until it comes up
    bool selectSpent = false;  // Start went down while Select was held: no Help as it comes up
    bool selectWas = false;    // Select was down last frame
};

// The keys the pad holds down this frame. chord: 0 = L3 + R3, 1 = Select + Start (input.hpp's chord_mask).
PadKeys pad_keys(const PadState& pad, int chord, PadKeysState& state) noexcept;

// A pad's buttons as the game reads them: game button g (a CELL_PAD_CTRL_* bit) is down when pad button from[g] is;
// -1, nothing presses it (the Kick's button, with the Kick on). from[g] = g for every g gives the buttons back.
unsigned through_map(unsigned buttons, const std::array<signed char, 16>& from) noexcept;

// Whether a held key repeats (the arrows and the tabs).
bool pad_key_repeats(int key) noexcept;

// The key a pad's key presses (Preview: P, Help: H, Compare: C, Hide: Shift; menu_input.hpp reads them).
Rml::Input::KeyIdentifier key_for(int key) noexcept;

}  // namespace ui::input
