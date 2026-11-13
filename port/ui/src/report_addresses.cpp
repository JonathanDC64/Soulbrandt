// Written for Demon's Souls. Internet addresses out of a report (report_addresses.hpp).
#include "report_addresses.hpp"

#include <cstddef>

namespace ui::report {
namespace {

bool digit(char c) { return c >= '0' && c <= '9'; }
bool letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

// As ps3recomp's ps3_net_ip_local: an address the internet can't reach.
bool local(const unsigned a[4]) {
    return a[0] == 0 || a[0] == 127 || a[0] == 10 || (a[0] == 172 && (a[1] & 0xF0) == 16) ||
           (a[0] == 192 && a[1] == 168) || (a[0] == 169 && a[1] == 254) || (a[0] == 100 && (a[1] & 0xC0) == 64) ||
           a[0] >= 224;
}

// A dotted IPv4 address at text[at] (1-3 digits a part, each 255 at most): its length, else 0.
size_t address_at(const std::string& text, size_t at, unsigned parts[4]) {
    size_t i = at;
    for (int p = 0; p < 4; p++) {
        if (p > 0) {
            if (i >= text.size() || text[i] != '.') return 0;
            i++;
        }
        size_t n = 0;
        unsigned v = 0;
        while (i < text.size() && digit(text[i]) && n < 4) {
            v = v * 10 + unsigned(text[i] - '0');
            i++;
            n++;
        }
        if (n == 0 || n > 3 || v > 255) return 0;
        parts[p] = v;
    }
    // (not a longer number or a version's fifth part)
    if (i < text.size() && (digit(text[i]) || letter(text[i]))) return 0;
    if (i + 1 < text.size() && text[i] == '.' && digit(text[i + 1])) return 0;
    return i - at;
}

}  // namespace

std::string hide_addresses(std::string text) {
    std::string out;
    size_t copied = 0;
    for (size_t i = 0; i < text.size(); i++) {
        if (!digit(text[i])) continue;
        const char before = i > 0 ? text[i - 1] : ' ';
        if (digit(before) || letter(before) || before == '.' || before == '_' || before == '-') {
            while (i + 1 < text.size() && (digit(text[i + 1]) || text[i + 1] == '.')) i++;   // (the rest of it)
            continue;
        }
        unsigned parts[4];
        const size_t n = address_at(text, i, parts);
        if (n == 0) {
            while (i + 1 < text.size() && (digit(text[i + 1]) || text[i + 1] == '.')) i++;
            continue;
        }
        if (!local(parts)) {
            out.append(text, copied, i - copied);
            out += "<address>";
            copied = i + n;
        }
        i += n - 1;
    }
    if (copied == 0) return text;
    out.append(text, copied, std::string::npos);
    return out;
}

}  // namespace ui::report
