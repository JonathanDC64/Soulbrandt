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

PadGive give_pad_button(PadRows& r, int target, int slot, int host, bool move) {
    PadGive out;
    if (host < 0 || host >= kPadHostCount) return out;
    const unsigned bit = 1u << host;
    PadRows after = r;
    const auto let_go = [&](int keep) {  // (a pad button is in one place only)
        for (int i = 0; i < kPadCount; i++)
            if (i != keep) after.also[i] &= ~bit;
    };
    if (target == kKickTarget) {
        after.kick = host;  // (one of the 16 stays its game button's: none presses that while the Kick is on, kick_takes)
        let_go(-1);
        r = after;
        out.changed = true;
        return out;
    }
    if (target < 0 || target >= kPadCount) return out;
    if (slot == 0) {
        if (host >= kPadCount) return out;  // (a row's own button is one of the 16)
        for (int i = 0; i < kPadCount; i++) {
            if (i != target && r.button[i] == host) after.button[i] = r.button[target];  // (the two swap)
        }
        after.button[target] = host;
        let_go(-1);
    } else if (r.also[target] & bit) {
        after.also[target] &= ~bit;  // pressed again on its 2nd box: taken off
        r = after;
        out.changed = true;
        return out;
    } else if (r.button[target] == host) {
        out.owner = target;  // its own already: nothing to do
        return out;
    } else {
        for (int i = 0; i < kPadCount; i++) {
            if (r.button[i] == host) after.button[i] = -1;  // another row's own: it moves
        }
        let_go(target);
        after.also[target] = bit;  // (the row's 2nd: it replaces the one it had)
    }
    if (after.kick == host) after.kick = -1;
    // Another row that loses its own button and gets none back (taken for a 2nd, or a swap with a row that had none),
    // or is left with no button at all: asked first.
    for (int i = 0; i < kPadCount && !move; i++) {
        if (i == target) continue;
        const bool lostOwn = r.button[i] >= 0 && after.button[i] < 0;
        const bool emptied = (r.button[i] >= 0 || r.also[i] != 0) && after.button[i] < 0 && after.also[i] == 0;
        if (lostOwn || emptied) {
            out.owner = i;
            out.ask = true;
            return out;
        }
    }
    r = after;
    out.changed = true;
    return out;
}

bool clear_pad_box(PadRows& r, int target, int slot) {
    if (target == kKickTarget) {
        const bool had = r.kick >= 0;
        r.kick = -1;
        return had;
    }
    if (target < 0 || target >= kPadCount) return false;
    if (slot == 0) {
        const bool had = r.button[target] >= 0;
        r.button[target] = -1;
        return had;
    }
    const bool had = r.also[target] != 0;
    r.also[target] = 0;
    return had;
}

}  // namespace ui::controls
