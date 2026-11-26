// Written for Demon's Souls. Seamless co-op's notices (docs/146, "Notices"): short notes on the screen when a player
// joins, leaves, dies or is back, how this game's own join went, and who waits to join a host. The UI shows them as
// toasts (port/ui/src/coop_notices.cpp), and Settings' "Co-op notices" turns them off. Each one is in the co-op log too.
//
// Who is who comes from the games themselves: each co-op game says its online name and its state (NAME, below) to
// the others through the host's room server, at once when the state changes and every 3 s. The game's own member
// list keeps names only inside its id objects, so it can't give them.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace des::seamless {

struct Notice {
    bool warn = false;     // a problem to tell the player (the toast's warning look)
    bool always = false;   // shown even with Settings' Co-op notices off: co-op didn't start (it isn't chatter)
    std::string text;
};

// This game's own state, each frame (des_seamless_tick): what goes out in its NAME, and what the notices follow.
struct NoticeFrame {
    bool hosting = false;      // co-op as the host
    bool joining = false;      // co-op as a friend
    bool in_session = false;   // the host: its room is up; a friend: in the host's world (it arrived, until it leaves)
    bool dead = false;         // its player's HP is at 0
    bool hold = false;         // a load or a cutscene: the notices wait until it is over
    uint32_t player_id = 0;    // its player character's id (chr +0x498), 0 with none
};
void notices_tick(const NoticeFrame& f);

// The first thing des_seamless_game.cpp's message handler does: true when the message was a NAME (taken here).
bool notice_message(uint32_t from, const uint8_t* d, uint32_t n);

// The host's own death (host_death_flags): alone, the setting's "You respawn alone"; stands, a friend still stands;
// fight, it died in a boss fight (it comes back once the boss is dead).
void notice_host_died(bool alone, bool stands, bool fight);

// Co-op didn't start (des_seamless.cpp's start, or held off before it): why, in its words. It blocks hosting or
// joining, so its notice shows even with the setting off.
void notice_start_failed(bool host, const std::string& why);

// The UI: whether notices wait to be shown now (none while a load or a cutscene holds them), and those notices.
bool notices_waiting();
std::vector<Notice> take_notices();

}  // namespace des::seamless
