// The Co-op window (coop_window.hpp). One modal: what co-op is and Host / Join when off; the invite code, the router's
// answer and the players while hosting; Leave while in a game. The host's part is drawn again each half second (the
// router answers a few seconds after the start, and players come and go).
#include "coop_window.hpp"

#include "ack.hpp"
#include "app.hpp"
#include "crash_report.hpp"
#include "osk_window.hpp"
#include "platform.hpp"
#include "settings.hpp"
#include "sound.hpp"
#include "ui.hpp"

#include "../../des_seamless.h"

#include <RmlUi/Core.h>

#include <chrono>
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

// Into co-op: the first time (co-op's save still empty), the player may first bring one of their characters along.
void start_coop(const std::vector<std::string>& set) {
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

// Before a player's first Host or Join on this PC (ack.hpp): co-op is still a test, how to report what goes wrong, and
// that an invite code holds the host's internet address. Asked once; Cancel leaves co-op as it was.
void with_notice(void (*then)()) {
    with_ack(
        {
            .file = L"coop_notice_ack.txt",
            .kept = "The player has read co-op's notice: still a test, and invite codes kept private "
                    "(coop_window.cpp).\n",
            .title = "Seamless co-op is still being tested",
            .bodyRml = "Expect some problems, like a friend dropping out or a crash. Your normal characters aren't "
                       "affected.<br/><br/>"
                       "If something goes wrong, choose Report an issue in the menu, and ask your friends to report it "
                       "too.<br/><br/>"
                       "<modal-warning>Invite codes contain the host's IP address. Only share them privately with "
                       "friends.</modal-warning>",
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
                   "If Windows asks about the network, allow it. Keep the code private: it holds your internet "
                   "address.",
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
                                        .limit = 19,  // 16 letters and digits, and three dashes
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

void open_host() {
    with_notice(open_host_now);
}

void open_join() {
    with_notice(open_join_now);
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

void leave_now() {
    play_sound(kSoundWindowClose);
    restart_with({"DES_SEAMLESS=", "DES_SEAMLESS_CODE="});
}

// A guest's leave: its game goes back to its own world and saves there (des_seamless_game.cpp), then starts again.
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
                leave_now();
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
    push_document(std::make_unique<LeavingWindow>(Modal::Props{
        .title = "Leaving seamless co-op",
        .bodyRml = "Going back to your own world, then to your normal characters.",
        .actions = {ModalAction{.label = "Leave now",
                                .onPressed = [](Modal&) { leave_now(); }}},
        // Back or Esc doesn't close it: the leave is under way (the session left), and only this window starts the
        // game again. Leave now is the way out.
        .onDismiss = [](Modal&) {},
    }));
}

void open_leave() {
    const bool guest = co::status().mode == co::Mode::Join;
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Leave seamless co-op?",
        .bodyRml = guest ? "Your game saves in your own world, then starts again on your normal characters."
                         : "The game starts again on your normal characters. Your friends go back to their own "
                           "worlds.",
        .actions = {ModalAction{.label = "Cancel", .onPressed = close_action},
                    ModalAction{.label = "Leave",
                                .onPressed = [guest](Modal& m) {
                                    if (!guest) {
                                        leave_now();
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
               "Co-op uses its own characters, kept apart from your normal ones. No online game server is used: "
               "only your friends' games and yours. A host whose router can't tell its internet address asks "
               "Google's STUN server for it.";
    if (!s.started) return "Co-op did not start: " + escape(s.error) + ".";
    if (s.mode == co::Mode::Join) {
        if (s.link == co::Status::Link::Connecting) return "Reaching the host's game...";
        if (s.link == co::Status::Link::Connected) return "You're connected to the host's game.";
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
    if (!s.code.empty()) {
        b += "Your invite code:<br/><b>" + escape(s.code) + "</b><br/>Send it to your friends. Don't post it in "
             "public: it holds your internet address.<br/><br/>";
    }
    b += escape(s.router.empty() ? "Asking your router to open the port..." : s.router);
    if (!s.lan_code.empty() && (s.code.empty() || s.lan_code != s.code))
        b += "<br/><br/>On your own network: <b>" + escape(s.lan_code) + "</b>";
    b += "<br/><br/>Players: " + std::to_string(s.players > 0 ? s.players : 1) + " of " +
         std::to_string(s.max_players) + " (Settings > Network).";
    if (s.waiting > 0) b += " Waiting to join: " + std::to_string(s.waiting) + ".";
    return b;
}

// Copy code: the invite code on the clipboard (the one for the player's own network while the router hasn't answered).
void copy_code_now() {
    const co::Status now = co::status();
    const std::string code = !now.code.empty() ? now.code : now.lan_code;
    if (code.empty()) {
        toast("warning", "There is no code yet: the router hasn't answered.");
        return;
    }
    if (auto* sys = Rml::GetSystemInterface()) sys->SetClipboardText(code);
    play_sound(kSoundItemEnable);
    toast("info", now.code.empty() ? "Copied the code for players on your own network: it doesn't work over the "
                                     "internet."
                                   : "Copied. Send it to your friends, not in public.");
}

bool no_code_now() {
    const co::Status now = co::status();
    return now.code.empty() && now.lan_code.empty();
}

class CoopWindow final : public Modal {
public:
    explicit CoopWindow(Props props) : Modal(std::move(props)) {}

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
void open_join() {
    ui::open_join();
}
void copy_code() {
    copy_code_now();
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
