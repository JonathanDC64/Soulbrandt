// Written for Demon's Souls. Change Buttons' Gamepad rows and PAD_MAP's text (controls_model.hpp's PadRows), on their
// own: no settings and no pads, so tests/input_map_check.cpp can run them.
#include "controls_model.hpp"

#include "portable.hpp"

namespace ui::controls {

const char* const kPadNames[kPadCount] = {"select", "l3", "r3", "start", "up", "right", "down", "left",
                                          "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square"};
const char* const kPadExtraNames[kPadExtraCount] = {"paddle1", "paddle2", "paddle3", "paddle4", "misc", "touchpad"};

const char* pad_host_name(int host) {
    if (host >= 0 && host < kPadCount) return kPadNames[host];
    if (host >= kPadCount && host < kPadHostCount) return kPadExtraNames[host - kPadCount];
    return "";
}

namespace {

int host_index(const std::string& s) {
    for (int i = 0; i < kPadHostCount; i++) {
        if (same_name(s.c_str(), pad_host_name(i))) return i;
    }
    return -1;
}

}  // namespace

PadRows parse_pad_rows(const std::string& text) {
    PadRows r;
    for (int i = 0; i < kPadCount; i++) r.button[i] = i;
    r.also.fill(0);
    size_t at = 0;
    while (at < text.size()) {  // "game=value" pairs separated by commas (or spaces or semicolons), as cellPad reads
        size_t end = text.find_first_of(", ;", at);
        if (end == std::string::npos) end = text.size();
        const std::string pair = text.substr(at, end - at);
        at = end + 1;
        const size_t eq = pair.find('=');
        if (eq == std::string::npos) continue;
        const std::string game = pair.substr(0, eq), value = pair.substr(eq + 1);
        if (same_name(game.c_str(), "kick")) {
            r.kick = host_index(value);  // (named again, it replaces)
            continue;
        }
        const int dst = host_index(game);
        if (dst < 0 || dst >= kPadCount) continue;
        // The value's parts, each a name ("circle|paddle1"), or "none": cellPad leaves out a pair with a part that
        // isn't one, so the row keeps its own button.
        int own = -1;
        unsigned added = 0;
        bool ok = !value.empty();
        if (!same_name(value.c_str(), "none")) {
            for (size_t a = 0; a <= value.size() && ok;) {
                size_t bar = value.find('|', a);
                if (bar == std::string::npos) bar = value.size();
                const int h = host_index(value.substr(a, bar - a));
                a = bar + 1;
                if (h < 0) ok = false;
                else if (own < 0 && h < kPadCount) own = h;
                else if (h != own) added |= 1u << h;
            }
        }
        if (!ok) continue;
        r.button[dst] = own;
        r.also[dst] = added;
    }
    return r;
}

std::string pad_rows_text(const PadRows& r) {
    std::string out;
    for (int i = 0; i < kPadCount; i++) {
        if (r.button[i] == i && r.also[i] == 0) continue;
        std::string value = r.button[i] >= 0 && r.button[i] < kPadCount ? kPadNames[r.button[i]] : "";
        for (int h = 0; h < kPadHostCount; h++) {
            if ((r.also[i] & (1u << h)) && h != r.button[i])
                value += (value.empty() ? "" : "|") + std::string(pad_host_name(h));
        }
        if (!out.empty()) out += ',';
        out += std::string(kPadNames[i]) + "=" + (value.empty() ? "none" : value);
    }
    if (r.kick >= 0 && r.kick < kPadHostCount)
        out += std::string(out.empty() ? "" : ",") + "kick=" + pad_host_name(r.kick);
    return out;
}

PadGive give_pad_button(PadRows& r, int target, int slot, int host) {
    PadGive out;
    if (host < 0 || host >= kPadHostCount) return out;
    const unsigned bit = 1u << host;
    const auto let_go = [&](int keep) {  // (a pad button is in one place only)
        for (int i = 0; i < kPadCount; i++)
            if (i != keep) r.also[i] &= ~bit;
    };
    if (target == kKickTarget) {
        r.kick = host;  // (one of the 16 stays its game button's: none presses that while the Kick is on, kick_takes)
        let_go(-1);
        out.changed = true;
        return out;
    }
    if (target < 0 || target >= kPadCount) return out;
    if (slot == 0) {
        if (host >= kPadCount) return out;  // (a row's own button is one of the 16)
        for (int i = 0; i < kPadCount; i++) {
            if (i != target && r.button[i] == host) r.button[i] = r.button[target];
        }
        r.button[target] = host;
        let_go(-1);
    } else if (r.also[target] & bit) {
        r.also[target] &= ~bit;  // pressed again on its 2nd row: taken off
        out.changed = true;
        return out;
    } else {
        for (int i = 0; i < kPadCount; i++) {
            if (r.button[i] == host) {
                out.owner = i;  // a row's own button: it stays there
                return out;
            }
        }
        let_go(target);
        r.also[target] = bit;  // (the row's 2nd: it replaces the one it had)
    }
    if (r.kick == host) r.kick = -1;
    out.changed = true;
    return out;
}

void clear_pad_second(PadRows& r, int target) {
    if (target >= 0 && target < kPadCount) r.also[target] = 0;
}

}  // namespace ui::controls
