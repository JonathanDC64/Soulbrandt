// The Co-op window (coop_window.hpp). One modal: what co-op is and Host / Join when off; the invite code, the router's
// answer and the players while hosting; Leave while in a game. The host's part is drawn again each half second (the
// router answers a few seconds after the start, and players come and go). Each code has a small Copy button beside it,
// besides the window's Copy code.
#include "coop_window.hpp"

#include "ack.hpp"
#include "app.hpp"
#include "coop_session.hpp"
#include "crash_report.hpp"
#include "event.hpp"
#include "menu_model.hpp"
#include "menu_pages.hpp"
#include "mods.hpp"
#include "osk_window.hpp"
#include "platform.hpp"
#include "report.hpp"
#include "settings.hpp"
#include "sound.hpp"
#include "ui.hpp"

#include "../../des_seamless.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>

namespace ui {
namespace {

namespace co = des::seamless;

void toast(const char* type, const std::string& text) {
    push_toast({
        .type = type,
        .title = "Seamless co-op",
        .content = "<toast-message-text>" + escape(text) + "</toast-message-text>",
        .duration = std::chrono::seconds(6),
    });
}

// The game starts again with these set (and the rest of what it started with). False, with a toast, if it can't.
// Once only: a second press (or a Leave's own timer in the same frame) before this game has closed would start a
// second game on the same save.
bool restart_with(const std::vector<std::string>& set) {
    static bool restarting = false;   // (the UI thread's only)
    if (restarting) return true;
    if (!crash_report::start_again(set, false)) {
        toast("warning", "The game could not start again. Close it and start it again by hand.");
        return false;
    }
    restarting = true;
    std::fprintf(stderr, "[ui] co-op: the game starts again (%s)\n", set.front().c_str());
    if (app().quit) app().quit();
    return true;
}

// Text with nothing but spaces in it.
bool blank(const std::string& s) {
    return s.find_first_not_of(" \t\r\n") == std::string::npos;
}

void close_action(Modal& modal) {
    play_sound(kSoundWindowClose);
    modal.pop();
}

// The mods the player accepted at Host or Join (with_mods_warning), for the co-op log after the restart
// (DES_SEAMLESS_MODS_OK). The UI thread's only.
std::string sModsOk;

// Into co-op: the first time (co-op's save still empty), the player may first bring one of their characters along.
void start_coop(const std::vector<std::string>& given) {
    std::vector<std::string> set = given;
    set.push_back("DES_SEAMLESS_MODS_OK=" + sModsOk);
    const std::vector<co::Character> chars = co::characters_to_bring();
    if (chars.empty()) {
        play_sound(kSoundPlay);
        restart_with(set);
        return;
    }
    std::vector<ModalAction> actions;
    for (const co::Character& c : chars)
        actions.push_back({.label = c.name + " (soul level " + std::to_string(c.level) + ")",
                           .onPressed = [set, slot = c.slot](Modal&) {
                               const std::string why = co::bring_character(slot);
                               if (!why.empty()) {
                                   play_sound(kSoundItemDisable);
                                   toast("warning", why);
                                   return;
                               }
                               play_sound(kSoundPlay);
                               restart_with(set);
                           }});
    actions.push_back({.label = "Start with none",
                       .onPressed = [set](Modal&) {
                           play_sound(kSoundPlay);
                           restart_with(set);
                       }});
    actions.push_back({.label = "Cancel", .onPressed = close_action});
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Bring a character to seamless co-op?",
        .bodyRml = "Co-op gets a copy, and your character here stays as it is. Or start with none, and make one "
                   "there.",
        .actions = std::move(actions),
        .isVertical = true,
    }));
}

// Before a player's first Host or Join on this PC (ack.hpp): co-op is still a test, how to report what goes wrong, that
// anyone with a code can join and learn the host's internet address, and what the server that introduces players
// sees. Asked once; Cancel leaves co-op as it was.
void with_notice(void (*then)()) {
    with_ack(
        {
            .file = L"coop_notice_ack_2.txt",  // (_2: the notice since room codes, with the server's line)
            .kept = "The player has read co-op's notice: still a test, the server that introduces players, and "
                    "invite codes kept private (coop_window.cpp).\n",
            .title = "Seamless co-op is still being tested",
            .bodyRml = "Expect some problems, like a friend dropping out or a crash. Your normal characters aren't "
                       "affected.<br/><br/>"
                       "If something goes wrong, choose Report an issue in the menu, and ask your friends to report it "
                       "too.<br/><br/>"
                       "To find each other, your games tell this project's server their internet addresses. It "
                       "keeps them only while you host.<br/><br/>"
                       "<modal-warning>Anyone with an invite code can join you and learn your internet address. Only "
                       "share codes privately with friends.</modal-warning>",
            .tick = "I understand",
            .icon = "warning",
        },
        then);
}

