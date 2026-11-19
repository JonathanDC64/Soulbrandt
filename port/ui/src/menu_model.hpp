// Written for Demon's Souls. The F1 menu's state, apart from how it is drawn: no RmlUi, so tests/menu_logic_check.cpp
// runs it alone. Where the menu is (closed, its entries, a page opened from them), where Back goes from there, and what
// closing it does: with restart-only settings changed and waiting for the next start, it asks about a restart first,
// once for each set of changes. And what the Mods screens' Restart now does.
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

// "A", "A and B", "A, B and C".
std::string and_list(const std::vector<std::string>& items);

// Restart now on the Mods screens (menu_pages.hpp's restart_for_mods): whether it asks first, and where the new game
// comes back to. In the game it asks, as the restart for a setting does, and the new game comes back past the start
// screen. From the start screen nothing has been played: it starts again at once, and the start screen comes back. In a
// seamless co-op session it always asks, as the restart takes this game out of the session.
struct ModsRestart {
    bool ask = true;   // the question first: Restart now, or Later
    bool play = true;  // past the start screen (crash_report.hpp's start_again)
};
ModsRestart mods_restart(bool startScreen, bool coop);

// This game's part in a seamless co-op session that has started (des_seamless.h).
enum class Coop { None, Host, Guest };

// The text of a question about a restart (the F1 menu's as it closes, the Mods screens' Restart now): what takes
// effect (the mods when they changed, then the restart-only settings that wait, by their labels), and what the restart
// does to a co-op session.
std::string restart_body(const std::vector<std::string>& settings, bool mods, Coop coop);

// A restart that keeps this game in seamless co-op asks co-op's mods warning again (coop_window.hpp's before_restart,
// as Host and Join ask it) when the mods on at the next start are another set than this session's. Their order alone
// is not another set.
bool other_mods(std::vector<std::string> session, std::vector<std::string> next);

}  // namespace ui::menu
