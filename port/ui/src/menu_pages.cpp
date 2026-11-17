// Written for Demon's Souls (menu_pages.hpp). The entries from Dusklight's menu_bar.cpp by way of FFCCEOT-Recomp (CC0),
// with Demon's Souls' own pages.
#include "menu_pages.hpp"

#include "app.hpp"
#include "coop_page.hpp"
#include "coop_window.hpp"
#include "crash_report.hpp"
#include "looks.hpp"
#include "menu_shell.hpp"
#include "mods_page.hpp"
#include "mods_window.hpp"
#include "report_ack.hpp"
#include "settings.hpp"
#include "settings_catalog.hpp"
#include "trophies_page.hpp"
#include "trophies_window.hpp"
#include "ui.hpp"

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

// "A", "A and B", "A, B and C".
std::string and_list(const std::vector<std::string>& items) {
    std::string out;
    for (size_t i = 0; i < items.size(); i++) {
        if (i > 0) out += i + 1 == items.size() ? " and " : ", ";
        out += items[i];
    }
    return out;
}

// The game starts again with the settings as they are now, past the start screen (they were playing). Once only: a
// second press before this game has closed would start a second game on the same save.
void restart_now() {
    static bool restarting = false;  // (the UI thread's only)
    if (restarting) return;
    if (!crash_report::start_again({}, true)) {
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
    std::fprintf(stderr, "[ui] the game starts again for its settings\n");
    if (app().quit) app().quit();
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
    return waiting;
}

Question restart_question(std::function<void()> later) {
    std::vector<std::string> named;  // "Renderer"
    for (const std::string& key : settings::restart_pending()) {
        const std::optional<catalog::Shown> s = catalog::shown(key);
        named.push_back(s ? s->label : key);
    }
    return {
        .title = "Restart the game now?",
        .body = and_list(named) + (named.size() == 1 ? " takes" : " take") + " effect after a restart.",
        .answers = {{"Restart now", restart_now}, {"Later", std::move(later)}},
        .safe = 1,
    };
}

}  // namespace menu
}  // namespace ui