void open_host_now() {
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Host seamless co-op?",
        .bodyRml = "The game starts again on your co-op characters. Then send your friends the invite code from "
                   "Seamless co-op.<br/><br/>"
                   "If Windows asks about the network, allow it. Keep the code private: anyone with it can join "
                   "you.",
        .actions = {ModalAction{.label = "Cancel", .onPressed = close_action},
                    ModalAction{.label = "Host",
                                .onPressed = [](Modal&) { start_coop({"DES_SEAMLESS=host", "DES_SEAMLESS_CODE="}); }}},
    }));
}

// Join with a code the player pasted or typed; a toast says why not if it doesn't read as one.
void join_with(const std::string& text, const char* if_empty) {
    std::string code;
    const std::string why = co::check_code(text, &code);
    if (!why.empty()) {
        play_sound(kSoundItemDisable);
        toast("warning", text.empty() ? if_empty : why);
        return;
    }
    start_coop({"DES_SEAMLESS=join", "DES_SEAMLESS_CODE=" + code});
}

void open_join_now() {
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Join seamless co-op?",
        .bodyRml = "Copy your friend's invite code, then paste it or type it. The game starts again in their world.",
        .actions = {ModalAction{.label = "Cancel", .onPressed = close_action},
                    ModalAction{.label = "Type code",
                                .onPressed = [](Modal&) {
                                    play_sound(kSoundItemChange);
                                    push_document(std::make_unique<OskWindow>(OskWindow::Props{
                                        .prompt = "Your friend's invite code",
                                        .limit = 19,  // a direct code: 16 letters and digits, and three dashes
                                        .onDone =
                                            [](bool ok, const std::u16string& typed) {
                                                if (ok) join_with(utf16_to_utf8(typed), "Type the code first.");
                                            },
                                    }));
                                }},
                    ModalAction{.label = "Paste and join",
                                .onPressed = [](Modal&) {
                                    Rml::String text;
                                    if (auto* sys = Rml::GetSystemInterface()) sys->GetClipboardText(text);
                                    join_with(text, "Nothing to paste: copy the code first.");
                                }}},
    }));
}

// The mods that are on at the next start, by the names the Mods screens show: the mods folder's order file (what the
// Mods screens changed since this start included), or the ones a test run gave.
std::vector<std::string> mods_on_next() {
    std::vector<std::string> names;
    if (mods::from_environment()) {
        for (const mods::Mod& m : mods::in_use()) names.push_back(m.name);
        return names;
    }
    const auto dir = mods::folder(report::data_dir());
    if (dir.empty()) return names;
    const auto all = settings::pinned("DES_MODS") ? mods::scan(dir, settings::pinned_value("DES_MODS")) : mods::scan(dir);
    for (const mods::Mod& m : all)
        if (m.usable() && m.enabled) names.push_back(m.name);
    return names;
}

// Host or Join while mods are on: a warning to accept each time (they will probably cause problems, and every mod off
// is the first thing to try), until the player ticks it away for this set of mods; a different set asks again. Back
// leaves co-op as it was.
void with_mods_warning(void (*then)()) {
    sModsOk.clear();
    const std::vector<std::string> names = mods_on_next();
    if (names.empty()) {
        then();
        return;
    }
    coop::open_mods_warning(names, then);
}

void open_host() {
    with_notice([] { with_mods_warning(open_host_now); });
}

void open_join() {
    with_notice([] { with_mods_warning(open_join_now); });
}

// The host's public address, for a router that can't say it: kept in settings.ini for the next time.
void use_public_address(const std::string& text) {
    std::string ip;
    const std::string why = co::set_public_address(text, &ip);
    if (!why.empty()) {
        play_sound(kSoundItemDisable);
        toast("warning", why);
        return;
    }
    settings::set("DES_SEAMLESS_ADDRESS", ip);
    settings::now_in_effect("DES_SEAMLESS_ADDRESS");  // (set_public_address applied it: no restart to ask about)
    play_sound(kSoundItemEnable);
    toast("info", ip.empty() ? "The code uses your router's answer again."
                             : "The invite code now uses that address. Copy it again for your friends.");
}

