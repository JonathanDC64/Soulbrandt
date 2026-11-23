// Written for Demon's Souls (docs/146, "Notices"). Seamless co-op's notices as toasts: who joined, left, died or is
// back, how this game's join went, and who waits to join a host (des_seamless_notice.cpp makes them). Settings' "Co-op
// notices" off hides them, except a co-op that didn't start (it blocks co-op).
#pragma once

namespace ui::coop_notices {

// Notices wait to be shown (it wakes the UI). Cheap, for the frame hook's sleep check.
bool pending();
// The waiting notices, into toasts (the UI is awake).
void drain();
// No UI to show them with: dropped (the co-op log has each one).
void discard();

}  // namespace ui::coop_notices
