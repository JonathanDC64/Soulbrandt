// Adapted from Dusklight (CC0), src/dusk/ui/input.hpp, by way of FFCCEOT-Recomp. Adapted for Demon's Souls: the
// gamepad comes from the runtime's pad readers (cellPad_host_pads: XInput or SDL, and the Sony HID reader) as a
// polled state, not as events.
#pragma once

#include "prompts.hpp"

namespace ui::input {

// The gamepads, merged: CELL_PAD_CTRL_* buttons and the left stick (-1..1, y down).
struct PadState {
    unsigned buttons = 0;
    unsigned extra = 0;  // the buttons some pads have beyond the PS3's (the runtime's pad_map.h: PAD_EXTRA_*)
    float lx = 0.0f, ly = 0.0f;
    // What the game was last used with, as cellPad_host_glyph_device gives it (PAD_GLYPH_*: 1 the keyboard or mouse,
    // 2 a PlayStation pad, 3 an Xbox pad, 4 a Nintendo pad; 0 nothing yet).
    int glyph = 0;
};

// Once a frame while the UI is up: turns presses into the UI's keys (menu_pad.hpp: D-pad/left stick: arrows, Cross:
// Enter, Circle: Escape, R1/R2 and L1/L2: next and previous tab, Start or the menu chord: F1, Triangle: P, Square: C,
// Select: H, R3: Shift), with key repeat for the arrows and the tabs.
void update_pad(const PadState& pad, int chord) noexcept;
// The buttons the menus act on: a pad's own, or with Same buttons as the game on (settings.cpp's menu_follow_buttons)
// the game's, through Change Buttons (controls_model.hpp's game_button_sources, and each row's 2nd, a back button or
// paddle among them in `extra`: pad_also), so Cross is the game's Cross wherever the player put it. A binding being
// chosen still reads the pad's own (pad_buttons). Only while menus_follow: a layout that leaves the menus without a
// button they need keeps the pad's own.
unsigned menu_buttons(unsigned buttons, unsigned extra);
// Whether the menus take the game's buttons now: Same buttons as the game on, and Change Buttons leaving Cross, Circle
// and the tabs a button each (menu_pad.hpp's follow_works). Otherwise they keep the pad's own, so clearing Cross's box
// can't leave the menus with no way to confirm (#747).
bool menus_follow();
// Whether the menu chord's game buttons are left with no pad button (Change Buttons cleared or moved one:
// menu_pad.hpp's chord_has_buttons): then the pad's own chord buttons make the chord, over the game (des_ui.cpp) and
// in the menus (update_pad), whatever else they press.
bool chord_on_own_buttons();
// The key a keyboard key stands for in the menus: with Same buttons as the game on, Enter for the game's Cross key and
// Esc for its Circle key (controls_model.hpp's menu_key_for); else, and for every other key, the key itself.
int menu_key(int vk);
// The glyph a menu prompt shows for `key` (prompts.hpp's glyph), with Same buttons as the game on: on a pad, the pad
// button the game's button is on (Cross's prompt shows Circle when Change Buttons swapped them; the D-pad's stay); on
// the keyboard, Page Up and Page Down for the tabs when Q or E confirms or goes back (prompts.hpp's page_glyph).
prompts::Glyph menu_glyph(prompts::Device device, prompts::Key key);
void update_input() noexcept;
// Lets go of every key the pads hold (the window lost the focus, or a binding is being chosen).
void reset_input_state() noexcept;
// How many times reset_input_state has run: a page holding something down (Compare, Hide) lets it go when this
// changes, as keys held then may never come up.
unsigned release_count() noexcept;
void sync_input_block() noexcept;
void release_input_block() noexcept;

// The chord that opens the menu over the game: 0 = L3 + R3, 1 = Select + Start (CELL_PAD_CTRL_* masks).
unsigned chord_mask(int chord) noexcept;
const char* chord_label(int chord) noexcept;

// Whether the UI is taking the input (a document is showing): the game then gets none.
bool input_blocked() noexcept;

// The pointer's moves, as des_ui.cpp hands them to RmlUi: begin_pointer_move before, end_pointer_move after. While
// one is handed over, hover_moved() says whether the pointer really moved since the last: not for the first move after
// reset_input_state (the menus waking, a window opening), nor for a move Windows sends with no motion (after a key
// press or with a click). A page's hover selects a row only then, so a key press, or a list scrolling under a still
// pointer, never has its focus taken by the mouse.
void begin_pointer_move(int x, int y) noexcept;
void end_pointer_move() noexcept;
bool hover_moved() noexcept;

// A button or key is being chosen for a binding (controls_window.cpp): the pads, keys and mouse buttons then work
// nothing in the menus. The pads' buttons as last read (CELL_PAD_CTRL_*, the pads' own: PAD_MAP does not apply), and
// their extra ones (PadState's extra: the Kick row can take those), the last key pressed during a capture (a virtual-key code, 0 = none) and the wheel's notches since the last read
// (up positive); reading either clears it.
void set_binding_capture(bool on) noexcept;
bool binding_capture() noexcept;
unsigned pad_buttons() noexcept;
unsigned pad_extra_buttons() noexcept;
void capture_key(int vk) noexcept;
int take_captured_key() noexcept;
void capture_wheel(int notches) noexcept;
int take_captured_wheel() noexcept;

// The device the menus' prompts show (prompts.hpp): the Button icons row's choice, or with Automatic what the menus
// were last used with (before that, what the game was). update_pad notes a pad's presses; des_ui.cpp notes key
// presses and mouse buttons (1), and the device that pressed the chord opening the menu (PAD_GLYPH_*).
void note_device(int device) noexcept;
prompts::Device menu_device();

// This PC is a Steam Deck: Automatic then shows the Deck's buttons for an Xbox-style pad, which is how the Deck's own
// controls reach the game. Its firmware names the maker (as cellPad's PAD_KEYBOARD_WITH_PAD default reads it on
// Linux), and Steam sets SteamDeck=1 for the games it starts there, the Windows program under Proton too.
bool on_steam_deck() noexcept;

// The gamepads this build reads, for the settings: a short value and its help text.
struct PadSupport {
    const char* value;
    const char* help;
};
PadSupport pad_support() noexcept;

}  // namespace ui::input
