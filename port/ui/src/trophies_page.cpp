// Written for Demon's Souls. Trophies as a page of the F1 menu (trophies_page.hpp). The set comes from trophy_set.hpp,
// as the Trophies window's does; it is read again when the unlock hook sees an unlock (trophy_toasts.cpp), so a trophy
// earned with the page open shows at once, and every two seconds while no set is found.
#include "trophies_page.hpp"

#include "event.hpp"
#include "input.hpp"
#include "menu_pages.hpp"
#include "portable.hpp"
#include "settings.hpp"
#include "sound.hpp"
#include "text_fonts.hpp"
#include "trophy_toasts.hpp"
#include "ui.hpp"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace ui {
namespace {

constexpr int kPageRows = 6;                  // L1 / R1 move this many rows, as the window's did
constexpr unsigned long long kRetryMs = 2000;  // no trophy set found yet: look again this often
constexpr const char* kShowHidden = "trophies_show_hidden";

using trophy_set::text_of;

std::string icon_rml(int id) {
    const std::string source = trophies::icon_source(id);
    return source.empty() ? "<icon class=\"trophy\"/>" : "<img src=\"" + source + "\"/>";
}

void set_class(Rml::Element* e, const char* name, bool on) {
    if (e->IsClassSet(name) != on) e->SetClass(name, on);
}

}  // namespace

TrophiesPage::TrophiesPage(Rml::Element* host, menu::PageHost& shell)
    : mHost(host), mShell(shell), mSnap(std::make_unique<sceNpTrophySnapshot>()),
      mTestHidden(trophy_set::test_hidden()) {
    mSeenUnlocks = trophies::unlock_count();  // (before the read: an unlock in between reads the list again)
    load();
    mNextRetryAt = now_ms() + kRetryMs;
    std::fprintf(stderr, "[ui] trophies page: %s\n",
                 mFound ? (std::to_string(mSnap->unlocked) + " of " + std::to_string(mSnap->count) + " unlocked, " +
                           std::to_string(mHiddenCount) + " hidden")
                              .c_str()
                        : "no trophy set found");
    build();
}

TrophiesPage::~TrophiesPage() {
    mListeners.clear();
}

void TrophiesPage::load() {
    *mSnap = sceNpTrophySnapshot{};
    mFound = trophy_set::read(*mSnap);
    mHiddenCount = 0;
    std::string text = mFound ? text_of(mSnap->title, sizeof mSnap->title) : std::string();  // (for cover_text)
    for (unsigned i = 0; mFound && i < mSnap->count; i++) {
        const sceNpTrophySnapshotEntry& t = mSnap->trophies[i];
        if (hidden(t)) mHiddenCount++;
        text += text_of(t.name, sizeof t.name);
        text += text_of(t.detail, sizeof t.detail);
    }
    // The Japanese and Asian discs' trophy lists are in Japanese, Chinese or Korean: a font for them, before the rows
    // are drawn.
    host::cover_text(text);
    mGeneration++;
}

bool TrophiesPage::hidden(const sceNpTrophySnapshotEntry& t) const {
    return t.hidden || mTestHidden.count(t.trophyId);
}

bool TrophiesPage::can_reveal(const sceNpTrophySnapshotEntry& t) const {
    return hidden(t) && !t.unlocked && settings::get_int(kShowHidden) == 0;
}

bool TrophiesPage::concealed(const sceNpTrophySnapshotEntry& t) const {
    return can_reveal(t) && !mRevealed.count(t.trophyId);
}