void open_public_address() {
    const std::string now = settings::get("DES_SEAMLESS_ADDRESS");
    std::vector<ModalAction> actions;
    if (!now.empty())
        actions.push_back({.label = "Use the router's address",
                           .onPressed = [](Modal& m) {
                               use_public_address("");
                               m.pop();
                           }});
    actions.push_back({.label = "Type address",
                       .onPressed = [now](Modal& m) {
                           std::u16string text(now.begin(), now.end());   // (before the pop: it closes this window)
                           play_sound(kSoundItemChange);
                           m.pop();
                           push_document(std::make_unique<OskWindow>(OskWindow::Props{
                               .prompt = "Your public address, like 203.0.113.7",
                               .text = std::move(text),
                               .limit = 15,
                               .onDone =
                                   [](bool ok, const std::u16string& typed) {
                                       if (!ok) return;
                                       const std::string text = utf16_to_utf8(typed);
                                       if (blank(text)) {   // (clearing it is "Use the router's address")
                                           play_sound(kSoundItemDisable);
                                           toast("warning", "No address typed: nothing changed.");
                                           return;
                                       }
                                       use_public_address(text);
                                   },
                           }));
                       }});
    actions.push_back({.label = "Paste",
                       .onPressed = [](Modal& m) {
                           Rml::String text;
                           if (auto* sys = Rml::GetSystemInterface()) sys->GetClipboardText(text);
                           if (blank(text)) {
                               play_sound(kSoundItemDisable);
                               toast("warning", "Nothing to paste: copy the address first.");
                               return;
                           }
                           use_public_address(text);
                           m.pop();
                       }});
    actions.push_back({.label = "Cancel", .onPressed = close_action});  // (last, as in every question's list)
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Set your public address?",
        .bodyRml = "If your router can't open port " + std::to_string(co::status().port) +
                   " (TCP and UDP), forward it to this PC and give the address the internet sees." +
                   (now.empty() ? std::string() : "<br/><br/>Now: " + escape(now)),
        .actions = std::move(actions),
        .isVertical = true,
    }));
}

// A new invite code: for a code that went further than meant, or once too many wrong codes spent it.
void open_new_code() {
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Make a new invite code?",
        .bodyRml = "The old code stops working. Players already here stay.",
        .actions = {ModalAction{.label = "Cancel", .onPressed = close_action},
                    ModalAction{.label = "New code",
                                .onPressed =
                                    [](Modal& m) {
                                        const std::string why = co::new_code();
                                        if (!why.empty()) {
                                            play_sound(kSoundItemDisable);
                                            toast("warning", why);
                                            return;
                                        }
                                        play_sound(kSoundItemEnable);
                                        toast("info", "A new invite code. Copy it again for your friends.");
                                        m.pop();
                                    }}},
    }));
}

// Out of co-op: the game starts again on the normal characters.
void restart_alone() {
    play_sound(kSoundWindowClose);
    restart_with({"DES_SEAMLESS=", "DES_SEAMLESS_CODE=", "DES_SEAMLESS_MODS_OK="});
}

// A guest's leave: its game goes back to its own world where it stands (in a boss fight, to the area's start) and
// saves there (des_seamless_game.cpp's going_home), then starts again.
// Leave now doesn't wait for the trip home: the game saves the guest's progress where it stands if it can save there
// (else at home, at once), and the window starts the game again when that save is written.
class LeavingWindow final : public Modal {
public:
    explicit LeavingWindow(Props props) : Modal(std::move(props)) {}

    void update() override {
        const auto now = std::chrono::steady_clock::now();
        if (mStart == std::chrono::steady_clock::time_point{}) mStart = now;
        if (!mDone && now >= mNext) {
            mNext = now + std::chrono::milliseconds(500);
            // The game side gives up after 2 minutes; this is for a game that never gets to look.
            if (co::left() || now - mStart > std::chrono::seconds(150)) {
                mDone = true;
                restart_alone();
            }
        }
        Modal::update();
    }

private:
    std::chrono::steady_clock::time_point mStart{}, mNext{};
    bool mDone = false;
};

