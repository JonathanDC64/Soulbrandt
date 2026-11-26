// Adapted from Dusklight (CC0), src/dusk/ui/prelaunch.cpp, by way of FFCCEOT-Recomp. Adapted for Demon's Souls: no ROM
// or disc to set up (the executable is built from the game already), so the screen is the title and three buttons over
// the game booting behind it.
#include "prelaunch.hpp"

#include "app.hpp"
#include "event.hpp"
#include "host.hpp"
#include "input.hpp"
#include "modal.hpp"
#include "folder_picker.hpp"
#include "path_utf8.hpp"
#include "prompts.hpp"
#include "question.hpp"
#include "save_offer.hpp"
#include "settings.hpp"
#include "setup_folder.hpp"
#include "start_sword.hpp"
#include "menu_pages.hpp"
#include "menu_shell.hpp"
#include "ui.hpp"
#include "updates.hpp"
#include "update_window.hpp"
#include "version.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace ui {
namespace {

const Rml::String kDocumentSource = R"RML(
<rml>
<head>
    <link type="text/rcss" href="res/rml/theme.rcss" />
    <link type="text/rcss" href="res/rml/controls.rcss" />
    <link type="text/rcss" href="res/rml/prelaunch.rcss" />
</head>
<body class="prelaunch">
    <prelaunch-gradient />
    <start-sword id="sword-window" />
    <start-sword id="sword-column" />
    <prelaunch-vignette />
    <content id="root" open>
        <menu>
            <hero class="intro-item delay-0">
                <start-sword id="sword-logo" />
                <start-sword id="sword-title" />
                <title-logo />
                <title-rule />
            </hero>
            <menu-list id="menu-list"><start-sword id="sword-menu" /></menu-list>
        </menu>
        <update-note id="update-note">
            <update-mark />
            <update-text><update-title id="update-title" /><update-line>See what's new, and update</update-line></update-text>
        </update-note>
        <hint-info class="intro-item delay-6">
            <hint-label id="hint-text" />
        </hint-info>
        <version-info class="intro-item delay-7">
            <version-label>Version <version-number id="version-text"></version-number></version-label>
        </version-info>
        <prelaunch-prompts id="prompts" class="intro-item delay-7" />
        <sword-label id="sword-label" />
    </content>
</body>
</rml>
)RML";

void apply_intro_animation(Rml::Element* element, const char* delay_class) {
    if (element == nullptr || delay_class == nullptr) {
        return;
    }
    element->SetClass("intro-item", true);
    element->SetClass(delay_class, true);
}

}  // namespace

