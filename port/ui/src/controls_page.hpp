// Written for Demon's Souls. Change Buttons as a page over Settings (menu_page.hpp's open_over), for a look that draws
// the menu's screens as its pages (looks.hpp's pages: DeS UI); Alpha UI keeps the Controls window
// (controls_window.hpp).
// What the rows change, and how a row's new input is caught, are controls_model.hpp's, the window's too.
//   Gamepad: a row for each of the game's buttons, in sections (face buttons, shoulders, sticks, Start and Select,
//   D-pad), its value the pad button that presses it, as a picture of it.
//   Keyboard and mouse: a row for each action, its two inputs in two boxes side by side; Left and Right choose one.
// Under the rows, Restore default buttons. Enter waits for the input (Esc cancels; on the keys, Delete clears it); H
// opens the whole help in a box; the tabs change with the band's buttons (L1 and R1, Q and E) or a click.
//
// Its elements, the same in every look (how they look is the look's sheet), as Settings' (settings_page.hpp):
//   menu-tabs > menu-tab-key(.prev .next) > menu-glyph, menu-tab(.on)
//   menu-body > menu-rows(.pad .keys) > menu-heading > menu-bracket
//                                       menu-row(.focused .locked .modified .waiting)
//                                         > menu-label > menu-mark, menu-name
//                                           menu-value(.slot .on .wait .modified) > controls-fill, icon.lock,
//                                             controls-art > menu-art (the gamepad's), menu-text(.none)
//                                       controls-restore(.focused .locked)
//   menu-body > menu-help > menu-helpbox > h3, menu-helptext, menu-facts (while Help has it open: body.helping)
#pragma once

#include "controls_model.hpp"
#include "menu_page.hpp"
#include "prompts.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Rml {
class Element;
class Event;
}

namespace ui {

class ScopedEventListener;

class ControlsPage final : public menu::Page {
public:
    ControlsPage(Rml::Element* host, menu::PageHost& shell);
    ~ControlsPage() override;

    std::string title() const override { return "Change Buttons"; }
    void update() override;
    bool command(menu::Command command, bool down) override;
    bool back() override;
    // (no release(): starting a capture lets go of what the menus hold, input.hpp's set_binding_capture, and the
    // capture carries on, as the window's does)
    void hidden() override;
    std::vector<prompts::Prompt> prompts(const std::string& backLabel) const override;
    std::string strip() const override;
    std::string classes() const override;
    void closing() override;

    // For the tests (ui_render_compare): the keyboard and mouse tab (or the gamepad's), the focus on that row (the
    // count of rows: Restore) and input, waiting for that row's input, Help open.
    void show_keys(bool keys);
    void focus(int row, int slot);
    void wait();
    void open_help();

private:
    struct RowView {
        Rml::Element* row = nullptr;
        Rml::Element* values[controls::kSlots] = {};
        Rml::Element* texts[controls::kSlots] = {};
        Rml::Element* art = nullptr;  // the gamepad's: the pad button's picture
        std::string artShown;
    };

    void build();
    void update_tabs();
    void update_rows();
    void update_help();
    void scroll_to_focus();
    void move(int by);
    void press();
    void set_tab(bool keys);
    bool locked() const;      // set when the game started (PAD_MAP, PAD_KEYS), so the rows can't change it
    int row_count() const;    // the rows, without Restore
    int target(int row) const;  // the row's game button (kPadNames) or action (kTargets)
    void listen(Rml::Element* element, std::function<void(Rml::Event&)> callback);

    Rml::Element* mHost;
    menu::PageHost& mShell;
    Rml::Element* mTabs = nullptr;
    Rml::Element* mRows = nullptr;
    Rml::Element* mHelp = nullptr;
    Rml::Element* mRestore = nullptr;
    std::vector<Rml::Element*> mTabElems;
    std::vector<RowView> mViews;
    std::vector<std::unique_ptr<ScopedEventListener>> mListeners;
    std::vector<std::unique_ptr<ScopedEventListener>> mRowListeners;
    controls::Capture mCapture;
    bool mKeys = false;   // the keyboard and mouse tab
    bool mBuiltKeys = true;
    int mFocus = 0;       // the row (row_count(): Restore)
    int mSlot = 0;        // the keys' input of the focused row
    bool mHelping = false;
    int mScrolledFocus = -1;
    prompts::Device mGlyphDevice = prompts::Device::Keyboard;
    bool mGlyphsDrawn = false;
    std::string mHelpShown;
};

}  // namespace ui
