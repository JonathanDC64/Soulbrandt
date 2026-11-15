// Written for Demon's Souls. The F1 menu's state, apart from how it is drawn: no RmlUi, so tests/menu_logic_check.cpp
// runs it alone. So far, what closing the menu does: with restart-only settings changed and waiting for the next start,
// it asks about a restart first, once for each set of changes.
#pragma once

#include <string>
#include <vector>

namespace ui::menu {

class Model {
public:
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
    std::vector<std::string> mLater;
};

}  // namespace ui::menu
