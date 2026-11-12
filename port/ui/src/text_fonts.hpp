// Written for Demon's Souls. Fonts for the text the UI's own fonts cannot draw: Japanese, Chinese and Korean, such as
// the trophy names of the Japanese and Asian discs (docs/149). Alegreya Sans and Cinzel have Latin, Greek and Cyrillic
// letters only, and RmlUi leaves out a character that no loaded font has, so that text showed as blanks and stray
// punctuation. Nothing is bundled: the first time cover_text meets kana, hangul or Han characters it adds one of the
// system's fonts for them (text_fonts.cpp) as a fallback font, which RmlUi then draws those characters with.
//
// The header holds no RmlUi: scripts_of is the part tests/text_fonts_check.cpp tests on its own.
#pragma once

#include <cstdint>
#include <string_view>

namespace ui::text_fonts {

// The scripts a string has that need a font of the system's.
enum : unsigned {
    kKana = 1,    // Japanese: hiragana and katakana
    kHangul = 2,  // Korean
    kHan = 4,     // Han characters (kanji, hanzi, hanja), with the CJK punctuation and full-width forms
};

// Which of the scripts above a UTF-8 string has. A byte that does not start a whole UTF-8 character counts for nothing.
inline unsigned scripts_of(std::string_view s) {
    unsigned found = 0;
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        if (b < 0xE0) {  // ASCII, Latin, Greek, Cyrillic (one or two bytes): the UI's own fonts
            i++;
            continue;
        }
        const size_t n = b < 0xF0 ? 3 : b < 0xF5 ? 4 : 1;
        if (n == 1 || i + n > s.size()) {
            i++;
            continue;
        }
        uint32_t cp = b & (n == 3 ? 0x0Fu : 0x07u);
        bool ok = true;
        for (size_t k = 1; k < n; k++) {
            const unsigned char c = static_cast<unsigned char>(s[i + k]);
            if ((c & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (c & 0x3Fu);
        }
        if (!ok) {
            i++;
            continue;
        }
        i += n;
        if ((cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x31F0 && cp <= 0x31FF) || (cp >= 0xFF66 && cp <= 0xFF9F) ||
            (cp >= 0x1B000 && cp <= 0x1B16F))
            found |= kKana;
        else if ((cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F) || (cp >= 0xA960 && cp <= 0xA97F) ||
                 (cp >= 0xAC00 && cp <= 0xD7FF) || (cp >= 0xFFA0 && cp <= 0xFFDC))
            found |= kHangul;
        else if ((cp >= 0x2E80 && cp <= 0x303F) || (cp >= 0x3100 && cp <= 0x312F) || (cp >= 0x3190 && cp <= 0x31EF) ||
                 (cp >= 0x3200 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
                 (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x20000 && cp <= 0x3FFFF))
            found |= kHan;
    }
    return found;
}

}  // namespace ui::text_fonts

namespace ui::host {

// Before a string is shown (put in a document): makes sure a font can draw its Japanese, Chinese or Korean characters.
// The first string with kana loads a Japanese font, the first with hangul a Korean one, and Han characters with
// neither a Chinese (Traditional) one, unless a font is loaded already: each of them has the Han characters. Each
// font loads once, from the system's fonts (memory-mapped, so only the parts drawn are read), and stays for the run.
// DES_UI_CJK_FONT=<font file> is tried first. On RmlUi's thread (the render thread), after host::init; any other
// time it does nothing. Cheap for text without those characters.
void cover_text(std::string_view utf8);

// After Rml::Shutdown (host::shutdown): the fonts are gone with it, so a next init loads them again.
void forget_text_fonts();

}  // namespace ui::host