// The list in its box beside the summary; a row for each trophy, in the set's order.
void TrophiesPage::build() {
    mListeners.clear();
    mRows.clear();
    clear_children(mHost);
    mPage = append(mHost, "trophy-page");
    mBox = append(mPage, "trophy-box");
    mList = append(mBox, "trophy-list");
    mMore[0] = append(mBox, "list-more");
    mMore[0]->SetClass("up", true);
    mMore[1] = append(mBox, "list-more");
    mMore[1]->SetClass("down", true);
    mSummary = append(mPage, "trophy-summary");
    if (!mFound) {
        append_text_element(mList, "trophy-note",
                            "The game's trophy set (PS3_GAME/TROPDIR on the disc) was not found, so there is nothing "
                            "to list.");
    }
    for (unsigned i = 0; mFound && i < mSnap->count; i++) {
        Rml::Element* row = append(mList, "trophy-row");
        mRows.push_back(row);
        const int index = static_cast<int>(i);
        mListeners.push_back(std::make_unique<ScopedEventListener>(row, Rml::EventId::Click, [this, index](Rml::Event&) {
            if (index != mFocus) {  // (a click picks the row and does what Enter does, in one)
                mFocus = index;
                play_sound(kSoundItemFocus);
            }
            confirm();
        }));
        mListeners.push_back(std::make_unique<ScopedEventListener>(row, Rml::EventId::Mousemove, [this, index](Rml::Event&) {
            if (!input::hover_moved() || index == mFocus) return;  // (a real move of the pointer: input.hpp)
            mFocus = index;
            play_sound(kSoundItemFocus);
        }));
    }
    mFocus = std::clamp(mFocus, 0, std::max(0, static_cast<int>(mRows.size()) - 1));
    mFocusShown = -1;
    mScrolledFocus = -1;
    mListHeight = -1.f;
    mShown = 0;
}

void TrophiesPage::render_rows() {
    for (size_t i = 0; i < mRows.size(); i++) {
        const sceNpTrophySnapshotEntry& t = mSnap->trophies[i];
        const bool isHidden = hidden(t);
        const bool isConcealed = concealed(t);
        Rml::Element* row = mRows[i];
        set_class(row, "unlocked", t.unlocked != 0);
        set_class(row, "locked", t.unlocked == 0);
        set_class(row, "concealed", isConcealed);
        set_class(row, "can-reveal", can_reveal(t));

        std::string rml = std::string("<trophy-icon class=\"") + trophies::grade_class(t.grade) + "\">";
        rml += isConcealed ? "<icon class=\"trophy-hidden\"/>" : icon_rml(t.trophyId);
        rml += "</trophy-icon><trophy-text>";
        if (isConcealed) {
            rml += "<trophy-name>Hidden trophy</trophy-name><trophy-detail>Press to reveal it.</trophy-detail>";
        } else {
            const std::string name = text_of(t.name, sizeof t.name);
            rml += "<trophy-name>" + escape(name.empty() ? "Trophy " + std::to_string(t.trophyId) : name);
            if (isHidden) rml += "<trophy-tag>Hidden</trophy-tag>";
            rml += "</trophy-name><trophy-detail>" + escape(text_of(t.detail, sizeof t.detail)) + "</trophy-detail>";
        }
        rml += "</trophy-text><trophy-meta>";
        if (*trophies::grade_class(t.grade))
            rml += std::string("<trophy-grade class=\"") + trophies::grade_class(t.grade) + "\">" +
                   trophies::grade_label(t.grade) + "</trophy-grade>";
        if (t.unlocked) {
            const std::string date = t.unix_time ? trophy_set::unlock_date(t.unix_time) : std::string();
            rml += "<trophy-date>" + (date.empty() ? std::string("Unlocked") : escape(date)) + "</trophy-date>";
        } else {
            rml += "<trophy-date class=\"locked\"><icon class=\"lock\"/>Locked</trophy-date>";
        }
        rml += "</trophy-meta>";
        row->SetInnerRML(rml);
    }
}

