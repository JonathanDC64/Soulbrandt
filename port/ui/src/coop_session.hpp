// Written for Demon's Souls (docs/169). Who is in the seamless co-op session, for the Seamless co-op page (DeS UI) and
// window (Alpha UI): one line per player (name, what it does, and a friend's ping to the host's game), the
// games that wait to join and why, and one line about the session (players, joins, how long together). From
// des_seamless_roster.h.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ui::coop_session {

// max_players: the host's own limit, 0 for a friend (it doesn't know the host's).
// The page's box: <info-label>s, <info-players> with an <info-player> line each, and an <info-line>. "" with no
// session. Each line in the session moves a player (class pick, data-row its place in picks()): another player's
// Warp, your own Unstuck, in an <info-act> (class disabled when it can't now) that shows on the line with the focus
// (row, -1 none) or under the pointer. *company: anyone but you, in the session or waiting (else the host alone).
std::string page_rml(int max_players, int row, bool* company);
// The window's text: lines with <br/>. "" with no session.
std::string window_rml(int max_players);

// The lines in the session, as page_rml shows them: Warp to another player, or Unstuck on your own
// (des_seamless_roster.h).
struct Pick {
    bool you = false;
    uint32_t user = 0;
    std::string name;      // as its line shows it
    std::string blocked;   // why it can't now ("" when it can)
};
std::vector<Pick> picks();
void do_pick(const Pick& p);   // asks for the move; the game makes it on its next frame

}  // namespace ui::coop_session
