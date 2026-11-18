// Written for Demon's Souls. The Settings page's state (settings_model.hpp).
#include "settings_model.hpp"

#include <algorithm>

namespace ui::menu {

using prompts::Key;

SettingsModel::SettingsModel(int tabs) : mFocus(static_cast<size_t>(std::max(tabs, 1)), 0) {}

const RowFacts* SettingsModel::focused(std::span<const RowFacts> rows) const {
    const int f = focus();
    return f >= 0 && f < static_cast<int>(rows.size()) ? &rows[f] : nullptr;
}

int SettingsModel::next(int from, int dir, std::span<const RowFacts> rows) const {
    for (int i = from + dir; i >= 0 && i < static_cast<int>(rows.size()); i += dir)
        if (mMode != Mode::Preview || rows[i].previewable) return i;
    return -1;
}

void SettingsModel::rows_changed(std::span<const RowFacts> rows) {
    int& f = mFocus[mTab];
    f = rows.empty() ? 0 : std::clamp(f, 0, static_cast<int>(rows.size()) - 1);
    const RowFacts* r = focused(rows);
    if (mMode == Mode::List && (!r || r->items == 0)) mMode = Mode::Rows;
    if (mMode == Mode::List) mCursor = std::clamp(mCursor, 0, r->items - 1);
    if (mMode == Mode::Preview && (!r || !r->previewable)) mMode = Mode::Rows;
    if (mMode == Mode::Help && (!r || !mHelpKey)) mMode = Mode::Rows;
}

bool SettingsModel::move(int dir, std::span<const RowFacts> rows) {
    if (mMode == Mode::List || mMode == Mode::Edit || mMode == Mode::Help) return false;
    const int to = next(focus(), dir < 0 ? -1 : 1, rows);
    if (to < 0) return false;
    mFocus[mTab] = to;
    return true;
}

bool SettingsModel::set_focus(int row, std::span<const RowFacts> rows) {
    if (row < 0 || row >= static_cast<int>(rows.size()) || row == focus()) return false;
    if (mMode == Mode::Preview && !rows[row].previewable) return false;
    if (mMode != Mode::Preview) mMode = Mode::Rows;
    mFocus[mTab] = row;
    return true;
}

bool SettingsModel::step_tab(int dir) {
    return set_tab(std::clamp(mTab + (dir < 0 ? -1 : 1), 0, static_cast<int>(mFocus.size()) - 1));
}

bool SettingsModel::set_tab(int tab) {
    if (tab < 0 || tab >= static_cast<int>(mFocus.size()) || tab == mTab) return false;
    mTab = tab;
    mMode = Mode::Rows;
    return true;
}

SettingsModel::Action SettingsModel::enter(std::span<const RowFacts> rows) {
    const RowFacts* r = focused(rows);
    if (!r || mMode != Mode::Rows) return Action::None;
    switch (r->enter) {
    case RowFacts::Enter::List:
        if (r->items == 0) return Action::None;
        mMode = Mode::List;
        mCursor = std::clamp(r->current, 0, r->items - 1);
        return Action::List;
    case RowFacts::Enter::Edit: mMode = Mode::Edit; return Action::Edit;
    case RowFacts::Enter::Open: return Action::Open;
    case RowFacts::Enter::Press: return Action::Press;
    case RowFacts::Enter::Read:
        if (!mHelpKey) return Action::None;  // (the help is beside the rows already)
        mMode = Mode::Help;
        return Action::Read;
    case RowFacts::Enter::None: break;
    }
    return Action::None;
}

bool SettingsModel::list_move(int dir, std::span<const RowFacts> rows) {
    const RowFacts* r = focused(rows);
    if (mMode != Mode::List || !r) return false;
    const int to = std::clamp(mCursor + (dir < 0 ? -1 : 1), 0, r->items - 1);
    if (to == mCursor) return false;
    mCursor = to;
    return true;
}

int SettingsModel::list_pick() {
    if (mMode != Mode::List) return -1;
    mMode = Mode::Rows;
    return mCursor;
}

bool SettingsModel::set_cursor(int item, std::span<const RowFacts> rows) {
    const RowFacts* r = focused(rows);
    if (mMode != Mode::List || !r || item < 0 || item >= r->items || item == mCursor) return false;
    mCursor = item;
    return true;
}

void SettingsModel::edit_done() {
    if (mMode == Mode::Edit) mMode = Mode::Rows;
}

bool SettingsModel::preview(std::span<const RowFacts> rows) {
    const RowFacts* r = focused(rows);
    if (mMode != Mode::Rows || !r || !r->previewable) return false;
    mMode = Mode::Preview;
    return true;
}

bool SettingsModel::help(std::span<const RowFacts> rows) {
    if (mMode == Mode::Help) {
        mMode = Mode::Rows;
        return true;
    }
    if (!mHelpKey || mMode != Mode::Rows || !focused(rows)) return false;
    mMode = Mode::Help;
    return true;
}

bool SettingsModel::back() {
    if (mMode == Mode::Rows) return false;
    mMode = Mode::Rows;
    return true;
}

std::vector<prompts::Prompt> SettingsModel::prompts(std::span<const RowFacts> rows, const std::string& back) const {
    const RowFacts* r = focused(rows);
    std::vector<prompts::Prompt> out;
    switch (mMode) {
    case Mode::List:
        out = {{Key::Move, "Choose"}, {Key::Confirm, "Pick"}, {Key::Back, "Back"}};
        break;
    case Mode::Edit:
        out = {{Key::Confirm, "Done"}, {Key::Back, "Cancel"}};
        break;
    case Mode::Help:
        out = {{Key::Back, "Close"}};
        break;
    case Mode::Preview:
        out.push_back({Key::Move, "Select"});
        if (r && r->steps) out.push_back({Key::Change, "Change"});
        if (r && r->comparable) out.push_back({Key::Compare, "Hold to compare"});
        out.push_back({Key::Back, "Back"});
        break;
    case Mode::Rows:
        out.push_back({Key::PrevTab, "", "tabs"});  // (one prompt with the next: L1 R1 Tabs)
        out.push_back({Key::NextTab, "Tabs", "tabs"});
        out.push_back({Key::Move, "Select"});
        if (r && r->steps) out.push_back({Key::Change, "Change"});
        if (r && r->enter != RowFacts::Enter::None && !(r->enter == RowFacts::Enter::Read && !mHelpKey) &&
            !r->enterLabel.empty())
            out.push_back({Key::Confirm, r->enterLabel});
        if (r && r->previewable) out.push_back({Key::Preview, "Preview"});
        if (r && mHelpKey) out.push_back({Key::Help, "Help"});
        out.push_back({Key::Back, back});
        break;
    }
    return out;
}

}  // namespace ui::menu
