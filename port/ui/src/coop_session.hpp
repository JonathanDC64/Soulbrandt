// Written for Demon's Souls (docs/169). Who is in the seamless co-op session, for the Seamless co-op page (DeS UI) and
// window (Alpha UI): one line per player (name, state, area, form, HP, and a friend's ping to the host's game), the
// games that wait to join and why, and one line about the session (players, joins, how long together). From
// des_seamless_roster.h.
#pragma once

#include <string>

namespace ui::coop_session {

// max_players: the host's own limit, 0 for a friend (it doesn't know the host's).
// The page's box: an <info-label> and <info-line>s. "" with no session.
std::string page_rml(int max_players);
// The window's text: lines with <br/>. "" with no session.
std::string window_rml(int max_players);

}  // namespace ui::coop_session
