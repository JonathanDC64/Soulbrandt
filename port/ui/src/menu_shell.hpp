// Written for Demon's Souls. The F1 menu as one document: its entries (menu_pages.hpp) and, opened from them, a page
// drawn inside it (menu_page.hpp: Settings), with the prompts for what the keys do now. The same document shows
// Settings on its own from the start screen and setup (standalone). It replaces the menu bar and the settings window.
//
// Its elements, the same in every look (how they look is the look's style sheet, looks.hpp):
//   body(.entries .page .standalone .hiding .closing .motion, and the page's: .preview .listing .typing .comparing)
//     menu-entries > menu-entry(.on), menu-close       the F1 menu's entries
//     menu-frame > menu-slot > menu-slot-entry(.on)    the page's frame: the entries, the one open marked
//                  menu-page                           the page's own elements (settings_page.hpp)
//                  menu-prompts > menu-prompt > menu-glyph > menu-key, menu-says
//     menu-badge(.before .now)                         Compare's line over the game
//
// Back closes one thing at a time: what the page has open, the page (back to the entries), then the menu, which asks
// about a restart first when restart-only settings changed (menu_model.hpp). Shift, or R3, held hides it all.
#pragma once

#include "document.hpp"
#include "menu_input.hpp"
#include "menu_model.hpp"
#include "menu_page.hpp"
#include "prompts.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ui {

class SettingsPage;

namespace menu {
// A button as prompts and tab bars show it: <menu-glyph class="face ps-cross"><menu-key>✕</menu-key></menu-glyph>.
std::string glyph_rml(prompts::Device device, prompts::Key key);
}  // namespace menu

class MenuShell final : public Document, public menu::PageHost {
public:
    // The F1 menu over the game (DocumentScope::MenuBar), made once and shown by F1, the chord or Esc.
    MenuShell();
    // Settings on its own (the start screen's, setup's): Back closes it.
    struct Standalone {};
    explicit MenuShell(Standalone);
    ~MenuShell() override;

    void show() override;
    void hide(bool close) override;
    void update() override;
    bool focus() override;
    bool visible() const override;
    bool permanent() const override { return !mModel.standalone(); }  // (the F1 menu is made once)
    void cover() override;

    // What the entries do (menu_pages.cpp): another document over the menu, which shows again when it closes, or
    // Settings as the menu's page.
    void open(std::unique_ptr<Document> document);
    void open_settings();

    // Closes the menu as Back does from its entries: asks about a restart first when something waits for one.
    void close_menu();
    // Closes it without asking (Report an issue: the picture is taken without the menu).
    void dismiss();

    // The page shown, for the tests (ui_render_compare): Settings, or null.
    SettingsPage* settings_page();

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
    void hiding(bool on);
    void draw();
    void update_safe_area();
    void update_frame_height(bool preview);

    menu::Model mModel;
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
    Rml::Element* mPageHost = nullptr;
    Rml::Element* mPrompts = nullptr;
    Rml::Element* mBadge = nullptr;
    std::vector<Rml::Element*> mEntryElems;
    std::vector<Rml::Element*> mSlotElems;
    std::string mClassesShown;
    std::string mPromptsShown;
    clock::time_point mBadgeUntil{};
    Insets mSafeArea{};
    float mFrameMax = -1.f;  // the frame's max-height set, in px; -1 = none (the look's sheet)
};

}  // namespace ui
