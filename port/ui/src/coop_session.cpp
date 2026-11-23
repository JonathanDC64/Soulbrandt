// Written for Demon's Souls (docs/169). See coop_session.hpp.
#include "coop_session.hpp"

#include "ui.hpp"
#include "../../des_seamless_roster.h"

#include <string>
#include <vector>

namespace ui::coop_session {
namespace {

namespace co = des::seamless;

// A ping above this reads "slow", in words (the game says things in words, not colours).
constexpr int kSlowMs = 150;

std::string name_of(const co::SessionPlayer& p) {
    std::string n = p.name.empty() ? std::string("A player") : p.name;
    if (p.host && p.you) return n + " (Host, you)";
    if (p.host) return n + " (Host)";
    if (p.you) return n + " (You)";
    return n;
}

// "Boletarian Palace 1-1. Playing, body form, HP 560 of 560." and a friend's ping.
std::string what_of(const co::SessionPlayer& p) {
    const std::string where = co::area_label(p.area);
    std::string s = where.empty() ? std::string() : where + ". ";
    s += p.state + (p.soul ? ", soul form" : ", body form");
    if (p.hp_max > 0) s += ", HP " + std::to_string(p.hp > 0 ? p.hp : 0) + " of " + std::to_string(p.hp_max);
    s += ".";
    if (!p.host && p.ping_ms >= 0)
        s += " Ping " + std::to_string(p.ping_ms) + " ms" + (p.ping_ms > kSlowMs ? ", slow." : ".");
    return s;
}

std::string waiting_of(const co::SessionPlayer& p) {
    std::string s = p.state;
    if (!s.empty() && s.back() != '.') s += ".";
    return s;
}

std::string together(int seconds) {
    const int m = seconds / 60, h = m / 60;
    if (h > 0) return std::to_string(h) + " h " + std::to_string(m % 60) + " min";
    return std::to_string(m) + " min";
}

struct Row {
    std::string name, what;
};

struct Lines {
    std::vector<Row> in, waiting;
    std::string session;
};

Lines lines(int max_players) {
    Lines l;
    const std::vector<co::SessionPlayer> all = co::session_players();
    bool fight = false;
    for (const co::SessionPlayer& p : all) {
        if (p.in_session) l.in.push_back({name_of(p), what_of(p)});
        else if (!p.you) l.waiting.push_back({name_of(p), waiting_of(p)});
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

}  // namespace

// A player: the name, then what it does in a smaller line under it.
std::string row_rml(const Row& r) {
    return "<info-player><info-name>" + escape(r.name) + "</info-name><info-what>" + escape(r.what) +
           "</info-what></info-player>";
}

std::string page_rml(int max_players) {
    const Lines l = lines(max_players);
    if (l.in.empty()) return {};
    std::string b = "<info-label>In the session</info-label>";
    for (const Row& r : l.in) b += row_rml(r);
    if (!l.waiting.empty()) {
        b += "<info-label>Waiting to join</info-label>";
        for (const Row& r : l.waiting) b += row_rml(r);
    }
    return b + "<info-line>" + escape(l.session) + "</info-line>";
}

std::string window_rml(int max_players) {
    const Lines l = lines(max_players);
    if (l.in.empty()) return {};
    std::string b = "<b>In the session</b>";
    for (const Row& r : l.in) b += "<br/>" + escape(r.name) + ": " + escape(r.what);
    if (!l.waiting.empty()) {
        b += "<br/><br/><b>Waiting to join</b>";
        for (const Row& r : l.waiting) b += "<br/>" + escape(r.name) + ": " + escape(r.what);
    }
    return b + "<br/><br/>" + escape(l.session);
}

}  // namespace ui::coop_session
