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
    int players = 0;         // host: games connected to the room server, this one included
    enum class Link { Connecting, Connected, Lost };
    Link link = Link::Connecting;  // join: the connection to the host's game (Lost: it was there, and is gone)
};
Status status();

// The game side (des_seamless_game.cpp) keeps a guest's link up to date, every frame.
Status::Link guest_link();

// Host: the public IPv4 address the player gave (the Co-op window's Public address), for a router that can't say it
// (behind another router, no UPnP). The invite code is made from it at once; "" goes back to the router's answer.
// "" when taken (the address, tidied, in *clean), else why not, in plain words.
std::string set_public_address(const std::string& text, std::string* clean);

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
