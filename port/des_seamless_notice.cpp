// Written for Demon's Souls. Seamless co-op's notices: see des_seamless_notice.h and docs/146 ("Notices").
//
// NAME (each co-op game, through the host's room server to every other game connected there): u32 flags, u32 the
// player character's id (+0x498, 0 with none), u8 the name's length, then the online name (np_psnr_online_id, at most
// 32 bytes). Sent at once when the flags change, and every 3 s. Flags:
//   1 in the session (the host: its room is up; a friend: in the host's world), 2 the host, 4 dead (HP 0),
//   8 waits for a place (the host's game is full), 16 waits for the host's boss fight to end,
//   bits 8-9, the host while dead: how it comes back (1 alone, 2 once the boss is dead, 3 everyone goes back).
// Each game keeps the others' last NAME by the sender's user number, and a change in it is a notice: a join, a leave,
// a death, a return, a wait to join (the host only). A game that sends none for 12 s while its link is up stopped
// answering (it closed, or its connection dropped).
//
// A friend's own join comes from its own state: in the host's world (you joined), and the Co-op window's link and
// join line (full, a boss fight, the connection lost, a join that can't get through). The host's own trouble comes from
// the same window's lines: its room code can't be used (the server that introduces players), its code spent, a friend
// that found it but couldn't get through. Co-op that didn't start at all (des_seamless.cpp: the room server's port
// taken, no online name, a bad code) is the one notice shown with the setting off too: it blocks co-op, and
// the player would otherwise only find out by opening the menu.
//
// No spam: the same text again within 60 s is dropped, at most 6 wait, and none shows during a load or a cutscene
// (they wait, and go if held for more than 90 s).
#include "des_seamless_notice.h"

#include "des_seamless.h"
#include "ps3emu/psnr_host.h"   // psnr_app_send

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>

extern "C" const char* np_psnr_online_id(void);   // ps3recomp's np_psnr.c: this player's online name
extern "C" int np_psnr_connected(void);           // ... connected to the room server

namespace des::seamless {
namespace {

using clock_type = std::chrono::steady_clock;

constexpr char kMsgName[4] = {'N', 'A', 'M', 'E'};
constexpr uint32_t kInSession = 1, kIsHost = 2, kDead = 4, kWaitFull = 8, kWaitFight = 16;
constexpr uint32_t kDeathShift = 8, kDeathMask = 3u << kDeathShift;
constexpr uint32_t kDeathAlone = 1, kDeathFight = 2, kDeathAll = 3;
constexpr size_t kNameMax = 32;
constexpr auto kSendEvery = std::chrono::seconds(3);
constexpr auto kSilent = std::chrono::seconds(12);
constexpr auto kSame = std::chrono::seconds(60);
constexpr auto kStale = std::chrono::seconds(90);
constexpr size_t kMaxWaiting = 6;
constexpr int kLinkFrames = 30;   // the link and join line are read every this many frames

struct Peer {
    std::string name;   // "" if it sent none
    uint32_t flags = 0;
    uint32_t id = 0;
    clock_type::time_point heard;
};

struct Held {
    Notice notice;
    clock_type::time_point at;
};

std::mutex sLock;
std::map<uint32_t, Peer> sPeers;                    // the others' last NAME, by the sender's user number
std::deque<Held> sWaiting;                          // queued for the UI
std::map<std::string, clock_type::time_point> sSaid;   // each text, when it was last queued
bool sInSession = false;                            // this game in the session: the others' joins and leaves count
bool sHosting = false;
uint32_t sHostDeath = 0;                            // the host's own death (kDeath*), while it is dead
std::atomic<bool> sHold{false};
std::atomic<bool> sAny{false};                      // sWaiting has some (cheap for the UI's frame hook)

uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
void put32(std::vector<uint8_t>& b, uint32_t v)
{
    for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i)));
}

// An online name as the runtime makes them (3-16 letters, digits, - or _), so nothing else reaches the screen.
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

void say_locked(bool warn, const std::string& text, bool always = false)
{
    const auto now = clock_type::now();
    for (auto it = sSaid.begin(); it != sSaid.end();) it = now - it->second >= kSame ? sSaid.erase(it) : std::next(it);
    if (sSaid.count(text)) return;
    sSaid[text] = now;
    if (sWaiting.size() >= kMaxWaiting) sWaiting.pop_front();
    sWaiting.push_back({{warn, always, text}, now});
    sAny = true;
    std::fprintf(stderr, "[coop] notice%s: %s\n", always ? " (shown with the setting off too)" : "", text.c_str());
}

std::string who(const Peer& p) { return p.name.empty() ? std::string("A player") : p.name; }

