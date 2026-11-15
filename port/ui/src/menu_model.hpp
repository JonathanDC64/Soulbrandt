// Written for Demon's Souls. The F1 menu's state, apart from how it is drawn: no RmlUi, so tests/menu_logic_check.cpp
// runs it alone. Where the menu is (closed, its entries, a page opened from them), where Back goes from there, and what
// closing it does: with restart-only settings changed and waiting for the next start, it asks about a restart first,
// once for each set of changes.
#pragma once

#include <string>
#include <vector>

namespace ui::menu {

class Model {
public:
    // standalone: a page on its own (Settings from the start screen or setup), with no entries to go back to.
    explicit Model(bool standalone = false) : mStandalone(standalone) {}

    enum class Level {
        Closed,
        Entries,  // the F1 menu's entries (menu_pages.hpp)
        Page,     // a page opened from them, in the menu (Settings)
    };
    Level level() const { return mLevel; }
    bool standalone() const { return mStandalone; }
    void open() { mLevel = mStandalone ? Level::Page : Level::Entries; }
    void open_page() { mLevel = Level::Page; }
    void closed() { mLevel = Level::Closed; }

    // Back from where the menu is, once the page has nothing open to close (settings_model.hpp's back): from a page to
    // the entries, or the menu closes (a standalone page closes with it).
    enum class Back { ToEntries, Close };
    Back back();

    // What Back (Esc, B, Circle), F1 or the close button does on the F1 menu itself.
    enum class Close {
        Now,         // close it
        AskRestart,  // ask about a restart first (menu_pages.hpp's restart_question)
    };

    // `waiting`: what waits for a restart, one "KEY=value" for each setting (menu_pages.hpp's waiting_for_restart).
    Close close(const std::vector<std::string>& waiting) const;

    // "Later" to these: the menu closes without asking until what waits is something else.
    void later(std::vector<std::string> waiting);

private:
    bool mStandalone = false;
    Level mLevel = Level::Closed;
    std::vector<std::string> mLater;
};

}  // namespace ui::menu
