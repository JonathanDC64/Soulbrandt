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
    uint32_t user = 0;         // the other game's number in the room (ask_warp's; the host's may be 0); not for you
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
    bool ground = false;       // the ground it last stood on is known (place)
    float place[3] = {0, 0, 0};
    bool arrived = false;      // where it stood once this area's load was done is known (arrival, in arrival_area)
    float arrival[3] = {0, 0, 0};
    uint32_t arrival_area = 0xFFFFFFFFu;
};
void roster_tick(const RosterFrame& f);

// Warp and Unstuck (F1 > Seamless co-op; docs/146). Warp takes this player to the ground another player in the session
// last stood on, in the same world (what one load brings in); Unstuck back to where it stood once that load was done. Neither in or into a
// boss fight, nor while this player or the other loads, is dead or watches a cutscene. Each in a line, why it can't
// now ("" when it can); the asks are done on the game's next frame (take_move).
std::string warp_blocked(uint32_t user);
std::string unstuck_blocked();
void ask_warp(uint32_t user);
void ask_unstuck();
// des_seamless_tick: the move asked for, checked again now. 1: move the player to xyz; -1: it can't any more (what
// says why); 0: none asked. what: the log's words for the move ("Warp to <name>", "Unstuck").
int take_move(float* xyz, std::string* what);
// What the last move did, for the page's strip, for 5 s after it ("" after that, or with none).
std::string last_move();

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
