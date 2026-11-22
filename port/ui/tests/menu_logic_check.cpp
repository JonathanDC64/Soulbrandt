// The F1 menu's rules on their own (menu_model.hpp, settings_model.hpp), with no RmlUi and no settings: what waits for
// a restart and what each row can do are made up here. Closing the menu closes it when nothing waits; while something
// does, it asks about a restart, until the player answers Later to that very set of changes, and asks again once what
// waits is different. Back closes one thing at a time: what Settings has open (a list, the typing, the preview), the
// page (back to the entries), then the menu. The prompts say what the keys do now. The Mods screens' Restart now asks
// in the game and in co-op, not on the start screen, and comes back past the start screen only from the game.
//
// Prints a line per failure and "menu_logic_check: N checks, M failed"; exit 1 on a failure. Built on demand:
// cmake --build <build> --target menu_logic_check, then menu_logic_check.
#include "menu_model.hpp"
#include "settings_model.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_fail = 0;

void check(bool ok, const char* what) {
    g_checks++;
    if (!ok) {
        std::printf("  FAIL %s\n", what);
        g_fail++;
    }
}

// The prompts' keys, in order.
std::vector<ui::prompts::Key> keys(const std::vector<ui::prompts::Prompt>& prompts) {
    std::vector<ui::prompts::Key> out;
    for (const ui::prompts::Prompt& p : prompts) out.push_back(p.key);
    return out;
}

void levels() {
    using ui::menu::Model;
    using Level = Model::Level;
    Model m;
    check(m.level() == Level::Closed, "the menu starts closed");
    m.open();
    check(m.level() == Level::Entries, "F1 opens it on its entries");
    m.open_page();
    check(m.level() == Level::Page, "an entry opens its page");
    check(m.back() == Model::Back::ToEntries && m.level() == Level::Entries, "Back from the page: the entries");
    check(m.back() == Model::Back::Close, "Back from the entries: the menu closes");
    m.closed();
    check(m.level() == Level::Closed, "closed");

    Model s(true);
    check(s.standalone(), "standalone");
    s.open();
    check(s.level() == Level::Page, "Settings on its own opens as its page");
    check(s.back() == Model::Back::Close && s.level() == Level::Page, "Back from it closes it: no entries to go to");
}

