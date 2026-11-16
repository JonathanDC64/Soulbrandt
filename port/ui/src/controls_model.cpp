// Written for Demon's Souls. What Change Buttons changes (controls_model.hpp); moved out of controls_window.cpp.
#include "controls_model.hpp"

#include "input.hpp"
#include "keys.hpp"
#include "platform.hpp"
#include "portable.hpp"
#include "settings.hpp"
#include "sound.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace ui::controls {

const char* const kPadNames[kPadCount] = {"select", "l3", "r3", "start", "up", "right", "down", "left",
                                          "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square"};
const char* const kPadLabels[kPadCount] = {"Select", "L3", "R3", "Start", "Up", "Right", "Down", "Left",
                                           "L2", "R2", "L1", "R1", "Triangle", "Circle", "Cross", "Square"};
const int kPadRows[kPadCount] = {14, 13, 15, 12, 10, 11, 8, 9, 1, 2, 3, 0, 4, 6, 7, 5};
const char* const kPadSections[kPadCount] = {"Face buttons", nullptr, nullptr, nullptr, "Shoulders", nullptr,
                                             nullptr, nullptr, "Sticks", nullptr, "Start and Select", nullptr,
                                             "D-pad", nullptr, nullptr, nullptr};

const Target kTargets[] = {
    {"ls_up", "Move forward", "Movement", nullptr},
    {"ls_down", "Move back", nullptr, nullptr},
    {"ls_left", "Move left", nullptr, nullptr},
    {"ls_right", "Move right", nullptr, nullptr},
    {"walk", "Walk (held)", nullptr, nullptr},
    {"circle", "Roll, backstep, dash (Circle)", "Actions", nullptr},
    {"l3", "Left stick press (L3)", nullptr, nullptr},
    {"r1", "Attack (R1)", nullptr, nullptr},
    // the port's, not cellPad's (settings.cpp's keys_for_pad)
    {"kick", "Kick (own key)", nullptr,
     "The forward attack, or kick, when Settings > Controls > Forward attack is Own key."},
    {"r2", "Strong attack (R2)", nullptr, nullptr},
    {"l1", "Left hand, guard (L1)", nullptr, nullptr},
    {"l2", "Left hand, parry (L2)", nullptr, nullptr},
    {"r3", "Lock on (R3)", nullptr, nullptr},
    {"cross", "Action (Cross)", nullptr, nullptr},
    {"square", "Use item (Square)", nullptr, nullptr},
    {"triangle", "Two-hand (Triangle)", nullptr, nullptr},
    {"up", "Switch spell (Up)", "Equipment", nullptr},
    {"down", "Switch item (Down)", nullptr, nullptr},
    {"left", "Switch left hand (Left)", nullptr, nullptr},
    {"right", "Switch right hand (Right)", nullptr, nullptr},
    {"start", "Menu (Start)", "Menus", nullptr},
    {"select", "Select", nullptr, nullptr},
    {"rs_up", "Camera up", "Camera keys", nullptr},
    {"rs_down", "Camera down", nullptr, nullptr},
    {"rs_left", "Camera left", nullptr, nullptr},
    {"rs_right", "Camera right", nullptr, nullptr},
};
static_assert(sizeof kTargets / sizeof kTargets[0] == kTargetCount);

