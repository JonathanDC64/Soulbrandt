// The Co-op window (coop_window.hpp). One modal: what co-op is and Host / Join when off; the invite code, the router's
// answer and the players while hosting; Leave while in a game. The host's part is drawn again each half second (the
// router answers a few seconds after the start, and players come and go).
#include "coop_window.hpp"

#include "app.hpp"
#include "crash_report.hpp"
#include "sound.hpp"
#include "ui.hpp"

#include "../../des_seamless.h"

#include <RmlUi/Core.h>

#include <chrono>
#include <string>

namespace ui {
namespace {

namespace co = des::seamless;

constexpr int kMaxPlayers = 4;

void toast(const char* type, const std::string& text) {
    push_toast({
        .type = type,
        .title = "Co-op",
        .content = "<toast-message-text>" + escape(text) + "</toast-message-text>",
        .duration = std::chrono::seconds(6),
    });
}

// The game starts again with these set (and the rest of what it started with). False, with a toast, if it can't.
bool restart_with(const std::vector<std::string>& set) {
    if (!crash_report::start_again(set, false)) {
        toast("warning", "The game could not start again. Close it and start it again by hand.");
        return false;
    }
    std::fprintf(stderr, "[ui] co-op: the game starts again (%s)\n", set.front().c_str());
    if (app().quit) app().quit();
    return true;
}

void close_action(Modal& modal) {
    play_sound(kSoundWindowClose);
    modal.pop();
}

void open_host() {
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Host co-op",
        .bodyRml = "The game starts again on your co-op characters. Then open Co-op again for your invite code, and "
                   "send it to your friends.<br/><br/>"
                   "Windows may ask whether the game may use the network: allow it, or your friends can't "
                   "connect.<br/><br/>"
                   "Don't post the invite code in public: it holds your internet address.",
        .actions = {ModalAction{.label = "Cancel", .onPressed = close_action},
                    ModalAction{.label = "Host",
                                .onPressed = [](Modal&) {
                                    play_sound(kSoundPlay);
                                    restart_with({"DES_SEAMLESS=host", "DES_SEAMLESS_CODE="});
                                }}},
    }));
}

void open_join() {
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Join co-op",
        .bodyRml = "Copy your friend's invite code (16 letters and digits, like 7KQ2-M9XD-0RTB-4HWE), then choose "
                   "Paste and join. The game starts again on your co-op characters, in your friend's world.",
        .actions = {ModalAction{.label = "Cancel", .onPressed = close_action},
                    ModalAction{.label = "Paste and join",
                                .onPressed = [](Modal&) {
                                    Rml::String text;
                                    if (auto* sys = Rml::GetSystemInterface()) sys->GetClipboardText(text);
                                    std::string code;
                                    const std::string why = co::check_code(text, &code);
                                    if (!why.empty()) {
                                        play_sound(kSoundItemDisable);
                                        toast("warning", text.empty() ? "Nothing to paste: copy the code first."
                                                                      : why);
                                        return;
                                    }
                                    play_sound(kSoundPlay);
                                    restart_with({"DES_SEAMLESS=join", "DES_SEAMLESS_CODE=" + code});
                                }}},
    }));
}

void open_leave() {
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Leave co-op",
        .bodyRml = "The game starts again on your normal characters. Your co-op characters stay as they are, for the "
                   "next time.",
        .actions = {ModalAction{.label = "Cancel", .onPressed = close_action},
                    ModalAction{.label = "Leave",
                                .onPressed = [](Modal&) {
                                    play_sound(kSoundWindowClose);
                                    restart_with({"DES_SEAMLESS=", "DES_SEAMLESS_CODE="});
                                }}},
    }));
}

std::string body(const co::Status& s) {
    if (s.mode == co::Mode::Off)
        return "Play through the game with up to three friends. One of you hosts; the others join with the host's "
               "invite code and follow the host from area to area.<br/><br/>"
               "Co-op uses its own characters, kept apart from your normal ones. No online server is used: only "
               "your friends' games and yours.";
    if (!s.started) return "Co-op did not start: " + escape(s.error) + ".";
    if (s.mode == co::Mode::Join) return "You are in a friend's game.";
    std::string b;
    if (!s.code.empty()) {
        b += "Your invite code:<br/><b>" + escape(s.code) + "</b><br/>Send it to your friends. Don't post it in "
             "public: it holds your internet address.<br/><br/>";
    }
    b += escape(s.router.empty() ? "Asking your router to open the port..." : s.router);
    if (!s.lan_code.empty() && (s.code.empty() || s.lan_code != s.code))
        b += "<br/><br/>On your own network: <b>" + escape(s.lan_code) + "</b>";
    b += "<br/><br/>Players: " + std::to_string(s.players > 0 ? s.players : 1) + " of " + std::to_string(kMaxPlayers) +
         ".";
    return b;
}

class CoopWindow final : public Modal {
public:
    explicit CoopWindow(Props props) : Modal(std::move(props)) {}

    void update() override {
        const auto now = std::chrono::steady_clock::now();
        if (now >= mNext) {
            mNext = now + std::chrono::milliseconds(500);
            const co::Status s = co::status();
            if (s.mode == co::Mode::Host && s.started) {
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
                               .onPressed =
                                   [](Modal&) {
                                       const co::Status now = co::status();
                                       const std::string code = !now.code.empty() ? now.code : now.lan_code;
                                       if (code.empty()) {
                                           toast("warning", "There is no code yet: the router hasn't answered.");
                                           return;
                                       }
                                       if (auto* sys = Rml::GetSystemInterface()) sys->SetClipboardText(code);
                                       play_sound(kSoundItemEnable);
                                       toast("info", "Copied. Send it to your friends, not in public.");
                                   },
                               .isDisabled = [] {
                                   const co::Status now = co::status();
                                   return now.code.empty() && now.lan_code.empty();
                               }});
        actions.push_back({.label = "Leave co-op", .onPressed = [](Modal&) { open_leave(); }});
    }
    return std::make_unique<CoopWindow>(Modal::Props{
        .title = "Co-op",
        .bodyRml = body(s),
        .actions = std::move(actions),
    });
}

}  // namespace ui
