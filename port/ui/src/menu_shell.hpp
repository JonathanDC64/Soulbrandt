// Written for Demon's Souls. The F1 menu as one document: its entries (menu_pages.hpp) and, opened from them, a page
// drawn inside it (menu_page.hpp: Settings; in a look with looks.hpp's pages, Trophies too), with the prompts for what
// the keys do now. The same document shows one page on its own from the start screen and setup (standalone). It
// replaces the menu bar and the settings window.
//
// Its elements, the same in every look (how they look is the look's style sheet, looks.hpp; a look hides what it
// doesn't draw):
//   body(.entries .page .closed .standalone .hiding .closing .asking .motion, and the page's: .preview .listing
//        .typing .helping .comparing)                  .asking: a question is up (question.hpp)
//     menu-entries > menu-end.left(.end), menu-entry(.on), menu-end.right(.end), menu-close
//                                                      the F1 menu's entries; at their ends the buttons that move
//                                                      along them, as menu-glyph (.end: at the first or the last)
//     menu-frame > menu-slot > menu-title, menu-slot-entry(.on), menu-slot-close
//                                                      the page's frame: its title, the entries (a click switches page),
//                                                      the one open marked, and a close button
//                  menu-strip                          a line on what has the focus (an entry's, a row's)
//                  menu-page                           the page's own elements (settings_page.hpp)
//                  menu-prompts > menu-prompt > menu-glyph > menu-art, menu-key; menu-says
//                                                      menu-art: the button's picture (button_art.hpp)
//     menu-badge(.on .before .now)                     Compare's line over the game
//
// A look that keeps its contents in a centred column sets --menu-column on the body; the shell sets --menu-side, the
// room each side of it (update_column).
//
// Back closes one thing at a time: what the page has open, the page (back to the entries), then the menu, which asks
// about a restart first when restart-only settings changed (menu_model.hpp). Shift, or R3, held hides it all.
#pragma once

#include "document.hpp"
#include "menu_input.hpp"
#include "menu_model.hpp"
#include "menu_page.hpp"
#include "prompts.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui {

class SettingsPage;

namespace menu {
// A button as prompts and tab bars show it: its picture and its name, the look drawing one of them
// (<menu-glyph class="face ps-cross"><menu-art device="playstation" button="cross"/><menu-key>✕</menu-key></menu-glyph>).
std::string glyph_rml(prompts::Device device, prompts::Key key);
}  // namespace menu

class MenuShell final : public Document, public menu::PageHost {
public:
    // The F1 menu over the game (DocumentScope::MenuBar), made once and shown by F1, the chord or Esc.
    MenuShell();
    // One entry's page on its own (the start screen's, setup's): Back closes it.
    struct Standalone {
        std::string entry = "Settings";  // its title (menu_pages.hpp)
    };
    explicit MenuShell(Standalone alone);
    ~MenuShell() override;

    void show() override;
    void hide(bool close) override;
    void update() override;
    bool focus() override;
    bool visible() const override;
    bool permanent() const override { return !mModel.standalone(); }  // (the F1 menu is made once)
    void cover() override;

    // What the entries do (menu_pages.cpp): another document over the menu, which shows again when it closes, or a
    // page in the menu's frame, made for the entry of that title.
    using PageMaker = std::function<std::unique_ptr<menu::Page>(Rml::Element* host, menu::PageHost& shell)>;
    void open(std::unique_ptr<Document> document);
    void open_page(const std::string& entry, const PageMaker& make);
    void open_settings();

    // Closes the menu as Back does from its entries: asks about a restart first when something waits for one.
    void close_menu();
    // Closes it without asking (Report an issue: the picture is taken without the menu).
    void dismiss();

    // The page shown, for the tests (ui_render_compare): Settings, or null; any page, or null.
    SettingsPage* settings_page();
    menu::Page* page() { return mPage.get(); }

    // menu::PageHost
    void badge(const std::string& text, const std::string& kind) override;
    void push(std::unique_ptr<Document> document) override;

protected:
    bool handle_nav_command(Rml::Event& event, NavCommand cmd) override;

private:
    MenuShell(bool standalone, DocumentScope scope);
    void on_key(Rml::Event& event, bool down);
    void command(menu::Command command, bool down);
    void back();
    void close_page();
    void finish_close();
    void choose_entry(int index);
    // A click on one of a page's entries (the frame's): the page closes and that entry opens, as from the entries.
    void switch_entry(int index);
    void hiding(bool on);
    void draw();
    void update_safe_area();
    void update_frame_height(bool preview);
    void update_column();

    menu::Model mModel;
    std::string mAlone;  // standalone: the entry whose page it shows
    std::unique_ptr<menu::Page> mPage;
    menu::Holds mHolds;
    int mEntry = 0;
    bool mHiding = false;
    bool mClosing = false;
    bool mCovering = false;
    clock::time_point mCloseAt{};
    unsigned mReleases = 0;
    Rml::Element* mBody = nullptr;
    Rml::Element* mEntries = nullptr;
    Rml::Element* mFrame = nullptr;
    Rml::Element* mSlot = nullptr;
    Rml::Element* mStrip = nullptr;
    Rml::Element* mPageHost = nullptr;
    Rml::Element* mPrompts = nullptr;
    Rml::Element* mBadge = nullptr;
    std::vector<Rml::Element*> mEntryElems;
    // The entries' ends, left and right: the buttons that move along them (L1 and R1, Q and E), in a look that draws
    // them; drawn again when the device used changes.
    Rml::Element* mEnds[2] = {};
    std::string mEndsShown;
    std::vector<Rml::Element*> mSlotElems;
    std::string mClassesShown;
    std::string mPromptsShown;
    clock::time_point mBadgeUntil{};
    Insets mSafeArea{};
    float mFrameMax = -1.f;  // the frame's max-height set, in px; -1 = none (the look's sheet)
    float mSide = -1.f;      // --menu-side set, in px; -1 = none (the look has no column)
};

}  // namespace ui
