// Written for Demon's Souls. Players as a page over Settings (players_page.hpp).
#include "players_page.hpp"

#include "event.hpp"
#include "input.hpp"
#include "menu_shell.hpp"
#include "settings_catalog.hpp"
#include "sound.hpp"
#include "ui.hpp"

#include <RmlUi/Core.h>

#include <algorithm>

namespace ui {
namespace {

using namespace players;

void set_class(Rml::Element* e, const char* name, bool on) {
    if (e && e->IsClassSet(name) != on) e->SetClass(name, on);
}

}  // namespace

PlayersPage::PlayersPage(Rml::Element* host, menu::PageHost& shell) : mHost(host), mShell(shell) {
    // Settings' tabs, as Settings shows them, Network marked: the band's buttons go back to Settings.
    mTabs = append(mHost, "menu-tabs");
    append(mTabs, "menu-tab-key")->SetClass("prev", true);
    for (const catalog::TabDesc& t : catalog::tabs())
        append_text_element(mTabs, "menu-tab", t.title)->SetClass("on", t.title == "Network");
    append(mTabs, "menu-tab-key")->SetClass("next", true);

    mPage = append(mHost, "players-page");
    Rml::Element* box = append(mPage, "players-box");
    for (int i = 0; i < kPageRows; i++) {
        Rml::Element* item = append(box, "player-item");
        mItems.push_back(item);
        mListeners.push_back(std::make_unique<ScopedEventListener>(item, Rml::EventId::Click, [this, i](Rml::Event&) {
            const int index = mTop + i;
            if (index >= count()) return;
            if (index != mFocus) {
                mFocus = index;
                play_sound(kSoundItemFocus);
                draw();
            } else {
                press();
            }
        }));
    }
    for (int w = 0; w < 2; w++) {
        mMore[w] = append(box, "list-more");
        mMore[w]->SetClass(w ? "down" : "up", true);
    }
    mState.start();
    draw();
}

PlayersPage::~PlayersPage() {
    mListeners.clear();
}

void PlayersPage::draw() {
    const int n = count();
    mFocus = std::clamp(mFocus, 0, std::max(0, n - 1));
    // The six shown keep the focus inside, moving only as far as it needs.
    if (mFocus < mTop) mTop = mFocus;
    if (mFocus >= mTop + kPageRows) mTop = mFocus - kPageRows + 1;
    mTop = std::clamp(mTop, 0, std::max(0, n - kPageRows));
    std::string all = std::to_string(mState.generation) + '/' + std::to_string(mFocus) + '/' + std::to_string(mTop);
    if (all == mShown) return;
    mShown = all;
    set_class(mPage, "none", n == 0);
    for (int i = 0; i < kPageRows; i++) {
        Rml::Element* item = mItems[static_cast<size_t>(i)];
        const int index = mTop + i;
        set_class(item, "shown", index < n);
        if (index >= n) {
            set_class(item, "focused", false);
            set_class(item, "blocked", false);
            item->SetInnerRML("");
            continue;
        }
        const ps3_net_player& p = mState.players[static_cast<size_t>(index)];
        set_class(item, "focused", index == mFocus);
        set_class(item, "blocked", p.blocked != 0);
        const bool met = std::string(met_icon(p)) != "met-blocked";
        std::string rml = std::string("<player-tile><icon class=\"") + met_icon(p) + "\"/>";
        if (met) rml += std::string("<icon class=\"way ") + (p.host ? "in" : "out") + "\"/>";
        rml += "</player-tile><player-words><player-name>" + escape(name_of(p));
        if (p.blocked) rml += "<player-tag>Blocked</player-tag>";
        if (p.reported) rml += "<player-tag>Reported</player-tag>";
        rml += "</player-name><player-how>" + escape(how_met(p)) + ", key " + escape(key_text(p).substr(0, 9)) +
               "</player-how></player-words><player-ago>" + escape(ago(p.at)) + "</player-ago>";
        item->SetInnerRML(rml);
    }
    set_class(mMore[0], "on", mTop > 0);
    set_class(mMore[1], "on", mTop + kPageRows < n);
}

void PlayersPage::update() {
    const prompts::Device device = input::menu_device();
    if (!mGlyphsDrawn || device != mGlyphDevice) {
        mGlyphDevice = device;
        mGlyphsDrawn = true;
        mTabs->GetFirstChild()->SetInnerRML(menu::glyph_rml(device, prompts::Key::PrevTab));
        mTabs->GetLastChild()->SetInnerRML(menu::glyph_rml(device, prompts::Key::NextTab));
    }
    mState.update();
    draw();
}

void PlayersPage::move(int by) {
    const int to = std::clamp(mFocus + by, 0, std::max(0, count() - 1));
    if (to == mFocus) return;
    mFocus = to;
    play_sound(kSoundItemFocus);
    draw();
}

void PlayersPage::press() {
    if (mFocus < 0 || mFocus >= count()) return;
    play_sound(kSoundClick);
    open_actions(mState.players[static_cast<size_t>(mFocus)]);
}

bool PlayersPage::command(menu::Command command, bool down) {
    using menu::Command;
    if (!down) return false;
    switch (command) {
    case Command::Up: move(-1); return true;
    case Command::Down: move(1); return true;
    case Command::Confirm: press(); return true;
    default: return false;  // (the band's buttons: back to Settings, menu_shell.cpp)
    }
}

std::vector<prompts::Prompt> PlayersPage::prompts(const std::string& backLabel) const {
    using prompts::Key;
    if (count() == 0) return {{Key::Back, backLabel}};
    return {{Key::Move, "Select"}, {Key::Confirm, "Block or report"}, {Key::Back, backLabel}};
}

std::string PlayersPage::strip() const {
    return mState.summary();
}

void PlayersPage::focus_player(int index) {
    mFocus = index;
    draw();
}

}  // namespace ui
