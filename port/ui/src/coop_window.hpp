// Written for Demon's Souls. The Co-op window (seamless co-op, ../../des_seamless.h; docs/146): host a game or join a
// friend's with an invite code; while in one, the code to send and who is connected; and leaving it. Opened from the
// F1 menu's Co-op tab and the start screen's Co-op button. Hosting, joining and leaving start the game again (the
// network is set up once, at the start, and co-op has its own characters).
#pragma once

#include "modal.hpp"

#include <memory>

namespace ui {

std::unique_ptr<Modal> make_coop_window();

}  // namespace ui
