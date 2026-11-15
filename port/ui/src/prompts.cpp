// Written for Demon's Souls. The menus' prompts (prompts.hpp).
#include "prompts.hpp"

namespace ui::prompts {
namespace {

constexpr int kKeys = static_cast<int>(Key::Count);

// A row per device (Device's order), a button per prompt (Key's order).
constexpr Glyph kGlyphs[4][kKeys] = {
    // Keyboard
    {{"↑ ↓", "arrows"}, {"← →", "arrows"}, {"Enter", "key"}, {"Esc", "key"}, {"P", "key"}, {"H", "key"},
     {"C", "key"}, {"Shift", "key"}, {"Q", "key"}, {"E", "key"}},
    // PlayStation
    {{"↑ ↓", "arrows"}, {"← →", "arrows"}, {"✕", "face ps-cross"}, {"○", "face ps-circle"},
     {"△", "face ps-triangle"}, {"SELECT", "system"}, {"□", "face ps-square"}, {"R3", "stick"},
     {"L1", "shoulder"}, {"R1", "shoulder"}},
    // Xbox
    {{"↑ ↓", "arrows"}, {"← →", "arrows"}, {"A", "face xbox-a"}, {"B", "face xbox-b"}, {"Y", "face xbox-y"},
     {"View", "system"}, {"X", "face xbox-x"}, {"RS", "stick"}, {"LB", "shoulder"}, {"RB", "shoulder"}},
    // Nintendo
    {{"↑ ↓", "arrows"}, {"← →", "arrows"}, {"A", "face"}, {"B", "face"}, {"Y", "face"}, {"−", "system"},
     {"X", "face"}, {"RS", "stick"}, {"L", "shoulder"}, {"R", "shoulder"}},
};

}  // namespace

Glyph glyph(Device device, Key key) noexcept {
    const int d = static_cast<int>(device), k = static_cast<int>(key);
    if (d < 0 || d > 3 || k < 0 || k >= kKeys) return {};
    return kGlyphs[d][k];
}

Device resolve(std::string_view glyphs, int used) noexcept {
    if (glyphs == "keyboard") return Device::Keyboard;
    if (glyphs == "playstation") return Device::PlayStation;
    if (glyphs == "xbox") return Device::Xbox;
    if (glyphs == "nintendo") return Device::Nintendo;
    return used == 1 ? Device::Keyboard : used == 3 ? Device::Xbox : used == 4 ? Device::Nintendo : Device::PlayStation;
}

}  // namespace ui::prompts
