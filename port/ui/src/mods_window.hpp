// The Mods window (docs/143), opened from the menu bar and the start screen: one row per folder in the mods folder.
// A mod that can be used is on or off; one that can't is listed under "Can't be turned on", its row saying why and
// its help what to do. Below: the mods folder, this session's mods and online play.
#pragma once

#include "window.hpp"

namespace ui {

class ModsWindow : public Window {
public:
    ModsWindow();

    void update() override;

private:
    void build_tab(Rml::Element* content);
};

}  // namespace ui