void open_leaving() {
    co::leave();
    static bool now_pressed;
    now_pressed = false;
    push_document(std::make_unique<LeavingWindow>(Modal::Props{
        .title = "Leaving seamless co-op",
        .bodyRml = "Going back to your own world where you stand, then to your normal characters.",
        .actions = {ModalAction{.label = "Leave now",
                                .onPressed =
                                    [](Modal& m) {
                                        if (now_pressed) return;
                                        now_pressed = true;
                                        play_sound(kSoundItemEnable);
                                        co::leave_now();
                                        m.set_body("Saving your game first, then going to your normal characters.");
                                    },
                                .isDisabled = [] { return now_pressed; }}},
        // Back or Esc doesn't close it: the leave is under way (the session left), and only this window starts the
        // game again, once the game has saved (or the leave gave up).
        .onDismiss = [](Modal&) {},
    }));
}

void open_leave() {
    const bool guest = co::status().mode == co::Mode::Join;
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Leave seamless co-op?",
        .bodyRml = guest ? "Your game saves where you stand, in your own world, then starts again on your normal "
                           "characters."
                         : "The game starts again on your normal characters. Your friends go back to their own "
                           "worlds.",
        .actions = {ModalAction{.label = "Cancel", .onPressed = close_action},
                    ModalAction{.label = "Leave",
                                .onPressed = [guest](Modal& m) {
                                    if (!guest) {
                                        restart_alone();
                                        return;
                                    }
                                    play_sound(kSoundWindowClose);
                                    m.pop();
                                    open_leaving();
                                }}},
    }));
}

std::string body(const co::Status& s) {
    if (s.mode == co::Mode::Off)
        return "Play through the game with up to three friends. One of you hosts; the others join with the host's "
               "invite code and follow the host from area to area.<br/><br/>"
               "Co-op uses its own characters, kept apart from your normal ones. This project's server only "
               "introduces your games to each other: what you play goes straight between them. A host whose router "
               "can't tell its internet address asks Google's STUN server for it.";
    if (!s.started) return "Co-op did not start: " + escape(s.error) + ".";
    if (s.mode == co::Mode::Join) {
        if (s.link == co::Status::Link::Connecting && !s.join.empty())
            return s.join_warn ? "<modal-warning>" + escape(s.join) + "</modal-warning>" : escape(s.join);
        if (s.link == co::Status::Link::Connecting) return "Reaching the host's game...";
        if (s.link == co::Status::Link::Connected) {
            const std::string who = coop_session::window_rml(0);
            return who.empty() ? "You're connected to the host's game." : who;
        }
        if (s.link == co::Status::Link::Fight)
            return "You're connected to the host's game. The host is in a boss fight, and no one joins during one, as "
                   "in the original game. You join as soon as it's over.";
        if (s.link == co::Status::Link::Full)
            return "You're connected to the host's game, but it is full: it has all the players the host takes. You "
                   "join as soon as a place frees up.";
        return "The connection to the host's game was lost (the host closed its game, or a network dropped). Your "
               "game takes you back to your own world and keeps trying to reach the host's.<br/><br/>If the host "
               "starts hosting again, its invite code is a new one: <b>Leave co-op</b>, then join with the new code.";
    }
    std::string b;
    if (s.code_spent)
        b += "<b>Many wrong codes were tried</b>, so your code no longer lets anyone in. Choose <b>New code</b> and "
             "send the new one to your friends. Players already here stay.<br/><br/>";
    if (!s.room_code.empty())  // (it holds no address: the server introduces the games)
        b += "Your invite code:<br/><b>" + escape(s.room_code) + "</b>" + coop::copy_button("room") +
             "<br/>Send it to your friends. Anyone with this code can join you.<br/><br/>";
    if (!s.intro.empty())
        b += (s.intro_trouble ? "<modal-warning>" + escape(s.intro) + "</modal-warning>" : escape(s.intro)) +
             "<br/><br/>";
    if (!s.stuck.empty()) b += "<modal-warning>" + escape(s.stuck) + "</modal-warning><br/><br/>";
    if (s.show_direct && !s.code.empty()) {
        b += (s.room_code.empty() ? "Your invite code:<br/><b>" : "Or the direct code, for your open port:<br/><b>") +
             escape(s.code) + "</b>" + coop::copy_button("direct") +
             "<br/>Send it to your friends. Don't post it in public: it holds your internet address.<br/><br/>";
    }
    if (s.show_direct) b += escape(s.router.empty() ? "Asking your router to open the port..." : s.router);
    if (!s.lan_code.empty() && (s.code.empty() || s.lan_code != s.code) && s.show_direct)
        b += "<br/><br/>On your own network: <b>" + escape(s.lan_code) + "</b>" + coop::copy_button("lan");
    if (const std::string who = coop_session::window_rml(s.max_players); !who.empty())
        return b + (s.show_direct ? "<br/><br/>" : "") + who;
    b += std::string(s.show_direct ? "<br/><br/>" : "") + "Players: " +
         std::to_string(s.players > 0 ? s.players : 1) + " of " + std::to_string(s.max_players) +
         " (Settings > Network).";
    if (s.waiting > 0) b += " Waiting to join: " + std::to_string(s.waiting) + ".";
    return b;
}

