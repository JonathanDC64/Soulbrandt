// Adapted from Dusklight (CC0), src/dusk/ui/menu_bar.hpp, by way of FFCCEOT-Recomp. Adapted for Demon's Souls: its
// tabs are the entries of menu_pages.hpp, and closing it asks about a restart when settings wait for one
// (menu_model.hpp).
#pragma once

#include "button.hpp"
#include "document.hpp"
#include "menu_model.hpp"
#include "tab_bar.hpp"

#include <memory>
#include <utility>

namespace ui {

class MenuBar : public Document {
public:
    MenuBar();

    MenuBar(const MenuBar&) = delete;
    MenuBar& operator=(const MenuBar&) = delete;

    void show() override;
    void hide(bool close) override;
    void update() override;
    bool focus() override;
    bool visible() const override;
    bool permanent() const override { return true; }

    static void refresh_tabs();

    // For the entries (menu_pages.hpp): a page over the menu, and no tab shown as chosen (a question, a small window).
    void open(std::unique_ptr<Document> page) { push(std::move(page)); }
    void unselect();

protected:
    bool handle_nav_command(Rml::Event& event, NavCommand cmd) override;

private:
    void build_tabs();
    void update_safe_area() noexcept;
    // Back, F1 or the close button on the menu itself: closes it, or first asks about a restart.
    void close_menu();

    Rml::Element* mRoot;
    std::unique_ptr<TabBar> mTabBar;
    std::unique_ptr<Button> mCloseButton;
    Insets mTabBarPadding;
    Rml::String mFocusedTabTitle;
    menu::Model mModel;
};

}  // namespace ui
