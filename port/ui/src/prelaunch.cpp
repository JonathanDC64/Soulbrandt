// Adapted from Dusklight (CC0), src/dusk/ui/prelaunch.cpp, by way of FFCCEOT-Recomp. Adapted for Demon's Souls: no ROM
// or disc to set up (the executable is built from the game already), so the screen is the title and three buttons over
// the game booting behind it.
#include "prelaunch.hpp"

#include "app.hpp"
#include "coop_window.hpp"
#include "host.hpp"
#include "input.hpp"
#include "modal.hpp"
#include "prompts.hpp"
#include "settings.hpp"
#include "menu_pages.hpp"
#include "menu_shell.hpp"
#include "ui.hpp"
#include "updates.hpp"
#include "update_window.hpp"
#include "version.hpp"

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
    <prelaunch-vignette />
    <content id="root" open>
        <menu>
            <hero class="intro-item delay-0">
                <title-text>)RML" DES_PRODUCT_NAME R"RML(</title-text>
                <title-rule />
                <eyebrow>Demon's Souls on PC</eyebrow>
            </hero>
            <menu-list id="menu-list" />
        </menu>
        <hint-info class="intro-item delay-6">
            <hint-label id="hint-text" />
        </hint-info>
        <version-info class="intro-item delay-7">
            <version-label>Version <version-number id="version-text"></version-number></version-label>
        </version-info>
        <prelaunch-prompts id="prompts" class="intro-item delay-7" />
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
    // A click on a prompt does what its button does: Open presses the choice with the focus, Quit asks.
    listen(mPrompts, Rml::EventId::Click, [this](Rml::Event& e) {
        for (Rml::Element* t = e.GetTargetElement(); t && t != mPrompts; t = t->GetParentNode()) {
            if (t->GetTagName() != "menu-prompt") continue;
            const auto key = static_cast<prompts::Key>(t->GetAttribute<int>("data-key", -1));
            if (key == prompts::Key::Back) {
                request_quit_confirm();
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

    mMenuButtons.push_back(std::make_unique<Button>(menuList, "Co-op"));
    mMenuButtons.back()->on_pressed([this] {
        play_sound(kSoundWindowOpen);
        push(make_coop_window());
    });
    apply_intro_animation(mMenuButtons.back()->root(), "delay-5");

    // Hidden until the update check finds a newer release (show_update).
    mUpdateIndex = mMenuButtons.size();
    mMenuButtons.push_back(std::make_unique<Button>(menuList, "Update"));
    mMenuButtons.back()->on_pressed([] {
        play_sound(kSoundWindowOpen);
        push_document(std::make_unique<UpdateWindow>(
            UpdateWindow::Props{.open = UpdateWindow::Open::Notes, .release = updates::found(),
                                .overGame = updates::over_game()}));
    });
    mMenuButtons.back()->root()->SetClass("anim-done", true);
    set_display(mMenuButtons.back()->root(), Rml::Style::Display::None);

    mMenuButtons.push_back(std::make_unique<Button>(menuList, "Quit"));
    mMenuButtons.back()->on_pressed([] { request_quit_confirm(); });
    apply_intro_animation(mMenuButtons.back()->root(), "delay-5");
}

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
}

void Prelaunch::update() {
    if (!mEntranceAnimationStarted && mDocument != nullptr) {
        // Without animations (DES_UI_NO_ANIM: the tests' pictures, uishots) the screen is at once as it ends.
        mDocument->SetClass("still", !host::animations_enabled());
        mDocument->SetClass("animate-in", true);
        mEntranceAnimationStarted = true;
    }
    show_update();
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

    Document::update();
}

// The prompts for the keys here, as the menus show theirs (a look that draws them: DeS UI), drawn again only when
// they change (the device used last).
void Prelaunch::update_prompts() {
    if (mPrompts == nullptr) return;
    const prompts::Device device = input::menu_device();
    std::string rml;
    for (const prompts::Prompt& p : {prompts::Prompt{prompts::Key::Change, "Select"},
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
        request_quit_confirm();
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
