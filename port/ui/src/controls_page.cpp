// Written for Demon's Souls. Change Buttons as a page over Settings (controls_page.hpp).
#include "controls_page.hpp"

#include "button_art.hpp"
#include "event.hpp"
#include "input.hpp"
#include "menu_shell.hpp"
#include "question.hpp"
#include "settings.hpp"
#include "sound.hpp"
#include "ui.hpp"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>

namespace ui {
namespace {

using namespace controls;

void set_class(Rml::Element* e, const char* name, bool on) {
    if (e && e->IsClassSet(name) != on) e->SetClass(name, on);
}

void set_px(Rml::Element* e, Rml::PropertyId id, float px) {
    e->SetProperty(id, Rml::Property(std::round(px), Rml::Unit::PX));
}

const char* key_of(bool keys) {
    return keys ? "PAD_KEYS" : "PAD_MAP";
}

// A pad button's picture as the pad in use shows it (the game's own, PlayStation, while the keyboard is in use).
std::string pad_art(int button) {
    prompts::Device device = input::menu_device();
    if (device == prompts::Device::Keyboard) device = prompts::Device::PlayStation;
    prompts::Art art;
    const std::string_view name = kPadNames[button];
    if (name == "up" || name == "right" || name == "down" || name == "left") {
        art.is = prompts::Art::Is::Dpad;
        art.code = name == "up" ? 1 : name == "right" ? 2 : name == "down" ? 4 : 8;
    } else {
        art.is = prompts::Art::Is::Button;
        art.name = name;
    }
    return button_art::rml(device, art);
}

// A section's bracket: as long as its rows (set after layout).
struct Bracket {
    Rml::Element* heading = nullptr;
    Rml::Element* line = nullptr;
    Rml::Element* last = nullptr;
    float span = -1.f;
};
std::vector<Bracket> sBrackets;  // (one page shows at a time)

}  // namespace

ControlsPage::ControlsPage(Rml::Element* host, menu::PageHost& shell) : mHost(host), mShell(shell) {
    mTabs = append(mHost, "menu-tabs");
    Rml::Element* body = append(mHost, "menu-body");
    mRows = append(body, "menu-rows");
    mHelp = append(body, "menu-help");
    append(mTabs, "menu-tab-key")->SetClass("prev", true);
    for (int t = 0; t < 2; t++) {
        Rml::Element* e = append_text_element(mTabs, "menu-tab", t ? "Keyboard and mouse" : "Gamepad");
        mTabElems.push_back(e);
        listen(e, [this, t](Rml::Event&) { set_tab(t != 0); });
    }
    append(mTabs, "menu-tab-key")->SetClass("next", true);
    listen(mTabs->GetFirstChild(), [this](Rml::Event&) { set_tab(false); });
    listen(mTabs->GetLastChild(), [this](Rml::Event&) { set_tab(true); });
    build();
}

ControlsPage::~ControlsPage() {
    mCapture.cancel();
    mRowListeners.clear();
    mListeners.clear();
    sBrackets.clear();
}

void ControlsPage::listen(Rml::Element* element, std::function<void(Rml::Event&)> callback) {
    mListeners.push_back(std::make_unique<ScopedEventListener>(element, Rml::EventId::Click, std::move(callback)));
}

int ControlsPage::row_count() const {
    return mKeys ? kTargetCount : kPadCount + 1;  // (the gamepad's: the game's buttons, then the Kick)
}

int ControlsPage::target(int row) const {
    if (mKeys) return row;
    return row < kPadCount ? kPadRows[row] : kKickTarget;
}

bool ControlsPage::locked() const {
    return settings::pinned(key_of(mKeys));
}

void ControlsPage::build() {
    mCapture.cancel();
    mRowListeners.clear();
    mViews.clear();
    sBrackets.clear();
    clear_children(mRows);
    mBuiltKeys = mKeys;
    mScrolledFocus = -1;
    set_class(mRows, "keys", mKeys);
    set_class(mRows, "pad", !mKeys);
    for (int r = 0; r < row_count(); r++) {
        const char* section = mKeys ? kTargets[r].section : r < kPadCount ? kPadSections[r] : "Forward attack";
        if (section) {
            Rml::Element* h = append_text_element(mRows, "menu-heading", section);
            sBrackets.push_back(Bracket{.heading = h, .line = append(h, "menu-bracket")});
        }
        RowView v;
        v.row = append(mRows, "menu-row");
        Rml::Element* label = append(v.row, "menu-label");
        append(label, "menu-mark");
        append_text_element(label, "menu-name",
                            mKeys ? kTargets[r].label : r < kPadCount ? kPadLabels[kPadRows[r]] : kKickLabel);
        for (int s = 0; s < (mKeys ? kSlots : 1); s++) {
            v.values[s] = append(v.row, "menu-value");
            set_class(v.values[s], "slot", mKeys);
            append(v.values[s], "controls-fill");  // (the chosen box's red, inside its cut corners)
            append(v.values[s], "icon")->SetClass("lock", true);
            if (!mKeys) v.art = append(v.values[s], "controls-art");
            v.texts[s] = append(v.values[s], "menu-text");
        }
        if (!sBrackets.empty()) sBrackets.back().last = v.row;
        const int index = r;
        // The row and key box under the pointer take the focus (on a real move of the pointer: input.hpp's
        // hover_moved); a click also does what Enter does, in one.
        const auto take_focus = [this, index](Rml::Event& e) {
            int slot = mSlot;
            for (int s = 0; s < kSlots; s++)
                for (Rml::Element* t = e.GetTargetElement(); t && t != mViews[index].row; t = t->GetParentNode())
                    if (t == mViews[index].values[s]) slot = s;
            if (index == mFocus && (!mKeys || slot == mSlot)) return;
            mFocus = index;
            mSlot = mKeys ? slot : 0;
            play_sound(kSoundItemFocus);
        };
        const auto click = [this, take_focus](Rml::Event& e) {
            if (mCapture.active()) return;
            take_focus(e);
            press();
        };
        const auto hover = [this, take_focus](Rml::Event& e) {
            if (!mCapture.active() && input::hover_moved()) take_focus(e);
        };
        mRowListeners.push_back(std::make_unique<ScopedEventListener>(v.row, Rml::EventId::Click, click));
        mRowListeners.push_back(std::make_unique<ScopedEventListener>(v.row, Rml::EventId::Mousemove, hover));
        mViews.push_back(std::move(v));
    }
    mRestore = append_text_element(mRows, "controls-restore", "Restore default buttons");
    mRowListeners.push_back(std::make_unique<ScopedEventListener>(mRestore, Rml::EventId::Click, [this](Rml::Event&) {
        if (mCapture.active()) return;
        mFocus = row_count();
        press();  // (it asks first)
    }));
    mRowListeners.push_back(std::make_unique<ScopedEventListener>(mRestore, Rml::EventId::Mousemove, [this](Rml::Event&) {
        if (mCapture.active() || !input::hover_moved() || mFocus == row_count()) return;
        mFocus = row_count();
        play_sound(kSoundItemFocus);
    }));
    mFocus = std::clamp(mFocus, 0, row_count());
    mHelpShown.clear();
}

void ControlsPage::set_tab(bool keys) {
    if (keys == mKeys || mCapture.active()) return;
    mKeys = keys;
    mFocus = 0;
    mSlot = 0;
    play_sound(kSoundTabChanged);
}

void ControlsPage::update() {
    mCapture.update();
    if (mKeys != mBuiltKeys) build();
    update_tabs();
    update_rows();
    update_help();
    for (Bracket& b : sBrackets) {
        if (b.last == nullptr) continue;
        const float span = std::round(b.last->GetOffsetTop() + b.last->GetOffsetHeight() - b.heading->GetOffsetTop());
        if (span <= 0.f || span == b.span) continue;
        b.span = span;
        set_px(b.line, Rml::PropertyId::Height, span);
    }
    scroll_to_focus();
}

void ControlsPage::update_tabs() {
    const prompts::Device device = input::menu_device();
    if (!mGlyphsDrawn || device != mGlyphDevice) {
        mGlyphDevice = device;
        mGlyphsDrawn = true;
        mTabs->GetFirstChild()->SetInnerRML(menu::glyph_rml(device, prompts::Key::PrevTab));
        mTabs->GetLastChild()->SetInnerRML(menu::glyph_rml(device, prompts::Key::NextTab));
    }
    for (size_t t = 0; t < mTabElems.size(); t++) set_class(mTabElems[t], "on", (t != 0) == mKeys);
}

void ControlsPage::update_rows() {
    const bool pinned = locked();
    const prompts::Device device = input::menu_device();  // (the extra buttons' names)
    for (int r = 0; r < static_cast<int>(mViews.size()); r++) {
        RowView& v = mViews[r];
        const bool focused = r == mFocus;
        const int t = target(r);
        const bool waiting = mCapture.active() && mCapture.target() == t && mCapture.pad() == !mKeys;
        bool modified = false;
        set_class(v.row, "focused", focused);
        set_class(v.row, "locked", pinned);
        for (int s = 0; s < (mKeys ? kSlots : 1); s++) {
            const bool wait = waiting && (!mKeys || mCapture.slot() == s);
            const bool changed = mKeys                ? key_modified(t, s)
                                 : t == kKickTarget ? pad_kick() >= 0
                                                    : pad_modified(t) || kick_takes(t);
            modified = modified || changed;
            set_class(v.values[s], "on", focused && mKeys && s == mSlot && !pinned);
            set_class(v.values[s], "wait", wait);
            set_class(v.values[s], "modified", changed);
            std::string text;
            if (wait) text = mCapture.pending_label();
            else if (mKeys) text = slot_label(t, s);
            else if (t == kKickTarget) text = kick_label(device);
            else text = pad_value_label(t, device);
            if (v.art) {
                // A picture of one of the PS3's buttons; the others (the extra ones, none) by name. A row with a back
                // button or paddle added shows its own button's picture, then "+ Right back button".
                const int host = t == kKickTarget ? pad_kick() : kick_takes(t) ? -1 : pad_map()[t];
                const std::string art = wait || host < 0 || host >= kPadCount ? std::string() : pad_art(host);
                const std::string added = wait || art.empty() || t == kKickTarget ? "" : pad_added_label(t, device);
                if (!added.empty()) text = "+ " + added;
                set_class(v.values[s], "named", art.empty() && !wait);
                set_class(v.values[s], "added", !added.empty());
                if (art != v.artShown) {
                    v.artShown = art;
                    v.art->SetInnerRML(art);
                }
            }
            set_text_content(v.texts[s], text);
            set_class(v.texts[s], "none", text == "None" || text == "None (Kick)");
        }
        set_class(v.row, "modified", modified);
        set_class(v.row, "waiting", waiting);
    }
    set_class(mRestore, "focused", mFocus == row_count());
    set_class(mRestore, "locked", pinned);
}

void ControlsPage::update_help() {
    if (!mHelping) {
        if (!mHelpShown.empty()) {
            mHelpShown.clear();
            clear_children(mHelp);
        }
        return;
    }
    std::string rml = "<menu-helpbox><h3>" + std::string(mKeys ? "Keyboard and mouse" : "Gamepad") + "</h3>";
    if (locked())
        rml += "<menu-notes><status-badge class=\"warning\">Set to " + escape(settings::pinned_value(key_of(mKeys))) +
               " when the game started (" + key_of(mKeys) + "), so these can't change it.</status-badge></menu-notes>";
    rml += mKeys ? "<menu-helptext>Choose a row and one of its two boxes, then press a key, mouse button or wheel. "
                   "Hold Shift, Ctrl or Alt for a combination. Delete clears the box; Esc cancels.<br/><br/>"
                   "The layout starts as the Souls layout this port ships with. A key works one action only: given "
                   "to another, it leaves the one it had.</menu-helptext>"
                 : "<menu-helptext>Choose a row, then press the button for it. Esc cancels.<br/><br/>A row is the "
                   "game's button; its box shows the button on your pad that presses it. Give a row a button another "
                   "row had, and the two swap.<br/><br/>A back button or paddle is added to the row's button, so "
                   "both press it. Press the same one again on that row to take it off.<br/><br/>Kick: " +
                       std::string(kKickAbout) + "</menu-helptext>";
    rml += "<menu-facts><menu-fact><menu-fact-name>Variable</menu-fact-name><menu-fact-value class=\"key\">" +
           std::string(key_of(mKeys)) + "</menu-fact-value></menu-fact></menu-facts></menu-helpbox>";
    if (rml != mHelpShown) {
        mHelpShown = rml;
        mHelp->SetInnerRML(rml);
    }
}

// The focused row (or Restore) in view, the rows moving by whole rows and taking a section's heading with its first.
void ControlsPage::scroll_to_focus() {
    if (mFocus == mScrolledFocus) return;
    const float view = mRows->GetClientHeight();
    if (view <= 0.f) return;  // (not laid out yet)
    Rml::Element* row = mFocus < static_cast<int>(mViews.size()) ? mViews[mFocus].row : mRestore;
    mScrolledFocus = mFocus;
    const float top = row->GetOffsetTop(), bottom = top + row->GetOffsetHeight();
    const float edge = row->GetOffsetHeight() * 0.6f;
    float scroll = mRows->GetScrollTop();
    std::vector<float> stops;
    for (int i = 0; i < mRows->GetNumChildren(); i++) stops.push_back(mRows->GetChild(i)->GetOffsetTop());
    if (top < scroll + edge) {
        float to = 0.f;
        for (const float s : stops)
            if (s <= top - edge) to = s;
        scroll = to;
    } else if (bottom > scroll + view - edge) {
        for (const float s : stops)
            if (s >= bottom - view + edge) {
                scroll = s;
                break;
            }
    }
    mRows->SetScrollTop(std::clamp(scroll, 0.f, std::max(0.f, mRows->GetScrollHeight() - view)));
}

void ControlsPage::move(int by) {
    const int to = std::clamp(mFocus + by, 0, row_count());
    if (to == mFocus) return;
    mFocus = to;
    play_sound(kSoundItemFocus);
}

void ControlsPage::press() {
    if (locked()) {
        play_sound(kSoundWarning);
        return;
    }
    if (mFocus == row_count()) {
        play_sound(kSoundClick);
        ask(Question{
            .title = "Restore default buttons?",
            .body = "Gamepad buttons and keys go back to their defaults.",
            .answers = {{"Restore",
                            [] {
                                play_sound(kSoundClick);
                                restore_defaults();
                            }},
                        {"Cancel", {}}},
            .safe = 1,
        });
        return;
    }
    play_sound(kSoundClick);
    mCapture.start(target(mFocus), !mKeys, mKeys ? mSlot : 0);
}

bool ControlsPage::command(menu::Command c, bool down) {
    using menu::Command;
    if (!down) return false;
    if (mCapture.active()) return true;  // (the capture reads its own input: input.hpp's binding capture)
    if (mHelping) {
        if (c == Command::Help) {
            mHelping = false;
            play_sound(kSoundWindowClose);
        }
        return true;
    }
    switch (c) {
    case Command::Up: move(-1); return true;
    case Command::Down: move(1); return true;
    case Command::Left:
    case Command::Right:
        if (mKeys && mFocus < row_count()) {
            const int to = c == Command::Left ? 0 : 1;
            if (to != mSlot) {
                mSlot = to;
                play_sound(kSoundItemFocus);
            }
        }
        return true;
    case Command::PrevTab: set_tab(false); return true;
    case Command::NextTab: set_tab(true); return true;
    case Command::Confirm: press(); return true;
    case Command::Help:
        mHelping = true;
        play_sound(kSoundWindowOpen);
        return true;
    default: return false;
    }
}

bool ControlsPage::back() {
    if (mCapture.active()) {
        play_sound(kSoundWindowClose);
        mCapture.cancel();
        return true;
    }
    if (mHelping) {
        mHelping = false;
        play_sound(kSoundWindowClose);
        return true;
    }
    return false;
}

void ControlsPage::hidden() {
    mCapture.cancel();
    settings::save();
}

void ControlsPage::closing() {
    mCapture.cancel();
    settings::save();
}

std::vector<prompts::Prompt> ControlsPage::prompts(const std::string& backLabel) const {
    using prompts::Key;
    if (mCapture.active()) return {{Key::Back, "Cancel"}};
    if (mHelping) return {{Key::Back, "Close"}};
    std::vector<prompts::Prompt> out = {{Key::Move, "Select"}};
    if (mKeys && mFocus < row_count()) out.push_back({Key::Change, "First or second"});
    if (!locked()) out.push_back({Key::Confirm, mFocus == row_count() ? "Restore" : "Change"});
    out.push_back({Key::Help, "Help"});
    out.push_back({Key::Back, backLabel});
    return out;
}

std::string ControlsPage::strip() const {
    if (locked())
        return std::string("Set when the game started (") + key_of(mKeys) + "), so it can't be changed here.";
    if (mCapture.active()) {
        const int t = mCapture.target();
        const std::string what = mKeys ? kTargets[t].label : t == kKickTarget ? kKickLabel : kPadLabels[t];
        return mKeys ? "Press a key, mouse button or wheel for " + what + ". Esc cancels."
                     : "Press the button for " + what + ". Esc cancels.";
    }
    if (mFocus == row_count()) return "Puts all buttons and keys back to the defaults.";
    if (mKeys && kTargets[mFocus].about) return kTargets[mFocus].about;
    if (!mKeys && target(mFocus) == kKickTarget) return kKickAbout;
    if (!mKeys && kick_takes(target(mFocus)))
        return "The Kick has its button, so nothing presses it. Choose the row, then press another button for it.";
    return mKeys ? "Choose a row, then press a key, mouse button or wheel."
                 : "Choose a row, then press the button for it.";
}

std::string ControlsPage::classes() const {
    std::string out = "controls";
    if (mHelping) out += " helping";
    if (mCapture.active()) out += " capturing";
    return out;
}

void ControlsPage::show_keys(bool keys) {
    mKeys = keys;
    if (mKeys != mBuiltKeys) build();
}

void ControlsPage::focus(int row, int slot) {
    mFocus = std::clamp(row, 0, row_count());
    mSlot = mKeys ? std::clamp(slot, 0, kSlots - 1) : 0;
}

void ControlsPage::wait() {
    press();
}

void ControlsPage::open_help() {
    mHelping = true;
}

}  // namespace ui
