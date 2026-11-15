// Written for Demon's Souls (menu_pages.hpp). The entries from Dusklight's menu_bar.cpp by way of FFCCEOT-Recomp (CC0),
// with Demon's Souls' own pages.
#include "menu_pages.hpp"

#include "app.hpp"
#include "coop_window.hpp"
#include "crash_report.hpp"
#include "menu_shell.hpp"
#include "mods_window.hpp"
#include "report_ack.hpp"
#include "settings.hpp"
#include "settings_catalog.hpp"
#include "trophies_window.hpp"
#include "ui.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <utility>

namespace ui {

// The Quit tab, the window's close button and the start screen's Quit all ask the same question.
void request_quit_confirm() {
    ask(menu::quit_question());
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

}  // namespace

const std::vector<Entry>& entries() {
    static const std::vector<Entry> sEntries = {
        {"Settings", [](MenuShell& menu) { menu.open_settings(); }},
        {"Trophies", [](MenuShell& menu) { menu.open(std::make_unique<TrophiesWindow>()); }},
        {"Mods", [](MenuShell& menu) { menu.open(std::make_unique<ModsWindow>()); }},
        {"Co-op", [](MenuShell& menu) { menu.open(make_coop_window()); }},
        {"Report an issue",
            [](MenuShell& menu) {
                ask({
                    .title = "Report an issue?",
                    .body = "Opens a new issue on GitHub with the details filled in, and a zip to attach with a "
                            "screenshot, your PC's details, your settings, what each part of the game was doing, the "
                            "game's recent messages and your save (your characters and their names), so the problem "
                            "can be seen where it happened. It has no game files; nothing is sent until you submit the "
                            "issue. Making it takes a few seconds.",
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
        {"Quit", [](MenuShell&) { request_quit_confirm(); }},
    };
    return sEntries;
}

Question quit_question() {
    return {
        .title = "Quit to the desktop?",
        .body = "Progress since the game last saved will be lost.",
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
    std::vector<std::string> named;  // "Renderer (Vulkan)"
    for (const std::string& key : settings::restart_pending()) {
        const std::optional<catalog::Shown> s = catalog::shown(key);
        named.push_back(!s ? key : s->value.empty() ? s->label : s->label + " (" + s->value + ")");
    }
    const bool one = named.size() == 1;
    return {
        .title = "Restart the game now?",
        .body = and_list(named) + (one ? " takes" : " take") + " effect when the game starts again. Later keeps " +
                (one ? "it" : "them") + " for the next start.",
        .answers = {{"Restart now", restart_now}, {"Later", std::move(later)}},
        .safe = 1,
    };
}

}  // namespace menu
}  // namespace ui
