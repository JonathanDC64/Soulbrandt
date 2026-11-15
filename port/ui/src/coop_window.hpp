// Written for Demon's Souls. The Co-op window (seamless co-op, ../../des_seamless.h; docs/146): host a game or join a
// friend's with an invite code; while in one, the code to send and who is connected; and leaving it. Opened from the
// F1 menu's Co-op tab and the start screen's Co-op button. Hosting, joining and leaving start the game again (the
// network is set up once, at the start, and co-op has its own characters).
#pragma once

#include "modal.hpp"

#include <memory>

namespace ui {

std::unique_ptr<Modal> make_coop_window();

// What the window's buttons do, for the Co-op page (coop_page.hpp, a look's): each opens the window's question for it,
// or (copy_code) puts the invite code on the clipboard; no_code: there is none to copy yet.
namespace coop {
void open_host();
void open_join();
void copy_code();
bool no_code();
void open_new_code();
void open_public_address();
void open_leave();
}  // namespace coop

}  // namespace ui