// The host's name (a friend's), kept past a lost connection for the notice that says so: as the start of a
// possessive ("StoneA's"), or "The host's" / "the host's" before its first NAME.
std::string sHostName;
std::string host_s_locked(bool start)
{
    return !sHostName.empty() ? sHostName + "'s" : start ? "The host's" : "the host's";
}
std::string host_locked(bool start)
{
    return !sHostName.empty() ? sHostName : start ? "The host" : "the host";
}

bool friends_in_locked()
{
    for (const auto& [user, p] : sPeers)
        if ((p.flags & kInSession) && !(p.flags & kIsHost)) return true;
    return false;
}

// Deaths and returns ("X died.", "X is back.", the host's own death) are not shown for now: the user, 11-23,
// after the first play test ("disable the death related notifications for now"). The games still send their dead
// state, so turning this on again needs no other change.
constexpr bool kDeathNotices = false;

// A NAME from another game, against its last one.
void heard_locked(const Peer& was, bool known, const Peer& p)
{
    const bool host = p.flags & kIsHost;
    const bool in = p.flags & kInSession, was_in = known && (was.flags & kInSession);
    if (sInSession && known) {
        if (!was_in && in && !host) say_locked(false, who(p) + (sHosting ? " joined your world." : " joined."));
        if (was_in && !in && !host) say_locked(false, who(p) + " left co-op.");   // (the host's: the friend's link says)
        if (was_in && in && kDeathNotices) {
            const bool dead = p.flags & kDead, was_dead = was.flags & kDead;
            if (dead && !was_dead) {
                const uint32_t how = (p.flags & kDeathMask) >> kDeathShift;
                if (!host) say_locked(false, who(p) + " died.");
                else if (how == kDeathAlone) say_locked(false, who(p) + " died and respawns alone. Play on.");
                else if (how == kDeathFight)
                    say_locked(false, who(p) + " died in the boss fight and comes back once the boss is dead.");
                else say_locked(false, who(p) + " died, and everyone goes back to the area's start.");
            }
            if (was_dead && !dead) say_locked(false, who(p) + " is back.");
        }
    }
    // The host: who waits to join it (a friend's game connected but not in the session)
    if (sHosting && !in) {
        const bool full = p.flags & kWaitFull, fight = p.flags & kWaitFight;
        if (full && !(known && (was.flags & kWaitFull))) say_locked(false, who(p) + " waits to join: your game is full.");
        if (fight && !(known && (was.flags & kWaitFight)))
            say_locked(false, who(p) + " joins once your boss fight is over.");
    }
}

// This game's NAME.
void send_name(uint32_t flags, uint32_t id)
{
    const char* n = np_psnr_online_id();
    const size_t len = n ? std::min(std::strlen(n), kNameMax) : 0;
    std::vector<uint8_t> m(kMsgName, kMsgName + 4);
    put32(m, flags);
    put32(m, id);
    m.push_back((uint8_t)len);
    m.insert(m.end(), n, n + len);
    psnr_app_send(m.data(), (uint32_t)m.size());
}

}  // namespace

bool notice_message(uint32_t from, const uint8_t* d, uint32_t n)
{
    if (n < 4 || std::memcmp(d, kMsgName, 4)) return false;
    if (n < 13) return true;
    const size_t len = std::min<size_t>({d[12], n - 13, kNameMax});
    Peer p;
    p.flags = get32(d + 4);
    p.id = get32(d + 8);
    p.name = clean_name(d + 13, len);
    p.heard = clock_type::now();
    std::lock_guard<std::mutex> l(sLock);
    const auto it = sPeers.find(from);
    const bool known = it != sPeers.end();
    const Peer was = known ? it->second : Peer{};
    sPeers[from] = p;
    if ((p.flags & kIsHost) && !p.name.empty()) sHostName = p.name;
    heard_locked(was, known, p);
    return true;
}

void notice_host_died(bool alone, bool stands, bool fight)
{
    std::lock_guard<std::mutex> l(sLock);
    sHostDeath = !alone || !stands ? kDeathAll : fight ? kDeathFight : kDeathAlone;
    if (!kDeathNotices || !friends_in_locked()) return;   // (alone in its session: its death is the game's own)
    if (!alone)
        say_locked(false, "Everyone goes back with you (Settings: When you die as host, Everyone goes back).");
    else if (!stands)
        say_locked(false, "No friend is standing, so everyone goes back with you.");
    else if (fight)
        say_locked(false, "You come back at your bloodstain once the boss is dead. Your friends fight on.");
    else
        say_locked(false, "You respawn alone at the area's start. Your friends play on.");
}

void notice_start_failed(bool host, const std::string& why)
{
    std::string w = why;
    if (!w.empty() && w.back() != '.') w += '.';
    std::lock_guard<std::mutex> l(sLock);
    say_locked(true, std::string(host ? "Hosting" : "Joining") + " didn't start: " + w +
                         " Seamless co-op, in the menu, says more.", true);
}

