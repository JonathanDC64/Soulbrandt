// Written for Demon's Souls. The players met online lately (docs/69, "Players you met"), for both their screens (the
// Players window, players_window.hpp, and DeS UI's page, players_page.hpp); moved out of players_window.cpp. The list,
// blocks and reports are the online server's (its docs/social.md), through the runtime's ps3_net_players calls
// (ps3recomp's docs/ONLINE.md, "Sign-in"), which never wait on the network here.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// ---- What this uses of the runtime (ps3emu/ps3_net_signin.h, declared here: des_ui does not take ps3recomp's
// include paths) ----
extern "C" {
typedef struct ps3_net_player {
    char name[20];
    char fingerprint[20];
    char met[12];  // "help", "invasion", "monk"; "" for one blocked and not met lately
    int host;      // the meeting was in this player's world
    long long at;  // seconds since 1970 UTC; -1 unknown
    int blocked;
    int reported;
    int reportable;
} ps3_net_player;
}

namespace ui::players {

enum { kNone, kAsking, kReady, kFailed };  // PS3_NET_PLAYERS_*: where the list is
constexpr int kPageRows = 6;                // rows a page of the list moves by (L1 and R1 in the window)

std::string name_of(const ps3_net_player& p);
// How the two met, as this player sees it: "Came to help you", "You invaded them", "Blocked".
std::string how_met(const ps3_net_player& p);
// Its mark (an icon's class): met-help, met-invasion, met-monk, met-blocked.
const char* met_icon(const ps3_net_player& p);
// "just now", "5 minutes ago", "3 hours ago", "2 days ago"; "" when unknown.
std::string ago(long long at);
// The key's fingerprint in groups of four: "a1b2 c3d4 e5f6 0718".
std::string key_text(const ps3_net_player& p);

// What can be done with a player: Block or Unblock, and Report (which asks why).
void open_actions(const ps3_net_player& p);

// The list as last read, asked for again while there's no sign-in or the ask failed.
struct State {
    std::vector<ps3_net_player> players;  // the list shown
    int status = kNone;
    int days = 0;
    unsigned serial = 0;
    std::string why;           // kFailed: for the player
    bool signedIn = false;     // the runtime took the ask (online play is on, with a sign-in)
    unsigned generation = 1;   // moves with every change the rows show
    unsigned long long nextAskAt = 0;

    // Asks for the list (the screen opens), and reads what there is so far.
    void start();
    // Once a frame: true when the list was read again (the rows to draw again).
    bool update();
    void load();
    // A line on the list: none yet and why, or how far back it goes.
    std::string summary() const;
};

// For the tests (ui_render_compare): the list as these made-up players, signed in, over that many days; empty: the
// runtime's again. Offline: no sign-in.
void set_test_list(std::vector<ps3_net_player> players, int days, bool offline = false);
void clear_test_list();

}  // namespace ui::players
