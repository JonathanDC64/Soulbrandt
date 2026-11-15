// Written for Demon's Souls. The players met online lately (players_state.hpp). A block or a report goes to the
// runtime, which sends it, asks for the list again, and hands its outcome to net_notices.cpp, so the toast comes even
// when the screen has closed by then.
#include "players_state.hpp"

#include "modal.hpp"
#include "portable.hpp"
#include "ui.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

extern "C" {
int ps3_net_players_refresh(void);
int ps3_net_players(ps3_net_player* out, int max, int* state, int* days, unsigned* serial, char* msg, size_t mcap);
int ps3_net_player_block(const char* name, int blocked);
int ps3_net_player_report(const char* name, const char* reason);
}
static_assert(sizeof(ps3_net_player) == 80 && offsetof(ps3_net_player, at) == 56 &&
                  offsetof(ps3_net_player, reportable) == 72,
              "ps3_net_player as ps3emu/ps3_net_signin.h declares it");

namespace ui::players {
namespace {

constexpr int kMaxPlayers = 100;                 // PS3_NET_PLAYERS_MAX
constexpr unsigned long long kRetryMs = 15000;  // no sign-in yet, or a failed ask: ask again this often

// The tests' made-up list (set_test_list), or none.
struct TestList {
    bool on = false;
    bool offline = false;
    std::vector<ps3_net_player> players;
    int days = 0;
};
TestList& test_list() {
    static TestList t;
    return t;
}

int refresh_list() {
    const TestList& t = test_list();
    if (t.on) return t.offline ? -1 : 0;
    return ps3_net_players_refresh();
}

int read_list(ps3_net_player* out, int max, int* state, int* days, unsigned* serial, char* msg, size_t mcap) {
    const TestList& t = test_list();
    if (!t.on) return ps3_net_players(out, max, state, days, serial, msg, mcap);
    if (state) *state = t.offline ? kNone : kReady;
    if (days) *days = t.days;
    if (serial) *serial = 1;
    if (msg && mcap) msg[0] = 0;
    const int n = t.offline ? 0 : std::min(max, static_cast<int>(t.players.size()));
    for (int i = 0; out && i < n; i++) out[i] = t.players[static_cast<size_t>(i)];
    return n;
}

std::string text_of(const char* s, size_t max) {
    return std::string(s, strnlen(s, max));
}

void busy_toast() {
    push_toast({
        .type = "warning",
        .title = "Players",
        .content = "<toast-message-text>The last change is still on its way. Try again in a moment."
                   "</toast-message-text>",
        .duration = std::chrono::seconds(5),
    });
}

// Report a player: why, then the runtime sends it.
void open_report(const ps3_net_player& p) {
    const std::string name = text_of(p.name, sizeof p.name);
    const auto reason = [name](const char* label, const char* why) {
        return ModalAction{.label = label,
            .onPressed = [name, why](Modal& modal) {
                if (ps3_net_player_report(name.c_str(), why) != 0) {
                    busy_toast();
                    return;
                }
                play_sound(kSoundClick);
                modal.pop();
            }};
    };
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Report " + name + "?",
        .bodyRml = "The server's admins see how and when you met. Why are you reporting them?",
        .actions =
            {
                reason("Cheating", "cheating"),
                reason("Offensive name", "name"),
                reason("Other", "other"),
                ModalAction{.label = "Cancel",
                    .onPressed =
                        [](Modal& modal) {
                            play_sound(kSoundWindowClose);
                            modal.pop();
                        }},
            },
        .onDismiss = [](Modal& modal) { modal.pop(); },
        .icon = "warning",
        .isVertical = true,
    }));
}

}  // namespace

std::string name_of(const ps3_net_player& p) {
    return text_of(p.name, sizeof p.name);
}

std::string how_met(const ps3_net_player& p) {
    const std::string met = text_of(p.met, sizeof p.met);
    if (met == "help") return p.host ? "Came to help you" : "You helped them";
    if (met == "invasion") return p.host ? "Invaded you" : "You invaded them";
    if (met == "monk") return p.host ? "Came as your Old Monk" : "You were their Old Monk";
    return "Blocked";
}

const char* met_icon(const ps3_net_player& p) {
    const std::string met = text_of(p.met, sizeof p.met);
    if (met == "help") return "met-help";
    if (met == "invasion") return "met-invasion";
    if (met == "monk") return "met-monk";
    return "met-blocked";
}

