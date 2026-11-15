// Adapted from Dusklight (CC0), src/dusk/ui/input.cpp, by way of FFCCEOT-Recomp. Adapted for Demon's Souls: the pads
// are read as a polled state (see input.hpp), and their buttons become the menus' keys in menu_pad.cpp. The menu keys
// follow Dusklight's: Cross confirms, Circle cancels, the shoulders change tabs, and Start or the menu chord (L3 + R3 by
// default) is the menu button; Triangle, Square, Select and R3 are P, C, H and Shift (menu_input.hpp).
#include "input.hpp"

#include "host.hpp"
#include "menu_pad.hpp"
#include "settings.hpp"
#include "ui.hpp"

#include <RmlUi/Core.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ui::input {
namespace {

constexpr double kRepeatInitialDelay = 0.32;
constexpr double kRepeatStartInterval = 0.12;
constexpr double kRepeatMinInterval = 0.045;
constexpr double kRepeatRampDuration = 1.0;

struct Repeat {
    bool held = false;
    double pressedAt = 0.0;
    double nextRepeatAt = 0.0;
};

bool sInputBlocked = false;
std::array<Repeat, kPadKeyCount> sRepeats;
PadKeysState sPadKeys;
bool sPrimed = false;  // the first state after a reset only records what is held (no presses)
bool sBindingCapture = false;
unsigned sPadButtons = 0;
int sCapturedKey = 0;
int sCapturedWheel = 0;
unsigned sReleases = 0;
int sDevice = 0;      // what the menus were last used with (PAD_GLYPH_*: 1 the keyboard or mouse, 2-4 a pad; 0 none yet)
int sGameDevice = 0;  // what the game was, as update_pad last saw it (PadState::glyph)

double now_seconds() noexcept {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

double repeat_interval(double heldFor) noexcept {
    const double ramp = std::clamp(heldFor / kRepeatRampDuration, 0.0, 1.0);
    return kRepeatStartInterval + (kRepeatMinInterval - kRepeatStartInterval) * ramp;
}

}  // namespace

const char* chord_label(int chord) noexcept {
    return chord == 1 ? "Select + Start" : "L3 + R3";
}

bool input_blocked() noexcept {
    return sInputBlocked;
}

void sync_input_block() noexcept {
    sInputBlocked = any_document_visible();
}

void release_input_block() noexcept {
    sInputBlocked = false;
}

void reset_input_state() noexcept {
    auto* context = host::context();
    for (int k = 0; k < kPadKeyCount; k++) {
        if (sRepeats[k].held && context) context->ProcessKeyUp(key_for(k), 0);
        sRepeats[k] = {};
    }
    sPadKeys = {};
    sPrimed = false;
    sReleases++;
}

unsigned release_count() noexcept {
    return sReleases;
}

void set_binding_capture(bool on) noexcept {
    sBindingCapture = on;
    sCapturedKey = 0;
    sCapturedWheel = 0;
    reset_input_state();  // what is held when a capture starts or ends is not a press
}

bool binding_capture() noexcept {
    return sBindingCapture;
}

unsigned pad_buttons() noexcept {
    return sPadButtons;
}

void capture_key(int vk) noexcept {
    sCapturedKey = vk;
}

int take_captured_key() noexcept {
    const int vk = sCapturedKey;
    sCapturedKey = 0;
    return vk;
}

void capture_wheel(int notches) noexcept {
    sCapturedWheel += notches;
}

int take_captured_wheel() noexcept {
    const int n = sCapturedWheel;
    sCapturedWheel = 0;
    return n;
}

void update_pad(const PadState& pad, int chord) noexcept {
    auto* context = host::context();
    if (context == nullptr) return;
    sPadButtons = pad.buttons;
    if (sBindingCapture) {
        sync_input_block();
        return;
    }
    sGameDevice = pad.glyph;
    if (sDevice >= 2 && pad.glyph >= 2) sDevice = pad.glyph;  // (cellPad knows the pad's kind a poll after its press)
    const PadKeys down = pad_keys(pad, chord, sPadKeys);
    const double now = now_seconds();
    for (int k = 0; k < kPadKeyCount; k++) {
        Repeat& r = sRepeats[k];
        if (down[k] && !r.held) {
            r.held = true;
            r.pressedAt = now;
            r.nextRepeatAt = now + kRepeatInitialDelay;
            if (sPrimed) {
                sDevice = pad.glyph >= 2 ? pad.glyph : 2;
                context->ProcessMouseLeave();
                context->ProcessKeyDown(key_for(k), 0);
            }
        } else if (!down[k] && r.held) {
            r = {};
            if (sPrimed) context->ProcessKeyUp(key_for(k), 0);
        }
    }
    sPrimed = true;
    sync_input_block();
}

void update_input() noexcept {
    auto* context = host::context();
    if (context == nullptr) return;
    const double now = now_seconds();
    for (int k = 0; k < kPadKeyCount; k++) {
        Repeat& r = sRepeats[k];
        if (!r.held || !pad_key_repeats(k) || now < r.nextRepeatAt) continue;
        context->ProcessKeyDown(key_for(k), 0);
        r.nextRepeatAt = now + repeat_interval(now - r.pressedAt);
    }
}

void note_device(int device) noexcept {
    if (device > 0) sDevice = device;
}

prompts::Device menu_device() {
    return prompts::resolve(settings::get("DES_GLYPHS"), sDevice ? sDevice : sGameDevice, on_steam_deck());
}

bool on_steam_deck() noexcept {
    static const bool deck = [] {
        if (const char* e = std::getenv("SteamDeck"); e && std::strcmp(e, "1") == 0) return true;
#if defined(__linux__)
        bool valve = false;
        if (FILE* f = std::fopen("/sys/class/dmi/id/sys_vendor", "r")) {
            char vendor[8] = {};
            valve = std::fread(vendor, 1, 5, f) == 5 && std::memcmp(vendor, "Valve", 5) == 0;
            std::fclose(f);
        }
        return valve;
#else
        return false;
#endif
    }();
    return deck;
}

PadSupport pad_support() noexcept {
#ifdef _WIN32
    return {"XInput, DualSense, DualShock 4", "Xbox controllers, DualSense and DualShock 4, over USB or Bluetooth."};
#else
    return {"SDL game controllers", "Xbox, PlayStation and other controllers SDL knows, over USB or Bluetooth."};
#endif
}

}  // namespace ui::input