Prelaunch::Prelaunch() : Document(kDocumentSource, false, DocumentScope::Prelaunch) {
    mRoot = mDocument->GetElementById("root");

    build_menu_buttons();

    mVersion = mDocument->GetElementById("version-text");
    mHint = mDocument->GetElementById("hint-text");
    mPrompts = mDocument->GetElementById("prompts");

    // The sword (a prototype, start_sword.hpp), off unless DES_START_SWORD=<1 to 9, or a look's id> names the one it
    // opens with: then keys 1 to 9 pick one, 0 none, T draws a faint one solid; Left / Right and L1 / R1 (Page Up /
    // Down) go through them, and a line at the top left names it. Off, none of its keys are taken and the screen is as
    // it was.
    mSwordWindow = mDocument->GetElementById("sword-window");
    mSwordLogo = mDocument->GetElementById("sword-logo");
    mSwordColumn = mDocument->GetElementById("sword-column");
    mSwordMenu = mDocument->GetElementById("sword-menu");
    mSwordTitle = mDocument->GetElementById("sword-title");
    mSwordLabel = mDocument->GetElementById("sword-label");
    if (const char* v = std::getenv("DES_START_SWORD"); v && *v && std::strcmp(v, "0") != 0) {
        mSwordOn = true;
        const char* solid = std::getenv("DES_START_SWORD_SOLID");
        mSwordSolid = solid && *solid && *solid != '0';
        const int by_id = start_sword::find(v);
        set_sword_look(by_id >= 0 ? by_id + 1 : std::atoi(v));
    } else if (mSwordLabel) {
        set_display(mSwordLabel, Rml::Style::Display::None);
    }
    if (mSwordOn) listen(mDocument, Rml::EventId::Keydown, [this](Rml::Event& e) {
        const int key = e.GetParameter<int>("key_identifier", 0);
        const int n = int(start_sword::looks().size());
        if (key == Rml::Input::KI_T) {  // (solid or faint, from its start again)
            mSwordSolid = !mSwordSolid;
            set_sword_look(mSwordLook);
            e.StopPropagation();
            return;
        }
        for (const int first : {int(Rml::Input::KI_0), int(Rml::Input::KI_NUMPAD0)}) {
            if (key >= first && key <= first + n) {
                set_sword_look(key - first);
                e.StopPropagation();
                return;
            }
        }
    });
    // A click on a prompt does what its button does: Open presses the choice with the focus, Quit asks.
    listen(mPrompts, Rml::EventId::Click, [this](Rml::Event& e) {
        for (Rml::Element* t = e.GetTargetElement(); t && t != mPrompts; t = t->GetParentNode()) {
            if (t->GetTagName() != "menu-prompt") continue;
            const auto key = static_cast<prompts::Key>(t->GetAttribute<int>("data-key", -1));
            if (key == prompts::Key::Back) {
                request_quit_confirm(false);
            } else if (key == prompts::Key::Confirm) {
                for (const auto& button : mMenuButtons)
                    if (Rml::Element* focused = mDocument->GetContext()->GetFocusElement(); button->contains(focused)) {
                        button->root()->Click();
                        break;
                    }
            }
            return;
        }
    });

    listen(mDocument, Rml::EventId::Transitionend, [this](Rml::Event& event) {
        auto* target = event.GetTargetElement();
        if (target == nullptr) {
            return;
        }
        if (target == mDocument && !mDocument->HasAttribute("open")) {
            Document::hide(true);
        } else if (target->GetTagName() == "button" && !target->IsClassSet("anim-done")) {
            target->SetClass("anim-done", true);
        }
    });
}

