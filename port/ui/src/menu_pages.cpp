// Written for Demon's Souls (menu_pages.hpp). The entries from Dusklight's menu_bar.cpp by way of FFCCEOT-Recomp (CC0),
// with Demon's Souls' own pages.
#include "menu_pages.hpp"

#include "app.hpp"
#include "coop_page.hpp"
#include "coop_window.hpp"
#include "crash_report.hpp"
#include "looks.hpp"
#include "menu_model.hpp"
#include "menu_shell.hpp"
#include "mods_page.hpp"
#include "mods_state.hpp"
#include "mods_window.hpp"
#include "report_ack.hpp"
#include "settings.hpp"
#include "settings_catalog.hpp"
#include "trophies_page.hpp"
#include "trophies_window.hpp"
#include "ui.hpp"

#include "../../des_seamless.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <utility>

namespace ui {

// The Quit tab, the window's close button and the start screen's Quit all ask the same question.
void request_quit_confirm(bool played) {
    ask(menu::quit_question(played));
}

namespace menu {
namespace {

// The game starts again with the settings and the mods as they are now: past the start screen (play: they were
// playing), or to it. `why` is for the log. In seamless co-op with another set of mods on, co-op's mods warning comes
// first (coop_window.hpp's before_restart), and the new game is told what was accepted. Once only: a second press
// before this game has closed would start a second game on the same save.
void restart_now(bool play, const char* why) {
    coop::before_restart([play, why](const std::vector<std::string>& set) {
        static bool restarting = false;  // (the UI thread's only)
        if (restarting) return;
        if (!crash_report::start_again(set, play)) {
            push_toast({
                .type = "warning",
                .title = "Restart",
                .content = "<toast-message-text>The game could not start again. Close it and start it again by "
                           "hand.</toast-message-text>",
                .duration = std::chrono::seconds(6),
            });
            return;
        }
        restarting = true;
        std::fprintf(stderr, "[ui] the game starts again for its %s%s\n", why, play ? "" : ", to the start screen");
        if (app().quit) app().quit();
    });
}

// This game's part in a seamless co-op session that has started.
Coop coop_now() {
    namespace co = des::seamless;
    const co::Status s = co::status();
    if (!s.started) return Coop::None;
    return s.mode == co::Mode::Host ? Coop::Host : s.mode == co::Mode::Join ? Coop::Guest : Coop::None;
}

// The restart-only settings that wait for the next start, by their labels ("Renderer").
std::vector<std::string> waiting_named() {
    std::vector<std::string> named;
    for (const std::string& key : settings::restart_pending()) {
        const std::optional<catalog::Shown> s = catalog::shown(key);
        named.push_back(s ? s->label : key);
    }
    return named;
}

std::unique_ptr<Page> make_trophies_page(Rml::Element* host, PageHost& shell) {
    return std::make_unique<TrophiesPage>(host, shell);
}

std::unique_ptr<Page> make_mods_page(Rml::Element* host, PageHost& shell) {
    return std::make_unique<ModsPage>(host, shell);
}

std::unique_ptr<Page> make_coop_page(Rml::Element* host, PageHost& shell) {
    return std::make_unique<CoopPage>(host, shell);
}

}  // namespace

const std::vector<Entry>& entries() {
    static const std::vector<Entry> sEntries = {
        {"Settings", "Change how the game looks, sounds and plays.",
            [](MenuShell& menu) { menu.open_settings(); }},
        {"Trophies", "See the trophies you have earned, and the ones still to find.",
            [](MenuShell& menu) {
                if (looks::current().pages) menu.open_page("Trophies", make_trophies_page);
                else menu.open(std::make_unique<TrophiesWindow>());
            }},
        {"Mods", "Turn mods on or off, and choose the order they load in.",
            [](MenuShell& menu) {
                if (looks::current().pages) menu.open_page("Mods", make_mods_page);
                else menu.open(std::make_unique<ModsWindow>());
            }},
        {"Seamless co-op", "Play through the game with up to three friends.",
            [](MenuShell& menu) {
                if (looks::current().pages) menu.open_page("Seamless co-op", make_coop_page);
                else menu.open(make_coop_window());
            }},
        {"Report an issue", "Make a report of a problem, with what the game was doing.",
            [](MenuShell& menu) {
                ask({
                    .title = "Report an issue?",
                    .body = "Opens a GitHub issue, and makes a zip to attach: a screenshot, your PC's details, "
                            "your settings, recent messages and your save. Nothing is sent until you submit it.",
                    .answers = {{"Report",
                                    [&menu] {
                                        with_zip_ack([&menu] {  // (the first report on this PC: report_ack.hpp)
                                            menu.dismiss();
                                            if (app().report_issue) app().report_issue();
                                        });
                                    }},
                        {"Cancel", {}}},
                    .safe = 1,
                });
            }},
        {"Quit", "Close the game and return to the desktop.", [](MenuShell&) { request_quit_confirm(); }},
    };
    return sEntries;
}

std::unique_ptr<Document> screen_alone(const std::string& entry) {
    if (entry == "Settings") return std::make_unique<MenuShell>(MenuShell::Standalone{});
    if (looks::current().pages && (entry == "Trophies" || entry == "Mods" || entry == "Seamless co-op"))
        return std::make_unique<MenuShell>(MenuShell::Standalone{entry});
    if (entry == "Trophies") return std::make_unique<TrophiesWindow>();
    if (entry == "Mods") return std::make_unique<ModsWindow>();
    if (entry == "Seamless co-op") return make_coop_window();
    return nullptr;
}

Question quit_question(bool played) {
    return {
        .title = "Quit to the desktop?",
        .body = played ? "Progress since the game last saved will be lost." : "",
        .answers = {{"Quit game",
                        [] {
                            if (app().quit) app().quit();
                        }},
            {"Cancel", {}}},
        .safe = 1,
        .danger = true,
    };
}

std::vector<std::string> waiting_for_restart() {
    std::vector<std::string> waiting;
    for (const std::string& key : settings::restart_pending()) waiting.push_back(key + "=" + settings::get(key));
    if (std::string on; ModsState::waiting(&on)) waiting.push_back("mods=" + on);
    return waiting;
}

Question restart_question(std::function<void()> later) {
    const std::vector<std::string> named = waiting_named();
    const bool mods = ModsState::waiting(nullptr);
    const Coop coop = coop_now();
    const char* why = !mods ? "settings" : named.empty() ? "mods" : "mods and settings";
    return {
        .title = "Restart the game now?",
        .body = restart_body(named, mods, coop),
        .answers = {{"Restart now", [why] { restart_now(true, why); }}, {"Later", std::move(later)}},
        .safe = 1,
        .danger = coop != Coop::None,
    };
}

void restart_for_mods() {
    const Coop coop = coop_now();
    const ModsRestart r = mods_restart(is_prelaunch_open(), coop != Coop::None);
    if (!r.ask) {
        restart_now(r.play, "mods");
        return;
    }
    ask({
        .title = "Restart the game now?",
        .body = restart_body(waiting_named(), true, coop),
        .answers = {{"Restart now", [play = r.play] { restart_now(play, "mods"); }}, {"Later", {}}},
        .safe = 1,
        .danger = coop != Coop::None,
    });
}

}  // namespace menu
}  // namespace ui
