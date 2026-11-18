// Written for Demon's Souls. The F1 menu as one document (menu_shell.hpp).
#include "menu_shell.hpp"

#include "button_art.hpp"
#include "host.hpp"
#include "input.hpp"
#include "looks.hpp"
#include "menu_pages.hpp"
#include "question.hpp"
#include "settings.hpp"
#include "settings_page.hpp"

#include <RmlUi/Core.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace ui {
namespace {

const Rml::String kDocumentSource = R"RML(
<rml>
<head>
    <link type="text/rcss" href="res/rml/theme.rcss" />
    <link type="text/rcss" href="res/rml/controls.rcss" />
    <link type="text/rcss" href="res/rml/menu.rcss" />
</head>
<body>
    <menu-entries id="entries" />
    <menu-frame id="frame">
        <menu-slot id="slot"><menu-title>Menu</menu-title></menu-slot>
        <menu-strip id="strip" />
        <menu-page id="page" />
        <menu-prompts id="prompts" />
    </menu-frame>
    <menu-badge id="badge" />
</body>
</rml>
)RML";

// The menu takes as long to leave as its sheet's transitions (menu.rcss): the document hides after them.
constexpr auto kLeave = std::chrono::milliseconds(200);
// "Now: ..." stays this long after Compare is let go.
constexpr auto kNowShown = std::chrono::milliseconds(900);

// A prompt's button, for a click on it: what pressing it does.
menu::Command command_of(prompts::Key key) {
    using prompts::Key;
    switch (key) {
    case Key::Confirm: return menu::Command::Confirm;
    case Key::Back: return menu::Command::Back;
    case Key::Preview: return menu::Command::Preview;
    case Key::Help: return menu::Command::Help;
    case Key::PrevTab: return menu::Command::PrevTab;
    case Key::NextTab: return menu::Command::NextTab;
    default: return menu::Command::None;
    }
}

}  // namespace

namespace menu {

std::string glyph_rml(prompts::Device device, prompts::Key key) {
    const prompts::Glyph g = prompts::glyph(device, key);
    std::string art;
    for (const prompts::Art& a : g.art) art += button_art::rml(device, a);
    return "<menu-glyph class=\"" + std::string(g.classes) + "\">" + art + "<menu-key>" + escape(g.text) +
           "</menu-key></menu-glyph>";
}

}  // namespace menu

MenuShell::MenuShell() : MenuShell(false, DocumentScope::MenuBar) {}

MenuShell::MenuShell(Standalone alone) : MenuShell(true, DocumentScope::None) {
    mAlone = std::move(alone.entry);
}