void settings_page() {
    using ui::menu::RowFacts;
    using ui::menu::SettingsModel;
    using Mode = SettingsModel::Mode;
    using Action = SettingsModel::Action;
    using Key = ui::prompts::Key;

    // A tab: a choice that lists 3 values (the 2nd in use) and shows on the game, a toggle, an info row, a text row,
    // a picture setting whose value is not in its list, an action.
    RowFacts choice;
    choice.steps = choice.previewable = choice.comparable = true;
    choice.enter = RowFacts::Enter::List;
    choice.items = 3;
    choice.current = 1;
    choice.enterLabel = "List";
    RowFacts toggle;
    toggle.steps = toggle.comparable = true;
    RowFacts info;
    RowFacts text;
    text.enter = RowFacts::Enter::Edit;
    text.enterLabel = "Type";
    RowFacts picture = choice;
    picture.current = -1;
    RowFacts action;
    action.enter = RowFacts::Enter::Open;
    action.enterLabel = "Open";
    const std::vector<RowFacts> rows{choice, toggle, info, text, picture, action};

    SettingsModel m(3);
    m.rows_changed(rows);
    check(m.tab() == 0 && m.focus() == 0 && m.mode() == Mode::Rows, "opens on the first tab's first row");
    check(!m.move(-1, rows), "Up on the first row: nothing");
    check(m.move(1, rows) && m.focus() == 1, "Down: the next row");
    check(m.move(1, rows) && m.focus() == 2, "Down onto a row that does nothing: it takes the focus all the same");
    for (int i = 0; i < 3; i++) m.move(1, rows);
    check(m.focus() == 5 && !m.move(1, rows), "Down on the last row: nothing");

    // Each tab keeps its own focus.
    check(m.step_tab(1) && m.tab() == 1, "the next tab");
    m.rows_changed(rows);
    check(m.focus() == 0, "the other tab: its own first row");
    check(m.step_tab(-1) && m.tab() == 0 && m.focus() == 5, "back to the first tab: on the row it had");
    check(!m.step_tab(-1), "no tab before the first");
    check(m.set_tab(2) && !m.step_tab(1), "no tab after the last");
    m.set_tab(0);

    // Enter.
    check(m.enter(rows) == Action::Open && m.mode() == Mode::Rows, "Enter on an action: the page opens what it opens");
    m.set_focus(3, rows);
    check(m.enter(rows) == Action::Edit && m.mode() == Mode::Edit, "Enter on a text row: typing");
    check(!m.move(1, rows) && m.focus() == 3, "Up and Down while typing: the field's");
    check(keys(m.prompts(rows, "Back")) == std::vector<Key>{Key::Confirm, Key::Back}, "typing: Done and Cancel");
    m.edit_done();
    check(m.mode() == Mode::Rows, "typed: back to the rows");
    m.set_focus(2, rows);
    check(m.enter(rows) == Action::None, "Enter on an info row: nothing");

    // The list.
    m.set_focus(0, rows);
    check(m.enter(rows) == Action::List && m.mode() == Mode::List && m.cursor() == 1, "the list opens on the value in use");
    check(!m.move(1, rows) && m.focus() == 0, "Up and Down in the list move its cursor, not the focus");
    check(m.list_move(1, rows) && m.cursor() == 2 && !m.list_move(1, rows), "the cursor stops at the list's end");
    check(m.list_move(-1, rows) && m.list_move(-1, rows) && m.cursor() == 0 && !m.list_move(-1, rows), "and at its start");
    check(m.set_cursor(2, rows) && m.cursor() == 2 && !m.set_cursor(3, rows), "the mouse puts the cursor on an item");
    check(keys(m.prompts(rows, "Back")) == std::vector<Key>{Key::Move, Key::Confirm, Key::Back}, "the list's prompts");
    check(m.list_pick() == 2 && m.mode() == Mode::Rows, "Enter picks the item and the list closes");
    m.enter(rows);
    check(m.back() && m.mode() == Mode::Rows, "Back closes the list");
    m.enter(rows);
    check(m.set_focus(1, rows) && m.mode() == Mode::Rows, "a click on another row closes the list");
    m.set_focus(4, rows);
    check(m.enter(rows) == Action::List && m.cursor() == 0, "a value not in the list: the cursor on its first item");
    std::vector<RowFacts> fewer = rows;
    fewer[4].items = 0;
    m.rows_changed(fewer);
    check(m.mode() == Mode::Rows, "the list's values gone: the list closes");
    m.enter(rows);
    m.step_tab(1);
    check(m.mode() == Mode::Rows, "another tab: nothing open over its rows");
    m.set_tab(0);

    // The preview: only rows it can show.
    m.set_focus(1, rows);
    check(!m.preview(rows), "no preview of a row the game doesn't show");
    m.set_focus(0, rows);
    check(m.preview(rows) && m.mode() == Mode::Preview, "the preview of a picture setting");
    check(m.move(1, rows) && m.focus() == 4, "Down in the preview: the next row it can show");
    check(!m.move(1, rows) && m.focus() == 4, "none after it: nothing");
    check(!m.set_focus(5, rows) && m.focus() == 4, "the mouse can't pick a row it can't show");
    check(m.enter(rows) == Action::None, "Enter in the preview: nothing");
    check(keys(m.prompts(rows, "Back")) == std::vector<Key>{Key::Move, Key::Change, Key::Compare, Key::Back},
          "the preview's prompts: Select, Change, Hold to compare, Back");
    std::vector<RowFacts> hidden = rows;
    hidden[4].previewable = false;
    m.rows_changed(hidden);
    check(m.mode() == Mode::Rows, "the row can't be shown any more: the preview closes");
    m.preview(rows);
    check(m.back() && m.mode() == Mode::Rows && m.focus() == 4, "Back closes the preview, on the row it showed");
    check(!m.back(), "Back with nothing open: the page goes back");

    // The rows' prompts follow the row.
    m.set_focus(0, rows);
    check(keys(m.prompts(rows, "Close")) == std::vector<Key>{Key::PrevTab, Key::NextTab, Key::Move, Key::Change,
                                                             Key::Confirm, Key::Preview, Key::Back},
          "a picture choice: Tabs, Select, Change, List, Preview, Back");
    check(m.prompts(rows, "Close")[0].kind == "tabs" && m.prompts(rows, "Close")[1].kind == "tabs" &&
              m.prompts(rows, "Close")[0].label.empty() && m.prompts(rows, "Close")[1].label == "Tabs",
          "the tab buttons: one prompt, of kind tabs (for a look that draws them beside the tabs to hide)");
    check(m.prompts(rows, "Close").back().label == "Close", "Back's label is the menu's (Close on its own)");
    check(m.prompts(rows, "Close")[4].label == "List", "Enter's label is the row's");
    m.set_focus(2, rows);
    check(keys(m.prompts(rows, "Back")) == std::vector<Key>{Key::PrevTab, Key::NextTab, Key::Move, Key::Back},
          "an info row: Tabs, Select, Back");
    {
        SettingsModel one(1);  // (co-op's settings on their own: one tab)
        check(keys(one.prompts(rows, "Back")) == std::vector<Key>{Key::Move, Key::Change, Key::Confirm, Key::Preview,
                                                                  Key::Back},
              "one tab: no Tabs prompt");
        check(!one.step_tab(1) && one.tab() == 0, "one tab: the tab buttons change nothing");
    }

    // Help, where the look has no help beside the rows.
    m.set_focus(0, rows);
    check(!m.help(rows) && m.mode() == Mode::Rows, "no Help key where the help is beside the rows");
    check(keys(m.prompts(rows, "Back")).back() == Key::Back &&
              keys(m.prompts(rows, "Back")).end()[-2] == Key::Preview,
          "and no Help prompt");
    m.set_help_key(true);
    check(keys(m.prompts(rows, "Back")).end()[-2] == Key::Help, "a look with a strip: Help before Back");
    check(m.help(rows) && m.mode() == Mode::Help, "Help opens the row's help");
    check(!m.move(1, rows) && m.focus() == 0, "Up and Down with the help open: nothing");
    check(m.enter(rows) == Action::None && !m.preview(rows), "nor Enter or Preview");
    check(keys(m.prompts(rows, "Back")) == std::vector<Key>{Key::Back}, "the help's prompt: Close");
    check(m.help(rows) && m.mode() == Mode::Rows, "Help again closes it");
    m.help(rows);
    check(m.back() && m.mode() == Mode::Rows, "Back closes it");
    m.enter(rows);
    check(!m.help(rows) && m.mode() == Mode::List, "no Help over a list");
    m.back();
    m.help(rows);
    m.set_help_key(false);
    m.rows_changed(rows);
    check(m.mode() == Mode::Rows, "the look changed to one with the help beside the rows: the box closes");

    // An info row with text to read (About online play): Enter opens its help where the look has a Help key.
    std::vector<RowFacts> readable = rows;
    readable[2].enter = RowFacts::Enter::Read;
    readable[2].enterLabel = "Read";
    m.rows_changed(readable);
    m.set_focus(2, readable);
    check(m.enter(readable) == Action::None && m.mode() == Mode::Rows,
          "Enter on it where the help is beside the rows: nothing");
    check(keys(m.prompts(readable, "Back")) == std::vector<Key>{Key::PrevTab, Key::NextTab, Key::Move, Key::Back},
          "and no Read prompt");
    m.set_help_key(true);
    check(keys(m.prompts(readable, "Back")) ==
              std::vector<Key>{Key::PrevTab, Key::NextTab, Key::Move, Key::Confirm, Key::Help, Key::Back},
          "a look with a strip: Read, then Help");
    check(m.prompts(readable, "Back")[3].label == "Read", "Enter's label: Read");
    check(m.enter(readable) == Action::Read && m.mode() == Mode::Help, "Enter opens the row's help");
    check(keys(m.prompts(readable, "Back")) == std::vector<Key>{Key::Back}, "the help's prompt: Close");
    check(m.back() && m.mode() == Mode::Rows, "Back closes it");
    m.set_help_key(false);
    m.rows_changed(rows);

    // Rows that come or go.
    m.set_focus(5, rows);
    const std::vector<RowFacts> shorter(rows.begin(), rows.begin() + 3);
    m.rows_changed(shorter);
    check(m.focus() == 2, "the row with the focus gone: the last row");
    m.rows_changed({});
    check(m.focus() == 0 && !m.move(1, {}) && m.enter({}) == Action::None, "a tab with no rows");
}