namespace {

constexpr unsigned long long kCaptureTimeoutMs = 8000;

int name_index(const std::string& s) {
    for (int i = 0; i < kPadCount; i++) {
        if (same_name(s.c_str(), kPadNames[i])) return i;
    }
    return -1;
}

// "game=value" pairs separated by commas (or spaces or semicolons), as cellPad reads them.
template <typename F>
void each_pair(const std::string& list, F&& f) {
    size_t at = 0;
    while (at < list.size()) {
        size_t end = list.find_first_of(", ;", at);
        if (end == std::string::npos) end = list.size();
        const std::string pair = list.substr(at, end - at);
        if (const size_t eq = pair.find('='); eq != std::string::npos) f(pair.substr(0, eq), pair.substr(eq + 1));
        at = end + 1;
    }
}

// cellPad's input names (s_key_names in cellPad.c, and f1 to f24). Letters and digits stand for themselves, and any
// other key is written as its virtual-key code ("0xBA").
struct InName {
    const char* name;
    int code;
};
constexpr InName kInNames[] = {
    {"space", VK_SPACE},     {"enter", VK_RETURN},     {"tab", VK_TAB},          {"esc", VK_ESCAPE},
    {"backspace", VK_BACK},  {"shift", VK_SHIFT},      {"lshift", VK_LSHIFT},    {"rshift", VK_RSHIFT},
    {"ctrl", VK_CONTROL},    {"lctrl", VK_LCONTROL},   {"rctrl", VK_RCONTROL},   {"alt", VK_MENU},
    {"lalt", VK_LMENU},      {"ralt", VK_RMENU},       {"up", VK_UP},            {"down", VK_DOWN},
    {"left", VK_LEFT},       {"right", VK_RIGHT},      {"insert", VK_INSERT},    {"delete", VK_DELETE},
    {"home", VK_HOME},       {"end", VK_END},          {"pageup", VK_PRIOR},     {"pagedown", VK_NEXT},
    {"capslock", VK_CAPITAL}, {"mouse1", kInMouse},    {"mouse2", kInMouse + 1}, {"mouse3", kInMouse + 2},
    {"mouse4", kInMouse + 3}, {"mouse5", kInMouse + 4}, {"wheelup", kInWheelUp}, {"wheeldown", kInWheelDown},
    {"return", VK_RETURN},   {"escape", VK_ESCAPE},  // (other spellings cellPad reads; the first name is written)
};

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// One input's code, as cellPad's pad_parse_input reads it (0 = not an input).
int parse_input(const std::string& text) {
    const std::string s = lower(text);
    const auto digit = [&](size_t i) { return i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])); };
    if (s.size() == 1 && std::isalnum(static_cast<unsigned char>(s[0]))) {
        return std::toupper(static_cast<unsigned char>(s[0]));
    }
    for (const InName& n : kInNames) {
        if (s == n.name) return n.code;
    }
    if ((s.size() == 2 || s.size() == 3) && s[0] == 'f' && digit(1) && s[1] != '0' && (s.size() == 2 || digit(2))) {
        const int f = std::atoi(s.c_str() + 1);
        return f >= 1 && f <= 24 ? VK_F1 + f - 1 : 0;
    }
    if (digit(0) && s.size() < 16) return int(std::strtol(s.c_str(), nullptr, 0)) & 0xFF;
    return 0;
}

std::string input_name(int code) {
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) {
        return std::string(1, char(std::tolower(code)));
    }
    for (const InName& n : kInNames) {
        if (n.code == code) return n.name;
    }
    if (code >= VK_F1 && code <= VK_F24) return "f" + std::to_string(code - VK_F1 + 1);
    char text[8];
    std::snprintf(text, sizeof text, "0x%02X", code & 0xFF);
    return text;
}

// A target's value: bindings separated by '|', each "[menu:][modifier+]input"; "none" or nothing is no binding. The
// ones cellPad would skip are left out, as it leaves them out.
Bindings parse_bindings(const std::string& value) {
    Bindings out;
    size_t at = 0;
    while (at <= value.size()) {
        size_t end = value.find('|', at);
        if (end == std::string::npos) end = value.size();
        std::string item = value.substr(at, end - at);
        at = end + 1;
        Binding b;
        if (item.size() > 5 && lower(item.substr(0, 5)) == "menu:") {
            b.menu = true;
            item = item.substr(5);
        }
        const size_t plus = item.find('+');
        if (plus == std::string::npos) {
            b.in = parse_input(item);
        } else {
            b.mod = parse_input(item.substr(0, plus));
            b.in = parse_input(item.substr(plus + 1));
        }
        const bool ok = b.in != 0 && (plus == std::string::npos || (b.mod != 0 && b.mod < kInWheelUp));
        if (ok && int(out.size()) < kBindMax) out.push_back(b);
    }
    return out;
}

std::string bindings_text(const Bindings& bindings) {
    if (bindings.empty()) return "none";
    std::string out;
    for (const Binding& b : bindings) {
        if (!out.empty()) out += '|';
        if (b.menu) out += "menu:";
        if (b.mod) out += input_name(b.mod) + "+";
        out += input_name(b.in);
    }
    return out;
}

// Applies "target=bindings" pairs in order: a target named again replaces what it had, as in cellPad.
void apply_pairs(KeyMap& m, const std::string& list) {
    each_pair(list, [&](const std::string& target, const std::string& value) {
        if (const int t = target_index(target); t >= 0) m[t] = parse_bindings(value);
    });
}

// A modifier's either-side code, for a chord.
int either_side(int vk) {
    switch (vk) {
    case VK_LSHIFT: case VK_RSHIFT: return VK_SHIFT;
    case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
    case VK_LMENU: case VK_RMENU: return VK_MENU;
    default: return vk;
    }
}

// Nothing is held: no key, mouse button or pad button (the press that started the capture would otherwise be taken
// as the choice).
bool input_neutral() {
    if (input::pad_buttons() != 0) return false;
    if (settings::no_user_input()) return true;  // PS3_NO_USER_INPUT: a key held is the user's
    for (int vk = 1; vk < 0xFF; vk++) {
        if (keys::key_down(vk)) return false;
    }
    return true;
}

}  // namespace

