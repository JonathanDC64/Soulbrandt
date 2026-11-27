// Written for Demon's Souls. A pad's buttons as the menus' keys (menu_pad.hpp).
#include "menu_pad.hpp"

#include "input.hpp"

namespace ui::input {
namespace {

using namespace cell;

constexpr float kStickPress = 0.5f, kStickRelease = 0.37f;

void stick_axis(float v, bool (&held)[2]) noexcept {
    held[0] = held[0] ? v < -kStickRelease : v < -kStickPress;
    held[1] = held[1] ? v > kStickRelease : v > kStickPress;
}

}  // namespace

unsigned through_map(unsigned buttons, const std::array<signed char, 16>& from) noexcept {
    unsigned out = 0;
    for (int g = 0; g < 16; g++)
        if (from[g] >= 0 && (buttons >> from[g]) & 1u) out |= 1u << g;
    return out;
}

unsigned through_added(unsigned host, const std::array<unsigned, 16>& added) noexcept {
    unsigned out = 0;
    for (int g = 0; g < 16; g++)
        if (host & added[g]) out |= 1u << g;
    return out;
}

unsigned chord_mask(int chord) noexcept {
    return chord == 1 ? (kSelect | kStart) : (kL3 | kR3);
}

PadKeys pad_keys(const PadState& pad, int chord, PadKeysState& s) noexcept {
    stick_axis(pad.lx, s.stickX);
    stick_axis(pad.ly, s.stickY);
    const unsigned b = pad.buttons;
    const unsigned mask = chord_mask(chord);
    const bool chordMade = (b & mask) == mask;
    PadKeys k;
    k[kPadUp] = (b & kUp) || s.stickY[0];
    k[kPadDown] = (b & kDown) || s.stickY[1];
    k[kPadLeft] = (b & kLeft) || s.stickX[0];
    k[kPadRight] = (b & kRight) || s.stickX[1];
    k[kPadConfirm] = (b & kCross) != 0;
    k[kPadCancel] = (b & kCircle) != 0;
    k[kPadNext] = (b & (kR1 | kR2)) != 0;
    k[kPadPrev] = (b & (kL1 | kL2)) != 0;
    k[kPadMenu] = (b & kStart) || chordMade;
    k[kPadPreview] = (b & kTriangle) != 0;
    k[kPadCompare] = (b & kSquare) != 0;

    // R3: Hide, unless it is in the chord and L3 is down too (then not until it comes up).
    const bool r3 = (b & kR3) != 0;
    if (!r3) s.r3Spent = false;
    else if ((mask & kR3) && chordMade) s.r3Spent = true;
    k[kPadHide] = r3 && !s.r3Spent && !((mask & kR3) && (b & kL3));

    // Select: Help as it goes down, or, in the chord, a tap as it comes up alone.
    const bool select = (b & kSelect) != 0;
    if (!(mask & kSelect)) {
        k[kPadHelp] = select;
    } else {
        if (select && (b & kStart)) s.selectSpent = true;
        k[kPadHelp] = s.selectWas && !select && !s.selectSpent;  // (down for this one frame)
        if (!select) s.selectSpent = false;
    }
    s.selectWas = select;
    return k;
}

Rml::Input::KeyIdentifier key_for(int key) noexcept {
    using K = Rml::Input::KeyIdentifier;
    static constexpr K kKeys[kPadKeyCount] = {
        K::KI_UP,    K::KI_DOWN, K::KI_LEFT, K::KI_RIGHT, K::KI_RETURN, K::KI_ESCAPE, K::KI_NEXT,
        K::KI_PRIOR, K::KI_F1,   K::KI_P,    K::KI_H,     K::KI_C,      K::KI_LSHIFT,
    };
    return key >= 0 && key < kPadKeyCount ? kKeys[key] : K::KI_UNKNOWN;
}

bool pad_key_repeats(int key) noexcept {
    return key == kPadUp || key == kPadDown || key == kPadLeft || key == kPadRight || key == kPadNext || key == kPadPrev;
}

}  // namespace ui::input
