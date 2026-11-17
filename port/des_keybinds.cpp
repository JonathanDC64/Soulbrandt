/* des_keybinds.cpp: the port's own keys as the Keyboard page writes them (des_keybinds.h). Moved out of des_options.cpp,
 * where the Kick key read them, for the item slots' keys too. */
#include "des_keybinds.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

extern "C" int cellPad_host_key_down(int vk);   /* cellPad.c: a key or mouse button held now */

int des_key_vk(const char* s, unsigned n)
{
    static const struct { const char* name; int vk; } kNames[] = {
        {"space", 0x20},   {"enter", 0x0D},    {"return", 0x0D},   {"tab", 0x09},      {"esc", 0x1B},
        {"escape", 0x1B},  {"backspace", 0x08}, {"shift", 0x10},   {"lshift", 0xA0},   {"rshift", 0xA1},
        {"ctrl", 0x11},    {"lctrl", 0xA2},    {"rctrl", 0xA3},    {"alt", 0x12},      {"lalt", 0xA4},
        {"ralt", 0xA5},    {"up", 0x26},       {"down", 0x28},     {"left", 0x25},     {"right", 0x27},
        {"insert", 0x2D},  {"delete", 0x2E},   {"home", 0x24},     {"end", 0x23},      {"pageup", 0x21},
        {"pagedown", 0x22}, {"capslock", 0x14}, {"mouse1", 0x01},  {"mouse2", 0x02},   {"mouse3", 0x04},
        {"mouse4", 0x05},  {"mouse5", 0x06},
    };
    if (n == 1 && std::isalnum((unsigned char)s[0])) return std::toupper((unsigned char)s[0]);
    const auto same = [&](const char* name) {
        if (std::strlen(name) != n) return false;
        for (unsigned i = 0; i < n; i++)
            if (std::tolower((unsigned char)s[i]) != name[i]) return false;
        return true;
    };
    for (const auto& k : kNames)
        if (same(k.name)) return k.vk;
    if ((n == 2 || n == 3) && (s[0] | 0x20) == 'f' && s[1] >= '1' && s[1] <= '9') {
        const int f = n == 2 ? s[1] - '0' : (s[2] >= '0' && s[2] <= '9' ? (s[1] - '0') * 10 + s[2] - '0' : 0);
        return f >= 1 && f <= 24 ? 0x70 + f - 1 : 0;
    }
    if (n > 2 && n < 8 && s[0] == '0' && (s[1] | 0x20) == 'x') {
        char buf[8];
        std::memcpy(buf, s, n);
        buf[n] = 0;
        return int(std::strtol(buf, nullptr, 16) & 0xFF);
    }
    return 0;
}

DesKeys des_keys_parse(const char* s, const char* end)
{
    DesKeys k{};
    if (!s) return k;
    if (!end) end = s + std::strlen(s);
    while (s < end && k.n < 4) {
        const char* bar = static_cast<const char*>(std::memchr(s, '|', size_t(end - s)));
        if (!bar) bar = end;
        const char* plus = static_cast<const char*>(std::memchr(s, '+', size_t(bar - s)));
        DesKeyBind b{};
        if (plus) {
            b.mod = des_key_vk(s, unsigned(plus - s));
            b.in = des_key_vk(plus + 1, unsigned(bar - plus - 1));
        } else {
            b.in = des_key_vk(s, unsigned(bar - s));
        }
        if (b.in && (!plus || b.mod)) k.b[k.n++] = b;
        s = bar < end ? bar + 1 : end;
    }
    return k;
}

bool des_keys_held(const DesKeys& k)
{
    for (int i = 0; i < k.n; i++)
        if (cellPad_host_key_down(k.b[i].in) && (!k.b[i].mod || cellPad_host_key_down(k.b[i].mod))) return true;
    return false;
}
