// Written for Demon's Souls. What Change Buttons changes, for both its screens (the Controls window,
// controls_window.hpp, and DeS UI's page, controls_page.hpp); moved out of controls_window.cpp, whose header says where
// it comes from.
//   The gamepad: cellPad's PAD_MAP, each game button and the pad button that presses it (and a 2nd one, such as a back
//   button), and the port's own Kick row
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

// A pad button: one of the 16 (kPadNames) or one some pads have beyond them (the extra ones, pad_map.h in the runtime:
// paddle1 to paddle4, misc, touchpad), an index into both: 0-15, then 16 on.
constexpr int kPadExtraCount = 6;
constexpr int kPadHostCount = kPadCount + kPadExtraCount;
extern const char* const kPadExtraNames[kPadExtraCount];
const char* pad_host_name(int host);
// A pad button's name as the pad in use calls it: "Square", "P1", "Right back button", "R4", "Share".
std::string pad_host_label(int host, prompts::Device device);

using PadMap = std::array<int, kPadCount>;
// The pad button each game button reads (an index into kPadNames; -1: none, in a PAD_MAP written by hand).
PadMap pad_map();
// The pad buttons that also press each game button (bit i: pad button i): its 2nd row's (#747), so an action can have
// its usual button and a back button too. A PAD_MAP written by hand can give it more; the 2nd row shows them all.
using PadAlso = std::array<unsigned, kPadCount>;
PadAlso pad_also();
// The pad buttons each row can be given (a Capture's slot): its own one of the 16 (slot 0), and a 2nd (slot 1).
constexpr int kPadSlots = 2;
bool pad_modified(int button, int slot);
// Game button `button`'s 2nd pad buttons as shown ("Right back button", two joined by " + "), "None" for none.
std::string pad_second_label(int button, prompts::Device device);

// The Kick row: a pad button that kicks (R1 as a kick, with Settings > Controls > Forward attack at Own key) and then
// does nothing else, one of the 22. kKickTarget is its Capture target.
constexpr int kKickTarget = kPadCount;
int pad_kick();  // -1: none

// The Gamepad page as PAD_MAP's text has it (pad_rows.cpp, on its own: no settings): "circle=circle|paddle1" gives
// Circle a back button as its 2nd, "kick=paddle2" is the Kick's.
struct PadRows {
    PadMap button{};
    PadAlso also{};
    int kick = -1;
};
PadRows parse_pad_rows(const std::string& text);
std::string pad_rows_text(const PadRows& r);
// A Capture's choice: pad button `host` for game button `target`'s own button (slot 0) or its 2nd (slot 1), or for the
// Kick (kKickTarget). A pad button is in one place only (#747):
// - Own button (one of the 16): a row that had it takes the old one (the two swap), and a 2nd or the Kick that had it
//   lets go of it.
// - 2nd: pressed again on its row, the 2nd is taken off; the row's own button already, nothing changes (`owner` is the
//   row). Otherwise it replaces the row's 2nd, and the row, 2nd or Kick that had it lets go of it.
// - Kick: any of the 22. A 2nd that had it lets go; one of the 16 stays its row's, and nothing presses that row while
//   the Kick is on (kick_takes).
// A choice that would leave another row without its own button (taken for a 2nd, or a swap with a row that had none)
// or with no button at all waits for a yes: `ask`, `owner` that row, and nothing changes. With `move`, it's made.
struct PadGive {
    bool changed = false;
    int owner = -1;    // not given: the row it would leave without a button (ask), or the target, whose own it is
    bool ask = false;  // it waits for a yes: give_pad_button again with move = true makes it
};
PadGive give_pad_button(PadRows& r, int target, int slot, int host, bool move = false);
// Clears a box (Clear on it, or Delete while it waits): game button `target`'s own button (slot 0: nothing presses it
// then), its 2nd (slot 1), or the Kick's. False: it had none.
bool clear_pad_box(PadRows& r, int target, int slot);
// A choice that waits for a yes (give_pad_button's ask): what the question says, and the move once it's yes.
struct PadMove {
    int target = -1, slot = 0, host = -1, owner = -1;
};
std::string pad_move_title(const PadMove& m, prompts::Device device);  // "L3 is L3's. Move it to R1's 2nd?"
std::string pad_move_body(const PadMove& m, prompts::Device device);   // "L3 will have no button."
void apply_pad_move(const PadMove& m);
// Clears box `slot` of row `target` (pad: a game button, or kKickTarget; keys: an action) and keeps it. False: it was
// empty already.
bool clear_box(int target, bool pad, int slot);
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
// into the game's with it (input.hpp's menu_buttons), and through_added with the 2nd ones (pad_also). The
// Kick's button keeps its game button here: a kick means nothing in these menus, so Select stays Help when the Kick is
// on Select.
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

    // Waits for the input for game button `target`'s own button (slot 0) or 2nd (slot 1) (pad; kKickTarget: the Kick
    // row) or action `target`'s input `slot` (keys).
    void start(int target, bool pad, int slot);
    void cancel();
    // Once a frame: takes the input once nothing is held any more (the press that started it would be taken
    // otherwise), and gives up after a few seconds. Esc cancels; Delete or Backspace clears the box. A pad button
    // that's the row's own, pressed for its 2nd, is refused: the row says so for a moment, and the capture waits for
    // another. One that would leave another row without a button ends the capture with a move to ask about
    // (take_move).
    void update();
    // A choice that waits for a yes: the page asks (apply_pad_move makes it). Once: true and the move, then false.
    bool take_move(PadMove& out);

    bool active() const { return mTarget >= 0; }
    bool pad() const { return mPad; }
    int target() const { return mTarget; }
    int slot() const { return mSlot; }
    bool armed() const { return mArmed; }
    // What the row shows meanwhile: "..." until nothing is held, then what to press (or why a button was refused).
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
    std::string mRefused;           // a pad button refused for a 2nd: "R1 is R1's already", shown for a moment
    unsigned long long mRefusedAt = 0;
    PadMove mMove;
    bool mMoveWaiting = false;
};

}  // namespace ui::controls