void Prelaunch::build_menu_buttons() {
    auto* menuList = mDocument->GetElementById("menu-list");
    if (menuList == nullptr) {
        return;
    }

    mMenuButtons.push_back(std::make_unique<Button>(menuList, "Play"));
    mMenuButtons.back()->on_pressed([this] { play(); });
    apply_intro_animation(mMenuButtons.back()->root(), "delay-1");

    mMenuButtons.push_back(std::make_unique<Button>(menuList, "Settings"));
    mMenuButtons.back()->on_pressed([this] {
        play_sound(kSoundWindowOpen);
        push(std::make_unique<MenuShell>(MenuShell::Standalone{}));
    });
    apply_intro_animation(mMenuButtons.back()->root(), "delay-2");

    mMenuButtons.push_back(std::make_unique<Button>(menuList, "Trophies"));
    mMenuButtons.back()->on_pressed([this] {
        play_sound(kSoundWindowOpen);
        push(menu::screen_alone("Trophies"));
    });
    apply_intro_animation(mMenuButtons.back()->root(), "delay-3");

    mMenuButtons.push_back(std::make_unique<Button>(menuList, "Mods"));
    mMenuButtons.back()->on_pressed([this] {
        play_sound(kSoundWindowOpen);
        push(menu::screen_alone("Mods"));
    });
    apply_intro_animation(mMenuButtons.back()->root(), "delay-4");

    mMenuButtons.push_back(std::make_unique<Button>(menuList, "Seamless co-op"));
    mMenuButtons.back()->on_pressed([this] {
        play_sound(kSoundWindowOpen);
        push(menu::screen_alone("Seamless co-op"));
    });
    apply_intro_animation(mMenuButtons.back()->root(), "delay-5");

    // Hidden until the update check finds a newer release (show_update), marked (.update) so it stands out; the note
    // at the top right (update-note) opens the same.
    const auto open_update = [] {
        play_sound(kSoundWindowOpen);
        push_document(std::make_unique<UpdateWindow>(
            UpdateWindow::Props{.open = UpdateWindow::Open::Notes, .release = updates::found(),
                                .overGame = updates::over_game()}));
    };
    mUpdateIndex = mMenuButtons.size();
    mMenuButtons.push_back(std::make_unique<Button>(menuList, "Update"));
    mMenuButtons.back()->on_pressed(open_update);
    mMenuButtons.back()->root()->SetClass("update", true);
    if (Rml::Element* note = mDocument->GetElementById("update-note"))
        mUpdateNoteListener = std::make_unique<ScopedEventListener>(note, Rml::EventId::Click,
                                                                    [open_update](Rml::Event&) { open_update(); });
    mMenuButtons.back()->root()->SetClass("anim-done", true);
    set_display(mMenuButtons.back()->root(), Rml::Style::Display::None);

    mMenuButtons.push_back(std::make_unique<Button>(menuList, "Quit"));
    mMenuButtons.back()->on_pressed([] { request_quit_confirm(false); });
    apply_intro_animation(mMenuButtons.back()->root(), "delay-5");

    // The pointer over a choice picks it, as Up and Down do, so the chosen one (its blood smear in DeS UI) follows the
    // mouse and there is only ever one. Only a real move of the pointer (input::hover_moved), so a key press is never
    // undone by a pointer resting on another choice; as the menus' entries do.
    for (const auto& b : mMenuButtons) {
        Button* button = b.get();
        button->listen(Rml::EventId::Mousemove, [button](Rml::Event&) {
            Rml::Element* root = button->root();
            Rml::Context* context = root->GetContext();
            if (!input::hover_moved() || (context && context->GetFocusElement() == root)) return;
            if (button->focus()) play_sound(kSoundItemFocus);
        });
    }
}

Prelaunch::~Prelaunch() = default;

// Play: the game is already running behind this screen; hide it and hand over the input.
void Prelaunch::play() {
    play_sound(kSoundPlay);
    if (settings::get_int("menu_hint") != 0) {
        show_menu_notification();
    }
    hide(true);
}

void Prelaunch::show() {
    Document::show();
    mDocument->SetAttribute("open", "");
    mRoot->SetAttribute("open", "");
}

void Prelaunch::hide(bool close) {
    if (close) {
        if (!mEntranceAnimationStarted || !host::animations_enabled()) {
            // Close document immediately
            Document::hide(true);
        } else {
            mPendingClose = true;
        }
        mDocument->RemoveAttribute("open");
    } else {
        mRoot->RemoveAttribute("open");
    }
}

