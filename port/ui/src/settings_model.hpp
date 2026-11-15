// Written for Demon's Souls. The Settings page's state, apart from how it is drawn: the tab shown, the row with the
// focus on each tab, and what is open over the rows (the list of a row's values, a value being typed, the preview).
// No RmlUi and no settings: the page (settings_page.cpp) gives it what each row can do now (RowFacts, from
// settings_rows.hpp), so tests/menu_logic_check.cpp runs it alone.
//
// Back closes one thing at a time: the list, the typing, the preview or the help, then the page.
#pragma once

#include "prompts.hpp"

#include <span>
#include <string>
#include <vector>

namespace ui::menu {

// What a row of the tab shown can do now.
struct RowFacts {
    bool steps = false;        // Left and Right change it
    bool previewable = false;  // Preview shows it on the game
    bool comparable = false;   // Compare shows the value the page opened with
    enum class Enter { None, List, Edit, Open, Press } enter = Enter::None;
    int items = 0;             // what Enter lists
    int current = -1;          // the one of them in use
    std::string enterLabel;    // Enter's prompt ("List", "Type", "Open the website")
};

class SettingsModel {
public:
    enum class Mode { Rows, List, Edit, Preview, Help };

    explicit SettingsModel(int tabs);

    int tab() const { return mTab; }
    int focus() const { return mFocus[mTab]; }
    int focus_on(int tab) const { return mFocus[tab]; }  // the row a tab has the focus on, shown or not
    Mode mode() const { return mMode; }
    int cursor() const { return mCursor; }  // the list's

    // The rows of the tab shown, as they are now: the focus stays on a row (a row can come or go, a disc's Language).
    void rows_changed(std::span<const RowFacts> rows);

    // Up (-1) or Down (+1): the next row; in the preview, the next one it can show. Whether the focus moved.
    bool move(int dir, std::span<const RowFacts> rows);
    // A row chosen with the mouse (in the rows: a list or the typing closes; in the preview, only rows it can show).
    bool set_focus(int row, std::span<const RowFacts> rows);
    // The tab beside (-1, +1), or a tab chosen: on the row it had, with nothing open over it. Whether it changed.
    bool step_tab(int dir);
    bool set_tab(int tab);

    // Enter on the row with the focus: what the page does. A list opens with the value in use under the cursor.
    enum class Action { None, List, Edit, Open, Press };
    Action enter(std::span<const RowFacts> rows);
    bool list_move(int dir, std::span<const RowFacts> rows);
    // Enter in the list: the item under the cursor, and the list closes.
    int list_pick();
    bool set_cursor(int item, std::span<const RowFacts> rows);
    void edit_done();  // the typed value set or dropped

    // Preview on the row with the focus: whether it opened (only on a row it can show).
    bool preview(std::span<const RowFacts> rows);

    // Help on the row with the focus: its whole help opens, or closes again. Only where the look has no help beside
    // the rows (set_help_key): whether it opened or closed.
    void set_help_key(bool on) { mHelpKey = on; }
    bool help(std::span<const RowFacts> rows);

    // Back: closes what is open (the list, the typing, the preview, the help). False when nothing is: the page goes
    // back.
    bool back();

    // The prompts for now, in the order shown. back: Back's label ("Back", "Close").
    std::vector<prompts::Prompt> prompts(std::span<const RowFacts> rows, const std::string& back) const;

private:
    const RowFacts* focused(std::span<const RowFacts> rows) const;
    int next(int from, int dir, std::span<const RowFacts> rows) const;

    int mTab = 0;
    std::vector<int> mFocus;
    Mode mMode = Mode::Rows;
    int mCursor = 0;
    bool mHelpKey = false;
};

}  // namespace ui::menu
