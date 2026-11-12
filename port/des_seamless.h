// Written for Demon's Souls. Seamless co-op (docs/145): one player's game hosts, and friends join it with an invite
// code. No outside server is used.
//
//   DES_SEAMLESS=host         this game runs the room server (ps3recomp's psnr_host) on DES_SEAMLESS_PORT (36100),
//                             asks the router to forward that port (UPnP), and makes the invite code from the
//                             router's public address, the port and a new key.
//   DES_SEAMLESS=join         this game joins the host named by DES_SEAMLESS_CODE.
//   DES_SEAMLESS_UPNP=0       the host leaves the router alone (the port forwarded by hand).
//   DES_SEAMLESS_ADDRESS=ip   the host's public IPv4 address, for the code, when the router doesn't give it.
//   DES_SEAMLESS_CODE_FILE    tests: the host writes its codes there (public, then local), and a joining game with no
//                             DES_SEAMLESS_CODE waits for the file and takes the code from it.
//
// Either way, the game's own online server (sign-in, messages, signs, ghosts: its HTTP requests) is answered inside
// this process, with nothing in it: no messages, no signs, no ghosts, and a neutral world tendency. No name the game
// looks up resolves, so nothing reaches any other server. The seamless save is its own folder
// (savedata-coop beside the normal one), so a co-op session never writes the normal characters.
#pragma once

#include <string>

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
    int port = 0;            // host: the room server's port
    int players = 0;         // host: games connected to the room server, this one included
};
Status status();

// A code the player typed or pasted: "" if it reads as a code (normalised into *clean as XXXX-XXXX-XXXX-XXXX), else
// why not, in plain words.
std::string check_code(const std::string& text, std::string* clean);

}  // namespace des::seamless