// The sword's look: its decorator on the element whose box the look takes (the window's, the logo's or the middle
// column's), none on the others, and the line at the top left that names it. A new decorator starts the look from its
// beginning. A look with the flame has it under the sword, its map made from the sword in that box (render_gpu.cpp).
void Prelaunch::set_sword_look(int look, double delay) {
    const auto all = start_sword::looks();
    const int n = int(all.size());
    look = ((look % (n + 1)) + n + 1) % (n + 1);
    mSwordLook = look;
    const start_sword::Look* l = look > 0 ? &all[size_t(look - 1)] : nullptr;
    char tail[48];
    std::snprintf(tail, sizeof tail, "@%.2f@%d@%d", delay, ++mSwordShown, mSwordSolid ? 1 : 0);
    std::string decorator = "none";
    if (l) {
        decorator = std::string("shader(\"model:soulbrandt@") + l->id + tail + "\")";
        // Its flames under it, a sword's each (the look's flame and sword, else the logo's and the decorator's).
        for (int i = 0; l->flame && i < (l->models[1] ? 2 : 1); i++)
            decorator += std::string(", shader(\"flame:") + (l->flames[i] ? l->flames[i] : "logo") + "@" +
                         start_sword::model_of(look - 1, i, "soulbrandt") + "@" + l->id + tail + "\")";
    }
    const auto on = [&](Rml::Element* e, start_sword::Place place) {
        if (e) e->SetProperty("decorator", l && l->place == place ? decorator : std::string("none"));
    };
    on(mSwordWindow, start_sword::Place::Window);
    on(mSwordLogo, start_sword::Place::Logo);
    on(mSwordColumn, start_sword::Place::Column);
    on(mSwordMenu, start_sword::Place::Menu);
    on(mSwordTitle, start_sword::Place::Title);
    if (mSwordLabel) {
        set_text_content(mSwordLabel, (l ? "Sword " + std::to_string(look) + " of " + std::to_string(n) + ": " + l->label
                                         : std::string("No sword")) +
                                          ". Left and Right, or 1 to " + std::to_string(n) + ", change it; 0 hides it; T makes it " +
                                          (mSwordSolid ? "faint again (solid now)." : "solid."));
    }
    std::fprintf(stderr, "[ui] start screen: sword %d (%s%s)\n", look, l ? l->id : "none", mSwordSolid ? ", solid" : "");
}

struct Prelaunch::SavesPick {
    std::mutex lock;
    bool done = false;
    std::filesystem::path folder;  // empty: closed without one
};

namespace {

// DES_UI_SAVE_OFFER (copy, later, never or find): the saves' offer answered with that, none shown (a test). find
// takes the folder from the dialog (DES_UI_PICK, folder_picker.hpp) and copies from it.
std::string saves_answer() {
    const char* v = std::getenv("DES_UI_SAVE_OFFER");
    return v ? v : "";
}

}  // namespace

// Not in a scripted run (DES_AUTO_CONTINUE from the command line, a test harness): nobody is there to answer, unless
// DES_UI_SAVE_OFFER answers. How the copy went is always said: the player asked for it.
void Prelaunch::ask_saves() {
    if (const saves::CopyResult* r = saves::launch_copy()) {
        const std::string from = path_utf8(saves::launch_copy_from());
        if (r->ok)
            ask({.title = "Your saves are copied",
                 .body = "This copy of the game uses the saves from " + from + " now. Those there are as they were.",
                 .answers = {{"OK", {}}}});
        else
            ask({.title = "Your saves were not copied",
                 .body = "Copying from " + from + " stopped: " + r->why + ". Nothing there or here was changed.",
                 .answers = {{"OK", {}}}});
        return;
    }
    const saves::Offer offer = saves::offer();
    if (offer.kind == saves::Offer::Kind::None) return;
    const std::string answer = saves_answer();
    if (answer.empty() && settings::scripted_run()) return;
    const std::filesystem::path folder = offer.folder, home = saves::home();
    if (!answer.empty()) {
        std::fprintf(stderr, "[saves] the offer (%s), answered by DES_UI_SAVE_OFFER=%s\n",
                     offer.kind == saves::Offer::Kind::Folder ? path_utf8(folder).c_str() : "find the folder",
                     answer.c_str());
        if (answer == "copy" && offer.kind == saves::Offer::Kind::Folder) saves::restart_to_copy(folder);
        else if (answer == "never") saves::never(offer.kind == saves::Offer::Kind::Folder ? folder : home);
        else if (answer == "find") pick_saves_folder();
        return;
    }
    if (offer.kind == saves::Offer::Kind::Folder) {
        ask({.title = "Copy your saves here?",
             .body = "An earlier copy of the game has saves in " + path_utf8(folder) +
                     ". Copying them leaves those as they are. The game starts again to copy them.",
             .answers = {{"Copy", [folder] { saves::restart_to_copy(folder); }},
                         {"Not now", {}},
                         {"Never", [folder] { saves::never(folder); }}},
             .safe = 1});
        return;
    }
    ask({.title = "Bring your saves from an earlier copy?",
         .body = "This PC has run an earlier version of the game. If its saves are in another folder, choose that "
                 "folder to copy them here.",
         .answers = {{"Choose the folder", [this] { pick_saves_folder(); }},
                     {"Not now", {}},
                     {"Never ask", [home] { saves::never(home); }}},
         .safe = 1});
}

