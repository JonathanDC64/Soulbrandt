// Written for Demon's Souls. Settings as a page of the F1 menu (settings_page.hpp). It replaces the settings window,
// keeping what its rows did: a row whose option was set in the environment when the game started can't change and
// says so, and Delete my data asks first.
#include "settings_page.hpp"

#include "button.hpp"
#include "component.hpp"
#include "controls_window.hpp"
#include "event.hpp"
#include "hud_editor.hpp"
#include "input.hpp"
#include "menu_shell.hpp"
#include "modal.hpp"
#include "players_window.hpp"
#include "privacy_window.hpp"
#include "settings.hpp"
#include "stick_preview.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace ui {
namespace {

using catalog::RowDesc;
using menu::RowFacts;
using menu::SettingsModel;

// ps3emu/ps3_net_signin.h (declared here: des_ui does not take ps3recomp's include paths). Its outcome comes back
// through ps3_net_erase_result, which net_notices.cpp shows.
extern "C" int ps3_net_player_erase(void);

// Delete my data (the server's docs/social.md), asked first.
void ask_erase() {
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Delete your data on the server?",
        .bodyText = "This deletes your name, your characters' online records, your messages and bloodstains, and the "
                    "rest the online server keeps about you (Privacy note). Online play turns off, so the game goes "
                    "back to its title screen. It cannot be undone.",
        .actions =
            {
                ModalAction{.label = "Cancel",
                    .onPressed =
                        [](Modal& modal) {
                            play_sound(kSoundWindowClose);
                            modal.pop();
                        }},
                ModalAction{.label = "Delete",
                    .onPressed =
                        [](Modal& modal) {
                            play_sound(kSoundClick);
                            modal.pop();
                            if (ps3_net_player_erase() != 0) {
                                push_toast({
                                    .type = "warning",
                                    .title = "Online",
                                    .content = "Not signed in to the online server, so nothing was deleted. Play "
                                               "online, then try again.",
                                    .duration = std::chrono::seconds(10),
                                });
                            }
                        }},
            },
        .onDismiss = [](Modal& modal) { modal.pop(); },
        .icon = "warning",
    }));
}

void play_button_sound(catalog::Sound sound) {
    switch (sound) {
    case catalog::Sound::Click: play_sound(kSoundClick); break;
    case catalog::Sound::Open: play_sound(kSoundWindowOpen); break;
    case catalog::Sound::None: break;
    }
}

// The kind of a row, as a class its element takes (a look can draw an action row's value as a chevron).
const char* kind_class(const RowDesc& row) {
    switch (row.kind.index()) {
    case 0: return "choice";
    case 1: return "toggle";
    case 2: return "number";
    case 3: return "text";
    case 4: return "info";
    default: return "action";
    }
}

// RML's text alone: the tags gone, a line break a space, the entities RML help uses read back.
std::string plain(std::string rml) {
    std::string out;
    out.reserve(rml.size());
    for (size_t i = 0; i < rml.size(); i++) {
        if (rml[i] == '<') {
            const size_t end = rml.find('>', i);
            if (end == std::string::npos) break;
            if (!out.empty() && out.back() != ' ') out += ' ';
            i = end;
            continue;
        }
        out += rml[i];
    }
    const std::pair<const char*, const char*> kEntities[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&nbsp;", " "}};
    for (const auto& [from, to] : kEntities)
        for (size_t at = out.find(from); at != std::string::npos; at = out.find(from, at + 1)) out.replace(at, std::strlen(from), to);
    return out;
}

// A row's help in one line: its first sentence.
std::string brief(const RowDesc& row) {
    const std::string text = plain(row.help());
    const size_t at = text.find(". ");
    return at == std::string::npos ? text : text.substr(0, at + 1);
}

RowFacts::Enter facts_enter(rows::Enter e) {
    switch (e) {
    case rows::Enter::List: return RowFacts::Enter::List;
    case rows::Enter::Edit: return RowFacts::Enter::Edit;
    case rows::Enter::Open: return RowFacts::Enter::Open;
    case rows::Enter::Press: return RowFacts::Enter::Press;
    case rows::Enter::None: break;
    }
    return RowFacts::Enter::None;
}

void set_class(Rml::Element* e, const char* name, bool on) {
    if (e && e->IsClassSet(name) != on) e->SetClass(name, on);
}

void set_px(Rml::Element* e, Rml::PropertyId id, float px) {
    e->SetProperty(id, Rml::Property(std::round(px), Rml::Unit::PX));
}

}  // namespace

