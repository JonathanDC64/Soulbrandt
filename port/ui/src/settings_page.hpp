// Written for Demon's Souls. Settings as a page of the F1 menu (menu_page.hpp), and on its own from the start screen
// and setup: the catalogue's tabs and rows (settings_catalog.hpp), each value changed on its row with Left and Right,
// Enter listing its values beside it (or typing it, or opening a page), Preview setting the menu aside to a strip so a
// picture setting shows on the game, and Compare, held, showing the value the page opened with.
//
// The page builds the same elements in every look; how they look is the look's style sheet (looks.hpp):
//   menu-tabs > menu-tab-key(.prev .next) > menu-glyph, menu-tab(.on)
//   menu-body > menu-rows > menu-heading, menu-row(.focused .locked .waits .modified .listing)
//       menu-row > menu-label > menu-mark, menu-name
//                  menu-value > menu-arrow.left(.end), icon.lock, menu-text, menu-tag, menu-arrow.right(.end)
//                  menu-list > menu-item(.cursor .now .disabled) > menu-text, menu-tag
//       menu-row's kind: .choice .toggle .number .text .info .action
//   menu-body > menu-help > h3, menu-notes, menu-helptext, menu-footnote, menu-buttons, menu-facts (beside the rows, or
//               a box over them while Help has it open: body.helping)
//   menu-peek > menu-peek-prev(.none), menu-peek-row > menu-name, menu-value (a row's), menu-peek-next(.none),
//               menu-peek-line
// The state of the model (settings_model.hpp) sets the classes; the rows' values come from settings_rows.hpp.
#pragma once

#include "menu_page.hpp"
#include "settings_catalog.hpp"
#include "settings_model.hpp"
#include "settings_rows.hpp"
#include "ui.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ui {

class Component;
class ScopedEventListener;

class SettingsPage final : public menu::Page {
public:
    SettingsPage(Rml::Element* host, menu::PageHost& shell);
    ~SettingsPage() override;

    std::string title() const override { return "Settings"; }
    void update() override;
    bool command(menu::Command command, bool down) override;
    bool back() override;
    void release() override;
    void hidden() override;
    std::vector<prompts::Prompt> prompts(const std::string& backLabel) const override;
    std::string strip() const override;
    std::string classes() const override;
    void closing() override;

    // For the tests (ui_render_compare): the tab of that title shown, the focus on the row with that label.
    bool show_tab(const std::string& title);
    bool focus_row(const std::string& label);

private:
    struct RowView {
        const catalog::RowDesc* desc = nullptr;
        Rml::Element* row = nullptr;
        Rml::Element* value = nullptr;
        Rml::Element* left = nullptr;
        Rml::Element* right = nullptr;
        Rml::Element* text = nullptr;
        Rml::Element* tag = nullptr;
        std::string info;  // an info row's value, read once (unless it is read each frame)
    };

    std::vector<const catalog::RowDesc*> visible_rows(int tab) const;
    void build_tab();
    void refresh_facts();
    void update_tabs();
    void update_rows();
    void update_list();
    void update_help();
    void update_peek();
    void scroll_to_focus();
    void place_list();
    void listen(Rml::Element* element, Rml::EventId event, std::function<void(Rml::Event&)> callback);

    const catalog::RowDesc* focused_row() const;
    void moved();  // the focus moved: what was held for the old row lets go
    void step(int dir);
    void enter();
    void pick();
    void open(const catalog::RowDesc& row);
    void start_edit();
    void stop_edit(bool commit);
    void compare(bool on);

    Rml::Element* mHost;
    menu::PageHost& mShell;
    menu::SettingsModel mModel;
    Rml::Element* mTabs = nullptr;
    Rml::Element* mRows = nullptr;
    Rml::Element* mHelp = nullptr;
    Rml::Element* mPeek = nullptr;
    std::vector<Rml::Element*> mTabElems;
    std::vector<RowView> mViews;
    std::vector<menu::RowFacts> mFacts;
    std::vector<std::unique_ptr<ScopedEventListener>> mTabListeners;  // the page's own (tabs)
    std::vector<std::unique_ptr<ScopedEventListener>> mRowListeners;  // the tab built's (its rows, the list)
    int mBuiltTab = -1;
    int mScrolledFocus = -1;
    prompts::Device mGlyphDevice = prompts::Device::Keyboard;
    bool mGlyphsDrawn = false;

    // The list open beside a value.
    Rml::Element* mList = nullptr;
    int mListRow = -1;
    std::vector<rows::Item> mItems;
    std::vector<Rml::Element*> mItemElems;

    // The help of the row with the focus, rebuilt when what it says changes.
    std::unique_ptr<Component> mHelpBox;
    std::string mHelpShown;

    // The preview's strip.
    Rml::Element* mPeekPrev = nullptr;
    Rml::Element* mPeekNext = nullptr;
    Rml::Element* mPeekName = nullptr;
    Rml::Element* mPeekLeft = nullptr;
    Rml::Element* mPeekValue = nullptr;
    Rml::Element* mPeekTag = nullptr;
    Rml::Element* mPeekRight = nullptr;
    Rml::Element* mPeekLine = nullptr;

    // A value being typed.
    Rml::Element* mInput = nullptr;
    std::unique_ptr<ScopedEventListener> mInputKeys;
    int mInputFocusFrames = 0;
    bool mStopEdit = false;
    bool mCommitEdit = false;

    // Compare: the settings as the page opened, and the one shown that way now.
    std::map<std::string, std::string, std::less<>> mOpened;
    std::string mComparing;
};

}  // namespace ui
