// Written for Demon's Souls. Co-op as a page of the F1 menu (menu_page.hpp), for a look that draws the menu's screens
// as its pages (looks.hpp's pages: DeS UI); Alpha UI keeps the Co-op window (coop_window.hpp). The window's actions
// and questions (coop_window.hpp's coop namespace) as tall bars: Host and Join when off; Copy code, New code, Public
// address and Leave co-op while hosting; Leave co-op when joined; and last in each, Settings: co-op's settings opened
// over the page (settings_page.hpp's Scope::Coop). Beside them, in a box: what co-op is, or the invite code, the
// router's answer and the players, read again each half second. Each code's line has a small Copy button
// (coop::copy_button) for the mouse. A guest's one line on the connection goes in the strip, and the box is left out
// (class none).
//
// Its elements, the same in every look (how they look is the look's sheet):
//   coop-page > coop-box > coop-action(.focused .disabled)
//               coop-info(.none .long) > info-head > (info-label | info-line), code-copy(.done)
//                                  info-label, info-code, info-line(.warning) > info-lan, info-rule
#pragma once

#include "menu_page.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace Rml {
class Element;
}

namespace ui {

class ScopedEventListener;

class CoopPage final : public menu::Page {
public:
    CoopPage(Rml::Element* host, menu::PageHost& shell);
    ~CoopPage() override;

    std::string title() const override { return "Seamless co-op"; }
    void update() override;
    bool command(menu::Command command, bool down) override;
    bool back() override { return false; }
    std::vector<prompts::Prompt> prompts(const std::string& backLabel) const override;
    std::string strip() const override;
    std::string classes() const override { return "coop"; }

    // The action with the focus (tests: ui_render_compare's pages).
    void focus_action(int index);

private:
    void build();
    void refresh();  // the box, the guest's line and the disabled actions, from co-op's status now
    void move(int by);
    void press();

    Rml::Element* mHost;
    menu::PageHost& mShell;
    Rml::Element* mInfo = nullptr;
    std::vector<Rml::Element*> mActions;
    std::vector<std::unique_ptr<ScopedEventListener>> mListeners;
    int mFocus = 0;
    int mFocusShown = -1;
    int mMode = -1;  // the co-op mode the actions were made for
    std::string mInfoShown;
    std::string mLine;  // a guest's line for the strip, or ""
    std::chrono::steady_clock::time_point mNext{};
};

}  // namespace ui