// Restart now on the Mods screens: from where it was pressed, whether it asks and where the new game comes back to;
// what the restart question says (the Mods screens' and the F1 menu's closing, for the mods and the settings); when
// co-op warns about the mods again before the restart.
void mods_restart() {
    using ui::menu::Coop;
    using ui::menu::mods_restart;
    using ui::menu::restart_body;
    check(mods_restart(false, false).ask && mods_restart(false, false).play,
          "in the game: it asks, and the game comes back past the start screen");
    check(!mods_restart(true, false).ask && !mods_restart(true, false).play,
          "on the start screen: at once, and the start screen comes back");
    check(mods_restart(true, true).ask && !mods_restart(true, true).play,
          "on the start screen in co-op: it asks all the same, to the start screen");
    check(mods_restart(false, true).ask && mods_restart(false, true).play, "in the game in co-op: it asks");

    check(ui::menu::and_list({}) == "" && ui::menu::and_list({"A"}) == "A" &&
              ui::menu::and_list({"A", "B"}) == "A and B" && ui::menu::and_list({"A", "B", "C"}) == "A, B and C",
          "and_list");
    check(restart_body({}, true, Coop::None) == "Your mod changes take effect after a restart.", "the mods alone");
    check(restart_body({"Renderer"}, true, Coop::None) ==
              "Your mod changes and Renderer take effect after a restart.",
          "a restart-only setting waits too: named after the mods");
    check(restart_body({"Renderer"}, false, Coop::None) == "Renderer takes effect after a restart.",
          "a setting alone: as the F1 menu's closing always asked");
    check(restart_body({"Renderer", "Language"}, false, Coop::None) ==
              "Renderer and Language take effect after a restart.",
          "two settings");
    check(restart_body({"Renderer", "Language"}, true, Coop::None) ==
              "Your mod changes, Renderer and Language take effect after a restart.",
          "the mods and two settings: one question names all three");
    const std::string host = restart_body({}, true, Coop::Host), guest = restart_body({}, true, Coop::Guest);
    check(host.find("invite code changes") != std::string::npos && host.find("own worlds") != std::string::npos,
          "hosting: the friends go home, and the code changes");
    check(guest.find("leaves the host's world") != std::string::npos &&
              guest.find("joins it again") != std::string::npos,
          "a guest: it leaves the host's world, and joins again");
    check(restart_body({"Renderer"}, false, Coop::Guest).find("joins it again") != std::string::npos,
          "a setting's restart in co-op says the same");

    using ui::menu::other_mods;
    check(!other_mods({}, {}), "no mods, before and after: no warning again");
    check(!other_mods({"A", "B"}, {"A", "B"}), "the same mods: what was accepted holds");
    check(!other_mods({"A", "B"}, {"B", "A"}), "only their order changed: the same set, no warning again");
    check(other_mods({"A"}, {"A", "B"}), "one more mod: the warning again");
    check(other_mods({"A", "B"}, {"A"}), "one mod fewer: another set");
    check(other_mods({}, {"A"}), "the first mod on: the warning");
    check(other_mods({"A"}, {}), "every mod off: another set (nothing to warn about, the accepted ones go)");
}

}  // namespace

