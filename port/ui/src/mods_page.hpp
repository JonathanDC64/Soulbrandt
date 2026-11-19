// Written for Demon's Souls. Mods as a page of the F1 menu (menu_page.hpp), for a look that draws the menu's screens
// as its pages (looks.hpp's pages: DeS UI); Alpha UI keeps the Mods window (mods_window.hpp). The same mods folder and
// the same rules (mods_state.hpp): the mods that can be used under "Your mods", in their order, then the folders that
// need attention. Enter turns a mod on or off (or puts a mod's loose files in place); L1 and R1 move it up or down, as
// the higher one wins. The two buttons under the summary open the mods folder and read it again, and so do Compare
// and Preview (□ and △); Help opens "Good to know". With no mods yet, the page says how to add one, over the buttons.
// While the list differs from what this session runs with, the summary's note says to restart, with Restart now under
// it (menu_pages.hpp's restart_for_mods). Right takes the focus from the list to the buttons beside it (Up and Down
// between them), Left or Back takes it back; while Restart now shows, the prompts say so ("→ Restart now").
//
// Its elements, the same in every look (how they look is the look's sheet):
//   mods-page(.empty) > mods-box > mods-list > mods-section, mod-row(.focused .on .off .problem .fixable .pending)
//                                                > mod-check > icon; mod-text > mod-name, mod-line(.note .gameplay)
//                                                  > icon; mod-side > icon.restart, mod-move > move-up, move-down;
//                                                  state-word
//                                              mods-empty > empty-title, empty-lead, empty-step
//                                 list-more.up, list-more.down
//                      mods-side > mods-summary > summary-head > summary-line, summary-note(.info .warning .offline)
//                                                   > icon;
//                                                 mods-button.restart(.focused .none); mods-detail(.none)
//                                  mods-buttons > mods-button(.focused)
//                      mods-help > help-title, help-line, mods-button.close
#pragma once

#include "menu_page.hpp"
#include "mods_state.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Rml {
class Element;
}

namespace ui {

class ScopedEventListener;

class ModsPage final : public menu::Page {
public:
    ModsPage(Rml::Element* host, menu::PageHost& shell);
    ~ModsPage() override;

    std::string title() const override { return "Mods"; }
    void update() override;
    bool command(menu::Command command, bool down) override;
    bool back() override;
    std::vector<prompts::Prompt> prompts(const std::string& backLabel) const override;
    std::string strip() const override;
    std::string classes() const override { return mHelping ? "mods helping" : "mods"; }

    // For the tests (ui_render_compare): the focus on the mod of that name, scrolled to; on Restart now; Good to know
    // open.
    void focus_mod(const std::string& name);
    void focus_restart();
    void open_help();

private:
    // The buttons beside the list, top to bottom: Restart now (in the summary, under its note), then the two under it.
    enum { kRestart, kFolder, kRefresh, kButtons };

    void build();
    void render();
    void render_summary();
    void move_focus(int by);
    void move_mod(int step);
    void confirm();
    bool shown(int button) const;  // Restart now only while the list differs from this session
    void move_button(int by);      // the focus to the next button shown that way
    void press(int button);
    void restart();
    void open_folder();
    void refresh();
    void scroll_to_focus();
    void update_marks();
    int focused_mod() const;  // its index in the state's list, or -1 (none: no rows)

    Rml::Element* mHost;
    menu::PageHost& mShell;
    ModsState mState;
    Rml::Element* mPage = nullptr;
    Rml::Element* mList = nullptr;
    Rml::Element* mSummary = nullptr;
    Rml::Element* mSummaryHead = nullptr;
    Rml::Element* mDetail = nullptr;
    Rml::Element* mHelp = nullptr;
    Rml::Element* mMore[2] = {};
    Rml::Element* mButtons[kButtons] = {};
    std::vector<std::size_t> mOrder;  // the rows, as indexes into the state's list: the usable mods, then the rest
    std::vector<Rml::Element*> mRows;
    std::vector<std::unique_ptr<ScopedEventListener>> mListeners;
    int mFocus = 0;     // a row (kept while a button has the focus: the summary goes on showing its note)
    int mButton = -1;   // the button with the focus, or -1 for the list's row; with no rows, always a button
    int mFocusShown = -1;
    int mButtonShown = -2;
    int mScrolledFocus = -1;
    unsigned mShown = 0;
    bool mHelping = false;
};

}  // namespace ui