PadMap pad_map() {
    PadMap m;
    for (int i = 0; i < kPadCount; i++) m[i] = i;
    each_pair(settings::get("PAD_MAP"), [&](const std::string& game, const std::string& v) {
        const int dst = name_index(game), src = name_index(v);
        if (dst >= 0 && src >= 0) m[dst] = src;
    });
    return m;
}

void save_pad_map(const PadMap& m) {
    std::string out;
    for (int i = 0; i < kPadCount; i++) {
        if (m[i] == i) continue;
        if (!out.empty()) out += ',';
        out += std::string(kPadNames[i]) + "=" + kPadNames[m[i]];
    }
    settings::set("PAD_MAP", out);
}

void assign(PadMap& m, int button, int value) {
    for (int i = 0; i < kPadCount; i++) {
        if (i != button && m[i] == value) m[i] = m[button];
    }
    m[button] = value;
}

bool pad_modified(int button) {
    return pad_map()[button] != button;
}

int target_index(const std::string& name) {
    for (int t = 0; t < kTargetCount; t++) {
        if (same_name(name.c_str(), kTargets[t].name)) return t;
    }
    return -1;
}

const KeyMap& layout_map() {
    static const KeyMap m = [] {
        KeyMap k;
        apply_pairs(k, settings::kKeysLayout);
        return k;
    }();
    return m;
}

// (Each row asks once a frame: parsed again only when the setting changes.)
const KeyMap& key_map() {
    static std::string sValue("\x01");
    static KeyMap sMap;
    const std::string value = settings::get("PAD_KEYS");
    if (value != sValue) {
        sValue = value;
        sMap = layout_map();
        apply_pairs(sMap, value);
    }
    return sMap;
}

void save_key_map(const KeyMap& m) {
    const KeyMap& base = layout_map();
    std::string out;
    for (int t = 0; t < kTargetCount; t++) {
        if (m[t] == base[t]) continue;
        if (!out.empty()) out += ',';
        out += std::string(kTargets[t].name) + "=" + bindings_text(m[t]);
    }
    settings::set("PAD_KEYS", out);
}

void assign_key(KeyMap& m, int t, int slot, int in, int mod) {
    Bindings& own = m[t];
    if (in == 0) {
        if (slot < int(own.size())) own.erase(own.begin() + slot);
        return;
    }
    for (int u = 0; u < kTargetCount; u++) {
        Bindings& bs = m[u];
        for (int j = 0; j < int(bs.size());) {
            if (bs[j].in == in && bs[j].mod == mod && !(u == t && j == slot)) {
                bs.erase(bs.begin() + j);
                if (u == t && j < slot) slot--;
            } else {
                j++;
            }
        }
    }
    const Binding b{in, mod, false};
    if (slot < int(own.size())) {
        own[slot] = b;
    } else if (int(own.size()) < kBindMax) {
        own.push_back(b);
    }
}

bool key_modified(int t, int slot) {
    const Bindings& now = key_map()[t];
    const Bindings& base = layout_map()[t];
    const bool a = slot < int(now.size()), b = slot < int(base.size());
    return a != b || (a && !(now[slot] == base[slot]));
}

std::string key_label(int vk) {
    switch (vk) {
    case 0: return "None";
    case VK_UP: return "Up arrow";
    case VK_DOWN: return "Down arrow";
    case VK_LEFT: return "Left arrow";
    case VK_RIGHT: return "Right arrow";
    case VK_RETURN: return "Enter";
    case VK_SHIFT: return "Shift";
    case VK_CONTROL: return "Ctrl";
    case VK_MENU: return "Alt";
    case VK_LSHIFT: return "Left Shift";
    case VK_RSHIFT: return "Right Shift";
    case VK_LCONTROL: return "Left Ctrl";
    case VK_RCONTROL: return "Right Ctrl";
    case VK_LMENU: return "Left Alt";
    case VK_RMENU: return "Right Alt";
    case kInMouse: return "Left button";
    case kInMouse + 1: return "Right button";
    case kInMouse + 2: return "Middle button";
    case kInMouse + 3: return "Mouse button 4";
    case kInMouse + 4: return "Mouse button 5";
    case kInWheelUp: return "Wheel up";
    case kInWheelDown: return "Wheel down";
    default: break;
    }
    if (std::string name = keys::key_name(vk); !name.empty()) return name;
    char code[16];
    std::snprintf(code, sizeof code, "Key 0x%02X", vk);
    return code;
}

std::string binding_label(const Binding& b) {
    std::string out = b.menu ? "In menus: " : "";
    if (b.mod) out += key_label(b.mod) + " + ";
    return out + key_label(b.in);
}