int main() {
    levels();
    settings_page();
    mods_restart();

    using ui::menu::Model;
    using Waiting = std::vector<std::string>;
    const Waiting none, vulkan{"RSX_BACKEND=vulkan"}, d3d12{"RSX_BACKEND=d3d12"},
        two{"RSX_BACKEND=vulkan", "DES_LANGUAGE=ja"};
    constexpr auto now = Model::Close::Now, ask = Model::Close::AskRestart;

    Model m;
    check(m.close(none) == now, "nothing waits: the menu closes");
    check(m.close(vulkan) == ask, "a setting waits: it asks");
    check(m.close(vulkan) == ask, "not answered yet: it asks again");
    m.later(vulkan);
    check(m.close(vulkan) == now, "Later to it: the menu closes");
    check(m.close(d3d12) == ask, "the same setting at another value: it asks");
    check(m.close(two) == ask, "another setting as well: it asks");
    check(m.close(none) == now, "all changed back: nothing waits");
    check(m.close(vulkan) == now, "back to what Later answered: the menu closes");
    m.later(two);
    check(m.close(two) == now, "Later to two settings: the menu closes");
    check(m.close(vulkan) == ask, "one of them changed back: what waits is new, it asks");
    m.later(none);
    check(m.close(vulkan) == ask, "Later with nothing waiting forgets the earlier answer");

    // A mod change waits as one more entry (menu_pages.cpp's waiting_for_restart: "mods=" and the mods on).
    const Waiting mods{"mods=A|B"}, modsOther{"mods=B|A"}, both{"RSX_BACKEND=vulkan", "mods=A|B"};
    Model n;
    check(n.close(mods) == ask, "only a mod change waits: it asks");
    n.later(mods);
    check(n.close(mods) == now, "Later to it: the menu closes");
    check(n.close(modsOther) == ask, "the mods' order changed again: it asks");
    check(n.close(both) == ask, "a setting waits as well: one question for both");
    n.later(both);
    check(n.close(both) == now, "Later to both: the menu closes");

    std::printf("menu_logic_check: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
