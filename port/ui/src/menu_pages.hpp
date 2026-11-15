// Written for Demon's Souls. The F1 menu's content as data: its entries in order, each with its title and what choosing
// it does, and the questions it asks (question.hpp). The menu (menu_shell.cpp) draws an entry for each; nothing here
// knows how the menu is drawn.
//
// Adding an entry: one Entry in menu_pages.cpp's list, in its place.
#pragma once

#include "question.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui {
class Document;
class MenuShell;
}

namespace ui::menu {

struct Entry {
    std::string title;                       // its tab: "Settings"
    std::string brief;                       // what it is, in a line (a look's strip under the band)
    std::function<void(MenuShell&)> choose;  // opens its page (MenuShell::open_settings, open), or asks its question
};

// The F1 menu's entries, in order.
const std::vector<Entry>& entries();

// An entry's screen on its own, as the start screen's buttons and DES_UI_OPEN open it: in a look whose F1 screens are
// pages (looks.hpp's pages), the menu's frame with that page alone; else the entry's own window. Null for an entry
// that is a question (Report an issue, Quit).
std::unique_ptr<Document> screen_alone(const std::string& entry);

// Asked by Quit, the window's close button and the start screen's Quit.
Question quit_question(bool played = true);  // played: the game was played (not the start screen)

// What waits for a restart: "KEY=value" for each restart-only setting changed since the game started
// (settings::restart_pending), in that order. Empty when nothing does.
std::vector<std::string> waiting_for_restart();

// Asked as the F1 menu closes while something waits for a restart: Restart now starts the game again with the
// settings as they are now; Later runs `later`, and they wait for the next start.
Question restart_question(std::function<void()> later);

}  // namespace ui::menu
