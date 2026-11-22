// Written for Demon's Souls. Co-op as a page of the F1 menu (coop_page.hpp). What each action does, and the questions
// it asks, are the Co-op window's (coop_window.hpp's coop namespace); this page lays them out as the mock-up's.
#include "coop_page.hpp"

#include "coop_window.hpp"
#include "event.hpp"
#include "input.hpp"
#include "settings_page.hpp"
#include "sound.hpp"
#include "ui.hpp"

#include "../../des_seamless.h"

#include <RmlUi/Core.h>

#include <algorithm>

namespace ui {
namespace {

namespace co = des::seamless;

void set_class(Rml::Element* e, const char* name, bool on) {
    if (e->IsClassSet(name) != on) e->SetClass(name, on);
}

struct Action {
    const char* label;
    const char* brief;  // the strip's line while it has the focus
    void (*press)();    // none: Settings, which opens co-op's settings over the page
    bool (*disabled)();
};

bool never() {
    return false;
}

// Co-op's settings (settings_page.hpp's Scope::Coop), last in every mode.
constexpr Action kSettings{"Settings", "Change co-op's settings, the same ones as in Settings > Network.", nullptr,
                           never};

// The actions for co-op's mode now, in order.
std::vector<Action> actions_for(co::Mode mode) {
    if (mode == co::Mode::Off)
        return {{"Host", "Host a game, and send your friends the invite code.", coop::open_host, never},
                {"Join", "Join a friend's game with their invite code.", coop::open_join, never},
                kSettings};
    if (mode == co::Mode::Join)
        return {{"Leave co-op", "Go back to your own world where you stand, and to your normal characters.",
                 coop::open_leave, never},
                kSettings};
    return {{"Copy code", "Copy the invite code and send it to your friends.", coop::copy_code, coop::no_code},
            {"New code", "Make a new code. The old one stops working.", coop::open_new_code, never},
            {"Public address", "Give the address the internet sees, when your router can't.",
             coop::open_public_address, never},
            {"Leave co-op", "Go back to your normal characters.", coop::open_leave, never},
            kSettings};
}

// A guest's one line on how the connection is (in the strip: no box holds a single line); "" when there is more to
// say than a line, in the box beside the actions.
std::string guest_line(const co::Status& s) {
    if (s.mode != co::Mode::Join || !s.started) return "";
    switch (s.link) {
    case co::Status::Link::Connecting:   // (a problem goes in the box: it is more than a line)
        return s.join_warn ? "" : !s.join.empty() ? s.join : "Reaching the host's game...";
    case co::Status::Link::Connected: return "You're connected to the host's game.";
    case co::Status::Link::Full: return "The host's game is full. You join as soon as a place frees up.";
    case co::Status::Link::Fight: return "The host is in a boss fight. You join as soon as it's over.";
    default: return "";
    }
}

// Beside the actions: what co-op is, or the invite code, the router's answer and the players; "" for no box.
std::string info_rml(const co::Status& s) {
    if (s.mode == co::Mode::Off)
        return "<info-line>One of you hosts. The others join with the host's invite code and follow the host from "
               "area to area.</info-line>"
               "<info-line>Co-op uses its own characters, kept apart from your normal ones. This project's server only "
               "introduces your games to each other: what you play goes straight between them. A host whose router "
               "can't tell its internet address asks Google's STUN server for it.</info-line>";
    if (!s.started) return "<info-line class=\"warning\">Co-op did not start: " + escape(s.error) + ".</info-line>";
    if (s.mode == co::Mode::Join) {
        if (!guest_line(s).empty()) return "";
        if (s.link == co::Status::Link::Connecting)
            return "<info-line class=\"warning\">" + escape(s.join) + "</info-line>";
        return "<info-line class=\"warning\">The connection to the host's game was lost. Your game takes you back to "
               "your own world and keeps trying.</info-line><info-rule/>"
               "<info-line>If the host hosts again, its code is a new one: leave co-op, then join with the new code."
               "</info-line>";
    }
    std::string b;
    if (s.code_spent)
        b += "<info-line class=\"warning\">Many wrong codes were tried, so your code no longer lets anyone in. Make a "
             "new code and send it to your friends.</info-line><info-rule/>";
    if (!s.room_code.empty())
        b += "<info-head><info-label>Your invite code</info-label>" + coop::copy_button("room") + "</info-head>"
             "<info-code>" + escape(s.room_code) + "</info-code>"
             "<info-line>Send it to your friends. Anyone with this code can join you.</info-line>";
    if (!s.intro.empty())
        b += std::string(s.intro_trouble ? "<info-line class=\"warning\">" : "<info-line>") + escape(s.intro) +
             "</info-line>";
    if (!s.stuck.empty()) b += "<info-line class=\"warning\">" + escape(s.stuck) + "</info-line>";
    if (!s.room_code.empty() || !s.intro.empty()) b += "<info-rule/>";
    if (s.show_direct && !s.code.empty())
        b += std::string("<info-head><info-label>") +
             (s.room_code.empty() ? "Your invite code" : "Or the direct code") + "</info-label>" +
             coop::copy_button("direct") + "</info-head><info-code>" + escape(s.code) + "</info-code>"
             "<info-line>Send it to your friends. Don't post it in public: it holds your internet address."
             "</info-line><info-rule/>";
    if (s.show_direct)
        b += "<info-line>" + escape(s.router.empty() ? "Asking your router to open the port..." : s.router) +
             "</info-line>";
    if (!s.lan_code.empty() && (s.code.empty() || s.lan_code != s.code) && s.show_direct)
        b += "<info-head><info-line>On your own network: <info-lan>" + escape(s.lan_code) + "</info-lan></info-line>" +
             coop::copy_button("lan") + "</info-head>";
    b += std::string(s.show_direct ? "<info-rule/>" : "") + "<info-line>Players: " +
         std::to_string(s.players > 0 ? s.players : 1) + " of " + std::to_string(s.max_players) +
         " (Settings &gt; Network).";
    if (s.waiting > 0) b += " Waiting to join: " + std::to_string(s.waiting) + ".";
    return b + "</info-line>";
}

}  // namespace

CoopPage::CoopPage(Rml::Element* host, menu::PageHost& shell) : mHost(host), mShell(shell) {
    build();
}

CoopPage::~CoopPage() {
    mListeners.clear();
}

void CoopPage::build() {
    mListeners.clear();
    mActions.clear();
    clear_children(mHost);
    const co::Status s = co::status();
    mMode = static_cast<int>(s.mode);
    Rml::Element* page = append(mHost, "coop-page");
    Rml::Element* box = append(page, "coop-box");
    const std::vector<Action> list = actions_for(s.mode);
    for (size_t i = 0; i < list.size(); i++) {
        Rml::Element* a = append_text_element(box, "coop-action", list[i].label);
        mActions.push_back(a);
        const int index = static_cast<int>(i);
        mListeners.push_back(std::make_unique<ScopedEventListener>(a, Rml::EventId::Click, [this, index](Rml::Event&) {
            mFocus = index;  // (a click picks the action and does it, in one)
            press();
        }));
        mListeners.push_back(std::make_unique<ScopedEventListener>(a, Rml::EventId::Mousemove, [this, index](Rml::Event&) {
            if (!input::hover_moved() || index == mFocus) return;  // (a real move of the pointer: input.hpp)
            mFocus = index;
            play_sound(kSoundItemFocus);
        }));
    }
    mInfo = append(page, "coop-info");
    mListeners.push_back(std::make_unique<ScopedEventListener>(mInfo, Rml::EventId::Click, [this](Rml::Event& e) {
        if (coop::copy_pressed(e.GetTargetElement())) mNext = {};  // (a code's Copy button: "Copied" at once)
    }));
    mInfoShown = "\x01";  // (none shown yet: a guest's "" too sets the box's class)
    mFocus = std::clamp(mFocus, 0, std::max(0, static_cast<int>(mActions.size()) - 1));
    mFocusShown = -1;
    mNext = {};
    refresh();
}

void CoopPage::refresh() {
    const co::Status s = co::status();
    if (static_cast<int>(s.mode) != mMode) {
        build();
        return;
    }
    if (const std::string rml = info_rml(s); rml != mInfoShown) {
        mInfoShown = rml;
        mInfo->SetInnerRML(rml);
        set_class(mInfo, "none", rml.empty());
        // Both codes, the router's line and the one for this network (a public address, or an open port): closer
        // together, so the box still ends above the prompts.
        set_class(mInfo, "long",
                  s.mode == co::Mode::Host && !s.room_code.empty() && s.show_direct && !s.code.empty());
    }
    mLine = guest_line(s);
    const std::vector<Action> list = actions_for(s.mode);
    for (size_t i = 0; i < mActions.size() && i < list.size(); i++)
        set_class(mActions[i], "disabled", list[i].disabled());
}

void CoopPage::update() {
    const auto now = std::chrono::steady_clock::now();
    if (now >= mNext) {  // (the router answers a few seconds after the start, and players come and go)
        mNext = now + std::chrono::milliseconds(500);
        refresh();
    }
    if (mFocus != mFocusShown) {
        mFocusShown = mFocus;
        for (size_t i = 0; i < mActions.size(); i++) set_class(mActions[i], "focused", static_cast<int>(i) == mFocus);
    }
}

void CoopPage::move(int by) {
    const int to = std::clamp(mFocus + by, 0, std::max(0, static_cast<int>(mActions.size()) - 1));
    if (to == mFocus) return;
    mFocus = to;
    play_sound(kSoundItemFocus);
}

void CoopPage::press() {
    const std::vector<Action> list = actions_for(co::status().mode);
    if (mFocus < 0 || mFocus >= static_cast<int>(list.size())) return;
    if (list[mFocus].disabled()) {
        play_sound(kSoundWarning);
        return;
    }
    if (!list[mFocus].press) {
        mShell.open_over([](Rml::Element* host, menu::PageHost& shell) -> std::unique_ptr<menu::Page> {
            return std::make_unique<SettingsPage>(host, shell, SettingsPage::Scope::Coop);
        });
        return;
    }
    list[mFocus].press();
}

void CoopPage::focus_action(int index) {
    mFocus = std::clamp(index, 0, std::max(0, static_cast<int>(mActions.size()) - 1));
}

bool CoopPage::command(menu::Command command, bool down) {
    using menu::Command;
    if (!down) return false;
    switch (command) {
    case Command::Up: move(-1); return true;
    case Command::Down: move(1); return true;
    case Command::Confirm: press(); return true;
    default: return false;
    }
}

std::vector<prompts::Prompt> CoopPage::prompts(const std::string& backLabel) const {
    using prompts::Key;
    return {{Key::Move, "Select"}, {Key::Confirm, "Choose"}, {Key::Back, backLabel}};
}

std::string CoopPage::strip() const {
    if (!mLine.empty()) return mLine;
    const std::vector<Action> list = actions_for(static_cast<co::Mode>(mMode));
    return mFocus >= 0 && mFocus < static_cast<int>(list.size()) ? list[mFocus].brief : "";
}

}  // namespace ui
