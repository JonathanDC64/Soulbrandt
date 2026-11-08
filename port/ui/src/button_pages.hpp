// Written for Demon's Souls. The font page with the PS3's buttons on it, read from the player's own files and drawn
// again for another device (docs/137; button_icons.cpp uses it). No UI, renderer or game state in here, so
// tests/button_icons_check.cpp can draw pages without the game.
//
// The game's prompts are text: "<?conclusion?>:Enter" draws the character U+2463 (④) from its menu font, and that
// character's cell on the font page is a picture of Cross. The font's code map (dsfont24.ccm) says where each
// character's cell is and on which page; its texture pack (dsfont24.tpf) holds the pages, 512x512 DXT5 with their
// smaller levels. A page with button cells is kept with its bytes, which is how the renderer's texture is known
// when the game uploads it.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ui::button_pages {

// What each button character shows (file 90 of the game's menu messages, docs/125).
enum Role {
    kTriangle, kSquare, kCircle, kCross,
    kDpad, kDpadLR, kDpadUD, kUp, kDown, kLeft, kRight,
    kSelect, kStart,
    kR1, kR2, kL1, kL2, kR3, kL3,
    kRStick, kLStick,
    kRoles
};

struct Cell {
    Role role;
    int x, y, w, h;  // in the page's pixels
};

struct Page {
    std::string from;            // the file it was read from, for the log
    int index = 0;               // its place in the texture pack (the code map's page number)
    uint32_t width = 0, height = 0;
    uint32_t fmt = 0;            // the RSX format class the game uploads it as (0x86 DXT1, 0x87 DXT3, 0x88 DXT5)
    uint32_t level0 = 0;         // bytes of level 0, the first in `data`
    std::vector<uint8_t> data;   // its levels, as stored
    std::vector<Cell> cells;
};

// The pages of one font with button cells on them: its code map's and texture pack's bytes. Empty when there are
// none, or either file is not what the game's are.
std::vector<Page> read_font(const std::vector<uint8_t>& ccm, const std::vector<uint8_t>& tpf, const std::string& from);

// Level 0 of a page decoded: RGBA, straight alpha. Empty for a format it does not know.
std::vector<uint8_t> decode(const Page& page);

enum class Kind { PlayStation, Xbox, Nintendo, Keyboard };

// A keyboard or mouse input as cellPad codes it (controls_window.hpp, KeyInput) and its key cap's name.
struct Key {
    int in = 0;          // 0 = none bound; 0x100-0x104 mouse buttons; 0x105/0x106 wheel up/down; else a virtual key
    int mod = 0;         // a chord's modifier (Shift, Ctrl, Alt), 0 = none
    std::string label;   // the input's cap ("E", "Space"); empty for arrows, mouse and wheel
    std::string mod_label;
    bool operator==(const Key&) const = default;
};

// The keyboard targets the page draws (cellPad's PAD_KEYS names).
enum KeyTarget {
    kKeyTriangle, kKeySquare, kKeyCircle, kKeyCross, kKeySelect, kKeyStart,
    kKeyR1, kKeyR2, kKeyL1, kKeyL2, kKeyR3, kKeyL3,
    kKeyUp, kKeyDown, kKeyLeft, kKeyRight,
    kKeyLsUp, kKeyLsDown, kKeyLsLeft, kKeyLsRight,
    kKeyTargets
};
extern const char* const kKeyTargetNames[kKeyTargets];

// cellPad's button names, in CELL_PAD_CTRL bit order (PAD_MAP's names).
extern const char* const kPadNames[16];

// What a page is drawn for.
struct Spec {
    Kind kind = Kind::PlayStation;
    int scale = 1;                          // the page at this many times its size (1-4)
    std::array<int, 16> pad_map{};          // pads: the pad button (kPadNames index) each game button reads
    std::array<Key, kKeyTargets> keys{};    // keyboard: the first input bound to each target
    bool operator==(const Spec&) const = default;
};
// The Spec with what its kind does not use cleared, so two that draw the same page compare equal.
Spec normalized(Spec s);
// A short text naming the page drawn, for the log and as a cache key.
std::string describe(const Spec& s);

// The page drawn for `spec`: level 0 is the game's page (decoded, `base`) at spec.scale times its size with each
// button cell drawn again, followed by its smaller levels down to 1x1. RGBA, straight alpha. font is a TrueType
// font's bytes (the UI's Alegreya Sans Bold) for the letters.
struct Image {
    uint32_t width = 0, height = 0, levels = 0;
    std::vector<uint8_t> rgba;
};
Image draw(const Page& page, const std::vector<uint8_t>& base, const Spec& spec, const unsigned char* font);

}  // namespace ui::button_pages
