// Written for Demon's Souls. A path as UTF-8 text that never throws. std::filesystem's u8string() throws on Windows
// for a name that isn't valid UTF-16 (an unpaired surrogate), and Windows allows such names, in files and in the
// registry: setup crashed at its start on a PC whose list of programs started held one (its RPCS3 search,
// setup_rpcs3.cpp's recorded_programs). Here such a character becomes U+FFFD; a name with one matches nothing setup
// looks for, so that name is just passed over. Off Windows a path is already bytes: it is given as it is.
#pragma once

#include <filesystem>
#include <string>

namespace ui {

// UTF-16 to UTF-8, on any platform. An unpaired surrogate becomes U+FFFD.
template <class Char>
std::string utf8_from_utf16(const Char* s, size_t n) {
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n; i++) {
        char32_t c = char32_t(s[i]) & 0xFFFF;
        const char32_t next = i + 1 < n ? char32_t(s[i + 1]) & 0xFFFF : 0;
        if (c >= 0xD800 && c <= 0xDBFF && next >= 0xDC00 && next <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (next - 0xDC00);
            ++i;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD;
        }
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += char(0xE0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        } else {
            out += char(0xF0 | (c >> 18));
            out += char(0x80 | ((c >> 12) & 0x3F));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// The path as UTF-8, with Windows' separators (as u8string()).
inline std::string path_utf8(const std::filesystem::path& p) {
#ifdef _WIN32
    const std::wstring& w = p.native();
    return utf8_from_utf16(w.data(), w.size());
#else
    return p.native();
#endif
}

// The path as UTF-8 with '/' between its parts (as generic_u8string()).
inline std::string path_generic_utf8(const std::filesystem::path& p) {
#ifdef _WIN32
    const std::wstring w = p.generic_wstring();  // (only swaps the separators: it doesn't convert, so it can't throw)
    return utf8_from_utf16(w.data(), w.size());
#else
    return p.generic_string();
#endif
}

}  // namespace ui
