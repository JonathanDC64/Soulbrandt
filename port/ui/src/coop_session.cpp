// Written for Demon's Souls (docs/169). See coop_session.hpp.
#include "coop_session.hpp"

#include "ui.hpp"
#include "../../des_seamless_roster.h"

#include <string>
#include <vector>

namespace ui::coop_session {
namespace {

namespace co = des::seamless;

// A ping above this also says "slow", in words (the game says things in words, not colours).
constexpr int kSlowMs = 150;

// One player's line: the name, its mark (Host, You), what it does, and a friend's ping to the host's game.
struct Row {
    std::string name, mark, state, ping;
};

Row row_of(const co::SessionPlayer& p, bool waiting) {
    Row r;
    r.name = p.name.empty() ? std::string("A player") : p.name;
    r.mark = p.host && p.you ? "Host, you" : p.host ? "Host" : p.you ? "You" : "";
    r.state = p.state;
    if (!waiting && !p.host && p.ping_ms >= 0 && p.state.rfind("No answer", 0) != 0)
        r.ping = std::to_string(p.ping_ms) + " ms" + (p.ping_ms > kSlowMs ? ", slow" : "");
    return r;
}

std::string together(int seconds) {
    const int m = seconds / 60, h = m / 60;
    if (h > 0) return std::to_string(h) + " h " + std::to_string(m % 60) + " min";
    return std::to_string(m) + " min";
}

struct Lines {
    std::vector<Row> in, waiting;
    std::string session;
};

Lines lines(int max_players) {
    Lines l;
    const std::vector<co::SessionPlayer> all = co::session_players();
    bool fight = false;
    for (const co::SessionPlayer& p : all) {
        if (p.in_session) l.in.push_back(row_of(p, false));
        else if (!p.you) l.waiting.push_back(row_of(p, true));
        if (p.host && p.in_session && p.state == "In a boss fight") fight = true;
    }
    if (l.in.empty()) return {};
    const std::string n = std::to_string(l.in.size());
    l.session = (max_players > 0 ? n + " of " + std::to_string(max_players) + " players (Settings > Network). "
                                 : n + (l.in.size() == 1 ? " player. " : " players. ")) +
                (fight ? "Joins wait: the host is in a boss fight." : "Joins are open.");
    if (const int t = co::session_seconds(); t >= 60) l.session += " Together for " + together(t) + ".";
    return l;
}

// A player's line: name and mark on the left, what it does in the middle, the ping at the right (a waiting game's
// reason takes the middle and the right).
std::string row_rml(const Row& r) {
    std::string b = "<info-player><info-name>" + escape(r.name);
    if (!r.mark.empty()) b += " <info-mark>(" + escape(r.mark) + ")</info-mark>";
    b += "</info-name>";
    if (r.ping.empty()) return b + "<info-state class=\"wide\">" + escape(r.state) + "</info-state></info-player>";
    return b + "<info-state>" + escape(r.state) + "</info-state><info-ping>" + escape(r.ping) +
           "</info-ping></info-player>";
}

std::string line_of(const Row& r) {
    return escape(r.name) + (r.mark.empty() ? "" : " (" + escape(r.mark) + ")") + ": " + escape(r.state) +
           (r.ping.empty() ? "" : ", " + escape(r.ping));
}

}  // namespace

std::string page_rml(int max_players) {
    const Lines l = lines(max_players);
    if (l.in.size() < 2 && l.waiting.empty()) return {};  // (alone: the count line, under the codes)
    std::string b = "<info-label>In the session</info-label><info-players>";
    for (const Row& r : l.in) b += row_rml(r);
    b += "</info-players>";
    if (!l.waiting.empty()) {
        b += "<info-label>Waiting to join</info-label><info-players>";
        for (const Row& r : l.waiting) b += row_rml(r);
        b += "</info-players>";
    }
    return b + "<info-line class=\"session\">" + escape(l.session) + "</info-line>";
}

std::string window_rml(int max_players) {
    const Lines l = lines(max_players);
    if (l.in.empty()) return {};
    std::string b = "<b>In the session</b>";
    for (const Row& r : l.in) b += "<br/>" + line_of(r);
    if (!l.waiting.empty()) {
        b += "<br/><br/><b>Waiting to join</b>";
        for (const Row& r : l.waiting) b += "<br/>" + line_of(r);
    }
    return b + "<br/><br/>" + escape(l.session);
}

}  // namespace ui::coop_session
