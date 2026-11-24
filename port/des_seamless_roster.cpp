// Written for Demon's Souls. Who is in the seamless co-op session: see des_seamless_roster.h and docs/169.
//
// STAT (each co-op game, through the host's room server to every other game connected there): u32 flags, u32 the area
// it is in, i32 HP, i32 most HP, i32 its ping to the host's game in ms (-1: none, the host), u8 the name's length,
// then its online name (np_psnr_online_id, at most 32 bytes), then f32 x, y, z: the ground its player last stood on
// (NaN: not known; for Warp, after 11-24: an older game's STAT ends at the name). Sent at once when the flags or the
// area change, and every 2 s. Flags: 1 in the session, 2 the host, 4 dead, 8 soul form, 16 loading, 32 in a cutscene, 64 in a boss
// fight, 128 waits for a place (the host's game is full), 256 waits for the host's boss fight to end, 512 leaving.
// A game in the session heard from more than 6 s ago shows as "No answer for N s", and goes after 30 s; one out of
// it (waiting, or leaving) goes as soon as it is silent for 6 s: it left.
// Each game logs the list ("[coop] session: ...") when someone comes, goes or changes state, not for HP or ping.
//
// PNG? / PNG! (the ping): a friend's game sends PNG? (u32 its nonce, u32 a count) every 2 s; the host's game sends it
// back as PNG! from its next frame, to every game, and the friend whose nonce it is takes the round trip. A friend keeps
// the median of its last five, and gives it in its STAT, so every game shows each friend's ping to the host. It is
// the time through both games and the room server, to within a frame on each side.
#include "des_seamless_roster.h"

#include "ps3emu/psnr_host.h"   // psnr_app_send

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <random>

extern "C" const char* np_psnr_online_id(void);   // ps3recomp's np_psnr.c: this player's online name
extern "C" int np_psnr_connected(void);           // ... connected to the room server

namespace des::seamless {
namespace {

using clock_type = std::chrono::steady_clock;

constexpr char kMsgStat[4] = {'S', 'T', 'A', 'T'};
constexpr char kMsgPing[4] = {'P', 'N', 'G', '?'};
constexpr char kMsgPong[4] = {'P', 'N', 'G', '!'};
constexpr uint32_t kIn = 1, kHost = 2, kDead = 4, kSoul = 8, kLoading = 16, kCut = 32, kFight = 64, kWaitFull = 128,
                   kWaitFight = 256, kLeaving = 512;
constexpr size_t kNameMax = 32;
constexpr auto kSendEvery = std::chrono::seconds(2);
constexpr auto kQuiet = std::chrono::seconds(6);
constexpr auto kGone = std::chrono::seconds(30);
constexpr size_t kPings = 5;

struct Peer {
    std::string name;
    uint32_t flags = 0, area = 0xFFFFFFFFu;
    int hp = 0, hp_max = 0, ping = -1;
    clock_type::time_point heard;
    uint64_t order = 0;   // when it was first heard (the rows' order)
    bool ground = false;  // the ground it last stood on (place), for Warp
    float place[3] = {0, 0, 0};
};

std::mutex sLock;
std::map<uint32_t, Peer> sPeers;   // the others' last STAT, by the sender's user number
uint64_t sOrder = 0;
RosterFrame sOwn;                  // this game's, as last ticked
int sOwnPing = -1;                 // a friend's median ping to the host
std::deque<int> sRtts;             // its last round trips (ms)
std::deque<std::vector<uint8_t>> sAnswers;   // the host: PNG?s to send back as PNG!, from the next frame
const uint32_t sNonce = std::random_device{}();
std::map<uint32_t, clock_type::time_point> sPingSent;   // a friend: its PNG?s out, by count
clock_type::time_point sSince{};   // this game in the session since (epoch: not in it)
// Warp and Unstuck: the ask (0 none, 1 Warp to sAskUser, 2 Unstuck) and what the last one did, until sSaidUntil
int sAsk = 0;
uint32_t sAskUser = 0;
std::string sSaid;
clock_type::time_point sSaidUntil{};

uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
float getf(const uint8_t* p)
{
    const uint32_t u = get32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
void put32(std::vector<uint8_t>& b, uint32_t v)
{
    for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i)));
}

