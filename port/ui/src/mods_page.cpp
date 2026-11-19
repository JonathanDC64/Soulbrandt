// Written for Demon's Souls. Mods as a page of the F1 menu (mods_page.hpp). The mods folder is read when the page opens
// and on Refresh, through ModsState, as the Mods window reads it.
#include "mods_page.hpp"

#include "event.hpp"
#include "input.hpp"
#include "menu_pages.hpp"
#include "report.hpp"
#include "sound.hpp"
#include "ui.hpp"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>
#include <system_error>
#include <utility>

namespace ui {
namespace {

void set_class(Rml::Element* e, const char* name, bool on) {
    if (e->IsClassSet(name) != on) e->SetClass(name, on);
}

std::string quoted(const std::string& name) {
    return "\"" + escape(name) + "\"";
}

// Whether `el`, or a parent of it up to `row`, is a `tag` element.
bool within(Rml::Element* el, Rml::Element* row, const char* tag) {
    for (; el != nullptr && el != row; el = el->GetParentNode())
        if (el->GetTagName() == tag) return true;
    return false;
}

}  // namespace

ModsPage::ModsPage(Rml::Element* host, menu::PageHost& shell) : mHost(host), mShell(shell) {
    mState.load();
    build();
}

ModsPage::~ModsPage() {
    mListeners.clear();
}

// The list in its box: "Your mods" (the usable ones, in their order), then "Needs attention"; or, with no mods, how
// to add one. The summary (with Restart now under its note), the focused mod's whole note and the buttons beside it.
void ModsPage::build() {
    const bool hadRows = !mRows.empty();
    mListeners.clear();
    mRows.clear();
    mOrder.clear();
    clear_children(mHost);
    mPage = append(mHost, "mods-page");
    Rml::Element* box = append(mPage, "mods-box");
    mList = append(box, "mods-list");
    mMore[0] = append(box, "list-more");
    mMore[0]->SetClass("up", true);
    mMore[1] = append(box, "list-more");
    mMore[1]->SetClass("down", true);
    Rml::Element* side = append(mPage, "mods-side");
    mSummary = append(side, "mods-summary");
    mSummaryHead = append(mSummary, "summary-head");
    mButtons[kRestart] = append_text_element(mSummary, "mods-button", "Restart now");
    mButtons[kRestart]->SetClass("restart", true);
    mDetail = append(mSummary, "mods-detail");
    Rml::Element* buttons = append(side, "mods-buttons");
    mButtons[kFolder] = append_text_element(buttons, "mods-button", "Open the mods folder");
    mButtons[kRefresh] = append_text_element(buttons, "mods-button", "Refresh the list");
    for (int b = 0; b < kButtons; b++) {
        mListeners.push_back(std::make_unique<ScopedEventListener>(mButtons[b], Rml::EventId::Click, [this, b](Rml::Event&) {
            press(b);
        }));
        mListeners.push_back(std::make_unique<ScopedEventListener>(mButtons[b], Rml::EventId::Mousemove, [this, b](Rml::Event&) {
            if (mHelping || !input::hover_moved() || b == mButton || !shown(b)) return;  // (as a row's)
            mButton = b;
            play_sound(kSoundItemFocus);
        }));
    }
    mHelp = append(mPage, "mods-help");
    append_text_element(mHelp, "help-title", "Good to know");
    for (const std::string& line : mods_good_to_know()) append_text_element(mHelp, "help-line", line);
    Rml::Element* close = append_text_element(mHelp, "mods-button", "Close");
    close->SetClass("close", true);
    mListeners.push_back(std::make_unique<ScopedEventListener>(close, Rml::EventId::Click, [this](Rml::Event&) {
        mHelping = false;
        play_sound(kSoundWindowClose);
    }));

    const bool empty = mState.all.empty();
    set_class(mPage, "empty", empty);
    if (empty) {
        Rml::Element* e = append(mList, "mods-empty");
        if (mState.dir.empty()) {
            append_text_element(e, "empty-title", "No mods folder");
            append_text_element(e, "empty-lead", "This run keeps no data folder, so it has no mods folder.");
        } else {
            append_text_element(e, "empty-title", "No mods yet");
            append_text_element(e, "empty-lead", "How to add a mod");
            append_text_element(e, "empty-step", "1. Download a mod and unpack it if it is a .zip, .rar or .7z file.");
            append_text_element(e, "empty-step",
                                "2. Press \"Open the mods folder\" and put the mod there, in a folder of its own.");
            append_text_element(e, "empty-step",
                                "3. Press \"Refresh the list\", turn the mod on, then press \"Restart now\".");
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        bool heading = false;
        for (std::size_t i = 0; i < mState.all.size(); i++) {
            if (mState.all[i].usable() != (pass == 0)) continue;
            if (!heading) {
                append_text_element(mList, "mods-section", pass == 0 ? "Your mods" : "Needs attention");
                heading = true;
            }
            Rml::Element* row = append(mList, "mod-row");
            const int index = static_cast<int>(mRows.size());
            mRows.push_back(row);
            mOrder.push_back(i);
            mListeners.push_back(std::make_unique<ScopedEventListener>(row, Rml::EventId::Click, [this, index, row](Rml::Event& e) {
                mFocus = index;
                mButton = -1;
                if (within(e.GetTargetElement(), row, "move-up")) move_mod(-1);
                else if (within(e.GetTargetElement(), row, "move-down")) move_mod(1);
                else confirm();  // (a click picks the row and does what Enter does, in one)
            }));
            mListeners.push_back(std::make_unique<ScopedEventListener>(row, Rml::EventId::Mousemove, [this, index](Rml::Event&) {
                // (a real move of the pointer: input.hpp)
                if (mHelping || !input::hover_moved() || (index == mFocus && mButton < 0)) return;
                mFocus = index;
                mButton = -1;
                play_sound(kSoundItemFocus);
            }));
        }
    }
    mFocus = std::clamp(mFocus, 0, std::max(0, static_cast<int>(mRows.size()) - 1));
    // With no rows a button has the focus; rows that came with Refresh take it (to turn the new mod on).
    if (empty) mButton = std::max(mButton, static_cast<int>(kFolder));
    else if (!hadRows) mButton = -1;
    mFocusShown = -1;
    mButtonShown = -2;
    mScrolledFocus = -1;
    mShown = 0;
}

int ModsPage::focused_mod() const {
    return mFocus >= 0 && mFocus < static_cast<int>(mOrder.size()) ? static_cast<int>(mOrder[mFocus]) : -1;
}

void ModsPage::render() {
    const bool fixed = ModsState::fixed();
    for (std::size_t r = 0; r < mRows.size(); r++) {
        const std::size_t i = mOrder[r];
        const mods::Mod& m = mState.all[i];
        Rml::Element* row = mRows[r];
        std::string rml;
        if (m.usable()) {
            const bool on = m.enabled;
            set_class(row, "on", on);
            set_class(row, "off", !on);
            set_class(row, "pending", mState.pending(i));
            const bool top = mState.neighbour(i, -1) >= mState.all.size();
            const bool bottom = mState.neighbour(i, 1) >= mState.all.size();
            rml = "<mod-check><icon class=\"" + std::string(on ? "check-box" : "check-box-blank") +
                  "\"/></mod-check><mod-text><mod-name>" + escape(m.name) + "</mod-name>";
            if (const std::string by = mState.covered_by(i); !by.empty())
                rml += "<mod-line class=\"note\"><icon class=\"info\"/>Some of its changes are replaced by " +
                       quoted(by) + ".</mod-line>";
            else if (m.kind == mods::Kind::Gameplay)
                rml += "<mod-line class=\"gameplay\"><icon class=\"warning\"/>Changes how the game plays. You play "
                       "offline while it's on.</mod-line>";
            else
                rml += "<mod-line>Changes how the game looks or sounds.</mod-line>";
            rml += "</mod-text><mod-side>";
            if (mState.pending(i)) rml += "<icon class=\"restart\"/>";
            if (!fixed)
                rml += std::string("<mod-move><move-up class=\"") + (top ? "none" : "") +
                       "\"/><move-down class=\"" + (bottom ? "none" : "") + "\"/></mod-move>";
            rml += "</mod-side>";
        } else {
            const bool fixable = m.why == mods::Problem::NotInPlace;
            set_class(row, "problem", true);
            set_class(row, "fixable", fixable);
            rml = "<mod-check><icon class=\"" + std::string(fixable ? "warning" : "error") +
                  "\"/></mod-check><mod-text><mod-name>" + escape(m.name) + "</mod-name><mod-line>" +
                  escape(m.problem) + "</mod-line></mod-text><mod-side><state-word>" +
                  (fixable ? std::string("Fix it") : escape(mods::problem_label(m.why))) +
                  "</state-word></mod-side>";
        }
        row->SetInnerRML(rml);
    }
}

void ModsPage::render_summary() {
    std::string rml = "<summary-line>" + escape(mState.summary_line()) + "</summary-line>";
    if (ModsState::fixed())
        rml += "<summary-note class=\"info\"><icon class=\"info\"/>This run's mods were set when the game "
               "started, so they can't be changed here.</summary-note>";
    else if (mState.restart_needed())
        rml += "<summary-note class=\"warning\"><icon class=\"restart\"/>Restart the game to use your changes."
               "</summary-note>";
    if (mods::gameplay_in_use())
        rml += "<summary-note class=\"offline\"><icon class=\"offline\"/>Playing offline: a mod that changes how the "
               "game plays is on (" + escape(mods::gameplay_names()) + ").</summary-note>";
    // The focused mod's whole note, which its row has in short.
    std::string detail;
    if (const int i = focused_mod(); i >= 0) {
        const mods::Mod& m = mState.all[static_cast<std::size_t>(i)];
        if (!m.usable())
            detail = escape(m.problem) + " " + escape(mods::problem_advice(m.why));
        else if (const std::string by = mState.covered_by(static_cast<std::size_t>(i)); !by.empty())
            detail = "Some of its changes are replaced by " + quoted(by) +
                     ", which is higher in the list. Move this mod up to use its own.";
        else if (m.kind == mods::Kind::Gameplay)
            detail = "It changes how the game plays, so you play offline while it's on.";
        if (const std::string note = mods::unused_note(m); m.usable() && !note.empty())
            detail += (detail.empty() ? "" : " ") + escape(note);
    }
    mSummaryHead->SetInnerRML(rml);
    set_class(mButtons[kRestart], "none", !shown(kRestart));
    mDetail->SetInnerRML(detail);
    set_class(mDetail, "none", detail.empty());
}

void ModsPage::update() {
    if (mState.lookAgain) {  // (read again here, not inside the press)
        mState.lookAgain = false;
        mState.load();
        build();
    }
    const bool moved = mFocus != mFocusShown;
    if (moved) {
        mFocusShown = mFocus;
        mShown = 0;  // (the summary shows the focused mod's note)
    }
    if (mShown != mState.generation) {
        mShown = mState.generation;
        render();
        render_summary();
    }
    if (mButton >= 0 && !shown(mButton)) mButton = mRows.empty() ? kFolder : -1;  // (Restart now gone: the list)
    if (moved || mButton != mButtonShown) {
        mButtonShown = mButton;
        for (std::size_t r = 0; r < mRows.size(); r++)
            set_class(mRows[r], "focused", mButton < 0 && static_cast<int>(r) == mFocus);
        for (int b = 0; b < kButtons; b++) set_class(mButtons[b], "focused", b == mButton);
    }
    scroll_to_focus();
    update_marks();
}

void ModsPage::move_focus(int by) {
    const int to = std::clamp(mFocus + by, 0, std::max(0, static_cast<int>(mRows.size()) - 1));
    if (to == mFocus) return;
    mFocus = to;
    play_sound(kSoundItemFocus);
}

bool ModsPage::shown(int button) const {
    if (button != kRestart) return true;
    return !mRows.empty() && !ModsState::fixed() && mState.restart_needed();  // (no summary on an empty page)
}

void ModsPage::move_button(int by) {
    for (int b = mButton + by; b >= 0 && b < kButtons; b += by) {
        if (!shown(b)) continue;
        mButton = b;
        play_sound(kSoundItemFocus);
        return;
    }
}

void ModsPage::press(int button) {
    if (!shown(button)) return;
    mButton = button;  // (a click picks the button and presses it, in one)
    if (button == kRestart) restart();
    else if (button == kFolder) open_folder();
    else refresh();
}

void ModsPage::restart() {
    play_sound(kSoundClick);
    menu::restart_for_mods();
}

// L1 and R1 (or a row's arrows): the focused mod up or down past the next usable one; the focus goes with it.
void ModsPage::move_mod(int step) {
    const int i = focused_mod();
    if (i < 0 || !mState.all[static_cast<std::size_t>(i)].usable()) return;
    const std::size_t to = mState.move(static_cast<std::size_t>(i), step);
    if (to >= mState.all.size()) {  // (fixed, or the top or the bottom of the list already)
        play_sound(kSoundWarning);
        return;
    }
    play_sound(kSoundItemChange);
    for (std::size_t r = 0; r < mOrder.size(); r++)
        if (mOrder[r] == to) mFocus = static_cast<int>(r);
}

// Enter: the focused mod on or off, or its loose files put in place; or the focused button.
void ModsPage::confirm() {
    if (mButton >= 0) {
        press(mButton);
        return;
    }
    const int i = focused_mod();
    if (i < 0) return;
    const mods::Mod& m = mState.all[static_cast<std::size_t>(i)];
    if (m.usable()) {
        if (!mState.toggle(static_cast<std::size_t>(i))) play_sound(kSoundWarning);
        else play_sound(m.enabled ? kSoundItemEnable : kSoundItemDisable);
        return;
    }
    if (m.why != mods::Problem::NotInPlace) {
        play_sound(kSoundWarning);
        return;
    }
    play_sound(mState.fix(static_cast<std::size_t>(i)).empty() ? kSoundItemEnable : kSoundWarning);
}

void ModsPage::open_folder() {
    if (mState.dir.empty()) {
        play_sound(kSoundWarning);
        return;
    }
    play_sound(kSoundClick);
    std::error_code ec;
    std::filesystem::create_directories(mState.dir, ec);
    report::open_local(mState.dir);
}

void ModsPage::refresh() {
    play_sound(kSoundClick);
    mState.lookAgain = true;
}

bool ModsPage::command(menu::Command command, bool down) {
    using menu::Command;
    if (!down) return command == Command::Compare;  // (Compare acts as its key goes down)
    if (mHelping) {
        if (command == Command::Help || command == Command::Confirm) {
            mHelping = false;
            play_sound(kSoundWindowClose);
        }
        return true;
    }
    // The buttons sit in a column beside the list, or in a row under how to add a mod when there are no rows.
    const bool column = !mRows.empty();
    switch (command) {
    case Command::Up:
    case Command::Down: {
        const int by = command == Command::Up ? -1 : 1;
        if (mButton < 0) move_focus(by);
        else if (column) move_button(by);
        return true;
    }
    case Command::Left:
        if (!column) {
            move_button(-1);
        } else if (mButton >= 0) {
            mButton = -1;
            play_sound(kSoundItemFocus);
        }
        return true;
    case Command::Right:
        if (!column || mButton < 0) move_button(1);  // (from the list: the first button shown)
        return true;
    case Command::PrevTab:
    case Command::NextTab:
        if (mButton < 0) move_mod(command == Command::PrevTab ? -1 : 1);
        return true;
    case Command::Confirm: confirm(); return true;
    case Command::Compare: open_folder(); return true;
    case Command::Preview: refresh(); return true;
    case Command::Help: open_help(); return true;
    default: return false;
    }
}

// Back closes Good to know, or takes the focus from the buttons back to the list.
bool ModsPage::back() {
    if (mHelping) {
        mHelping = false;
        play_sound(kSoundWindowClose);
        return true;
    }
    if (mButton < 0 || mRows.empty()) return false;
    mButton = -1;
    play_sound(kSoundItemFocus);
    return true;
}

void ModsPage::open_help() {
    mHelping = true;
    play_sound(kSoundWindowOpen);
}

std::vector<prompts::Prompt> ModsPage::prompts(const std::string& backLabel) const {
    using prompts::Key;
    if (mHelping) return {{Key::Confirm, "Close"}, {Key::Back, "Back"}};
    std::vector<prompts::Prompt> out;
    if (mButton >= 0) {
        out.push_back({mRows.empty() ? Key::Change : Key::Move, "Select"});
        out.push_back({Key::Confirm, mButton == kRestart ? "Restart now" : mButton == kFolder ? "Open" : "Refresh"});
    } else {
        out.push_back({Key::Move, "Select"});
        if (const int i = focused_mod(); i >= 0) {
            const mods::Mod& m = mState.all[static_cast<std::size_t>(i)];
            if (m.usable() && !ModsState::fixed()) {
                out.push_back({Key::Confirm, m.enabled ? "Turn off" : "Turn on"});
                out.push_back({Key::PrevTab, ""});
                out.push_back({Key::NextTab, "Move"});
            } else if (m.why == mods::Problem::NotInPlace) {
                out.push_back({Key::Confirm, "Fix it"});
            }
        }
    }
    if (!mState.dir.empty()) out.push_back({Key::Compare, "Open the mods folder"});
    out.push_back({Key::Preview, "Refresh the list"});
    out.push_back({Key::Help, "Good to know"});
    out.push_back({Key::Back, mButton >= 0 && !mRows.empty() ? std::string("Back") : backLabel});  // (to the list)
    return out;
}

std::string ModsPage::strip() const {
    for (const menu::Entry& e : menu::entries())
        if (e.title == title()) return e.brief;
    return "";
}

void ModsPage::focus_mod(const std::string& name) {
    for (std::size_t r = 0; r < mOrder.size(); r++)
        if (mState.all[mOrder[r]].name == name) mFocus = static_cast<int>(r);
    update();
}

void ModsPage::focus_restart() {
    update();  // (the summary as the list is now: whether Restart now shows)
    if (shown(kRestart)) mButton = kRestart;
    update();
}

// The list moves by whole rows to keep the focused one in view, with a row's worth more past it where there is.
void ModsPage::scroll_to_focus() {
    if (mFocus == mScrolledFocus || mFocus < 0 || mFocus >= static_cast<int>(mRows.size())) return;
    const float view = mList->GetClientHeight();
    if (view <= 0.f) return;  // (not laid out yet)
    mScrolledFocus = mFocus;
    Rml::Element* row = mRows[mFocus];
    const float step = row->GetOffsetHeight();
    const float top = row->GetOffsetTop(), bottom = top + step;
    float scroll = mList->GetScrollTop();
    if (top - step < scroll) scroll = top - step;
    else if (bottom + step > scroll + view) scroll = bottom + step - view;
    mList->SetScrollTop(std::clamp(scroll, 0.f, std::max(0.f, mList->GetScrollHeight() - view)));
}

void ModsPage::update_marks() {
    const float view = mList->GetClientHeight();
    const float scroll = mList->GetScrollTop();
    set_class(mMore[0], "on", view > 0.f && scroll > 0.5f);
    set_class(mMore[1], "on", view > 0.f && scroll + view < mList->GetScrollHeight() - 0.5f);
    set_class(mHelp, "on", mHelping);
}

}  // namespace ui