std::string ago(long long at) {
    if (at <= 0) return {};
    const long long d = std::max(0LL, (long long)std::time(nullptr) - at);
    const auto count = [](long long n, const char* unit) {
        return std::to_string(n) + " " + unit + (n == 1 ? "" : "s") + " ago";
    };
    if (d < 60) return "just now";
    if (d < 3600) return count(d / 60, "minute");
    if (d < 86400) return count(d / 3600, "hour");
    return count(d / 86400, "day");
}

std::string key_text(const ps3_net_player& p) {
    const std::string f = text_of(p.fingerprint, sizeof p.fingerprint);
    std::string out;
    for (size_t i = 0; i < f.size(); i += 4) out += (i ? " " : "") + f.substr(i, 4);
    return out;
}

void open_actions(const ps3_net_player& p) {
    const std::string name = text_of(p.name, sizeof p.name);
    std::string body = escape(how_met(p));
    if (!ago(p.at).empty()) body += ", " + escape(ago(p.at));
    body += ". Key " + escape(key_text(p)) + ".<br/><br/>";
    body += p.blocked ? "Unblocking lets you meet them online again."
                      : "Blocked, you can't summon or invade each other.";
    if (p.reported) body += " You reported them today.";
    const bool blocked = p.blocked != 0;
    const bool reportable = p.reportable != 0;
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = (blocked ? "Unblock or report " : "Block or report ") + name + "?",
        .bodyRml = body,
        .actions =
            {
                ModalAction{.label = "Cancel",
                    .onPressed =
                        [](Modal& modal) {
                            play_sound(kSoundWindowClose);
                            modal.pop();
                        }},
                ModalAction{.label = blocked ? "Unblock" : "Block",
                    .onPressed =
                        [name, blocked](Modal& modal) {
                            if (ps3_net_player_block(name.c_str(), blocked ? 0 : 1) != 0) {
                                busy_toast();
                                return;
                            }
                            play_sound(blocked ? kSoundItemEnable : kSoundItemDisable);
                            modal.pop();
                        }},
                ModalAction{.label = "Report",
                    .onPressed =
                        [p](Modal& modal) {
                            play_sound(kSoundClick);
                            modal.pop();
                            open_report(p);
                        },
                    .isDisabled = [reportable] { return !reportable; }},
            },
        .onDismiss = [](Modal& modal) { modal.pop(); },
        .icon = "question-mark",
    }));
}

void State::load() {
    std::vector<ps3_net_player> got(kMaxPlayers);
    char msg[256] = "";
    const int n = read_list(got.data(), kMaxPlayers, &status, &days, &serial, msg, sizeof msg);
    got.resize(std::clamp(n, 0, kMaxPlayers));
    players = std::move(got);
    why = msg;
    generation++;
}

void State::start() {
    signedIn = refresh_list() == 0;
    load();
    nextAskAt = now_ms() + kRetryMs;
}

bool State::update() {
    const unsigned long long now = now_ms();
    bool changed = false;
    if ((!signedIn || status == kFailed) && now >= nextAskAt) {
        nextAskAt = now + kRetryMs;
        const bool was = signedIn;
        signedIn = refresh_list() == 0;
        changed = signedIn != was;
    }
    unsigned now_serial = 0;
    read_list(nullptr, 0, nullptr, nullptr, &now_serial, nullptr, 0);
    if (now_serial != serial) {
        load();
        changed = true;
    }
    return changed;
}

std::string State::summary() const {
    if (!signedIn) return "Play online to see the players you met: Settings > Network > Online.";
    if (status == kFailed) return why;
    if (status != kReady) return "Asking the online server...";
    if (players.empty())
        return "No one in the last " + std::to_string(days) + " days. Players who help you, you help, or who invade "
               "show here.";
    return "The last " + std::to_string(days) + " days, the latest first. Press a player to block or report them.";
}

void set_test_list(std::vector<ps3_net_player> players, int days, bool offline) {
    TestList& t = test_list();
    t.on = true;
    t.offline = offline;
    t.players = std::move(players);
    t.days = days;
}

void clear_test_list() {
    test_list() = {};
}

}  // namespace ui::players