std::string clean_name(const uint8_t* p, size_t n)
{
    std::string s;
    for (size_t i = 0; i < n && s.size() < 16; i++) {
        const char c = (char)p[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
            s += c;
        else
            return {};
    }
    return s;
}

uint32_t own_flags(const RosterFrame& f)
{
    uint32_t x = 0;
    if (f.in_session) x |= kIn;
    if (f.hosting) x |= kHost;
    if (f.dead) x |= kDead;
    if (f.soul) x |= kSoul;
    if (!f.world) x |= kLoading;
    if (f.cutscene) x |= kCut;
    if (f.fight) x |= kFight;
    if (f.wait_full) x |= kWaitFull;
    if (f.wait_fight) x |= kWaitFight;
    if (f.leaving) x |= kLeaving;
    return x;
}

void send_stat(uint32_t flags, const RosterFrame& f, int ping)
{
    const char* n = np_psnr_online_id();
    const size_t len = n ? std::min(std::strlen(n), kNameMax) : 0;
    std::vector<uint8_t> m(kMsgStat, kMsgStat + 4);
    put32(m, flags);
    put32(m, f.area);
    put32(m, (uint32_t)f.hp);
    put32(m, (uint32_t)f.hp_max);
    put32(m, (uint32_t)ping);
    m.push_back((uint8_t)len);
    m.insert(m.end(), n, n + len);
    for (int i = 0; i < 3; i++) {
        const float v = f.ground ? f.place[i] : NAN;
        uint32_t u;
        std::memcpy(&u, &v, 4);
        put32(m, u);
    }
    psnr_app_send(m.data(), (uint32_t)m.size());
}

std::string state_of(uint32_t flags)
{
    if (flags & kLeaving) return "Leaving co-op";
    if (!(flags & kIn)) {
        if (flags & kWaitFull) return "Waits for a place (the game is full)";
        if (flags & kWaitFight) return "Waits for the boss fight to end";
        return "Connecting";
    }
    if (flags & kDead) return "Dead, back soon";
    if (flags & kLoading) return "Loading";
    if (flags & kCut) return "In a cutscene";
    if (flags & kFight) return "In a boss fight";
    return "Playing";
}

SessionPlayer row_of(const std::string& name, uint32_t flags, uint32_t area, int hp, int hp_max, int ping)
{
    SessionPlayer r;
    r.name = name;
    r.host = flags & kHost;
    r.in_session = flags & kIn;
    r.state = state_of(flags);
    r.area = (flags & kLoading) ? 0xFFFFFFFFu : area;
    r.soul = flags & kSoul;
    r.hp = hp;
    r.hp_max = hp_max;
    r.ping_ms = r.host ? -1 : ping;
    return r;
}

}  // namespace

// The list in the log when someone comes, goes or changes state (the ping and HP, which change all the time, are in
// the line but don't make one). Once a second at most.
static void log_changes(clock_type::time_point now)
{
    static clock_type::time_point checked;
    static std::string last;
    if (now - checked < std::chrono::seconds(1)) return;
    checked = now;
    std::string key, line;
    for (const SessionPlayer& p : session_players()) {
        const std::string who = (p.name.empty() ? std::string("?") : p.name) + (p.host ? " (host)" : "") +
                                (p.you ? " (you)" : "");
        const std::string state = p.state.rfind("No answer", 0) == 0 ? std::string("No answer") : p.state;
        key += who + (p.in_session ? " in: " : " out: ") + state + "; ";
        line += (line.empty() ? "" : "; ") + who + (p.in_session ? "" : " (waiting)") + ": " + p.state;
        if (p.ping_ms >= 0) line += ", ping " + std::to_string(p.ping_ms) + " ms";
    }
    if (key == last) return;
    last = key;
    std::fprintf(stderr, "[coop] session: %s\n", line.empty() ? "none" : line.c_str());
}

bool roster_message(uint32_t from, const uint8_t* d, uint32_t n)
{
    if (n < 4) return false;
    if (!std::memcmp(d, kMsgPing, 4)) {   // a friend's: the host's game sends it back from its next frame
        if (n >= 12) {
            std::lock_guard<std::mutex> l(sLock);
            if (sOwn.hosting && sAnswers.size() < 16) sAnswers.emplace_back(d + 4, d + 12);
        }
        return true;
    }
    if (!std::memcmp(d, kMsgPong, 4)) {
        if (n >= 12 && get32(d + 4) == sNonce) {
            std::lock_guard<std::mutex> l(sLock);
            const auto it = sPingSent.find(get32(d + 8));
            if (it != sPingSent.end()) {
                const int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() -
                                                                                          it->second).count();
                sPingSent.erase(it);
                sRtts.push_back(ms);
                while (sRtts.size() > kPings) sRtts.pop_front();
                std::vector<int> v(sRtts.begin(), sRtts.end());
                std::sort(v.begin(), v.end());
                sOwnPing = v[v.size() / 2];
            }
        }
        return true;
    }
    if (std::memcmp(d, kMsgStat, 4)) return false;
    if (n < 25) return true;
    Peer p;
    p.flags = get32(d + 4);
    p.area = get32(d + 8);
    p.hp = (int)get32(d + 12);
    p.hp_max = (int)get32(d + 16);
    p.ping = (int)get32(d + 20);
    p.name = clean_name(d + 25, std::min<size_t>({d[24], n - 25, kNameMax}));
    if (const size_t at = 25 + (size_t)d[24]; n >= at + 12) {
        for (int i = 0; i < 3; i++) p.place[i] = getf(d + at + 4 * i);
        p.ground = std::isfinite(p.place[0]) && std::isfinite(p.place[1]) && std::isfinite(p.place[2]);
    }
    p.heard = clock_type::now();
    std::lock_guard<std::mutex> l(sLock);
    const auto it = sPeers.find(from);
    p.order = it != sPeers.end() ? it->second.order : ++sOrder;
    sPeers[from] = p;
    return true;
}