// Copy code: the invite code on the clipboard: the room code, else the direct code, else the one for the player's own
// network (while the router hasn't answered).
void copy_code_now() {
    const co::Status now = co::status();
    const std::string code = !now.room_code.empty() ? now.room_code : !now.code.empty() ? now.code : now.lan_code;
    if (code.empty()) {
        toast("warning", "There is no code yet: the router hasn't answered.");
        return;
    }
    if (auto* sys = Rml::GetSystemInterface()) sys->SetClipboardText(code);
    play_sound(kSoundItemEnable);
    toast("info", code == now.room_code ? "Copied. Send it to your friends."
                  : code == now.code     ? "Copied. Send it to your friends, not in public."  // (it holds the address)
                                         : "Copied the code for players on your own network: it doesn't work over "
                                           "the internet.");
}

bool no_code_now() {
    const co::Status now = co::status();
    return now.room_code.empty() && now.code.empty() && now.lan_code.empty();
}

// The code a Copy button copied last, and until when it says "Copied" (coop::copy_button). The UI thread's only.
std::string sCopiedKind;
std::chrono::steady_clock::time_point sCopiedUntil{};

class CoopWindow final : public Modal {
public:
    explicit CoopWindow(Props props) : Modal(std::move(props)) {
        mCopy = std::make_unique<ScopedEventListener>(mDialog->QuerySelector("modal-body"), Rml::EventId::Click,
                                                      [this](Rml::Event& e) {
                                                          if (coop::copy_pressed(e.GetTargetElement())) mNext = {};
                                                      });
    }

    void update() override {
        const auto now = std::chrono::steady_clock::now();
        if (now >= mNext) {
            mNext = now + std::chrono::milliseconds(500);
            const co::Status s = co::status();
            if (s.mode != co::Mode::Off && s.started) {
                const std::string b = body(s);
                if (b != mShown) {
                    mShown = b;
                    set_body(b);
                }
            }
        }
        Modal::update();
    }

private:
    std::chrono::steady_clock::time_point mNext{};
    std::string mShown;
    std::unique_ptr<ScopedEventListener> mCopy;  // a click on a code's Copy button
};

}  // namespace

std::unique_ptr<Modal> make_coop_window() {
    const co::Status s = co::status();
    std::vector<ModalAction> actions{ModalAction{.label = "Close", .onPressed = close_action}};
    if (s.mode == co::Mode::Off) {
        actions.push_back({.label = "Join", .onPressed = [](Modal&) { open_join(); }});
        actions.push_back({.label = "Host", .onPressed = [](Modal&) { open_host(); }});
    } else {
        if (s.mode == co::Mode::Host)
            actions.push_back({.label = "Copy code",
                               .onPressed = [](Modal&) { copy_code_now(); },
                               .isDisabled = [] { return no_code_now(); }});
        if (s.mode == co::Mode::Host) {
            actions.push_back({.label = "New code", .onPressed = [](Modal&) { open_new_code(); }});
            actions.push_back({.label = "Public address", .onPressed = [](Modal&) { open_public_address(); }});
        }
        actions.push_back({.label = "Leave co-op", .onPressed = [](Modal&) { open_leave(); }});
    }
    // Co-op's settings alone, over the window (the Seamless co-op page's Settings, in DeS UI)
    actions.push_back(
        {.label = "Settings", .onPressed = [](Modal&) { push_document(menu::screen_alone("Co-op settings")); }});
    return std::make_unique<CoopWindow>(Modal::Props{
        .title = "Seamless co-op",
        .bodyRml = body(s),
        .actions = std::move(actions),
    });
}

