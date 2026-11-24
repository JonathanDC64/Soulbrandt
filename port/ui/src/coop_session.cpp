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

// One player's line: the name, its mark (Host, You), what it does, and a friend's ping to the host's game; in the
// session, its move (Warp, or your own Unstuck) and whether it can't now.
struct Row {
    std::string name, mark, state, ping, act;
    bool blocked = false;
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
        if (p.in_session) {
            Row r = row_of(p, false);
            r.act = p.you ? "Unstuck" : "Warp";
            r.blocked = !(p.you ? co::unstuck_blocked() : co::warp_blocked(p.user)).empty();
            l.in.push_back(r);
        }
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
// reason takes the middle and the right). index >= 0: a line in the session, its move's button last (the look shows
// it over the ping's end on the focused line and the one under the pointer).
std::string row_rml(const Row& r, int index = -1, bool focused = false) {
    std::string b = index < 0 ? std::string("<info-player>")
                              : "<info-player class=\"pick" + std::string(focused ? " focused" : "") + "\" data-row=\"" +
                                    std::to_string(index) + "\">";
    const std::string act = index < 0 ? std::string()
                                      : "<info-act" + std::string(r.blocked ? " class=\"disabled\"" : "") + ">" +
                                            escape(r.act) + "</info-act>";
    b += "<info-name>" + escape(r.name);
    if (!r.mark.empty()) b += " <info-mark>(" + escape(r.mark) + ")</info-mark>";
    b += "</info-name>";
    if (r.ping.empty()) return b + "<info-state class=\"wide\">" + escape(r.state) + "</info-state>" + act + "</info-player>";
    return b + "<info-state>" + escape(r.state) + "</info-state><info-ping>" + escape(r.ping) + "</info-ping>" + act +
           "</info-player>";
}

std::string line_of(const Row& r) {
    return escape(r.name) + (r.mark.empty() ? "" : " (" + escape(r.mark) + ")") + ": " + escape(r.state) +
           (r.ping.empty() ? "" : ", " + escape(r.ping));
}

}  // namespace

std::string page_rml(int max_players, int row, bool* company) {
    const Lines l = lines(max_players);
    if (company) *company = l.in.size() > 1 || !l.waiting.empty();
    if (l.in.empty()) return {};
    std::string b = "<info-label>In the session</info-label><info-players>";
    for (size_t i = 0; i < l.in.size(); i++) b += row_rml(l.in[i], static_cast<int>(i), static_cast<int>(i) == row);
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

std::vector<Pick> picks() {
    std::vector<Pick> out;
    for (const co::SessionPlayer& p : co::session_players()) {
        if (!p.in_session) continue;
        Pick k;
        k.you = p.you;
        k.user = p.user;
        k.name = p.name.empty() ? std::string("a player") : p.name;
        k.blocked = p.you ? co::unstuck_blocked() : co::warp_blocked(p.user);
        out.push_back(k);
    }
    return out;
}

void do_pick(const Pick& p) {
    if (p.you) co::ask_unstuck();
    else co::ask_warp(p.user);
}

}  // namespace ui::coop_session
