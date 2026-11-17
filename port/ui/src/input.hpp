// Adapted from Dusklight (CC0), src/dusk/ui/input.hpp, by way of FFCCEOT-Recomp. Adapted for Demon's Souls: the
// gamepad comes from the port's own pad readers (XInput and the Sony HID reader) as a polled state, not as events.
#pragma once

#include "prompts.hpp"

namespace ui::input {

// The gamepads, merged: CELL_PAD_CTRL_* buttons and the left stick (-1..1, y down).
struct PadState {
    unsigned buttons = 0;
    float lx = 0.0f, ly = 0.0f;
    // What the game was last used with, as cellPad_host_glyph_device gives it (PAD_GLYPH_*: 1 the keyboard or mouse,
    // 2 a PlayStation pad, 3 an Xbox pad, 4 a Nintendo pad; 0 nothing yet).
    int glyph = 0;
};

// Once a frame while the UI is up: turns presses into the UI's keys (menu_pad.hpp: D-pad/left stick: arrows, Cross:
// Enter, Circle: Escape, R1/R2 and L1/L2: next and previous tab, Start or the menu chord: F1, Triangle: P, Square: C,
// Select: H, R3: Shift), with key repeat for the arrows and the tabs.
void update_pad(const PadState& pad, int chord) noexcept;
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
// nothing in the menus. The pads' buttons as last read (CELL_PAD_CTRL_*, the pads' own: PAD_MAP does not apply), the
// last key pressed during a capture (a virtual-key code, 0 = none) and the wheel's notches since the last read
// (up positive); reading either clears it.
void set_binding_capture(bool on) noexcept;
bool binding_capture() noexcept;
unsigned pad_buttons() noexcept;
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
