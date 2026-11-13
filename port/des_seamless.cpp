// Written for Demon's Souls. Seamless co-op: the host's room server, the invite code, and the game's own online
// server answered inside the process (des_seamless.h).
#include "des_seamless.h"

#include "ps3emu/net_invite.h"
#include "ps3emu/net_stun.h"
#include "ps3emu/net_upnp.h"
#include "ps3emu/psnr_host.h"

#include "mbedtls/aes.h"   // (ps3recomp's third_party/mbedtls, built into the runtime)

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
using sock_t = SOCKET;
static const sock_t kNoSock = INVALID_SOCKET;
static void sock_close(sock_t s) { closesocket(s); }
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
using sock_t = int;
static const sock_t kNoSock = -1;
static void sock_close(sock_t s) { close(s); }
#endif

extern "C" void ps3_setenv(const char* name, const char* value);   // boot_main.cpp: through the getenv cache

namespace des::seamless {
namespace {

constexpr int kDefaultPort = 36100;   // the room server's, TCP and UDP (psnr's usual port)
constexpr const char* kUpnpName = "Demon's Souls co-op";
// The STUN server a host asks for its internet address when its router gives none (DES_SEAMLESS_STUN).
constexpr const char* kStunHost = "stun.l.google.com";
constexpr uint16_t kStunPort = 19302;

std::mutex sLock;
Status sStatus;               // under sLock
// The host's code, under sLock: its key and port, the public address the player gave ("" = none), and the router's
// own answer (its code, if it gave a public address).
uint32_t sKey = 0;
int sPort = 0;
std::string sGiven;
struct RouterAnswer {
    std::string text, code;
    bool ok = false;
    bool opened = false;  // UPnP opened the port
    bool behind = false;  // ...on a router behind another one (its own address private)
} sRouter;
psnr_host* sHost = nullptr;
std::atomic<int> sActive{0};  // a Mode, once start() succeeded

bool env_given(const char* name) {
    const char* v = std::getenv(name);
    return v && *v;
}

// A runtime knob, unless the environment set it (as settings.cpp's set_net_knob).
void knob(const char* name, const std::string& value) {
    if (env_given(name)) {
        std::fprintf(stderr, "[coop] %s from the environment wins\n", name);
        return;
    }
    ps3_setenv(name, value.c_str());
}

// ---- the game's online server, answered here -------------------------------------------------------------------
// The game's HTTP requests (cellHttp) all come here through PS3_HTTP_LOCAL (ps3recomp's docs/ONLINE.md). The answers
// follow the open-source dessego server's, with nothing stored: the request bodies are AES-256-CBC with the game's
// fixed key (dessego's internal/crypto), and only one answer needs a field from one (initializeCharacter's id).

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(const std::string& in) {
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8 | (uint8_t)in[i + 2];
        out += kB64[v >> 18 & 63];
        out += kB64[v >> 12 & 63];
        out += kB64[v >> 6 & 63];
        out += kB64[v & 63];
    }
    if (i + 1 == in.size()) {
        const uint32_t v = (uint8_t)in[i] << 16;
        out += kB64[v >> 18 & 63];
        out += kB64[v >> 12 & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        const uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8;
        out += kB64[v >> 18 & 63];
        out += kB64[v >> 12 & 63];
        out += kB64[v >> 6 & 63];
        out += '=';
    }
    return out;
}

// One answer: base64(type, u32 LE length + 5, data) and a newline (dessego's transport.WriteResponse).
std::string answer(uint8_t type, const std::string& data) {
    std::string raw(1, (char)type);
    const uint32_t n = (uint32_t)data.size() + 5;
    for (int b = 0; b < 4; b++) raw += (char)(n >> (8 * b) & 0xFF);
    raw += data;
    return base64(raw) + "\n";
}

std::string u32le(uint32_t v) {
    std::string s;
    for (int b = 0; b < 4; b++) s += (char)(v >> (8 * b) & 0xFF);
    return s;
}

// The request's form, decrypted (AES-256-CBC, the first 16 bytes the IV, PKCS#7 padding); "" if it isn't one.
std::string decrypt(const std::string& body) {
    if (body.size() < 32 || body.size() % 16) return {};
    static const unsigned char kKey[] = "11111111222222223333333344444444";   // the game's own (32 bytes)
    unsigned char iv[16];
    std::memcpy(iv, body.data(), 16);
    std::string out(body.size() - 16, '\0');
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int rc = mbedtls_aes_setkey_dec(&aes, kKey, 256);
    if (!rc)
        rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, out.size(), iv,
                                   (const unsigned char*)body.data() + 16, (unsigned char*)&out[0]);
    mbedtls_aes_free(&aes);
    if (rc) return {};
    const unsigned pad = (uint8_t)out.back();
    if (pad < 1 || pad > 16 || pad > out.size()) return {};
    for (size_t i = out.size() - pad; i < out.size(); i++)
        if ((uint8_t)out[i] != pad) return {};
    out.resize(out.size() - pad);
    return out;
}

// One field of a form ("a=1&b=2"), URL-decoded.
std::string form_value(const std::string& form, const char* key) {
    const size_t klen = std::strlen(key);
    size_t at = 0;
    while (at <= form.size()) {
        const size_t end = std::min(form.find('&', at), form.size());
        if (end - at > klen && !form.compare(at, klen, key) && form[at + klen] == '=') {
            std::string v;
            for (size_t i = at + klen + 1; i < end; i++) {
                if (form[i] == '+') v += ' ';
                else if (form[i] == '%' && i + 2 < end && std::isxdigit((uint8_t)form[i + 1]) &&
                         std::isxdigit((uint8_t)form[i + 2])) {
                    v += (char)std::strtol(form.substr(i + 1, 2).c_str(), nullptr, 16);
                    i += 2;
                } else v += form[i];
            }
            return v;
        }
        at = end + 1;
    }
    return {};
}

// The bootstrap answer (the request to <region's host>:18000/.../ss.info): the game's server list, base64 of the
// same XML as dessego's res.tpl. The addresses don't matter (every request comes here) but must be well formed.
std::string bootstrap() {
    std::string x = "<ss>0</ss>\n";
    for (int n : {1, 2, 3, 4, 5, 6, 7, 8, 11, 12}) x += "<lang" + std::to_string(n) + "></lang" + std::to_string(n) + ">\n";
    for (int n : {1, 2, 3, 4, 5, 6, 7, 8, 11, 12})
        x += "<gameurl" + std::to_string(n) + ">http://127.0.0.1:18666/cgi-bin/</gameurl" + std::to_string(n) + ">\n";
    for (int n : {1, 2, 3}) x += "<browserurl" + std::to_string(n) + "></browserurl" + std::to_string(n) + ">\n";
    for (int n : {1, 2, 3, 4, 5, 6, 7, 8, 11, 12}) x += "<interval" + std::to_string(n) + ">120</interval" + std::to_string(n) + ">\n";
    x += "<getWanderingGhostInterval>20</getWanderingGhostInterval>\n"
         "<setWanderingGhostInterval>20</setWanderingGhostInterval>\n"
         "<getBloodMessageNum>80</getBloodMessageNum>\n"
         "<getReplayListNum>80</getReplayListNum>\n"
         "<enableWanderingGhost>1</enableWanderingGhost>";
    return base64(x);
}

// The answer to POST <path> with this body; false for a request this server doesn't know (the connection is then
// closed without one, as dessego does).
bool game_answer(const std::string& path, const std::string& body, std::string* out) {
    const size_t slash = path.rfind('/');
    const std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    const std::string ok(1, '\x01');
    if (name == "ss.info") { *out = bootstrap(); return true; }
    if (name == "login.spd") {
        *out = answer(0x02, std::string("\x01\x02", 2) + "Seamless co-op\r\n" + '\0' + "Hosted by a player's game." + '\0');
        return true;
    }
    if (name == "getTimeMessage.spd") { *out = answer(0x22, std::string(3, '\0')); return true; }
    if (name == "initializeCharacter.spd") {
        const std::string form = decrypt(body);
        const std::string id = form_value(form, "characterID") + form_value(form, "index");
        *out = answer(0x17, id + '\0');
        return true;
    }
    if (name == "getQWCData.spd") { *out = answer(0x0e, std::string(14 * 4, '\0')); return true; }   // neutral
    if (name == "addQWCData.spd") { *out = answer(0x09, ok); return true; }
    if (name == "getMultiPlayGrade.spd") { *out = answer(0x28, ok + std::string(6 * 4, '\0')); return true; }
    if (name == "getBloodMessageGrade.spd") { *out = answer(0x29, ok + std::string(4, '\0')); return true; }
    if (name == "getWanderingGhost.spd") { *out = answer(0x11, u32le(0) + u32le(0)); return true; }
    if (name == "setWanderingGhost.spd") { *out = answer(0x17, ok); return true; }
    if (name == "getBloodMessage.spd" || name == "getReplayList.spd") { *out = answer(0x1f, u32le(0)); return true; }
    if (name == "addBloodMessage.spd" || name == "addReplayData.spd") { *out = answer(0x1d, ok); return true; }
    if (name == "deleteBloodMessage.spd") { *out = answer(0x27, ok); return true; }
    if (name == "updateBloodMessageGrade.spd") { *out = answer(0x2a, ok); return true; }
    if (name == "getSosData.spd") { *out = answer(0x0f, u32le(0) + u32le(0)); return true; }
    if (name == "checkSosData.spd") { *out = answer(0x0b, std::string(1, '\0')); return true; }
    if (name == "addSosData.spd") { *out = answer(0x0a, ok); return true; }
    if (name == "summonOtherCharacter.spd") { *out = answer(0x0a, std::string(1, '\0')); return true; }   // no such sign
    if (name == "summonBlackGhost.spd") { *out = answer(0x23, std::string(1, '\0')); return true; }       // no invader
    if (name == "outOfBlock.spd" || name == "initializeMultiPlay.spd") { *out = answer(0x15, ok); return true; }
    if (name == "finalizeMultiPlay.spd") { *out = answer(0x21, ok); return true; }
    if (name == "updateOtherPlayerGrade.spd") { *out = answer(0x2b, ok); return true; }
    return false;
}

// Reads one HTTP request (the head, then Content-Length bytes); false on a timeout or a malformed one.
bool read_request(sock_t s, std::string* path, std::string* body) {
    std::string buf;
    char tmp[4096];
    size_t head_end = std::string::npos;
    while ((head_end = buf.find("\r\n\r\n")) == std::string::npos) {
        if (buf.size() > 64 * 1024) return false;
        const int n = (int)recv(s, tmp, sizeof tmp, 0);
        if (n <= 0) return false;
        buf.append(tmp, (size_t)n);
    }
    const std::string head = buf.substr(0, head_end);
    const size_t sp1 = head.find(' '), sp2 = sp1 == std::string::npos ? sp1 : head.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return false;
    *path = head.substr(sp1 + 1, sp2 - sp1 - 1);
    if (const size_t q = path->find('?'); q != std::string::npos) path->resize(q);
    size_t length = 0;
    for (size_t at = 0; at < head.size();) {
        const size_t eol = std::min(head.find("\r\n", at), head.size());
        std::string line = head.substr(at, eol - at);
        for (char& c : line) c = (char)std::tolower((uint8_t)c);
        if (!line.compare(0, 15, "content-length:")) length = (size_t)std::strtoul(line.c_str() + 15, nullptr, 10);
        at = eol + 2;
    }
    if (length > 1024 * 1024) return false;
    *body = buf.substr(head_end + 4);
    while (body->size() < length) {
        const int n = (int)recv(s, tmp, sizeof tmp, 0);
        if (n <= 0) return false;
        body->append(tmp, (size_t)n);
    }
    body->resize(length);
    return true;
}

void send_all(sock_t s, const std::string& data) {
    size_t at = 0;
    while (at < data.size()) {
        const int n = (int)send(s, data.data() + at, (int)(data.size() - at), 0);
        if (n <= 0) return;
        at += (size_t)n;
    }
}

void set_timeouts(sock_t s, int ms) {
#ifdef _WIN32
    const DWORD v = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&v, sizeof v);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&v, sizeof v);