namespace coop {
void open_host() {
    ui::open_host();
}

// The mods' names for the warning: up to three, else the first two and how many more (a long list would push the
// warning's main line out of sight).
static std::string mods_named(const std::vector<std::string>& names) {
    if (names.size() <= 3) return mods::join(names, ", ");
    return mods::join({names[0], names[1]}, ", ") + ", +" + std::to_string(names.size() - 2) + " more";
}

void open_mods_warning(const std::vector<std::string>& names, void (*then)()) {
    std::vector<std::string> sorted = names;
    std::sort(sorted.begin(), sorted.end());
    const std::string list = mods::join(names, ", ");
    with_ack(
        {
            .file = L"coop_mods_ack.txt",
            .kept = "The player asked not to be warned again about co-op with these mods on (coop_window.cpp).\n",
            .title = names.size() == 1 ? std::string("1 mod is on") : std::to_string(names.size()) + " mods are on",
            .bodyRml = "<modal-alert>If something goes wrong in co-op, turn every mod off (Mods, in the menu), "
                       "restart, and try again before you report it.</modal-alert>"
                       "Co-op with mods will probably have problems: games that don't match, missing enemies or "
                       "items, crashes. Everyone in the party should have the same mods that change how the game "
                       "plays.<br/><br/>"
                       "On now: " + escape(mods_named(names)) + ".",
            .tick = "Don't ask again for these mods",
            .icon = "warning",
            .tickOptional = true,
            .go = "Play with mods",
            .back = "Back",
            .key = mods::join(sorted, "|"),
        },
        [list, then] {
            ui::sModsOk = list;
            std::fprintf(stderr, "[coop] mods on, accepted: %s\n", list.c_str());
            if (then) then();
        });
}

// The restart, for after the warning: the UI thread's only, one at a time (the warning is modal).
static std::function<void(const std::vector<std::string>&)> sRestart;

void before_restart(std::function<void(const std::vector<std::string>&)> restart) {
    if (co::mode() == co::Mode::Off) {
        restart({});
        return;
    }
    std::vector<std::string> session;
    for (const mods::Mod& m : mods::in_use()) session.push_back(m.name);
    const std::vector<std::string> next = mods_on_next();
    if (!menu::other_mods(session, next)) {
        restart({});  // (what was accepted still holds)
        return;
    }
    if (next.empty()) {
        restart({"DES_SEAMLESS_MODS_OK="});
        return;
    }
    ui::sModsOk.clear();
    sRestart = std::move(restart);
    open_mods_warning(next, [] {
        const auto go = std::move(sRestart);
        sRestart = {};
        if (go) go({"DES_SEAMLESS_MODS_OK=" + ui::sModsOk});
    });
}

void open_join() {
    ui::open_join();
}
void copy_code() {
    copy_code_now();
}

std::string copy_button(const char* kind) {
    const bool done = sCopiedKind == kind && std::chrono::steady_clock::now() < sCopiedUntil;
    return std::string("<code-copy kind=\"") + kind + (done ? "\" class=\"done\">Copied" : "\">Copy") + "</code-copy>";
}

bool copy_pressed(Rml::Element* target) {
    for (Rml::Element* e = target; e; e = e->GetParentNode()) {
        if (e->GetTagName() != "code-copy") continue;
        const std::string kind = e->GetAttribute<Rml::String>("kind", "");
        const co::Status now = co::status();
        const std::string code = kind == "room"     ? now.room_code
                                 : kind == "direct" ? now.code
                                 : kind == "lan"    ? now.lan_code
                                                    : std::string();
        if (code.empty()) return false;  // (gone since the box was drawn: the next drawing drops its button)
        if (auto* sys = Rml::GetSystemInterface()) sys->SetClipboardText(code);
        play_sound(kSoundItemEnable);
        std::fprintf(stderr, "[coop] copied the %s code\n", kind.c_str());
        sCopiedKind = kind;
        sCopiedUntil = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        return true;
    }
    return false;
}
bool no_code() {
    return no_code_now();
}
void open_new_code() {
    ui::open_new_code();
}
void open_public_address() {
    ui::open_public_address();
}
void open_leave() {
    ui::open_leave();
}
}  // namespace coop

}  // namespace ui