void Prelaunch::pick_saves_folder() {
    auto pick = std::make_shared<SavesPick>();
    mSavesPick = pick;
    pick_folder({}, "Choose the folder of your earlier copy of the game", host::window(), [pick](PickedFolder p) {
        if (p.failed) std::fprintf(stderr, "[saves] no folder dialog: %s\n", p.why.c_str());
        std::lock_guard<std::mutex> lock(pick->lock);
        pick->folder = p.folder;
        pick->done = true;
    });
}

// The folder chosen: the game folder it names (the game's, its gamedata or one save's), or another try.
void Prelaunch::saves_picked(const std::filesystem::path& picked) {
    if (picked.empty()) return;  // (closed without one)
    const std::filesystem::path owner = setup::saves_owner(picked);
    const std::string named = owner.empty() ? std::string("no saves in it") : "the saves of " + path_utf8(owner);
    std::fprintf(stderr, "[saves] picked %s: %s\n", path_utf8(picked).c_str(), named.c_str());
    if (owner.empty() || setup::same_folder(owner, saves::home())) {
        if (!saves_answer().empty()) return;
        ask({.title = "No saves in that folder",
             .body = "Choose the folder of your earlier copy of the game: the one with its program and its "
                     "gamedata folder.",
             .answers = {{"Choose another", [this] { pick_saves_folder(); }}, {"Cancel", {}}},
             .safe = 1});
        return;
    }
    if (saves_answer() == "find") {
        saves::restart_to_copy(owner);
        return;
    }
    ask({.title = "Copy your saves here?",
         .body = "The saves in " + path_utf8(owner) +
                 " are copied into this copy of the game. Those there stay as they are. The game starts again to "
                 "copy them.",
         .answers = {{"Copy", [owner] { saves::restart_to_copy(owner); }}, {"Cancel", {}}},
         .safe = 1});
}

// "Update to <version>" while the check has found a release the player has not skipped.
void Prelaunch::show_update() {
    const bool found = updates::state() == updates::State::Found && !updates::skipped();
    const std::string version = found ? updates::found().version : std::string();
    if (version == mUpdateShown) return;
    mUpdateShown = version;
    Button& button = *mMenuButtons[mUpdateIndex];
    if (found) {
        button.set_text("Update to " + version);
        button.root()->RemoveProperty(Rml::PropertyId::Display);  // (back to the menu's own look)
    } else {
        set_display(button.root(), Rml::Style::Display::None);
    }
    if (Rml::Element* note = mDocument->GetElementById("update-note")) note->SetClass("on", found);
    mDocument->SetClass("update-found", found);  // (a seventh choice: the sheet's --start-room is taller)
    if (Rml::Element* title = mDocument->GetElementById("update-title"))
        set_text_content(title, found ? "Version " + version + " is out" : std::string());
}