#else
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
}

// Each connection: one request, one answer, closed (the game opens one per request).
void game_server_loop(sock_t listener) {
    int unknown = 0;
    for (;;) {
        sock_t c = accept(listener, nullptr, nullptr);
        if (c == kNoSock) {   // (no sockets or buffers left, say): a pause, not a core spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        set_timeouts(c, 5000);
        std::string path, body, reply;
        if (read_request(c, &path, &body)) {
            if (game_answer(path, body, &reply)) {
                const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                                         std::to_string(reply.size()) + "\r\nConnection: close\r\n\r\n";
                send_all(c, head + reply);
            } else if (unknown++ < 8) {
                std::fprintf(stderr, "[coop] the game's server: no answer for %s\n", path.c_str());
            }
        }
        sock_close(c);
    }
}

// The game's server on 127.0.0.1 (a port the system picks); 0 on failure.
int start_game_server() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kNoSock) return 0;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    socklen_t len = sizeof a;
    if (bind(s, (sockaddr*)&a, sizeof a) != 0 || listen(s, 16) != 0 || getsockname(s, (sockaddr*)&a, &len) != 0) {
        sock_close(s);
        return 0;
    }
    std::thread(game_server_loop, s).detach();
    return ntohs(a.sin_port);
}

// ---- the host: the router and the code ---------------------------------------------------------------------------

