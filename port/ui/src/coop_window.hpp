// Written for Demon's Souls. The Co-op window (seamless co-op, ../../des_seamless.h; docs/146): host a game or join a
// friend's with an invite code; while in one, the code to send and who is connected; and leaving it. Opened from the
// F1 menu's Co-op tab and the start screen's Co-op button. Hosting, joining and leaving start the game again (the
// network is set up once, at the start, and co-op has its own characters).
#pragma once

#include "modal.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Rml {
class Element;
}

namespace ui {

std::unique_ptr<Modal> make_coop_window();

// What the window's buttons do, for the Co-op page (coop_page.hpp, a look's): each opens the window's question for it,
// or (copy_code) puts the invite code on the clipboard; no_code: there is none to copy yet.
namespace coop {
void open_host();
// The warning before Host or Join while mods are on (names: as the Mods screens show them); then runs on "Play with
// mods" (or at once when the player ticked it away for these mods before). For the Co-op window, and its test pages.
void open_mods_warning(const std::vector<std::string>& names, void (*then)());
// Before a restart from the Mods or Settings screens (menu_pages.cpp): in seamless co-op, when the mods on at the next
// start are another set than this session's, the same warning first (Back: no restart), so a guest doesn't rejoin
// and a host doesn't host again on what was accepted for the old set. `restart` then gets what to set for the new
// game (DES_SEAMLESS_MODS_OK: the mods accepted, or none); at once, with nothing to set, out of co-op or with the
// same set.
void before_restart(std::function<void(const std::vector<std::string>&)> restart);
void open_join();
void copy_code();
bool no_code();
void open_new_code();
void open_public_address();
void open_leave();
// The small Copy button beside each code shown (kind: "room", "direct" or "lan"), for the window's body and the
// page's box: its RML, "Copied" for 2 s after a press. copy_pressed takes a click in that body or box: when it landed
// on one of these buttons, that code goes on the clipboard and it returns true (draw the body or box again).
std::string copy_button(const char* kind);
bool copy_pressed(Rml::Element* target);
}  // namespace coop

}  // namespace ui