MenuShell::MenuShell(bool standalone, DocumentScope scope)
    : Document(kDocumentSource, false, scope), mModel(standalone) {
    mBody = mDocument;
    mEntries = mDocument->GetElementById("entries");
    mFrame = mDocument->GetElementById("frame");
    mSlot = mDocument->GetElementById("slot");
    mStrip = mDocument->GetElementById("strip");
    mPageHost = mDocument->GetElementById("page");
    mPrompts = mDocument->GetElementById("prompts");
    mBadge = mDocument->GetElementById("badge");

    const std::vector<menu::Entry>& entries = menu::entries();
    // The ends of the entries (a look's: Alpha UI shows none): the buttons that move along them, dim at the first or
    // last; a click moves one way.
    const auto end = [this](int side) {
        Rml::Element* e = append(mEntries, "menu-end");
        e->SetClass(side ? "right" : "left", true);
        listen(e, Rml::EventId::Click, [this, side](Rml::Event&) {
            if (mModel.level() == menu::Model::Level::Entries) command(side ? menu::Command::Right : menu::Command::Left, true);
        });
        return e;
    };
    mEnds[0] = end(0);
    for (size_t i = 0; i < entries.size(); i++) {
        Rml::Element* e = append_text_element(mEntries, "menu-entry", entries[i].title);
        mEntryElems.push_back(e);
        listen(e, Rml::EventId::Click, [this, i](Rml::Event&) {
            if (mModel.level() == menu::Model::Level::Entries) choose_entry(static_cast<int>(i));
        });
        // The pointer over an entry picks it, as Left and Right do (its line shows under the band); only a real move
        // of the pointer (input.hpp's hover_moved), so a key press is never undone by a pointer resting there.
        listen(e, Rml::EventId::Mousemove, [this, i](Rml::Event&) {
            if (mModel.level() != menu::Model::Level::Entries || !input::hover_moved() || static_cast<int>(i) == mEntry) return;
            mEntry = static_cast<int>(i);
            play_sound(kSoundItemFocus);
        });
        Rml::Element* s = append_text_element(mSlot, "menu-slot-entry", entries[i].title);
        mSlotElems.push_back(s);
        listen(s, Rml::EventId::Click, [this, i](Rml::Event&) { switch_entry(static_cast<int>(i)); });
    }
    mEnds[1] = end(1);
    Rml::Element* close = append(mEntries, "menu-close");
    listen(close, Rml::EventId::Click, [this](Rml::Event&) { close_menu(); });
    // A page's frame closes the menu too, as the menu bar's close button did (a look that draws it: Alpha UI).
    Rml::Element* slotClose = append(mSlot, "menu-slot-close");
    listen(slotClose, Rml::EventId::Click, [this](Rml::Event&) {
        if (mModel.level() == menu::Model::Level::Page) close_menu();
    });

    listen(Rml::EventId::Keydown, [this](Rml::Event& e) { on_key(e, true); });
    listen(Rml::EventId::Keyup, [this](Rml::Event& e) { on_key(e, false); });
    // A right click goes back, as Esc does (and as in the game's own menus): an open list closes, a page goes back.
    listen(Rml::EventId::Mousedown, [this](Rml::Event& e) {
        if (e.GetParameter<int>("button", 0) != 1 || !visible() || mModel.level() == menu::Model::Level::Closed) return;
        command(menu::Command::Back, true);
        e.StopPropagation();
    });
    listen(mPrompts, Rml::EventId::Click, [this](Rml::Event& e) {
        for (Rml::Element* t = e.GetTargetElement(); t && t != mPrompts; t = t->GetParentNode())
            if (t->GetTagName() == "menu-prompt") {
                const int key = t->GetAttribute<int>("data-key", -1);
                const menu::Command c = command_of(static_cast<prompts::Key>(key));
                if (c != menu::Command::None) command(c, true);
                return;
            }
    });
}

MenuShell::~MenuShell() {
    if (mPage) mPage->closing();
    mPage.reset();
    while (!mUnder.empty()) {
        mUnder.back().page->closing();
        mUnder.pop_back();
    }
}

bool MenuShell::handle_nav_command(Rml::Event&, NavCommand) {
    return false;  // (the shell reads its keys itself: on_key)
}

void MenuShell::show() {
    if (mModel.level() == menu::Model::Level::Closed) {
        mModel.open();
        if (mModel.standalone()) {
            const std::vector<menu::Entry>& entries = menu::entries();
            int alone = -1;
            for (size_t i = 0; i < entries.size(); i++)
                if (entries[i].title == mAlone) alone = static_cast<int>(i);
            if (alone >= 0) choose_entry(alone);
            else open_settings();
        }
    }
    mClosing = false;
    mCovered = false;
    mReleases = input::release_count();
    Document::show();
    focus();
    draw();
}

void MenuShell::cover() {
    mCovering = true;
    mCovered = Document::visible();
    Document::cover();
    mCovering = false;
}

