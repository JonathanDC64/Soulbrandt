// Written for Demon's Souls. Seamless co-op (docs/146): one player's game hosts, and friends join it with an invite
// code. No outside server is used.
//
//   DES_SEAMLESS=host         this game runs the room server (ps3recomp's psnr_host) on DES_SEAMLESS_PORT (36100),
//                             asks the router to forward that port (UPnP), and makes the invite code from the
//                             router's public address, the port and a new key.
//   DES_SEAMLESS=join         this game joins the host named by DES_SEAMLESS_CODE.
//   DES_SEAMLESS_UPNP=0       the host leaves the router alone (the port forwarded by hand).
//   DES_SEAMLESS_ADDRESS=ip   the host's public IPv4 address, for the code, when the router doesn't give it (the Co-op
//                             window's Public address keeps it in settings.ini).
//   DES_SEAMLESS_PLAYERS=n    the host takes at most n players, itself included: 2 to 4, 4 by default (Settings >
//                             Network). One more stays connected and waits (the host answers FULL), and joins when
//                             a place frees.
//   DES_SEAMLESS_CODE_FILE    tests: the host writes its codes there (public, then local), and a joining game with no
//                             DES_SEAMLESS_CODE waits for the file and takes the code from it.
//
// Either way, the game's own online server (sign-in, messages, signs, ghosts: its HTTP requests) is answered inside
// this process, with nothing in it: no messages, no signs, no ghosts, and a neutral world tendency. No name the game
// looks up resolves, so nothing reaches any other server. The seamless save is its own folder
// (savedata-coop beside the normal one), so a co-op session never writes the normal characters.
#pragma once

#include <string>
#include <vector>

namespace des::seamless {

enum class Mode { Off, Host, Join };

// DES_SEAMLESS's mode ("host" or "join"; anything else is Off).
Mode mode();

// Once at start, in place of the Network rows (settings.cpp's apply_network), before the game's network starts:
// the in-process servers and the runtime's knobs. false, with why, if co-op can't start (the game then stays
// offline). name: the player's online name, already checked.
bool start(const std::string& name, std::string* why);

// In place of start when co-op is asked for (mode()) but held off before it (gameplay mods on, no online name): the
// status says so (mode, not started, why), so the Co-op window shows why and offers Leave, not Host and Join.
void held(const std::string& why);

struct Status {
    Mode mode = Mode::Off;
    bool started = false;
    std::string error;       // why it didn't start
    std::string code;        // host: the invite code for friends ("" until the router has answered)
    std::string lan_code;    // host: the code for players on this network (this PC's local address)
    std::string router;      // host: the router's answer, in plain words
    bool router_done = false;
    bool router_ok = false;  // the port is open and the address public
    bool address_given = false;  // host: the code is from the public address the player gave, not the router's
    int port = 0;            // host: the room server's port
    int players = 0;         // host: players in its session, itself included
    int waiting = 0;         // host: other games connected to it but not in the session (full, or between sessions)
    int max_players = 4;     // host: the most it takes (DES_SEAMLESS_PLAYERS)
    bool code_spent = false; // host: too many wrong codes came, so the code lets no one else in (new_code())
    enum class Link { Connecting, Connected, Lost, Full };
    Link link = Link::Connecting;  // join: the connection to the host's game (Lost: it was there, and is gone; Full:
                                   // the host has all the players it takes, and this one waits for a place)
};
Status status();

// The most players a host takes, itself included (DES_SEAMLESS_PLAYERS, 2 to 4; 4 when unset or out of range).
int party_size();
// Settings' row changed it: a host takes it at once (players already in stay).
void party_size_changed();
// Host: its session has party_size() players (the room server's room), so one more must wait.
bool party_full();

// The game side (des_seamless_game.cpp) keeps a guest's link up to date, every frame.
Status::Link guest_link();

// Host: the public IPv4 address the player gave (the Co-op window's Public address), for a router that can't say it
// (behind another router, no UPnP). The invite code is made from it at once; "" goes back to the router's answer.
// "" when taken (the address, tidied, in *clean), else why not, in plain words.
std::string set_public_address(const std::string& text, std::string* clean);

// Host: a new invite code (a new key): the old one lets no one else in, and players already in stay. For a code
// that went further than meant, or once it is spent (Status::code_spent). "" when done, else why not.
std::string new_code();

// A code the player typed or pasted: "" if it reads as a code (normalised into *clean as XXXX-XXXX-XXXX-XXXX), else
// why not, in plain words.
std::string check_code(const std::string& text, std::string* clean);

// Cheap, for the game side (des_seamless_game.cpp, every frame): co-op started, as the host or as a guest.
Mode active();

// A guest's leave (des_seamless_game.cpp): leave() asks its game to go back to its own world first, where the game
// saves what the group did; left() is true once that is done (or after 2 minutes), and always when not a guest.
void leave();
bool left();

// ---- co-op's own save (des_seamless_save.cpp) ----
// Its save root, beside the normal one (start() uses it unless PS3_SAVEDATA_ROOT is given).
const char* coop_save_root();

struct Character {
    int slot = 0;       // 0-3, as the game's Profile List
    std::string name;   // UTF-8
    int level = 0;      // soul level
};
// The normal characters a player can bring into co-op, once: none when co-op already has a save of its own, when
// PS3_SAVEDATA_ROOT is given (co-op then uses that folder too), or on a disc co-op refuses.
std::vector<Character> characters_to_bring();
// Copies one of them into co-op's save, which must have none yet; the normal save is only read. "" when done, else
// why not, in plain words.
std::string bring_character(int slot);

}  // namespace des::seamless