std::string encode(const char* ip, int port, uint32_t key) {
    net_invite inv{};
    unsigned b[4];
    if (std::sscanf(ip, "%u.%u.%u.%u", &b[0], &b[1], &b[2], &b[3]) != 4) return {};
    for (int i = 0; i < 4; i++) inv.ip[i] = (uint8_t)b[i];
    inv.port = (uint16_t)port;
    inv.key = key;
    char text[NET_INVITE_TEXT];
    net_invite_encode(&inv, text);
    return text;
}

void unmap_at_exit() { net_upnp_unmap(); }

// This PC's address on the way out (a UDP socket "connected" to a documentation address: nothing is sent), "" if
// there is no network.
std::string local_ip() {
    sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kNoSock) return {};
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(9);
    to.sin_addr.s_addr = htonl(0xC0000201u);   // 192.0.2.1 (TEST-NET-1)
    sockaddr_in me{};
    socklen_t len = sizeof me;
    std::string ip;
    if (connect(s, (sockaddr*)&to, sizeof to) == 0 && getsockname(s, (sockaddr*)&me, &len) == 0) {
        char buf[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, &me.sin_addr, buf, sizeof buf)) ip = buf;
    }
    sock_close(s);
    return ip == "0.0.0.0" ? std::string() : ip;
}