void notices_tick(const NoticeFrame& f)
{
    static NoticeFrame last;
    static bool intro_trouble = false, spent = false, stuck = false;   // the host's, as last told
    static uint32_t sent_flags = ~0u;
    static clock_type::time_point sent_at;
    static int frames = 0;
    static Status::Link link = Status::Link::Connecting;
    static std::string warned;   // the join line last told
    static bool lost = false;    // the link was lost, and isn't back: its notice says it all (the host closed, mostly)
    const auto now = clock_type::now();
    sHold = f.hold;

    // A friend's own join: the link and the join line (the Co-op window's), every kLinkFrames frames
    Status::Link now_link = link;
    std::string join;
    bool join_warn = false;
    if (f.joining && ++frames >= kLinkFrames) {
        frames = 0;
        const Status s = status();
        now_link = s.link;
        join = s.join;
        join_warn = s.join_warn;
    }
    // The host's own trouble once it hosts: the Co-op page's lines, which a player in the game doesn't see
    if (f.hosting && ++frames >= kLinkFrames) {
        frames = 0;
        const Status s = status();
        std::lock_guard<std::mutex> l(sLock);
        if (s.intro_trouble && !intro_trouble)
            say_locked(true, "Friends can't use your room code now. " + s.intro);
        if (s.code_spent && !spent)
            say_locked(true, "Too many wrong codes came, so your invite code lets no one else in. Seamless co-op, in "
                             "the menu, makes a new one.");
        if (!s.stuck.empty() && !stuck) say_locked(true, s.stuck);
        intro_trouble = s.intro_trouble;
        spent = s.code_spent;
        stuck = !s.stuck.empty();
    }

    uint32_t flags = 0;
    {
        std::lock_guard<std::mutex> l(sLock);
        sHosting = f.hosting;
        if (f.joining && !np_psnr_connected()) sPeers.clear();   // (the host's game is gone: so is everyone's NAME)
        // stopped answering: a game silent for kSilent while this one's link is up
        for (auto it = sPeers.begin(); it != sPeers.end();) {
            if (now - it->second.heard < kSilent) { ++it; continue; }
            if (sInSession && (it->second.flags & kInSession) && !(it->second.flags & kIsHost))
                say_locked(true, who(it->second) + "'s game stopped answering.");
            it = sPeers.erase(it);
        }
        if (f.joining && f.in_session && !last.in_session) say_locked(false, "You joined " + host_s_locked(false) + " world.");
        if (f.joining && now_link != link) {
            if (now_link == Status::Link::Full)
                say_locked(false, host_s_locked(true) + " game is full. You join as soon as a place frees up.");
            else if (now_link == Status::Link::Fight)
                say_locked(false, host_locked(true) + " is in a boss fight. You join once it's over.");
            else if (now_link == Status::Link::Lost)
                say_locked(true, "Lost the connection to " + host_s_locked(false) +
                                     " game. You go back to your own world, and join again once it's back.");
            if (now_link == Status::Link::Lost) lost = true;
            if (now_link == Status::Link::Connected) {
                warned.clear();
                lost = false;
            }
            link = now_link;
        }
        // (after a lost link the tries to get back say the host's game can't be reached, which the lost notice
        // said already: it closed its game, mostly, not a port to open)
        if (f.joining && join_warn && join != warned && !lost) {
            warned = join;
            say_locked(true, join + " You can leave from Seamless co-op in the menu.");
        }
        sInSession = f.in_session;
        if (!f.dead) sHostDeath = 0;
        flags = (f.in_session ? kInSession : 0) | (f.hosting ? kIsHost : 0);
        // (the host's death once its outcome is known: notice_host_died, the same frame)
        if (f.in_session && f.dead && (!f.hosting || sHostDeath)) flags |= kDead | (sHostDeath << kDeathShift);
        if (f.joining && !f.in_session && link == Status::Link::Full) flags |= kWaitFull;
        if (f.joining && !f.in_session && link == Status::Link::Fight) flags |= kWaitFight;
    }
    if ((f.hosting || f.joining) && np_psnr_connected() && (flags != sent_flags || now - sent_at >= kSendEvery)) {
        send_name(flags, f.player_id);
        sent_flags = flags;
        sent_at = now;
    }
    last = f;
}

bool notices_waiting()
{
    return sAny.load(std::memory_order_acquire) && !sHold.load(std::memory_order_relaxed);
}

std::vector<Notice> take_notices()
{
    std::vector<Notice> out;
    if (sHold.load()) return out;
    std::lock_guard<std::mutex> l(sLock);
    const auto now = clock_type::now();
    for (const Held& h : sWaiting)
        if (now - h.at < kStale) out.push_back(h.notice);
        else std::fprintf(stderr, "[coop] notice dropped (held for more than 90 s): %s\n", h.notice.text.c_str());
    sWaiting.clear();
    sAny = false;
    return out;
}

}  // namespace des::seamless
