// Written for Demon's Souls. What Change Buttons changes, for both its screens (the Controls window,
// controls_window.hpp, and DeS UI's page, controls_page.hpp); moved out of controls_window.cpp, whose header says where
// it comes from.
//   The gamepad: cellPad's PAD_MAP, each game button and the pad button that presses it, and the port's own Kick row
//   (kept in PAD_MAP as "kick=": settings.cpp's map_for_pad makes it R1's for cellPad, des_kick in des_options.cpp).
//   The keyboard and mouse: its PAD_KEYS, kept as changes to the Souls-style layout (settings.cpp, kKeysLayout): two
//   inputs an action, each a key, a mouse button, a wheel notch or one of those with Shift, Ctrl or Alt held (a chord).
// Both are kept in settings.ini. A Capture takes the next button or key pressed as a row's new input: the pads are
// polled (input.hpp), so a button is caught from the polled state; keys and mouse buttons from the key state
// (keys.hpp), and wheel notches from the UI's events.
#pragma once

#include "prompts.hpp"

#include <array>
#include <string>
#include <vector>

namespace ui::controls {

// ---- The gamepad ----

constexpr int kPadCount = 16;
// cellPad's button names in CELL_PAD_CTRL_* bit order (s_pad_names in cellPad.c), and the names shown.
extern const char* const kPadNames[kPadCount];
extern const char* const kPadLabels[kPadCount];
// The gamepad rows' order (indexes into kPadNames): face buttons, shoulders, sticks, Start and Select, D-pad; and the
// section each starts (nullptr: none).
extern const int kPadRows[kPadCount];
extern const char* const kPadSections[kPadCount];

using PadMap = std::array<int, kPadCount>;
// The pad button each game button reads (an index into kPadNames).
PadMap pad_map();
void save_pad_map(const PadMap& m);
// Gives game button `button` the pad button `value`. A game button that had it takes the old one instead, so no pad
// button presses two game buttons.
void assign(PadMap& m, int button, int value);
bool pad_modified(int button);

// The Kick row: a pad button that kicks (R1 as a kick, with Settings > Controls > Forward attack at Own key) and then
// does nothing else. Its pad button is one of the 16 (kPadNames) or one some pads have beyond them (the extra ones,
// pad_map.h in the runtime: paddle1 to paddle4, misc, touchpad), an index into both: 0-15, then 16 on. kKickTarget is
// its Capture target.
constexpr int kPadExtraCount = 6;
constexpr int kPadHostCount = kPadCount + kPadExtraCount;
constexpr int kKickTarget = kPadCount;
extern const char* const kPadExtraNames[kPadExtraCount];
const char* pad_host_name(int host);
// A pad button's name as the pad in use calls it: "Square", "P1", "Right back", "R4", "Share".
std::string pad_host_label(int host, prompts::Device device);
int pad_kick();  // -1: none
void set_pad_kick(int host);
// Forward attack is Own key: the Kick row's button kicks.
bool kick_on();
// Game button `button`'s pad button is the Kick's, with the Kick on: nothing presses that game button.
bool kick_takes(int button);
// What a row shows: game button `button`'s pad button ("None (Kick)" when kick_takes), the Kick's ("None" for none).
std::string pad_value_label(int button, prompts::Device device);
std::string kick_label(prompts::Device device);
// The Kick row's name and its line on what it does.
extern const char* const kKickLabel;
extern const char* const kKickAbout;

// ---- The keyboard and mouse ----

// An input as cellPad codes it (libs/input/cellPad.c): a Windows virtual-key code, or a mouse button or wheel notch.
constexpr int kInMouse = 0x100;  // mouse1 (left) to mouse5: 0x100 to 0x104
constexpr int kInWheelUp = 0x105, kInWheelDown = 0x106;

struct Binding {
    int in = 0;
    int mod = 0;        // a chord's other input (0 = none): Shift, Ctrl or Alt from these screens
    bool menu = false;  // "menu:", only while the game says a menu is open: not in the layout, kept as written
    bool operator==(const Binding&) const = default;
};
using Bindings = std::vector<Binding>;

// The actions: cellPad's PAD_KEYS targets, and the port's own (Kick, Item slot 1 to 5), in the screens' order.
struct Target {
    const char* name;
    const char* label;
    const char* section;  // the heading this row starts, or nullptr
    const char* about;    // a line on what it does, for a label that says too little (nullptr: the label says it)
};
extern const Target kTargets[];
constexpr int kTargetCount = 31;
constexpr int kSlots = 2;    // inputs an action shows (cellPad takes up to 4: a hand-written PAD_KEYS's others stay)
constexpr int kBindMax = 4;  // cellPad's PAD_BIND_MAX
using KeyMap = std::array<Bindings, kTargetCount>;

int target_index(const std::string& name);
// The bindings of the Souls-style layout, and of the PAD_KEYS setting (the layout, then its changes).
const KeyMap& layout_map();
const KeyMap& key_map();
// Keeps the actions that differ from the layout as the PAD_KEYS setting ("" when none do).
void save_key_map(const KeyMap& m);
// Gives action t's input `slot` the input `in` (with modifier `mod`), or clears it (in = 0). The same input leaves any
// other row first, so one input never works two actions.
void assign_key(KeyMap& m, int t, int slot, int in, int mod);
bool key_modified(int t, int slot);

std::string key_label(int vk);  // "Left button", "Up arrow", "E", "None" for 0
std::string binding_label(const Binding& b);
// Action t's input `slot` as shown: "Shift + Left button", or "None".
std::string slot_label(int t, int slot);

// ---- These menus with Same buttons as the game (settings.cpp's menu_follow_buttons) ----

// The pad button each game button reads, as in the game (pad_map): menu_pad.hpp's through_map turns a pad's buttons
// into the game's with it (input.hpp's menu_buttons). The Kick's button keeps its game button here: a kick means
// nothing in these menus, so Select stays Help when the Kick is on Select.
std::array<signed char, kPadCount> game_button_sources();
// The key a keyboard key stands for in these menus: Enter for a key that works Cross, Esc for one that works Circle
// (on its own, without a modifier; a "menu:" one too, since those are the game's menus' own), so it confirms and goes
// back as it does in the game's menus. 0: it keeps its own meaning, as Enter, Esc, the arrows and F1 always do.
int menu_key_for(int vk);

// Every gamepad button and key back to the defaults.
void restore_defaults();

// ---- Choosing a row's new input ----

class Capture {
public:
    ~Capture() { cancel(); }

    // Waits for the input for game button `target` (pad; kKickTarget: the Kick row) or action `target`'s input `slot`
    // (keys).
    void start(int target, bool pad, int slot);
    void cancel();
    // Once a frame: takes the input once nothing is held any more (the press that started it would be taken
    // otherwise), and gives up after a few seconds. Esc cancels; Delete or Backspace clears a key row.
    void update();

    bool active() const { return mTarget >= 0; }
    bool pad() const { return mPad; }
    int target() const { return mTarget; }
    int slot() const { return mSlot; }
    bool armed() const { return mArmed; }
    // What the row shows meanwhile: "..." until nothing is held, then what to press.
    std::string pending_label() const;

private:
    void update_pad();
    void update_keys();
    bool key_pressed(int code, int mod);
    void bind_key(int in, int mod);

    int mTarget = -1;
    bool mPad = false;
    int mSlot = 0;
    bool mArmed = false;
    unsigned long long mStartedAt = 0;
    std::array<bool, 256> mDown{};  // the keys and mouse buttons held at the last look (keys)
    int mModDown = 0;               // the Shift, Ctrl or Alt pressed first during the capture (its left or right key)
};

}  // namespace ui::controls