void TrophiesPage::render_summary() {
    if (!mFound) {
        mSummary->SetInnerRML("<trophy-title>Trophies</trophy-title><trophy-count><count-text>No trophy set found"
                              "</count-text></trophy-count>");
        return;
    }
    const sceNpTrophySnapshot& s = *mSnap;
    const std::string title = text_of(s.title, sizeof s.title);
    std::string rml = "<trophy-title>" + escape(title.empty() ? std::string("Trophies") : title) + "</trophy-title>";
    rml += "<trophy-count><count-text>" + std::to_string(s.unlocked) + " of " + std::to_string(s.count) +
           " unlocked</count-text><count-percent>" + std::to_string(s.percent) + "%</count-percent></trophy-count>";
    const int fill = s.count ? static_cast<int>(100.0 * s.unlocked / s.count + 0.5) : 0;
    rml += "<menu-gauge><menu-fill style=\"width: " + std::to_string(fill) + "%\"/></menu-gauge><trophy-grades>";
    for (unsigned g = 1; g <= 4; g++) {
        if (s.per_grade[g] == 0) continue;
        rml += std::string("<trophy-grade-count class=\"") + trophies::grade_class(g) +
               "\"><icon class=\"trophy\"/><grade-name>" + trophies::grade_label(g) + "</grade-name><grade-count>" +
               std::to_string(s.unlocked_per_grade[g]) + " / " + std::to_string(s.per_grade[g]) +
               "</grade-count></trophy-grade-count>";
    }
    rml += "</trophy-grades>";
    std::string hiddenValue = "None in this game";
    if (mHiddenCount > 0)
        hiddenValue = std::to_string(mHiddenCount) + (settings::get_int(kShowHidden) ? ", shown" : ", not shown");
    rml += "<trophy-hidden><icon class=\"trophy-hidden\"/><hidden-name>Hidden trophies</hidden-name><hidden-value>" +
           hiddenValue + "</hidden-value></trophy-hidden>";
    mSummary->SetInnerRML(rml);
}

void TrophiesPage::update() {
    const unsigned unlocks = trophies::unlock_count();
    const unsigned long long now = now_ms();
    if (unlocks != mSeenUnlocks || (!mFound && now >= mNextRetryAt)) {
        mSeenUnlocks = unlocks;
        mNextRetryAt = now + kRetryMs;
        const unsigned before = mFound ? mSnap->count : 0;
        load();
        if ((mFound ? mSnap->count : 0) != before) build();  // the set appeared (or changed): the list again
    }
    if (mShown != mGeneration) {
        mShown = mGeneration;
        render_rows();
        render_summary();
    }
    if (mFocus != mFocusShown) {
        mFocusShown = mFocus;
        for (size_t i = 0; i < mRows.size(); i++) set_class(mRows[i], "focused", static_cast<int>(i) == mFocus);
    }
    fit_list();
    scroll_to_focus();
    update_marks();
}

// The list shows whole rows only, as many as the page has room for (RmlUi has no calc(): worked out here, and set
// again only when it changes); the box around it is as tall as they are.
void TrophiesPage::fit_list() {
    if (mRows.empty()) return;
    const float step = mRows.front()->GetOffsetHeight();
    const float room = mPage->GetBox().GetSize(Rml::BoxArea::Content).y;
    const float chrome = mBox->GetOffsetHeight() - mList->GetOffsetHeight();  // (the box's padding and border)
    if (step <= 0.f || room <= 0.f) return;  // (not laid out yet)
    const int fit = std::clamp(static_cast<int>((room - chrome) / step), 1, static_cast<int>(mRows.size()));
    const float height = std::round(fit * step);
    if (height == mListHeight) return;
    mListHeight = height;
    mList->SetProperty("height", std::to_string(static_cast<int>(height)) + "px");
    mScrolledFocus = -1;  // (scrolled again for the new height)
}

void TrophiesPage::move(int by) {
    if (mRows.empty()) return;
    const int to = std::clamp(mFocus + by, 0, static_cast<int>(mRows.size()) - 1);
    if (to == mFocus) return;
    mFocus = to;
    play_sound(kSoundItemFocus);
}

