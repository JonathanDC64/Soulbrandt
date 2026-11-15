// Adapted from FFCCEOT-Recomp's controls_window.hpp, itself after Dusklight's ControllerConfigWindow (CC0,
// src/dusk/ui/controller_config.cpp): the pages on the left, the bindings of the chosen page on the right, and a
// binding is changed by pressing its row and then the button or key wanted. Adapted for Demon's Souls: the bindings are
// cellPad's PAD_MAP (gamepad) and PAD_KEYS (keyboard and mouse), kept in settings.ini.
#pragma once

#include "controls_model.hpp"
#include "pane.hpp"
#include "window.hpp"

#include <string>

namespace ui {

// A keyboard or mouse input as cellPad codes it (a Windows virtual-key code; 0x100-0x104 the mouse buttons, 0x105 and
// 0x106 the wheel), and the modifier held with it in a chord (0 = none).
struct KeyInput {
    int in = 0;
    int mod = 0;
    bool operator==(const KeyInput&) const = default;
};
// The first input bound to cellPad's target `name` ("cross", "up", "ls_up", ...) outside menus, as the PAD_KEYS
// setting has it; in = 0 when it has none. On the UI thread (the button icons, button_icons.cpp).
KeyInput first_key(const char* target);
// A key's name for a key cap: "E", "Space", "Esc", "F5"; empty for the arrows, mouse buttons and wheel, which are
// drawn as pictures.
std::string key_cap_label(int vk);

class ControlsWindow : public Window {
public:
    ControlsWindow();

    void update() override;
    void hide(bool close) override;

private:
    enum class Page {
        Gamepad,
        Keyboard,
    };

    void build_tab(Rml::Element* content);
    void render_page(Pane& pane, Page page);
    void render_pad_page(Pane& pane);
    void render_key_page(Pane& pane);

    Page mPage = Page::Gamepad;
    // The game button (gamepad page) or action and its row (keyboard page) whose input is being chosen.
    controls::Capture mCapture;
};

}  // namespace ui