void roster_tick(const RosterFrame& f)
{
    static uint32_t sent_flags = ~0u, sent_area = ~0u;
    static clock_type::time_point sent_at, pinged_at;
    static uint32_t count = 0;
    const auto now = clock_type::now();
    std::deque<std::vector<uint8_t>> answers;
    int ping;
    {
        std::lock_guard<std::mutex> l(sLock);
        if (f.in_session && !sOwn.in_session) sSince = now;
        if (!f.in_session) sSince = {};
        sOwn = f;
        if (f.joining && !np_psnr_connected()) {   // (the host's game is gone: so are the others)
            sPeers.clear();
            sRtts.clear();
            sPingSent.clear();
            sOwnPing = -1;
        }
        for (auto it = sPeers.begin(); it != sPeers.end();)
            it = now - it->second.heard >= kGone ? sPeers.erase(it) : std::next(it);
        answers.swap(sAnswers);
        ping = sOwnPing;
    }
    if (!(f.hosting || f.joining) || !np_psnr_connected()) return;
    for (const auto& a : answers) {
        std::vector<uint8_t> m(kMsgPong, kMsgPong + 4);
        m.insert(m.end(), a.begin(), a.end());
        psnr_app_send(m.data(), (uint32_t)m.size());
    }
    if (f.joining && now - pinged_at >= kSendEvery) {
        pinged_at = now;
        std::vector<uint8_t> m(kMsgPing, kMsgPing + 4);
        put32(m, sNonce);
        put32(m, ++count);
        {
            std::lock_guard<std::mutex> l(sLock);
            sPingSent[count] = now;
            while (sPingSent.size() > 8) sPingSent.erase(sPingSent.begin());
        }
        psnr_app_send(m.data(), (uint32_t)m.size());
    }
    const uint32_t flags = own_flags(f);
    if (flags != sent_flags || f.area != sent_area || now - sent_at >= kSendEvery) {
        send_stat(flags, f, f.joining ? ping : -1);
        sent_flags = flags;
        sent_area = f.area;
        sent_at = now;
    }
    log_changes(now);
}

std::vector<SessionPlayer> session_players()
{
    std::vector<SessionPlayer> in, waiting;
    std::lock_guard<std::mutex> l(sLock);
    if (!sOwn.hosting && !sOwn.joining) return {};
    const auto now = clock_type::now();
    std::vector<std::pair<uint64_t, SessionPlayer>> others;
    for (const auto& [user, p] : sPeers) {
        SessionPlayer r = row_of(p.name, p.flags, p.area, p.hp, p.hp_max, p.ping);
        r.user = user;
        if (now - p.heard >= kQuiet && (!(p.flags & kIn) || (p.flags & kLeaving))) continue;   // (it left)
        if (now - p.heard >= kQuiet) {
            r.state = "No answer for " +
                      std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now - p.heard).count()) + " s";
            r.area = 0xFFFFFFFFu;
        }
        others.emplace_back(r.host ? 0 : p.order, r);
    }
    const char* own_name = np_psnr_online_id();
    SessionPlayer me = row_of(own_name ? clean_name((const uint8_t*)own_name, std::strlen(own_name)) : "",
                              own_flags(sOwn), sOwn.area, sOwn.hp, sOwn.hp_max, sOwnPing);
    me.you = true;
    others.emplace_back(sOwn.hosting ? 0 : ~0ull, me);
    std::stable_sort(others.begin(), others.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [order, r] : others) (r.in_session ? in : waiting).push_back(r);
    in.insert(in.end(), waiting.begin(), waiting.end());
    return in;
}

int session_seconds()
{
    std::lock_guard<std::mutex> l(sLock);
    if (sSince == clock_type::time_point{}) return 0;
    return (int)std::chrono::duration_cast<std::chrono::seconds>(clock_type::now() - sSince).count();
}