void MenuShell::hide(bool close) {
    hiding(false);
    if (mPage) {
        mPage->release();
        mPage->hidden();
    }
    mHolds.release_all();
    if (!mCovering && mModel.level() != menu::Model::Level::Closed) {
        // Hidden by the game, not by Back or a document over it (ui::close_all_documents): it opens on its entries
        // next time, and nothing is asked.
        close_page();
        mModel.closed();
    }
    if (close) mPendingClose = true;
    if (!mCovering) mCovered = false;
    // Covered by another document (Controls, Trophies) or closed while the game runs: the document goes now. Closing
    // with the sheet's motion on, it goes once its transitions are done (update).
    if (mModel.level() == menu::Model::Level::Closed && host::animations_enabled() && !close) {
        mClosing = true;
        mCloseAt = clock::now() + kLeave;
        draw();
        return;
    }
    Document::hide(close);
}

bool MenuShell::visible() const {
    return Document::visible() && !mClosing;
}

// Open (closing included, and while a document it opened covers it): the width its look lays it out for.
float MenuShell::room() const {
    return Document::visible() || mCovered ? mRoom : 0.f;
}

bool MenuShell::focus() {
    if (mDocument == nullptr) return false;
    mDocument->Focus();
    return true;
}

void MenuShell::update() {
    if (mClosing) {
        if (clock::now() >= mCloseAt) {
            mClosing = false;
            Document::hide(mPendingClose);
        }
        return;
    }
    if (!Document::visible()) return;
    if (const unsigned r = input::release_count(); r != mReleases) {
        // Keys held as the window lost the focus may never come up: let go of what they held.
        mReleases = r;
        mHolds.release_all();
        hiding(false);
        if (mPage) mPage->release();
    }
    update_safe_area();
    update_column();
    if (mPage && mModel.level() == menu::Model::Level::Page) mPage->update();
    if (mBadgeUntil != clock::time_point{} && clock::now() >= mBadgeUntil) badge("", "");
    draw();
    Document::update();
}

void MenuShell::draw() {
    using Level = menu::Model::Level;
    const Level level = mModel.level();
    std::string classes = level == Level::Entries ? "entries" : level == Level::Page ? "page" : "closed";
    if (mModel.standalone()) classes += " standalone";
    if (mHiding) classes += " hiding";
    if (mClosing) classes += " closing";
    if (asking()) classes += " asking";
    if (host::animations_enabled()) classes += " motion";
    if (mPage && level == Level::Page)
        if (const std::string own = mPage->classes(); !own.empty()) classes += " " + own;
    if (!mUnder.empty() && level == Level::Page) classes += " over";
    if (classes != mClassesShown) {
        mClassesShown = classes;
        mBody->SetClassNames(classes);
    }
    update_frame_height((" " + classes + " ").find(" preview ") != std::string::npos);

    for (size_t i = 0; i < mEntryElems.size(); i++) {
        const bool on = static_cast<int>(i) == mEntry;
        if (mEntryElems[i]->IsClassSet("on") != on) mEntryElems[i]->SetClass("on", on);
        if (mSlotElems[i]->IsClassSet("on") != on) mSlotElems[i]->SetClass("on", on);
    }
    const bool first = mEntry <= 0, last = mEntry + 1 >= static_cast<int>(mEntryElems.size());
    if (mEnds[0]->IsClassSet("end") != first) mEnds[0]->SetClass("end", first);
    if (mEnds[1]->IsClassSet("end") != last) mEnds[1]->SetClass("end", last);

    // A page over another names itself in the frame (its entry's name is the page under it).
    if (const std::string title = !mUnder.empty() && mPage ? mPage->title() : "Menu"; title != mTitleShown) {
        mTitleShown = title;
        if (Rml::Element* t = mSlot->GetFirstChild()) set_text_content(t, title);
    }

    std::string strip;
    if (level == Level::Entries && mEntry >= 0 && mEntry < static_cast<int>(menu::entries().size()))
        strip = menu::entries()[mEntry].brief;
    else if (level == Level::Page && mPage)
        strip = mPage->strip();
    set_text_content(mStrip, strip);

    // The prompts for what the keys do now, drawn again only when they change.
    std::vector<prompts::Prompt> list;
    if (level == Level::Entries)
        list = {{prompts::Key::Change, "Select"}, {prompts::Key::Confirm, "Open"}, {prompts::Key::Back, "Close"}};
    else if (level == Level::Page && mPage)
        list = mPage->prompts(mModel.standalone() ? "Close" : "Back");
    const prompts::Device device = input::menu_device();
    if (const std::string ends = menu::glyph_rml(device, prompts::Key::PrevTab) +
                                 menu::glyph_rml(device, prompts::Key::NextTab);
        ends != mEndsShown) {
        mEndsShown = ends;
        mEnds[0]->SetInnerRML(menu::glyph_rml(device, prompts::Key::PrevTab));
        mEnds[1]->SetInnerRML(menu::glyph_rml(device, prompts::Key::NextTab));
    }
    std::string rml;
    // (a prompt without words shares the next one's: L1 R1 Scroll; it is .joined, for the look to set them close. Its
    // kind is a class too: .tabs)
    for (const prompts::Prompt& p : list) {
        std::string classes = p.label.empty() ? "joined" : "";
        if (!p.kind.empty()) classes += (classes.empty() ? "" : " ") + std::string(p.kind);
        rml += "<menu-prompt data-key=\"" + std::to_string(static_cast<int>(p.key)) + "\"" +
               (classes.empty() ? "" : " class=\"" + classes + "\"") + ">" + menu::glyph_rml(device, p.key) +
               "<menu-says>" + escape(p.label) + "</menu-says></menu-prompt>";
    }
    if (rml != mPromptsShown) {
        mPromptsShown = rml;
        mPrompts->SetInnerRML(rml);
    }
}