SettingsPage::SettingsPage(Rml::Element* host, menu::PageHost& shell)
    : mHost(host), mShell(shell), mModel(static_cast<int>(catalog::tabs().size())) {
    for (const settings::Def& d : settings::defs()) mOpened[d.key] = settings::get(d.key);

    mTabs = append(mHost, "menu-tabs");
    Rml::Element* body = append(mHost, "menu-body");
    mRows = append(body, "menu-rows");
    mHelp = append(body, "menu-help");
    mPeek = append(mHost, "menu-peek");
    mPeekPrev = append(mPeek, "menu-peek-prev");
    Rml::Element* peekRow = append(mPeek, "menu-peek-row");
    mPeekName = append(peekRow, "menu-name");
    Rml::Element* peekValue = append(peekRow, "menu-value");
    mPeekLeft = append(peekValue, "menu-arrow");
    mPeekLeft->SetClass("left", true);
    mPeekValue = append(peekValue, "menu-text");
    mPeekTag = append(peekValue, "menu-tag");
    mPeekRight = append(peekValue, "menu-arrow");
    mPeekRight->SetClass("right", true);
    mPeekNext = append(mPeek, "menu-peek-next");
    mPeekLine = append(mPeek, "menu-peek-line");
    listen(mPeekPrev, Rml::EventId::Click, [this](Rml::Event&) { command(menu::Command::Up, true); });
    listen(mPeekNext, Rml::EventId::Click, [this](Rml::Event&) { command(menu::Command::Down, true); });
    listen(peekValue, Rml::EventId::Click, [this, peekValue](Rml::Event& e) {
        const float x = e.GetParameter<float>("mouse_x", 0.f);
        const float mid = peekValue->GetAbsoluteLeft() + peekValue->GetClientWidth() / 2;
        step(x < mid ? -1 : 1);
    });
    build_tab();
}

SettingsPage::~SettingsPage() {
    compare(false);
    mInputKeys.reset();
    mRowListeners.clear();
    mTabListeners.clear();
    mHelpBox.reset();
}

void SettingsPage::listen(Rml::Element* element, Rml::EventId event, std::function<void(Rml::Event&)> callback) {
    mTabListeners.push_back(std::make_unique<ScopedEventListener>(element, event, std::move(callback)));
}

std::vector<const RowDesc*> SettingsPage::visible_rows(int tab) const {
    std::vector<const RowDesc*> out;
    const std::vector<catalog::TabDesc>& tabs = catalog::tabs();
    if (tab < 0 || tab >= static_cast<int>(tabs.size())) return out;
    for (const catalog::SectionDesc& s : tabs[tab].sections)
        for (const RowDesc& r : s.rows)
            if (!r.when || r.when()) out.push_back(&r);
    return out;
}

