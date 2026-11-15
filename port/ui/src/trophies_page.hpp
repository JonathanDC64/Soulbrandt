// Written for Demon's Souls. Trophies as a page of the F1 menu (menu_page.hpp), for a look that draws the menu's
// screens as its pages (looks.hpp's pages: DeS UI); Alpha UI keeps the Trophies window (trophies_window.hpp). The same
// set and the same rules: every trophy with its icon, name, description, grade and when it was unlocked, beside a
// summary of the set. A hidden trophy still locked shows as "Hidden trophy" until Enter on its row reveals it, or
// Preview shows them all (the trophies_show_hidden setting, as the window's row sets it).
//
// Its elements, the same in every look (how they look is the look's sheet):
//   trophy-page > trophy-box > trophy-list > trophy-row(.focused .unlocked .locked .concealed .can-reveal)
//                                              > trophy-icon > img or icon
//                                                trophy-text > trophy-name (> trophy-tag), trophy-detail
//                                                trophy-meta > trophy-grade.<grade>, trophy-date(.locked)
//                              list-more.up, list-more.down   where the list goes on, above or below
//                 trophy-summary > trophy-title, trophy-count > count-text, count-percent; menu-gauge > menu-fill;
//                                  trophy-grades > trophy-grade-count.<grade> > icon, grade-name, grade-count;
//                                  trophy-hidden > icon, hidden-name, hidden-value
//                 trophy-note                                 no set found
#pragma once

#include "menu_page.hpp"
#include "trophy_set.hpp"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Rml {
class Element;
}

namespace ui {

class ScopedEventListener;

class TrophiesPage final : public menu::Page {
public:
    TrophiesPage(Rml::Element* host, menu::PageHost& shell);
    ~TrophiesPage() override;

    std::string title() const override { return "Trophies"; }
    void update() override;
    bool command(menu::Command command, bool down) override;
    bool back() override { return false; }
    std::vector<prompts::Prompt> prompts(const std::string& backLabel) const override;
    std::string strip() const override;
    std::string classes() const override { return "trophies"; }

    // For the tests (ui_render_compare): the focus on that trophy's row (its place in the set), scrolled to.
    void focus_trophy(int index);

private:
    void load();
    void build();
    void render_rows();
    void render_summary();
    void move(int by);
    void confirm();
    void toggle_hidden();
    void fit_list();
    void scroll_to_focus();
    void update_marks();
    bool hidden(const sceNpTrophySnapshotEntry& t) const;
    bool concealed(const sceNpTrophySnapshotEntry& t) const;
    bool can_reveal(const sceNpTrophySnapshotEntry& t) const;

    Rml::Element* mHost;
    menu::PageHost& mShell;
    Rml::Element* mPage = nullptr;
    Rml::Element* mBox = nullptr;
    Rml::Element* mList = nullptr;
    Rml::Element* mSummary = nullptr;
    Rml::Element* mMore[2] = {};  // above, below
    std::unique_ptr<sceNpTrophySnapshot> mSnap;  // (148 KB: not the stack)
    bool mFound = false;
    int mHiddenCount = 0;
    std::set<int> mTestHidden;
    std::set<int> mRevealed;  // hidden trophies revealed on this page; not kept
    std::vector<Rml::Element*> mRows;
    std::vector<std::unique_ptr<ScopedEventListener>> mListeners;
    int mFocus = 0;
    int mFocusShown = -1;
    int mScrolledFocus = -1;
    float mListHeight = -1.f;  // set on the list, in px; -1: none yet
    unsigned mGeneration = 1;  // moves with every change the rows and summary show
    unsigned mShown = 0;
    unsigned mSeenUnlocks = 0;
    unsigned long long mNextRetryAt = 0;
};

}  // namespace ui