// The entries bar spans the window: kept inside the screen's safe area (a TV's, the Deck's), as the menu bar was.
// The frame's height, Interface > Menu height (ui_menu_height): from the 640 dp it has in a 720p window (0) to all
// the height the body leaves it (100). RmlUi has no calc(), so it is worked out here, and set again only when it
// changes. Preview's strip, and a look whose frame is sized otherwise (looks.hpp's menuHeight), keep the sheet's.
void MenuShell::update_frame_height(bool preview) {
    float px = -1.f;
    if (!preview && mFrame && looks::current().menuHeight) {
        const float base = 640.f * mDocument->GetContext()->GetDensityIndependentPixelRatio();
        const float room = mBody->GetBox().GetSize(Rml::BoxArea::Content).y;
        const int share = std::clamp(settings::get_int("ui_menu_height"), 0, 100);
        px = room <= base ? base : std::round(base + (room - base) * static_cast<float>(share) / 100.f);
    }
    if (px == mFrameMax || !mFrame) return;
    mFrameMax = px;
    if (px < 0.f)
        mFrame->RemoveProperty("max-height");
    else
        mFrame->SetProperty("max-height", std::to_string(static_cast<int>(px)) + "px");
}

void MenuShell::update_safe_area() {
    Rml::Context* context = mDocument->GetContext();
    if (!context) return;
    const Insets safe = safe_area_insets(context);
    const Insets sides{0.0f, std::round(safe.right), 0.0f, std::round(safe.left)};
    if (sides == mSafeArea) return;
    mSafeArea = sides;
    mEntries->SetProperty(Rml::PropertyId::PaddingRight, Rml::Property(sides.right, Rml::Unit::PX));
    mEntries->SetProperty(Rml::PropertyId::PaddingLeft, Rml::Property(sides.left, Rml::Unit::PX));
}