void SettingsPage::build_tab() {
    stop_edit(false);
    compare(false);
    mRowListeners.clear();
    mViews.clear();
    mList = nullptr;
    mListRow = -1;
    mItemElems.clear();
    clear_children(mRows);
    mBuiltTab = mModel.tab();
    mScrolledFocus = -1;
    mHelpShown.clear();

    // The tab bar: the tabs, between the buttons that change them.
    if (mTabElems.empty()) {
        const std::vector<catalog::TabDesc>& tabs = catalog::tabs();
        append(mTabs, "menu-glyph")->SetClass("prev", true);
        for (size_t t = 0; t < tabs.size(); t++) {
            Rml::Element* e = append_text_element(mTabs, "menu-tab", tabs[t].title);
            mTabElems.push_back(e);
            listen(e, Rml::EventId::Click, [this, t](Rml::Event&) {
                if (mModel.set_tab(static_cast<int>(t))) play_sound(kSoundTabChanged);
            });
        }
        append(mTabs, "menu-glyph")->SetClass("next", true);
        mGlyphsDrawn = false;
    }

    const catalog::TabDesc& tab = catalog::tabs()[mBuiltTab];
    for (const catalog::SectionDesc& s : tab.sections) {
        bool heading = !s.title.empty() && (!s.when || s.when());
        for (const RowDesc& r : s.rows) {
            if (r.when && !r.when()) continue;
            if (heading) {
                append_text_element(mRows, "menu-heading", s.title);
                heading = false;
            }
            RowView v;
            v.desc = &r;
            v.row = append(mRows, "menu-row");
            v.row->SetClass(kind_class(r), true);
            Rml::Element* label = append(v.row, "menu-label");
            append(label, "menu-mark");
            append_text_element(label, "menu-name", r.label);
            v.value = append(v.row, "menu-value");
            v.left = append(v.value, "menu-arrow");
            v.left->SetClass("left", true);
            append(v.value, "icon")->SetClass("lock", true);
            v.text = append(v.value, "menu-text");
            v.tag = append(v.value, "menu-tag");
            v.right = append(v.value, "menu-arrow");
            v.right->SetClass("right", true);
            if (const auto* info = std::get_if<catalog::InfoRow>(&r.kind); info && !info->everyFrame) v.info = info->value();
            const int index = static_cast<int>(mViews.size());
            mRowListeners.push_back(std::make_unique<ScopedEventListener>(v.row, Rml::EventId::Click, [this, index](Rml::Event& e) {
                if (index != mModel.focus()) {
                    if (mModel.set_focus(index, mFacts)) {
                        play_sound(kSoundItemFocus);
                        moved();
                    }
                    return;
                }
                const RowView& view = mViews[index];
                Rml::Element* t = e.GetTargetElement();
                if (mFacts[index].steps && (t == view.left || t == view.right)) step(t == view.left ? -1 : 1);
                else if (mModel.mode() == SettingsModel::Mode::Rows) enter();
            }));
            mViews.push_back(std::move(v));
        }
    }
    refresh_facts();
    mModel.rows_changed(mFacts);
}

void SettingsPage::refresh_facts() {
    mFacts.resize(mViews.size());
    for (size_t i = 0; i < mViews.size(); i++) {
        const RowDesc& r = *mViews[i].desc;
        RowFacts& f = mFacts[i];
        f.steps = rows::steps(r);
        f.previewable = rows::previewable(r);
        f.comparable = rows::comparable(r);
        f.enter = facts_enter(rows::enter(r));
        const bool focused = static_cast<int>(i) == mModel.focus();
        f.enterLabel = focused ? rows::enter_label(r) : std::string();
        if (focused && f.enter == RowFacts::Enter::List) {
            const std::vector<rows::Item> items = rows::items(r);
            f.items = static_cast<int>(items.size());
            f.current = -1;
            for (size_t k = 0; k < items.size(); k++)
                if (items[k].current) f.current = static_cast<int>(k);
        } else {
            f.items = 0;
            f.current = -1;
        }
    }
}

const RowDesc* SettingsPage::focused_row() const {
    const int f = mModel.focus();
    return f >= 0 && f < static_cast<int>(mViews.size()) ? mViews[f].desc : nullptr;
}

void SettingsPage::update() {
    if (mModel.tab() != mBuiltTab) build_tab();
    else {
        // A row can come or go while the page shows (a disc's Language row): the tab is built again.
        const std::vector<const RowDesc*> now = visible_rows(mBuiltTab);
        bool same = now.size() == mViews.size();
        for (size_t i = 0; same && i < now.size(); i++) same = now[i] == mViews[i].desc;
        if (!same) build_tab();
    }
    const int before = mModel.focus();
    refresh_facts();
    mModel.rows_changed(mFacts);
    if (mModel.focus() != before) moved();

    if (mStopEdit) stop_edit(mCommitEdit);
    if (mInputFocusFrames > 0 && --mInputFocusFrames == 0 && mInput) {
        if (mInput->Focus(true)) {
            if (auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(mInput)) {
                const int end = static_cast<int>(Rml::StringUtilities::LengthUTF8(input->GetValue()));
                input->SetSelectionRange(0, end);
            }
        }
    }

    update_tabs();
    update_rows();
    update_list();
    update_help();
    update_peek();
    scroll_to_focus();
    place_list();
}

