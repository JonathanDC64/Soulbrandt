// Written for Demon's Souls. The Players window (docs/69, "Players you met"), opened from Settings > Network: the
// players met online lately, the latest first, each with how and when, and the players blocked. Pressing a row offers
// Block (or Unblock) and Report. The list and those questions are players_state.hpp's, which DeS UI's page shares
// (players_page.hpp).
#pragma once

#include "window.hpp"

#include <memory>

namespace ui {

class PlayersWindow : public Window {
public:
    PlayersWindow();

    void update() override;

    struct State;  // the list and what the rows show, shared with them (players_window.cpp)

private:
    void build_tab(Rml::Element* content);
    void render_summary();

    std::shared_ptr<State> mState;
    Rml::Element* mSummary = nullptr;
};

}  // namespace ui