// A look that keeps its contents in a centred column names the column's width (its sheet's --menu-column on the body,
// in dp or px); the room left each side of it in the window is --menu-side, for the sheet to pad its bands with. RmlUi
// has no calc(), so it is worked out here, and set again only when it changes.
void MenuShell::update_column() {
    float side = -1.f;
    if (const Rml::Property* column = mBody->GetProperty("--menu-column")) {
        const std::string text = column->ToString();
        char* end = nullptr;
        const float n = std::strtof(text.c_str(), &end);
        if (end != text.c_str()) {
            const float ratio = std::string_view(end).starts_with("dp")
                                    ? mDocument->GetContext()->GetDensityIndependentPixelRatio()
                                    : 1.f;
            const float width = static_cast<float>(mDocument->GetContext()->GetDimensions().x);  // (the bands span it)
            side = std::max(0.f, std::round((width - n * ratio) / 2.f));
        }
    }
    // The width the look lays the menu out for, in dp (its sheet's --menu-room; room()).
    mRoom = dp_property(mBody, "--menu-room");
    if (side == mSide) return;
    mSide = side;
    if (side < 0.f)
        mBody->RemoveProperty("--menu-side");
    else
        mBody->SetProperty("--menu-side", std::to_string(static_cast<int>(side)) + "px");
}

void MenuShell::on_key(Rml::Event& event, bool down) {
    if (!visible() || mModel.level() == menu::Model::Level::Closed) return;
    const auto key = static_cast<Rml::Input::KeyIdentifier>(event.GetParameter<int>("key_identifier", 0));
    const Rml::Element* focused = mDocument->GetContext() ? mDocument->GetContext()->GetFocusElement() : nullptr;
    const bool typing = focused && (focused->GetTagName() == "input" || focused->GetTagName() == "textarea");
    const menu::Command c = menu::command_for_key(key, event.GetParameter<int>("shift_key", 0) != 0, typing);
    if (c == menu::Command::None) return;
    if (menu::held(c)) {
        if (down ? mHolds.down(key, c) : mHolds.up(key)) command(c, down);
    } else if (down) {
        command(c, true);
    } else {
        return;
    }
    event.StopPropagation();
}

void MenuShell::command(menu::Command c, bool down) {
    using menu::Command;
    using Level = menu::Model::Level;
    if (c == Command::Hide) {
        hiding(down);
        return;
    }
    if (!down) {
        if (mPage && mModel.level() == Level::Page) mPage->command(c, false);
        return;
    }
    if (c == Command::Back) {
        back();
        return;
    }
    if (c == Command::Menu) {
        if (!mModel.standalone() && !game_obscured_below(*this)) close_menu();
        return;
    }
    if (mModel.level() == Level::Entries) {
        const int count = static_cast<int>(mEntryElems.size());
        switch (c) {
        case Command::Left:
        case Command::Up:
        case Command::PrevTab:
            if (mEntry > 0) {
                mEntry--;
                play_sound(kSoundItemFocus);
            }
            break;
        case Command::Right:
        case Command::Down:
        case Command::NextTab:
            if (mEntry + 1 < count) {
                mEntry++;
                play_sound(kSoundItemFocus);
            }
            break;
        case Command::Confirm: choose_entry(mEntry); break;
        default: break;
        }
        return;
    }
    if (mPage && mModel.level() == Level::Page && !mPage->command(c, true) && !mUnder.empty() &&
        (c == Command::PrevTab || c == Command::NextTab)) {
        // A page over another with no tabs of its own (Players over Settings, whose tabs its band shows): the band's
        // buttons go back to the page under it and move its tab.
        close_over();
        play_sound(kSoundWindowClose);
        if (mPage) mPage->command(c, true);
    }
}

void MenuShell::back() {
    if (mPage && mModel.level() == menu::Model::Level::Page && mPage->back()) return;
    if (!mUnder.empty() && mModel.level() == menu::Model::Level::Page) {
        close_over();
        play_sound(kSoundWindowClose);
        return;
    }
    if (mModel.standalone()) {
        close_menu();
        return;
    }
    if (mModel.back() == menu::Model::Back::ToEntries) {
        close_page();
        play_sound(kSoundWindowClose);
        return;
    }
    close_menu();
}

void MenuShell::close_page() {
    badge("", "");
    while (!mUnder.empty()) close_over();
    if (mPage) mPage->closing();
    mPage.reset();
    clear_children(mPageHost);
}