void SettingsPage::update_tabs() {
    const prompts::Device device = input::menu_device();
    if (!mGlyphsDrawn || device != mGlyphDevice) {
        mGlyphDevice = device;
        mGlyphsDrawn = true;
        if (Rml::Element* prev = mTabs->GetFirstChild()) prev->SetInnerRML(menu::glyph_rml(device, prompts::Key::PrevTab));
        if (Rml::Element* next = mTabs->GetLastChild()) next->SetInnerRML(menu::glyph_rml(device, prompts::Key::NextTab));
    }
    for (size_t t = 0; t < mTabElems.size(); t++) set_class(mTabElems[t], "on", static_cast<int>(t) == mModel.tab());
}

void SettingsPage::update_rows() {
    const int focus = mModel.focus();
    const SettingsModel::Mode mode = mModel.mode();
    for (size_t i = 0; i < mViews.size(); i++) {
        RowView& v = mViews[i];
        const RowDesc& r = *v.desc;
        rows::Value value;
        if (std::holds_alternative<catalog::InfoRow>(r.kind) && !std::get<catalog::InfoRow>(r.kind).everyFrame)
            value.text = v.info;
        else
            value = rows::value(r);
        const bool focused = static_cast<int>(i) == focus;
        set_class(v.row, "focused", focused);
        set_class(v.row, "locked", rows::locked(r));
        set_class(v.row, "waits", rows::waits(r));
        set_class(v.row, "modified", rows::modified(r));
        set_class(v.row, "steps", mFacts[i].steps);
        set_class(v.row, "listing", focused && mode == SettingsModel::Mode::List);
        set_class(v.row, "typing", focused && mode == SettingsModel::Mode::Edit);
        set_class(v.row, "comparing", focused && !mComparing.empty());
        set_class(v.left, "end", value.first);
        set_class(v.right, "end", value.last);
        set_text_content(v.text, value.text);
        set_text_content(v.tag, value.original ? "(original)" : "");
    }
}

void SettingsPage::update_list() {
    const bool listing = mModel.mode() == SettingsModel::Mode::List;
    const int focus = mModel.focus();
    if (mList && (!listing || mListRow != focus)) {
        mList->GetParentNode()->RemoveChild(mList);
        mList = nullptr;
        mItemElems.clear();
        mListRow = -1;
    }
    const RowDesc* row = focused_row();
    if (!listing || !row) return;
    std::vector<rows::Item> items = rows::items(*row);
    bool same = mList && items.size() == mItems.size();
    for (size_t k = 0; same && k < items.size(); k++)
        same = items[k].label == mItems[k].label && items[k].current == mItems[k].current &&
               items[k].disabled == mItems[k].disabled;
    if (!same) {
        if (mList) mList->GetParentNode()->RemoveChild(mList);
        mItemElems.clear();
        mItems = std::move(items);
        mListRow = focus;
        mList = append(mViews[focus].row, "menu-list");
        for (size_t k = 0; k < mItems.size(); k++) {
            Rml::Element* e = append(mList, "menu-item");
            append_text_element(e, "menu-text", mItems[k].label);
            set_text_content(append(e, "menu-tag"), mItems[k].original ? "(original)" : mItems[k].current ? "now" : "");
            set_class(e, "now", mItems[k].current);
            set_class(e, "disabled", mItems[k].disabled);
            mRowListeners.push_back(std::make_unique<ScopedEventListener>(e, Rml::EventId::Click, [this, k](Rml::Event& ev) {
                ev.StopPropagation();
                mModel.set_cursor(static_cast<int>(k), mFacts);
                pick();
            }));
            mItemElems.push_back(e);
        }
    }
    for (size_t k = 0; k < mItemElems.size(); k++) set_class(mItemElems[k], "cursor", static_cast<int>(k) == mModel.cursor());
}