void Prelaunch::update() {
    if (!mEntranceAnimationStarted && mDocument != nullptr) {
        // Without animations (DES_UI_NO_ANIM: the tests' pictures, uishots) the screen is at once as it ends.
        mDocument->SetClass("still", !host::animations_enabled());
        mDocument->SetClass("animate-in", true);
        mEntranceAnimationStarted = true;
        if (mSwordOn) set_sword_look(mSwordLook, 0.5);  // (from the start again as the screen fades in: its motion is seen)
    }
    show_update();
    if (!mSavesAsked && visible() && !asking()) {
        mSavesAsked = true;
        ask_saves();
    }
    if (mSavesPick) {
        std::filesystem::path picked;
        bool done = false;
        {
            std::lock_guard<std::mutex> lock(mSavesPick->lock);
            done = mSavesPick->done;
            picked = mSavesPick->folder;
        }
        if (done) {
            mSavesPick.reset();
            saves_picked(picked);
        }
    }
    // Settings > Interface: the smoke and the logo's flame (DeS UI draws them) can be off; des.rcss leaves them out.
    mDocument->SetClass("no-smoke", settings::get_int("start_smoke") == 0);
    mDocument->SetClass("no-flame", settings::get_int("start_flame") == 0);
    for (const auto& button : mMenuButtons) {
        button->update();
    }
    if (mVersion != nullptr) {
        set_text_content(mVersion, kVersion);
    }
    if (mHint != nullptr) {
        set_text_content(mHint, Rml::String("In the game, F1 or ") +
                                    input::chord_label(settings::get_int("pad_menu_chord")) + " opens the menu.");
    }
    update_prompts();
    mRoomHeight = dp_property(mDocument, "--start-room");

    Document::update();
}

// The prompts for the keys here, as the menus show theirs (a look that draws them: DeS UI), drawn again only when
// they change (the device used last). The choices are a column, so Select is Up / Down (Move), as in Settings' rows.
void Prelaunch::update_prompts() {
    if (mPrompts == nullptr) return;
    const prompts::Device device = input::menu_device();
    std::string rml;
    for (const prompts::Prompt& p : {prompts::Prompt{prompts::Key::Move, "Select"},
                                     prompts::Prompt{prompts::Key::Confirm, "Open"},
                                     prompts::Prompt{prompts::Key::Back, "Quit"}})
        rml += "<menu-prompt data-key=\"" + std::to_string(static_cast<int>(p.key)) + "\">" +
               menu::glyph_rml(device, p.key) + "<menu-says>" + escape(p.label) + "</menu-says></menu-prompt>";
    if (rml == mPromptsShown) return;
    mPromptsShown = rml;
    mPrompts->SetInnerRML(rml);
}

bool Prelaunch::focus() {
    if (mMenuButtons.empty()) {
        return false;
    }
    return mMenuButtons.front()->focus();
}

bool Prelaunch::visible() const {
    return mDocument->HasAttribute("open") && mRoot->HasAttribute("open");
}

bool Prelaunch::handle_nav_command(Rml::Event& event, NavCommand cmd) {
    int direction = 0;
    if (cmd == NavCommand::Down) {
        direction = 1;
    } else if (cmd == NavCommand::Up) {
        direction = -1;
    } else if (cmd == NavCommand::Cancel) {
        // Circle / Escape on the start screen asks to quit, as a desktop program's start screen would.
        request_quit_confirm(false);
        return true;
    } else if (mSwordOn && (cmd == NavCommand::Left || cmd == NavCommand::Right || cmd == NavCommand::Previous ||
                            cmd == NavCommand::Next)) {  // the sword's next look, or the one before (none between the last and 1)
        set_sword_look(mSwordLook + (cmd == NavCommand::Right || cmd == NavCommand::Next ? 1 : -1));
        play_sound(kSoundItemFocus);
        event.StopPropagation();
        return true;
    } else {
        return false;
    }
    auto* target = event.GetTargetElement();
    int focusedButton = -1;
    for (int i = 0; i < int(mMenuButtons.size()); ++i) {
        if (mMenuButtons[i]->contains(target)) {
            focusedButton = i;
            break;
        }
    }
    const auto n = static_cast<int>(mMenuButtons.size());
    int i = ((focusedButton + direction) % n + n) % n;
    while (i >= 0 && i < int(mMenuButtons.size())) {
        if (size_t(i) == mUpdateIndex && mUpdateShown.empty()) {  // (hidden)
            i += direction;
            continue;
        }
        if (mMenuButtons[i]->focus()) {
            play_sound(kSoundItemFocus);
            event.StopPropagation();
            return true;
        }
        i += direction;
    }
    return false;
}

}  // namespace ui