std::string area_label(uint32_t area)
{
    if (area == 0xFFFFFFFFu) return {};
    const uint32_t map = area >> 24, block = (area >> 16) & 0xFF;
    // The map files' numbers are not the worlds' (m02 Boletaria is world 1, m06 Stonefang 2, m04 Latria 3, m03 the
    // Shrine 4, m05 the Valley 5); the Shrine's areas start at its block 1 (4-1 is m03_01).
    struct World { uint32_t map; const char* name; int number; int first; };
    static const World kWorlds[] = {{2, "Boletarian Palace", 1, 0}, {6, "Stonefang Tunnel", 2, 0},
                                    {4, "Tower of Latria", 3, 0},   {3, "Shrine of Storms", 4, 1},
                                    {5, "Valley of Defilement", 5, 0}};
    if (map == 1) return "The Nexus";
    if (map == 7) return "Northern Limit";
    if (map == 8) return "Tutorial";
    for (const World& w : kWorlds)
        if (w.map == map && (int)block >= w.first)
            return std::string(w.name) + " " + std::to_string(w.number) + "-" + std::to_string((int)block - w.first + 1);
    return {};
}


namespace {

// Why this game can't move now (under sLock), "" when it can.
std::string own_blocked(const RosterFrame& f)
{
    if (!f.hosting && !f.joining) return "Only in co-op.";
    if (!f.world) return "Not while you load.";
    if (f.dead) return "Not while you're dead.";
    if (f.cutscene) return "Not during a cutscene.";
    if (f.fight) return "Not in a boss fight.";
    return {};
}

std::string warp_blocked_locked(uint32_t user)   // (under sLock)
{
    if (!sOwn.in_session) return "Only with others in the session.";
    if (std::string own = own_blocked(sOwn); !own.empty()) return own;
    const auto it = sPeers.find(user);
    if (it == sPeers.end() || clock_type::now() - it->second.heard >= kQuiet) return "No answer from that player.";
    const Peer& p = it->second;
    const std::string who = p.name.empty() ? std::string("That player") : p.name;
    if (p.flags & kLeaving) return who + " is leaving co-op.";
    if (!(p.flags & kIn)) return who + " isn't in the session yet.";
    if (p.flags & kLoading) return who + " is loading.";
    if (p.flags & kDead) return who + " is dead.";
    if (p.flags & kCut) return who + " is in a cutscene.";
    if (p.flags & kFight) return who + " is in a boss fight.";
    // (the same world: one load brings in all its areas, and the area number changes as a player walks from one of its
    // blocks to the next: 1-2's Archstone, in the Phalanx room, is 02000000 of world 2, its start 02010000)
    if ((p.area >> 24) != (sOwn.area >> 24)) return who + " is in another area.";
    if (!p.ground) return "Where " + who + " stands isn't known yet.";
    return {};
}

std::string unstuck_blocked_locked()   // (under sLock)
{
    if (std::string own = own_blocked(sOwn); !own.empty()) return own;
    if (!sOwn.arrived || (sOwn.arrival_area >> 24) != (sOwn.area >> 24)) return "Where you arrived here isn't known yet.";
    return {};
}

void say(const std::string& line)   // (under sLock)
{
    sSaid = line;
    sSaidUntil = clock_type::now() + std::chrono::seconds(5);
}

}  // namespace

std::string warp_blocked(uint32_t user)
{
    std::lock_guard<std::mutex> l(sLock);
    return warp_blocked_locked(user);
}

std::string unstuck_blocked()
{
    std::lock_guard<std::mutex> l(sLock);
    return unstuck_blocked_locked();
}

void ask_warp(uint32_t user)
{
    std::lock_guard<std::mutex> l(sLock);
    sAsk = 1;
    sAskUser = user;
}

void ask_unstuck()
{
    std::lock_guard<std::mutex> l(sLock);
    sAsk = 2;
}

int take_move(float* xyz, std::string* what)
{
    std::lock_guard<std::mutex> l(sLock);
    const int ask = sAsk;
    sAsk = 0;
    if (!ask) return 0;
    if (ask == 1) {
        const auto it = sPeers.find(sAskUser);
        const std::string who = it == sPeers.end() || it->second.name.empty() ? std::string("a player") : it->second.name;
        *what = "Warp to " + who;
        if (const std::string no = warp_blocked_locked(sAskUser); !no.empty()) {
            say(no);
            *what += ": " + no;
            return -1;
        }
        for (int i = 0; i < 3; i++) xyz[i] = it->second.place[i];
        say("You warped to " + who + ".");
        return 1;
    }
    *what = "Unstuck";
    if (const std::string no = unstuck_blocked_locked(); !no.empty()) {
        say(no);
        *what += ": " + no;
        return -1;
    }
    for (int i = 0; i < 3; i++) xyz[i] = sOwn.arrival[i];
    say("You're back where you arrived in this area.");
    return 1;
}

std::string last_move()
{
    std::lock_guard<std::mutex> l(sLock);
    return clock_type::now() < sSaidUntil ? sSaid : std::string();
}

}  // namespace des::seamless
