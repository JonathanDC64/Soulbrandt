// Written for Demon's Souls. Players (docs/69, "Players you met") as a page over Settings (menu_page.hpp's open_over),
// for a look that draws the menu's screens as its pages (looks.hpp's pages: DeS UI); Alpha UI keeps the Players window
// (players_window.hpp). The list and its questions are players_state.hpp's, the window's too.
//   The band keeps Settings' tabs, Network marked: the band's buttons (L1 and R1, Q and E) go back to Settings and
//   move its tab. The list shows six players at a time, each with a mark for how you met (a summon sign for help
//   either way, swords for an invasion, the Old Monk's flame, a block) and an arrow for which way: into the mark when
//   they came to your world, out of it when you went to theirs. The strip says how far back the list goes; with no
//   list (offline, or no one met), its one line goes there and no box stands empty (class none).
//
// Its elements, the same in every look (how they look is the look's sheet):
//   menu-tabs > menu-tab-key(.prev .next) > menu-glyph, menu-tab(.on)
//   players-page(.none) > players-box > player-item(.focused .blocked)
//                                         > player-tile > icon.met-*, icon.way(.in .out)
//                                           player-words > player-name > player-tag; player-how
//                                           player-ago
//                                       list-more(.up .down)(.on)
#pragma once

#include "menu_page.hpp"
#include "players_state.hpp"
#include "prompts.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Rml {
class Element;
}

namespace ui {

class ScopedEventListener;

class PlayersPage final : public menu::Page {
public:
    PlayersPage(Rml::Element* host, menu::PageHost& shell);
    ~PlayersPage() override;

    std::string title() const override { return "Players"; }
    void update() override;
    bool command(menu::Command command, bool down) override;
    bool back() override { return false; }
    std::vector<prompts::Prompt> prompts(const std::string& backLabel) const override;
    std::string strip() const override;
    std::string classes() const override { return "players"; }

    // The focus on that player (tests: ui_render_compare's pages).
    void focus_player(int index);

private:
    void draw();
    void move(int by);
    void press();
    int count() const { return static_cast<int>(mState.players.size()); }

    Rml::Element* mHost;
    menu::PageHost& mShell;
    Rml::Element* mTabs = nullptr;
    Rml::Element* mPage = nullptr;
    Rml::Element* mMore[2] = {};
    std::vector<Rml::Element*> mItems;  // kPageRows of them, showing the players from mTop
    std::vector<std::unique_ptr<ScopedEventListener>> mListeners;
    players::State mState;
    int mFocus = 0;
    int mTop = 0;
    std::string mShown;  // what the rows show now, drawn again when it changes
    prompts::Device mGlyphDevice = prompts::Device::Keyboard;
    bool mGlyphsDrawn = false;
};

}  // namespace ui