void MenuShell::open_over(const PageMaker& make) {
    if (!mPage || mModel.level() != menu::Model::Level::Page) return;
    mPage->release();
    mPage->hidden();
    badge("", "");
    Rml::ElementPtr made = mDocument->CreateElement("menu-page");
    Rml::Element* host = mPageHost->GetParentNode()->InsertBefore(std::move(made), mPageHost->GetNextSibling());
    mPageHost->SetClass("under", true);
    mUnder.push_back({std::move(mPage), mPageHost});
    mPageHost = host;
    mPage = make(mPageHost, *this);
    play_sound(kSoundWindowOpen);
    draw();
}

void MenuShell::close_over() {
    if (mUnder.empty()) return;
    if (mPage) mPage->closing();
    mPage.reset();
    Rml::Element* gone = mPageHost;
    mPageHost = mUnder.back().host;
    mPage = std::move(mUnder.back().page);
    mUnder.pop_back();
    gone->GetParentNode()->RemoveChild(gone);
    mPageHost->SetClass("under", false);
    draw();
}

void MenuShell::close_menu() {
    if (mModel.level() == menu::Model::Level::Closed) return;
    close_page();
    if (mModel.standalone()) {
        play_sound(kSoundWindowClose);
        mModel.closed();
        pop();
        return;
    }
    std::vector<std::string> waiting = menu::waiting_for_restart();
    if (mModel.close(waiting) == menu::Model::Close::Now) {
        play_sound(kSoundMenuClose);
        finish_close();
        return;
    }
    mModel.open();  // (on its entries while the question is up)
    ask(menu::restart_question([this, waiting = std::move(waiting)] {
        mModel.later(waiting);
        finish_close();
    }));
}

void MenuShell::dismiss() {
    close_page();
    finish_close();
}

void MenuShell::finish_close() {
    mModel.closed();
    hide(false);
}

void MenuShell::switch_entry(int index) {
    if (mModel.level() != menu::Model::Level::Page || index == mEntry) return;
    if (mModel.back() == menu::Model::Back::ToEntries) close_page();
    choose_entry(index);
}

void MenuShell::choose_entry(int index) {
    const std::vector<menu::Entry>& entries = menu::entries();
    if (index < 0 || index >= static_cast<int>(entries.size())) return;
    mEntry = index;
    entries[index].choose(*this);
}

void MenuShell::open(std::unique_ptr<Document> document) {
    play_sound(kSoundWindowOpen);
    Document::push(std::move(document));
}

void MenuShell::open_page(const std::string& entry, const PageMaker& make) {
    close_page();
    for (size_t i = 0; i < menu::entries().size(); i++)
        if (menu::entries()[i].title == entry) mEntry = static_cast<int>(i);
    mPage = make(mPageHost, *this);
    mModel.open_page();
    if (!mModel.standalone()) play_sound(kSoundWindowOpen);
    draw();
}

void MenuShell::open_settings() {
    open_page("Settings", [](Rml::Element* host, menu::PageHost& shell) -> std::unique_ptr<menu::Page> {
        return std::make_unique<SettingsPage>(host, shell);
    });
}

SettingsPage* MenuShell::settings_page() {
    return dynamic_cast<SettingsPage*>(mPage.get());
}

void MenuShell::badge(const std::string& text, const std::string& kind) {
    set_text_content(mBadge, text);
    mBadge->SetClassNames(text.empty() ? "" : "on " + kind);
    mBadgeUntil = kind == "now" && !text.empty() ? clock::now() + kNowShown : clock::time_point{};
}

void MenuShell::push(std::unique_ptr<Document> document) {
    Document::push(std::move(document));
}

void MenuShell::hiding(bool on) {
    if (mHiding == on) return;
    mHiding = on;
    if (on && mPage) mPage->release();
    draw();
}

}  // namespace ui