void SettingsPage::update_help() {
    const RowDesc* row = focused_row();
    if (!row) {
        if (mHelpBox) {
            mHelpBox.reset();
            clear_children(mHelp);
            mHelpShown.clear();
        }
        return;
    }
    // What the help says now: rebuilt only when that changes.
    const bool changedSinceOpen = rows::comparable(*row) && mOpened.count(row->key) &&
                                  mOpened[row->key] != settings::get(row->key);
    const std::string compareKey(prompts::glyph(input::menu_device(), prompts::Key::Compare).text);
    std::string notes;
    if (row->notes)
        for (const catalog::Note& n : row->notes())
            notes += std::string("<status-badge class=\"") + (n.kind == catalog::Note::Kind::Warning ? "warning" : "info") +
                     "\">" + n.text + "</status-badge>";
    if (const std::string lock = rows::lock_note(*row); !lock.empty())
        notes += "<status-badge class=\"warning\">" + escape(lock) + "</status-badge>";
    else if (rows::waits(*row))
        notes += "<status-badge class=\"waits\">Takes effect when the game starts again. Closing the menu asks whether "
                 "to restart now.</status-badge>";
    else if (rows::applies(*row) == "After a restart")
        notes += "<status-badge class=\"info\">Takes effect the next time you start the game.</status-badge>";
    else if (changedSinceOpen && mComparing.empty())
        notes += "<status-badge class=\"info\">Changed since the menu opened. Hold " + escape(compareKey) +
                 " to see how it was.</status-badge>";
    if (const auto* c = std::get_if<catalog::ChoiceRow>(&row->kind); c && c->liveNote && c->liveNote->visible())
        notes += std::string("<status-badge class=\"") +
                 (c->liveNote->note.kind == catalog::Note::Kind::Warning ? "warning" : "info") + "\">" +
                 c->liveNote->note.text + "</status-badge>";
    std::string facts;
    if (const std::string when = rows::applies(*row); !when.empty())
        facts += "<menu-fact><menu-fact-name>Applies</menu-fact-name><menu-fact-value>" + escape(when) +
                 "</menu-fact-value></menu-fact>";
    if (!row->key.empty() && !(row->flags & catalog::kHideKey))
        facts += "<menu-fact><menu-fact-name>Variable</menu-fact-name><menu-fact-value class=\"key\">" +
                 escape(row->key) + "</menu-fact-value></menu-fact>";
    const std::string head = "<h3>" + escape(row->label) + "</h3>" +
                             (notes.empty() ? std::string() : "<menu-notes>" + notes + "</menu-notes>") +
                             "<menu-helptext>" + row->help() + "</menu-helptext>";
    const std::string foot = row->footnote();
    const std::string shown = std::to_string(mModel.focus()) + '\x1f' + head + '\x1f' + foot + '\x1f' + facts;
    if (shown != mHelpShown) {
        mHelpShown = shown;
        mHelpBox.reset();
        clear_children(mHelp);
        mHelpBox = std::make_unique<Component>(append(mHelp, "menu-helpbox"));
        mHelpBox->add_rml(head);
        if (!foot.empty()) mHelpBox->add_rml("<menu-footnote>" + foot + "</menu-footnote>");
        if (row->buttons) {
            for (catalog::Button& b : row->buttons()) {
                auto& button = mHelpBox->add_child<ControlledButton>(ControlledButton::Props{.text = b.text, .isDisabled = b.disabled});
                button.on_pressed([sound = b.sound, press = b.press] {
                    play_button_sound(sound);
                    if (press) press();
                });
            }
        }
        if (const auto* n = std::get_if<catalog::NumberRow>(&row->kind); n && n->stick >= 0)
            mHelpBox->add_child<StickPreview>(StickPreview::Props{.stick = n->stick, .zone = n->get});
        if (!facts.empty()) mHelpBox->add_rml("<menu-facts>" + facts + "</menu-facts>");
    }
    mHelpBox->update();
}

