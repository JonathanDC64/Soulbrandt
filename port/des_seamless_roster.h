// Written for Demon's Souls. Who is in the seamless co-op session (docs/169): each player's name, state, area, form, HP
// and ping, and who waits to join, for the Seamless co-op page and window. Each co-op game says its own (STAT, below)
// to the others through the host's room server, at once when its state changes and every 2 s; a friend's game also
// times its round trip to the host's (PNG?/PNG!).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace des::seamless {

struct SessionPlayer {
    std::string name;          // the online name ("" if none came)
    bool host = false;         // the host's game
    bool you = false;          // this game
    bool in_session = false;   // false: connected to the host's game, waiting to join
    std::string state;         // in plain words: "Playing", "Loading", "In a cutscene", "Dead, back soon", ...
    uint32_t area = 0xFFFFFFFFu;   // the area it is in (FFFFFFFF: none, or loading)
    bool soul = false;         // soul form
    int hp = 0, hp_max = 0;
    int ping_ms = -1;          // a friend's round trip to the host's game, the median of the last five (-1: none)
};

// This game's own state, each frame (des_seamless_tick).
struct RosterFrame {
    bool hosting = false, joining = false;
    bool in_session = false;   // the host: its room is up; a friend: in the host's world
    bool world = false;        // in the world (not loading)
    bool cutscene = false;
    bool dead = false, soul = false;
    bool fight = false;        // in a boss fight (the host's: no join until it's over)
    bool wait_full = false, wait_fight = false;   // a friend out of the session: why it waits
    bool leaving = false;      // a friend on its way out (leave(): back to its own world first)
    uint32_t area = 0xFFFFFFFFu;
    int hp = 0, hp_max = 0;
};
void roster_tick(const RosterFrame& f);

// The first thing des_seamless_game.cpp's message handler does: true when the message was a STAT or a ping (taken).
bool roster_message(uint32_t from, const uint8_t* d, uint32_t n);

// The session now: the host first, then the others as they came, this game among them (you); then the games that
// wait to join (in_session false). Empty out of co-op.
std::vector<SessionPlayer> session_players();

// Since when this game is in the session (seconds), 0 when it isn't.
int session_seconds();

// An area's name as the game's menus give it: "Boletarian Palace 1-1", "The Nexus"; "" for none.
std::string area_label(uint32_t area);

}  // namespace des::seamless