// Enter on a hidden trophy still locked: its name and description show, or hide again.
void TrophiesPage::confirm() {
    if (mFocus < 0 || mFocus >= static_cast<int>(mRows.size())) return;
    const sceNpTrophySnapshotEntry& t = mSnap->trophies[mFocus];
    if (!can_reveal(t)) return;
    const bool reveal = mRevealed.erase(t.trophyId) == 0;
    if (reveal) mRevealed.insert(t.trophyId);
    mGeneration++;
    play_sound(reveal ? kSoundItemEnable : kSoundItemDisable);
}

// Preview: every hidden trophy shown with its name, or none (the trophies_show_hidden setting).
void TrophiesPage::toggle_hidden() {
    if (mHiddenCount == 0) return;
    const bool on = settings::get_int(kShowHidden) == 0;
    settings::set(kShowHidden, on ? "1" : "0");
    settings::save();
    mGeneration++;
    play_sound(on ? kSoundItemEnable : kSoundItemDisable);
}

bool TrophiesPage::command(menu::Command command, bool down) {
    using menu::Command;
    if (!down) return false;
    switch (command) {
    case Command::Up: move(-1); return true;
    case Command::Down: move(1); return true;
    case Command::PrevTab: move(-kPageRows); return true;
    case Command::NextTab: move(kPageRows); return true;
    case Command::Confirm: confirm(); return true;
    case Command::Preview: toggle_hidden(); return true;
    default: return false;
    }
}

std::vector<prompts::Prompt> TrophiesPage::prompts(const std::string& backLabel) const {
    using prompts::Key;
    std::vector<prompts::Prompt> out;
    if (mRows.empty()) return {{Key::Back, backLabel}};
    out.push_back({Key::Move, "Select"});
    out.push_back({Key::PrevTab, ""});
    out.push_back({Key::NextTab, "Scroll"});
    const sceNpTrophySnapshotEntry& t = mSnap->trophies[std::clamp(mFocus, 0, static_cast<int>(mRows.size()) - 1)];
    if (can_reveal(t)) out.push_back({Key::Confirm, mRevealed.count(t.trophyId) ? "Hide it" : "Reveal it"});
    if (mHiddenCount > 0)
        out.push_back({Key::Preview, settings::get_int(kShowHidden) ? "Hide hidden ones" : "Show hidden ones"});
    out.push_back({Key::Back, backLabel});
    return out;
}

std::string TrophiesPage::strip() const {
    for (const menu::Entry& e : menu::entries())
        if (e.title == title()) return e.brief;
    return "";
}

void TrophiesPage::focus_trophy(int index) {
    if (index < 0 || index >= static_cast<int>(mRows.size())) return;
    mFocus = index;
    update();
}

// The list moves by whole rows to keep the focused one in view, with a row's worth of the list past it where there is
// more.
void TrophiesPage::scroll_to_focus() {
    if (mFocus == mScrolledFocus || mFocus < 0 || mFocus >= static_cast<int>(mRows.size())) return;
    const float view = mList->GetClientHeight();
    if (view <= 0.f || std::abs(view - mListHeight) > 1.f) return;  // (not laid out at its height yet)
    mScrolledFocus = mFocus;
    Rml::Element* row = mRows[mFocus];
    const float step = row->GetOffsetHeight();
    const float top = row->GetOffsetTop(), bottom = top + step;
    float scroll = mList->GetScrollTop();
    if (top - step < scroll) scroll = top - step;
    else if (bottom + step > scroll + view) scroll = bottom + step - view;
    if (step > 0.f) {
        const float first = mRows.front()->GetOffsetTop();
        scroll = first + std::ceil((scroll - first) / step - 0.01f) * step;
    }
    mList->SetScrollTop(std::clamp(scroll, 0.f, std::max(0.f, mList->GetScrollHeight() - view)));
}

// The marks where the list goes on past its box, above and below.
void TrophiesPage::update_marks() {
    const float view = mList->GetClientHeight();
    const float scroll = mList->GetScrollTop();
    set_class(mMore[0], "on", view > 0.f && scroll > 0.5f);
    set_class(mMore[1], "on", view > 0.f && scroll + view < mList->GetScrollHeight() - 0.5f);
}

}  // namespace ui