void SettingsPage::update_peek() {
    if (mModel.mode() != SettingsModel::Mode::Preview) return;
    const RowDesc* row = focused_row();
    if (!row) return;
    const int f = mModel.focus();
    const auto label_of = [&](int dir) -> std::string {
        for (int i = f + dir; i >= 0 && i < static_cast<int>(mViews.size()); i += dir)
            if (mFacts[i].previewable) return mViews[i].desc->label;
        return "";
    };
    const std::string prev = label_of(-1), next = label_of(1);
    set_text_content(mPeekPrev, prev);
    set_text_content(mPeekNext, next);
    set_class(mPeekPrev, "none", prev.empty());
    set_class(mPeekNext, "none", next.empty());
    set_text_content(mPeekName, row->label);
    const rows::Value v = rows::value(*row);
    set_text_content(mPeekValue, v.text);
    set_text_content(mPeekTag, v.original ? "(original)" : "");
    set_class(mPeekLeft, "end", v.first);
    set_class(mPeekRight, "end", v.last);
    set_text_content(mPeekLine, brief(*row));
}

// A list that scrolls moves by whole rows: the top lands on a heading or a row, never through one.
void SettingsPage::scroll_to_focus() {
    const int f = mModel.focus();
    if (f == mScrolledFocus || f < 0 || f >= static_cast<int>(mViews.size())) return;
    const float view = mRows->GetClientHeight();
    if (view <= 0.f) return;  // (not laid out yet)
    mScrolledFocus = f;
    Rml::Element* row = mViews[f].row;
    const float top = row->GetOffsetTop(), bottom = top + row->GetOffsetHeight();
    const float edge = row->GetOffsetHeight() * 0.6f;
    float scroll = mRows->GetScrollTop();
    std::vector<float> stops;
    for (int i = 0; i < mRows->GetNumChildren(); i++) {
        Rml::Element* c = mRows->GetChild(i);
        if (c->GetTagName() == "menu-row" || c->GetTagName() == "menu-heading") stops.push_back(c->GetOffsetTop());
    }
    if (top < scroll + edge) {
        float to = 0.f;
        for (const float s : stops)
            if (s <= top - edge) to = s;
        // (the heading just above the row comes with it)
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

// A list stays inside the rows: one that would run past their bottom opens above its row, or moves up beside its
// value, and scrolls when it still doesn't fit.
void SettingsPage::place_list() {
    if (!mList || mListRow < 0) return;
    Rml::Element* row = mViews[mListRow].row;
    const float view = mRows->GetClientHeight();
    const float h = mList->GetOffsetHeight();
    if (view <= 0.f || h <= 0.f) return;
    const float rowTop = row->GetOffsetTop() - mRows->GetScrollTop();
    const float rowH = row->GetOffsetHeight();
    float top = rowH + 3.f;
    if (rowTop + top + h > view - 2.f) {
        if (rowTop - h - 3.f >= 2.f) top = -h - 3.f;
        else top = std::max(view - 2.f - h - rowTop, 2.f - rowTop);
    }
    set_px(mList, Rml::PropertyId::Top, top);
    if (h > view - 4.f) {
        set_px(mList, Rml::PropertyId::MaxHeight, view - 4.f);
        mList->SetProperty(Rml::PropertyId::OverflowY, Rml::Property(Rml::Style::Overflow::Auto));
    }
}

void SettingsPage::moved() {
    compare(false);
    mScrolledFocus = -1;
}

void SettingsPage::step(int dir) {
    const RowDesc* row = focused_row();
    if (!row) return;
    compare(false);
    if (!rows::steps(*row)) {
        if (rows::locked(*row)) play_sound(kSoundItemDisable);  // (refused: set from outside)
        return;
    }
    if (rows::step(*row, dir)) play_sound(kSoundItemChange);
}

void SettingsPage::enter() {
    const RowDesc* row = focused_row();
    if (!row) return;
    compare(false);
    switch (mModel.enter(mFacts)) {
    case SettingsModel::Action::List: play_sound(kSoundClick); break;
    case SettingsModel::Action::Edit: start_edit(); break;
    case SettingsModel::Action::Open: open(*row); break;
    case SettingsModel::Action::Press:
        if (const std::vector<rows::Item> items = rows::items(*row); !items.empty() && !items.front().disabled) {
            play_button_sound(items.front().sound);
            rows::pick(*row, 0);
        } else {
            play_sound(kSoundItemDisable);
        }
        break;
    case SettingsModel::Action::None:
        if (rows::locked(*row)) play_sound(kSoundItemDisable);
        break;
    }
}

void SettingsPage::pick() {
    const RowDesc* row = focused_row();
    const int k = mModel.list_pick();
    if (!row || k < 0 || k >= static_cast<int>(mItems.size())) return;
    const rows::Item item = mItems[k];
    if (item.button) {
        if (item.disabled) {
            play_sound(kSoundItemDisable);
            return;
        }
        play_button_sound(item.sound);
        rows::pick(*row, k);
        return;
    }
    play_sound(rows::pick(*row, k) ? kSoundItemChange : kSoundClick);
}

void SettingsPage::open(const RowDesc& row) {
    using Opens = catalog::ActionRow::Opens;
    const auto* action = std::get_if<catalog::ActionRow>(&row.kind);
    if (!action) return;
    switch (action->opens) {
    case Opens::HudEditor:
        play_sound(kSoundClick);
        mShell.push(std::make_unique<HudEditor>());
        break;
    case Opens::Controls:
        play_sound(kSoundClick);
        mShell.push(std::make_unique<ControlsWindow>());
        break;
    case Opens::Players:
        play_sound(kSoundClick);
        mShell.push(std::make_unique<PlayersWindow>());
        break;
    case Opens::Privacy:
        play_sound(kSoundWindowOpen);
        mShell.push(std::make_unique<PrivacyWindow>(PrivacyWindow::Mode::Read));
        break;
    case Opens::DeleteData:
        play_sound(kSoundWindowOpen);
        ask_erase();
        break;
    }
}

void SettingsPage::start_edit() {
    const RowDesc* row = focused_row();
    if (!row || mInput) return;
    RowView& v = mViews[mModel.focus()];
    Rml::ElementDocument* doc = mHost->GetOwnerDocument();
    Rml::ElementPtr element = doc->CreateElement("input");
    element->SetAttribute("type", std::holds_alternative<catalog::NumberRow>(row->kind) ? "number" : "text");
    element->SetAttribute("value", rows::edit_text(*row));
    if (const int limit = rows::edit_limit(*row); limit > 0) element->SetAttribute("maxlength", limit);
    mInput = v.value->AppendChild(std::move(element));
    mInputFocusFrames = 2;  // (RmlUi lays the field out as it draws: focused once it has a place)
    mStopEdit = false;
    mInputKeys = std::make_unique<ScopedEventListener>(mInput, Rml::EventId::Keydown, [this](Rml::Event& e) {
        const auto key = static_cast<Rml::Input::KeyIdentifier>(e.GetParameter<int>("key_identifier", 0));
        if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER || key == Rml::Input::KI_ESCAPE) {
            mStopEdit = true;
            mCommitEdit = key != Rml::Input::KI_ESCAPE;
            e.StopImmediatePropagation();
        }
    });
    play_sound(kSoundClick);
}

void SettingsPage::stop_edit(bool commit) {
    mStopEdit = false;
    mInputFocusFrames = 0;
    if (!mInput) return;
    const RowDesc* row = focused_row();
    std::string typed;
    if (auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(mInput)) typed = input->GetValue();
    mInputKeys.reset();
    if (Rml::Element* parent = mInput->GetParentNode()) parent->RemoveChild(mInput);
    mInput = nullptr;
    mModel.edit_done();
    if (commit && row) play_sound(rows::commit_edit(*row, typed) ? kSoundItemChange : kSoundClick);
    else play_sound(kSoundWindowClose);
    if (Rml::ElementDocument* doc = mHost->GetOwnerDocument()) doc->Focus();
}

void SettingsPage::compare(bool on) {
    if (!on) {
        if (mComparing.empty()) return;
        settings::end_try(mComparing);
        mComparing.clear();
        if (const RowDesc* row = focused_row()) mShell.badge("Now: " + rows::value(*row).text, "now");
        else mShell.badge("", "");
        return;
    }
    const RowDesc* row = focused_row();
    if (!mComparing.empty() || !row || !rows::comparable(*row)) return;
    if (mModel.mode() == SettingsModel::Mode::List || mModel.mode() == SettingsModel::Mode::Edit) return;
    const auto it = mOpened.find(row->key);
    if (it == mOpened.end()) return;
    const bool same = it->second == settings::get(row->key);
    if (!same && !settings::try_value(row->key, it->second)) return;
    mComparing = row->key;
    mShell.badge("Before: " + rows::value(*row).text + (same ? " (unchanged)" : ""), "before");
}

bool SettingsPage::command(menu::Command c, bool down) {
    using menu::Command;
    if (!down) {
        if (c != Command::Compare) return false;
        compare(false);
        return true;
    }
    const SettingsModel::Mode mode = mModel.mode();
    if (mode == SettingsModel::Mode::Edit) return false;  // (the field takes the keys; Enter and Esc end it)
    switch (c) {
    case Command::Up:
    case Command::Down: {
        const int dir = c == Command::Up ? -1 : 1;
        if (mode == SettingsModel::Mode::List) {
            if (mModel.list_move(dir, mFacts)) play_sound(kSoundItemFocus);
        } else if (mModel.move(dir, mFacts)) {
            play_sound(kSoundItemFocus);
            moved();
        }
        return true;
    }
    case Command::Left:
        if (mode == SettingsModel::Mode::List) back();
        else step(-1);
        return true;
    case Command::Right:
        if (mode != SettingsModel::Mode::List) step(1);
        return true;
    case Command::Confirm:
        if (mode == SettingsModel::Mode::List) pick();
        else if (mode == SettingsModel::Mode::Rows) enter();
        return true;
    case Command::PrevTab:
    case Command::NextTab:
        if (mModel.step_tab(c == Command::PrevTab ? -1 : 1)) {
            compare(false);
            play_sound(kSoundTabChanged);
        }
        return true;
    case Command::Preview:
        if (mode == SettingsModel::Mode::Preview) back();
        else if (mModel.preview(mFacts)) play_sound(kSoundWindowOpen);
        else play_sound(kSoundItemDisable);
        return true;
    case Command::Compare:
        compare(true);
        return true;
    default:
        return false;
    }
}

bool SettingsPage::back() {
    if (mInput) {
        stop_edit(false);
        return true;
    }
    const SettingsModel::Mode mode = mModel.mode();
    if (!mModel.back()) return false;
    compare(false);
    play_sound(mode == SettingsModel::Mode::Preview ? kSoundWindowClose : kSoundClick);
    return true;
}

void SettingsPage::release() {
    compare(false);
}

void SettingsPage::hidden() {
    settings::save();
}

std::vector<prompts::Prompt> SettingsPage::prompts(const std::string& backLabel) const {
    return mModel.prompts(mFacts, backLabel);
}

std::string SettingsPage::classes() const {
    std::string out;
    switch (mModel.mode()) {
    case SettingsModel::Mode::List: out = "listing"; break;
    case SettingsModel::Mode::Edit: out = "typing"; break;
    case SettingsModel::Mode::Preview: out = "preview"; break;
    case SettingsModel::Mode::Rows: break;
    }
    if (!mComparing.empty()) out += out.empty() ? "comparing" : " comparing";
    return out;
}

void SettingsPage::closing() {
    stop_edit(true);
    compare(false);
    settings::save();
}

bool SettingsPage::show_tab(const std::string& title) {
    const std::vector<catalog::TabDesc>& tabs = catalog::tabs();
    for (size_t t = 0; t < tabs.size(); t++)
        if (tabs[t].title == title) {
            mModel.set_tab(static_cast<int>(t));
            if (mModel.tab() != mBuiltTab) build_tab();
            return true;
        }
    return false;
}

bool SettingsPage::focus_row(const std::string& label) {
    for (size_t i = 0; i < mViews.size(); i++)
        if (mViews[i].desc->label == label) {
            mModel.set_focus(static_cast<int>(i), mFacts);
            refresh_facts();  // (what Enter lists is read for the row with the focus)
            mModel.rows_changed(mFacts);
            return mModel.focus() == static_cast<int>(i);
        }
    return false;
}

}  // namespace ui