// For test runs that start a guest's game too (DES_SEAMLESS_CODE_FILE): the codes, one per line.
void write_code_file(const std::string& code, const std::string& lan_code) {
    const char* path = std::getenv("DES_SEAMLESS_CODE_FILE");
    if (!path || !*path) return;
    if (FILE* f = std::fopen(path, "wb")) {
        std::fprintf(f, "%s\n%s\n", code.c_str(), lan_code.c_str());
        std::fclose(f);
    }
}

// A joining test run's code (DES_SEAMLESS_CODE_FILE, with no DES_SEAMLESS_CODE): the host's file, once its game has
// written it (up to 2 minutes); the public code, else the local one.
std::string code_from_file() {
    const char* path = std::getenv("DES_SEAMLESS_CODE_FILE");
    if (!path || !*path) return {};
    for (int i = 0; i < 1200; i++) {
        if (FILE* f = std::fopen(path, "rb")) {
            char a[64] = {}, b[64] = {};
            const bool got = std::fgets(a, sizeof a, f) && std::fgets(b, sizeof b, f);
            std::fclose(f);
            if (got) {
                std::string code = a[0] == '\n' ? b : a;
                while (!code.empty() && (code.back() == '\n' || code.back() == '\r')) code.pop_back();
                return code;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return {};
}

// A public address as the player wrote it: 0 with it tidied into *out (no spaces, no leading zeros), 1 if it is
// private (a home network's, the provider's, loopback...), -1 if it isn't a dotted IPv4 address.
int tidy_public_ipv4(const std::string& text, std::string* out) {
    std::string t;
    for (const char c : text)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') t += c;
    const int priv = net_upnp_is_private_ipv4(t.c_str());
    if (priv != 0) return priv < 0 ? -1 : 1;
    unsigned b[4];
    if (std::sscanf(t.c_str(), "%u.%u.%u.%u", &b[0], &b[1], &b[2], &b[3]) != 4) return -1;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    *out = buf;
    return 0;
}

// The invite code and the words beside it (sLock held): from the address the player gave, else the router's answer.
void compose_locked() {
    sStatus.address_given = !sGiven.empty();
    if (sGiven.empty()) {
        sStatus.code = sRouter.code;
        sStatus.router_ok = sRouter.ok;
        sStatus.router = sRouter.text;
        return;
    }
    const std::string p = std::to_string(sPort);
    sStatus.code = encode(sGiven.c_str(), sPort, sKey);
    sStatus.router_ok = !sStatus.code.empty();
    sStatus.router = sRouter.behind
                         ? "The code uses the public address you gave. Your router opened port " + p +
                               ", but it is behind another router: forward port " + p +
                               " (TCP and UDP) on that one too, to your router."
                     : sRouter.opened ? "The code uses the public address you gave. Your router opened port " + p +
                                            " for co-op."
                                      : "The code uses the public address you gave. Port " + p +
                                            " (TCP and UDP) must be forwarded to this PC in your router's settings.";
}

// This PC's internet address from a STUN server, for a router that gave none ("" when off or no answer). On by
// default, with Google's server; DES_SEAMLESS_STUN=0 turns it off, DES_SEAMLESS_STUN=host:port names another one.
// *who says whose server it was, for the window.
std::string stun_public_ipv4(std::string* who) {
    const char* v = std::getenv("DES_SEAMLESS_STUN");
    if (v && !std::strcmp(v, "0")) return {};
    std::string host = kStunHost;
    uint16_t sport = kStunPort;
    if (v && *v) {
        const std::string s = v;
        const size_t colon = s.rfind(':');
        host = s.substr(0, colon);
        if (colon != std::string::npos && std::atoi(s.c_str() + colon + 1) > 0)
            sport = (uint16_t)std::atoi(s.c_str() + colon + 1);
    }
    *who = host == kStunHost ? "Google's STUN server" : "the STUN server " + host;
    char ip[16] = {};
    const int rc = net_stun_public_ipv4(host.c_str(), sport, 3500, ip, nullptr);
    if (rc != NET_STUN_OK) {
        std::fprintf(stderr, "[coop] STUN (%s): %s\n", host.c_str(), net_stun_error_text(rc));
        return {};
    }
    if (net_upnp_is_private_ipv4(ip) != 0) {   // a server on this side of the router: no use
        std::fprintf(stderr, "[coop] STUN (%s): it answered with a private address\n", host.c_str());
        return {};
    }
    std::fprintf(stderr, "[coop] STUN (%s): the internet address is known\n", host.c_str());   // (not the address)
    return ip;
}

// Off the game's thread: the router forwards the port, and its address makes the code. DES_SEAMLESS_UPNP=0 leaves
// the router alone (the port forwarded by hand). A public address the player gave (DES_SEAMLESS_ADDRESS, or the
// Co-op window's Public address) wins over the router's.
void router_thread(int port, uint32_t key) {
    char ext[16] = {};
    const char* upnp = std::getenv("DES_SEAMLESS_UPNP");
    const bool use_upnp = !(upnp && !std::strcmp(upnp, "0"));
    const int rc = use_upnp ? net_upnp_map((uint16_t)port, kUpnpName, 2000, ext) : NET_UPNP_ERR_NOT_BUILT;
    std::string lan = use_upnp ? net_upnp_lan_ip() : "";
    if (lan.empty()) lan = local_ip();
    RouterAnswer r;
    const std::string p = std::to_string(port);
    if (!use_upnp) {
        // Only DES_SEAMLESS_UPNP=0 gets here: the game's own UPnP use is off, which says nothing of the router's.
        r.text = "Co-op is set not to use UPnP (DES_SEAMLESS_UPNP=0), so your router wasn't asked. Forward port " + p +
                 " (TCP and UDP) to this PC in your router's settings, then choose Public address. Players on your "
                 "network can use the local code.";
    } else switch (rc) {
    case NET_UPNP_OK:
        r.ok = r.opened = true;
        r.code = encode(ext, port, key);
        r.text = "Your router opened port " + p + " for co-op.";
        break;
    case NET_UPNP_ERR_PRIVATE_IP:
        r.opened = r.behind = true;
        r.text = "Your router opened port " + p + ", but it is behind another router or your provider's network "
                 "(its own address is private), so friends can't reach it. Players on your network can use the "
                 "local code. If you can forward port " + p + " on the other router too, choose Public address.";
        break;
    case NET_UPNP_ERR_NO_ROUTER:
    case NET_UPNP_ERR_NOT_ROUTER:
        r.text = "No router answered for UPnP. Forward port " + p + " (TCP and UDP) to this PC in your router's "
                 "settings, then choose Public address. Players on your network can use the local code.";
        break;
    case NET_UPNP_ERR_TAKEN:
        r.text = "Port " + p + " is already forwarded to another device. Pick another port (DES_SEAMLESS_PORT) or "
                 "remove that forward in your router's settings.";
        break;
    default:
        r.text = std::string("The router did not open port ") + p + ": " + net_upnp_last_error();
        break;
    }
    // No public address from the router: a STUN server tells the address this PC's traffic leaves from, so the
    // code is right. Whether friends get through still depends on the port being forwarded, so the words say how.
    std::string who;
    const std::string stun_ip = r.code.empty() && rc != NET_UPNP_ERR_TAKEN ? stun_public_ipv4(&who) : std::string();
    if (!stun_ip.empty()) {
        r.code = encode(stun_ip.c_str(), port, key);
        const std::string from = "Your internet address came from " + who + ". ";
        if (!use_upnp)
            r.text = from + "Co-op is set not to use UPnP, so forward port " + p +
                     " (TCP and UDP) to this PC in your router's settings, or friends can't reach you.";
        else if (rc == NET_UPNP_ERR_PRIVATE_IP)
            r.text = from + "Your router opened port " + p + ", but it is behind another router or your provider's "
                     "network: friends reach you only if port " + p + " is forwarded on that one too. Players on "
                     "your network can use the local code.";
        else if (rc == NET_UPNP_ERR_NO_ROUTER || rc == NET_UPNP_ERR_NOT_ROUTER)
            r.text = from + "No router answered for UPnP: forward port " + p +
                     " (TCP and UDP) to this PC in your router's settings, or friends can't reach you.";
        else
            r.text = from + "The router did not open port " + p + " (" + net_upnp_last_error() +
                     "): forward it to this PC in your router's settings.";
    }
    if (r.opened) std::atexit(unmap_at_exit);
    const std::string lan_code = lan.empty() ? std::string() : encode(lan.c_str(), port, key);
    std::string code, said;
    bool given = false;
    {
        std::lock_guard<std::mutex> g(sLock);
        sRouter = r;
        compose_locked();
        sStatus.router_done = true;
        sStatus.lan_code = lan_code;
        code = sStatus.code;
        given = sStatus.address_given;
    }
    // (no addresses in the log)
    std::fprintf(stderr, "[coop] router: %s%s\n", r.ok ? "the port is open" : r.text.c_str(),
                 given ? "; the code uses the public address given" : "");
    write_code_file(code, lan_code);
}

}  // namespace

Mode mode() {
    const char* m = std::getenv("DES_SEAMLESS");
    if (m && !std::strcmp(m, "host")) return Mode::Host;
    if (m && !std::strcmp(m, "join")) return Mode::Join;
    return Mode::Off;
}

std::string check_code(const std::string& text, std::string* clean) {
    net_invite inv{};
    const int rc = net_invite_decode(text.c_str(), &inv);
    if (rc == NET_INVITE_NOT_A_CODE) return "That isn't an invite code: it has 16 letters and digits.";
    if (rc == NET_INVITE_TYPO) return "That code has a typo. Check it against the host's.";
    if (clean) {
        char t[NET_INVITE_TEXT];
        net_invite_encode(&inv, t);
        *clean = t;
    }
    return {};
}

bool start(const std::string& name, std::string* why) {
    const Mode m = mode();
    {
        std::lock_guard<std::mutex> g(sLock);
        sStatus = Status{};
        sStatus.mode = m;
    }
    auto fail = [&](const std::string& reason) {
        std::fprintf(stderr, "[coop] seamless co-op off: %s\n", reason.c_str());
        std::lock_guard<std::mutex> g(sLock);
        sStatus.error = reason;
        if (why) *why = reason;
        return false;
    };
    if (m == Mode::Off) return fail("not asked for");
    // Every disc plays with every other: the US and European discs are one program, and the Japanese and Asian 1.04
    // program is the same code at other addresses (port/regions/), with the same game data (docs/146).

    // The game's own server first: if it fails, nothing else has started (no room server, no router mapping)
    const int http = start_game_server();
    if (!http) return fail("the game's server did not start");

    std::string server;
    char key_hex[65] = {};
    int port = kDefaultPort;
    if (m == Mode::Host) {
        if (const char* p = std::getenv("DES_SEAMLESS_PORT"); p && std::atoi(p) > 0 && std::atoi(p) < 65536)
            port = std::atoi(p);
        // a test run's code file from an earlier run: gone until this run's codes are known (write_code_file)
        if (const char* cf = std::getenv("DES_SEAMLESS_CODE_FILE"); cf && *cf) std::remove(cf);
        const uint32_t key = net_invite_new_key();
        psnr_host_config cfg{};
        cfg.port = (uint16_t)port;
        cfg.has_key = 1;
        net_invite_key_blob(key, cfg.key);
        // Each disc signs in with its own communication ID: the European disc's (NPWR01249_00) and the Japanese and
        // Asian discs' (NPWR00245_00) play in the US disc's rooms (NPWR00881_00), as on the game server (its
        // sameGames). Without this a European friend's join of a US host's room fails as "no such room", and the
        // other way round.
        std::snprintf(cfg.same_game[0].other, sizeof cfg.same_game[0].other, "%s", "NPWR01249_00");
        std::snprintf(cfg.same_game[0].game, sizeof cfg.same_game[0].game, "%s", "NPWR00881_00");
        std::snprintf(cfg.same_game[1].other, sizeof cfg.same_game[1].other, "%s", "NPWR00245_00");
        std::snprintf(cfg.same_game[1].game, sizeof cfg.same_game[1].game, "%s", "NPWR00881_00");
        char err[160] = {};
        sHost = psnr_host_start(&cfg, err, sizeof err);
        if (!sHost) return fail(std::string("the room server did not start: ") + err);
        port = psnr_host_port(sHost);
        {
            std::lock_guard<std::mutex> g(sLock);
            sKey = key;
            sPort = port;
            sGiven.clear();
            if (env_given("DES_SEAMLESS_ADDRESS") &&
                tidy_public_ipv4(std::getenv("DES_SEAMLESS_ADDRESS"), &sGiven) != 0)
                std::fprintf(stderr, "[coop] DES_SEAMLESS_ADDRESS is not a public IPv4 address: the router's is used\n");
        }
        net_invite_key_hex(key, key_hex);
        server = "127.0.0.1:" + std::to_string(port);
        std::thread(router_thread, port, key).detach();
    } else {
        std::string code = env_given("DES_SEAMLESS_CODE") ? std::getenv("DES_SEAMLESS_CODE") : "";
        if (code.empty()) code = code_from_file();
        net_invite inv{};
        if (code.empty() || net_invite_decode(code.c_str(), &inv) != 0) return fail("no invite code, or not a valid one");
        net_invite_key_hex(inv.key, key_hex);
        char ip[16];
        std::snprintf(ip, sizeof ip, "%u.%u.%u.%u", inv.ip[0], inv.ip[1], inv.ip[2], inv.ip[3]);
        server = std::string(ip) + ":" + std::to_string(inv.port);
        port = inv.port;
    }
    knob("PS3_NET_ONLINE", "1");
    // With a host map, a name it doesn't list doesn't resolve, and an IPv4 address (PSNR_SERVER's) is used as it is
    // (ps3recomp's ps3_net_resolve). So no name the game looks up reaches another server.
    knob("PS3_NET_HOSTS", "localhost=127.0.0.1");
    knob("PS3_HTTP_LOCAL", std::to_string(http));
    knob("PS3_NP_ONLINE_ID", name);
    knob("PSNR_SERVER", server);
    knob("PSNR_KEY", key_hex);
    std::memset(key_hex, 0, sizeof key_hex);
    // The seamless characters: their own save folder (beside the normal one), so co-op never writes those.
    if (!env_given("PS3_SAVEDATA_ROOT")) ps3_setenv("PS3_SAVEDATA_ROOT", coop_save_root());

    std::fprintf(stderr, "[coop] seamless co-op: %s, room server port %d\n", m == Mode::Host ? "hosting" : "joining",
                 port);
    sActive = static_cast<int>(m);
    std::lock_guard<std::mutex> g(sLock);
    sStatus.started = true;
    sStatus.port = port;
    // (unless the router's answer is in already: with DES_SEAMLESS_UPNP=0 it comes at once)
    if (m == Mode::Host && !sStatus.router_done)
        sStatus.router = "Asking your router to open port " + std::to_string(port) + "...";
    return true;
}

Mode active() { return static_cast<Mode>(sActive.load(std::memory_order_relaxed)); }

Status status() {
    std::lock_guard<std::mutex> g(sLock);
    Status s = sStatus;
    if (sHost) s.players = psnr_host_players(sHost);
    if (s.mode == Mode::Join) s.link = guest_link();
    return s;
}

std::string set_public_address(const std::string& text, std::string* clean) {
    std::string ip;
    bool blank = true;
    for (const char c : text) blank = blank && (c == ' ' || c == '\t' || c == '\r' || c == '\n');
    if (!blank) {
        const int r = tidy_public_ipv4(text, &ip);
        if (r < 0) return "That isn't an internet address: four numbers from 0 to 255 with dots, like 203.0.113.7.";
        if (r > 0)
            return "That address only works inside a home or provider network. Give the one the internet sees: your "
                   "router's status page shows it, or a \"what is my IP\" website.";
    }
    std::string code, lan_code;
    bool done = false;
    {
        std::lock_guard<std::mutex> g(sLock);
        if (sStatus.mode != Mode::Host || !sStatus.started) return "Only while hosting.";
        sGiven = ip;
        done = sStatus.router_done;
        if (done) compose_locked();   // (else the router's answer, when it comes, uses it)
        code = sStatus.code;
        lan_code = sStatus.lan_code;
    }
    std::fprintf(stderr, "[coop] public address %s\n", ip.empty() ? "cleared: the router's is used" : "given");
    if (done) write_code_file(code, lan_code);
    if (clean) *clean = ip;
    return {};
}

}  // namespace des::seamless
