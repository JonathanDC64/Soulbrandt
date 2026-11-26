// Written for Demon's Souls. The menus' prompts (prompts.hpp).
#include "prompts.hpp"

#include <iterator>

namespace ui::prompts {
namespace {

constexpr int kKeys = static_cast<int>(Key::Count);

constexpr Art button(std::string_view name) { return {Art::Is::Button, name, 0}; }
constexpr Art dpad(int ways) { return {Art::Is::Dpad, {}, ways}; }
constexpr Art key(int vk, std::string_view cap = {}) { return {Art::Is::Key, cap, vk}; }
constexpr int kUpDown = 1 | 4, kLeftRight = 2 | 8, kRight = 2;   // the D-pad's ways (Art::code)

// The virtual-key codes of the menus' keys (menu_input.cpp's map: the same keys on every platform).
constexpr int kVkEnter = 0x0D, kVkShift = 0x10, kVkEsc = 0x1B;
constexpr int kVkLeft = 0x25, kVkUp = 0x26, kVkRight = 0x27, kVkDown = 0x28;
constexpr int kVkPageUp = 0x21, kVkPageDown = 0x22;

// A row per device (Device's order), a button per prompt (Key's order). The pads' pictures name the same buttons (a
// pad draws its own letters on them, by where the button sits: Cross, the bottom one, is A on an Xbox pad and a Steam
// Deck, and B on a Nintendo pad).
constexpr Glyph kGlyphs[][kKeys] = {
    // Keyboard
    {{"↑ ↓", "arrows up-down", {key(kVkUp), key(kVkDown)}},
     {"← →", "arrows left-right", {key(kVkLeft), key(kVkRight)}},
     {"Enter", "key", {key(kVkEnter, "Enter")}}, {"Esc", "key", {key(kVkEsc, "Esc")}},
     {"P", "key", {key('P', "P")}}, {"H", "key", {key('H', "H")}}, {"C", "key", {key('C', "C")}},
     {"Shift", "key", {key(kVkShift, "Shift")}}, {"Q", "key", {key('Q', "Q")}}, {"E", "key", {key('E', "E")}},
     {"→", "arrows right", {key(kVkRight)}}},
    // PlayStation
    {{"↑ ↓", "arrows up-down", {dpad(kUpDown)}}, {"← →", "arrows left-right", {dpad(kLeftRight)}},
     {"✕", "face ps-cross", {button("cross")}}, {"○", "face ps-circle", {button("circle")}},
     {"△", "face ps-triangle", {button("triangle")}}, {"SELECT", "system", {button("select")}},
     {"□", "face ps-square", {button("square")}}, {"R3", "stick", {button("r3")}},
     {"L1", "shoulder", {button("l1")}}, {"R1", "shoulder", {button("r1")}}, {"→", "arrows right", {dpad(kRight)}}},
    // Xbox
    {{"↑ ↓", "arrows up-down", {dpad(kUpDown)}}, {"← →", "arrows left-right", {dpad(kLeftRight)}},
     {"A", "face xbox-a", {button("cross")}}, {"B", "face xbox-b", {button("circle")}},
     {"Y", "face xbox-y", {button("triangle")}}, {"View", "system", {button("select")}},
     {"X", "face xbox-x", {button("square")}}, {"RS", "stick", {button("r3")}},
     {"LB", "shoulder", {button("l1")}}, {"RB", "shoulder", {button("r1")}}, {"→", "arrows right", {dpad(kRight)}}},
    // Nintendo
    {{"↑ ↓", "arrows up-down", {dpad(kUpDown)}}, {"← →", "arrows left-right", {dpad(kLeftRight)}},
     {"B", "face", {button("cross")}}, {"A", "face", {button("circle")}}, {"X", "face", {button("triangle")}},
     {"−", "system", {button("select")}}, {"Y", "face", {button("square")}}, {"RS", "stick", {button("r3")}},
     {"L", "shoulder", {button("l1")}}, {"R", "shoulder", {button("r1")}}, {"→", "arrows right", {dpad(kRight)}}},
    // Steam Deck
    {{"↑ ↓", "arrows up-down", {dpad(kUpDown)}}, {"← →", "arrows left-right", {dpad(kLeftRight)}},
     {"A", "face", {button("cross")}}, {"B", "face", {button("circle")}}, {"Y", "face", {button("triangle")}},
     {"View", "system", {button("select")}}, {"X", "face", {button("square")}}, {"R3", "stick", {button("r3")}},
     {"L1", "shoulder", {button("l1")}}, {"R1", "shoulder", {button("r1")}}, {"→", "arrows right", {dpad(kRight)}}},
};
static_assert(std::size(kGlyphs) == static_cast<size_t>(Device::SteamDeck) + 1, "a row per device");

}  // namespace

Glyph glyph(Device device, Key key) noexcept {
    const int d = static_cast<int>(device), k = static_cast<int>(key);
    if (d < 0 || d >= int(std::size(kGlyphs)) || k < 0 || k >= kKeys) return {};
    return kGlyphs[d][k];
}

Glyph button_glyph(Device device, std::string_view name) noexcept {
    const int d = static_cast<int>(device);
    if (device == Device::Keyboard || d < 0 || d >= int(std::size(kGlyphs))) return {};
    // The buttons a prompt already shows on this pad (the face buttons, L1, R1, Select, R3).
    for (const Glyph& g : kGlyphs[d])
        if (g.art[0].is == Art::Is::Button && g.art[1].is == Art::Is::None && g.art[0].name == name) return g;
    // The others, a row per pad device (Device's order from PlayStation): L2, R2, L3, Start.
    static constexpr Glyph kOthers[][4] = {
        {{"L2", "shoulder", {button("l2")}}, {"R2", "shoulder", {button("r2")}}, {"L3", "stick", {button("l3")}},
         {"START", "system", {button("start")}}},
        {{"LT", "shoulder", {button("l2")}}, {"RT", "shoulder", {button("r2")}}, {"LS", "stick", {button("l3")}},
         {"Menu", "system", {button("start")}}},
        {{"ZL", "shoulder", {button("l2")}}, {"ZR", "shoulder", {button("r2")}}, {"LS", "stick", {button("l3")}},
         {"+", "system", {button("start")}}},
        {{"L2", "shoulder", {button("l2")}}, {"R2", "shoulder", {button("r2")}}, {"L3", "stick", {button("l3")}},
         {"Menu", "system", {button("start")}}},
    };
    static_assert(std::size(kOthers) == static_cast<size_t>(Device::SteamDeck), "a row per pad device");
    for (const Glyph& g : kOthers[d - 1])
        if (g.art[0].name == name) return g;
    // The D-pad's ways, one at a time.
    static constexpr Glyph kWays[] = {{"↑", "arrows up", {dpad(1)}}, {"→", "arrows right", {dpad(2)}},
                                      {"↓", "arrows down", {dpad(4)}}, {"←", "arrows left", {dpad(8)}}};
    static constexpr std::string_view kWayNames[] = {"up", "right", "down", "left"};
    for (size_t i = 0; i < std::size(kWays); i++)
        if (kWayNames[i] == name) return kWays[i];
    return {};
}

Glyph page_glyph(Key which) noexcept {
    static constexpr Glyph kPageUp{"PgUp", "key", {key(kVkPageUp, "PgUp")}};
    static constexpr Glyph kPageDown{"PgDn", "key", {key(kVkPageDown, "PgDn")}};
    return which == Key::PrevTab ? kPageUp : which == Key::NextTab ? kPageDown : Glyph{};
}

Device resolve(std::string_view glyphs, int used, bool deck) noexcept {
    if (glyphs == "keyboard") return Device::Keyboard;
    if (glyphs == "playstation") return Device::PlayStation;
    if (glyphs == "xbox") return Device::Xbox;
    if (glyphs == "nintendo") return Device::Nintendo;
    if (glyphs == "steamdeck") return Device::SteamDeck;
    if (used == 3) return deck ? Device::SteamDeck : Device::Xbox;
    return used == 1 ? Device::Keyboard : used == 4 ? Device::Nintendo : Device::PlayStation;
}

}  // namespace ui::prompts