std::string slot_label(int t, int slot) {
    const Bindings& bs = key_map()[t];
    return slot < int(bs.size()) ? binding_label(bs[slot]) : "None";
}

void restore_defaults() {
    settings::set("PAD_MAP", "");
    settings::set("PAD_KEYS", "");
}

// ---- Capture ----

void Capture::start(int target, bool pad, int slot) {
    mTarget = target;
    mPad = pad;
    mSlot = slot;
    mArmed = false;
    mModDown = 0;
    mStartedAt = now_ms();
    input::set_binding_capture(true);
}

void Capture::cancel() {
    if (mTarget < 0) return;
    mTarget = -1;
    mArmed = false;
    input::set_binding_capture(false);
}

std::string Capture::pending_label() const {
    if (!mArmed) return "...";
    return mPad ? "Press a button" : "Press a key or button";
}

void Capture::update() {
    if (!active()) return;
    if (mPad) update_pad();
    else update_keys();
    if (active() && now_ms() - mStartedAt > kCaptureTimeoutMs) cancel();
}

void Capture::update_pad() {
    const int vk = input::take_captured_key();
    if (vk == VK_ESCAPE) {
        play_sound(kSoundWindowClose);
        cancel();
    } else if (!mArmed) {
        mArmed = input_neutral();
    } else if (const unsigned b = input::pad_buttons(); b != 0) {
        int host = 0;
        while (!(b & (1u << host))) host++;
        PadMap m = pad_map();
        assign(m, mTarget, host);
        save_pad_map(m);
        play_sound(kSoundBindingChanged);
        cancel();
    }
}

// Keys and mouse buttons are read from the key state (Alt and F10 never reach the UI as key presses), wheel notches
// from the UI's events. A Shift, Ctrl or Alt held when another input goes down makes a chord with it; let go with
// nothing else pressed, it is the choice itself.
void Capture::update_keys() {
    (void)input::take_captured_key();  // (the UI's key events only keep the keys from the menus here)
    int wheel = input::take_captured_wheel();
    // (Windows sends the wheel to the window under the cursor, focused or not)
    if (settings::no_user_input()) wheel = 0;
    if (!mArmed) {
        if (input_neutral()) {
            mArmed = true;
            mDown.fill(false);
            mModDown = 0;
        }
        return;
    }
    std::array<bool, 256> now{};
    if (!settings::no_user_input()) {  // PS3_NO_USER_INPUT: a key held is the user's, so a capture sees none
        for (int vk = 1; vk < 0xFF; vk++) now[vk] = keys::key_down(vk);
    }
    // The modifier first, so one pressed in the same frame as the other input still makes the chord.
    for (const int vk : {VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU}) {
        if (!mModDown && now[vk] && !mDown[vk]) mModDown = vk;
    }
    const int mod = mModDown && now[mModDown] ? either_side(mModDown) : 0;
    for (int vk = 1; vk < 0xFF; vk++) {
        const bool pressed = now[vk] && !mDown[vk];
        mDown[vk] = now[vk];
        if (pressed && key_pressed(vk, mod)) return;
    }
    if (wheel != 0 && key_pressed(wheel > 0 ? kInWheelUp : kInWheelDown, mod)) return;
    if (mModDown && !now[mModDown]) bind_key(mModDown, 0);
}

// A new press during a key capture, with modifier `mod` held (0 = none): true when it ends the capture.
bool Capture::key_pressed(int code, int mod) {
    switch (code) {
    case VK_ESCAPE:
        play_sound(kSoundWindowClose);
        cancel();
        return true;
    case VK_DELETE:
    case VK_BACK:
        if (mod) break;
        bind_key(0, 0);
        return true;
    case VK_SHIFT: case VK_CONTROL: case VK_MENU:  // (each also comes as its left or right key)
    case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL: case VK_LMENU: case VK_RMENU:
    case VK_CANCEL: case VK_LWIN: case VK_RWIN:
    case VK_F1: case VK_F9: case VK_F10: case VK_F11: case VK_F12:  // the menu's key and the backend's capture keys
        return false;
    default:
        break;
    }
    int in = code;
    switch (code) {
    case VK_LBUTTON: in = kInMouse; break;
    case VK_RBUTTON: in = kInMouse + 1; break;
    case VK_MBUTTON: in = kInMouse + 2; break;
    case VK_XBUTTON1: in = kInMouse + 3; break;
    case VK_XBUTTON2: in = kInMouse + 4; break;
    default: break;
    }
    bind_key(in, mod);
    return true;
}

void Capture::bind_key(int in, int mod) {
    KeyMap m = key_map();
    assign_key(m, mTarget, mSlot, in, mod);
    save_key_map(m);
    play_sound(kSoundBindingChanged);
    cancel();
}

}  // namespace ui::controls
