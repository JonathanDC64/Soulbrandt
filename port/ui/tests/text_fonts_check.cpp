// text_fonts_check: text_fonts.hpp's scripts_of on its own, the test that decides which of the system's fonts a string
// needs (docs/149). Made-up strings; no font is read. Exit code 0: every case passed.
#include "text_fonts.hpp"

#include <cstdio>
#include <string_view>

namespace {

int sFailed = 0;

void expect(const char* what, std::string_view text, unsigned want) {
    const unsigned got = ui::text_fonts::scripts_of(text);
    if (got != want) {
        std::printf("FAIL %s: scripts %u, expected %u\n", what, got, want);
        sFailed++;
    }
}

}  // namespace

int main() {
    using namespace ui::text_fonts;
    expect("empty", "", 0);
    expect("ASCII", "Demon's Souls & co.", 0);
    expect("Latin, Greek, Cyrillic", "\xC3\xA9t\xC3\xA9 \xCE\xB1\xCE\xB2 \xD0\xB4\xD0\xB0", 0);  // été αβ да
    expect("general punctuation", "\xE2\x80\x94 \xE2\x80\xA6", 0);                            // — …
    expect("hiragana", "\xE3\x81\xB2\xE3\x82\x89\xE3\x81\x8C\xE3\x81\xAA", kKana);           // ひらがな
    expect("katakana and the long mark", "\xE3\x82\xAB\xE3\x83\xBC\xE3\x83\x89", kKana);     // カード
    expect("half-width katakana", "\xEF\xBD\xB6", kKana);                                     // ｶ
    expect("kanji and kana", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x81\xAE", kKana | kHan);  // 日本語の
    expect("hangul", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4", kHangul);                        // 한국어
    expect("hangul jamo", "\xE1\x84\x80", kHangul);                                           // ᄀ
    expect("hangul and corner brackets", "\xE3\x80\x8C\xED\x95\x9C\xE3\x80\x8D", kHangul | kHan);  // 「한」
    expect("Han only", "\xE4\xB8\xAD\xE6\x96\x87", kHan);                                     // 中文
    expect("ideographic full stop", "\xE3\x80\x82", kHan);                                    // 。
    expect("full-width comma", "\xEF\xBC\x8C", kHan);                                         // ，
    expect("extension B", "\xF0\xA0\x80\x80", kHan);                                          // U+20000
    expect("emoji", "\xF0\x9F\x98\x80", 0);                                                   // U+1F600
    expect("a cut character", "\xE4\xB8", 0);
    expect("a bad continuation", "\xE4\x41\x42", 0);
    expect("a stray continuation, then Han", "\x80\xE4\xB8\xAD", kHan);
    expect("mixed", "Level 5 \xE6\xBC\xA2\xE5\xAD\x97 \xEA\xB0\x80 \xE3\x81\x82", kKana | kHangul | kHan);  // 漢字 가 あ
    if (sFailed) {
        std::printf("%d case(s) failed\n", sFailed);
        return 1;
    }
    std::printf("text_fonts_check: all cases passed\n");
    return 0;
}
