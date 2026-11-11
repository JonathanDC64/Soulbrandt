// The Mods window (docs/143), opened from the menu bar and the start screen. Made for players who have never used a
// mod: a summary on top (how many are on, whether a restart is needed, whether this session is offline), two buttons
// (open the mods folder, refresh the list), then one card per mod with a big check box and a sentence that says what
// it changes. Folders the game can't use are listed after them under "Needs attention", each saying why and what to
// do in plain words; a mod whose files only need moving is fixed by selecting it.
#pragma once

#include "window.hpp"

#include <memory>

namespace ui {

class ModsWindow : public Window {
public:
    ModsWindow();

    void update() override;

    struct State;  // the mods folder as read, shared with the cards (mods_window.cpp)

private:
    void build_tab(Rml::Element* content);
    void render_summary();

    std::shared_ptr<State> mState;
    Rml::Element* mSummary = nullptr;
    unsigned mSummaryShown = 0;
};

}  // namespace ui
