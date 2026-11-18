// Adapted from Dusklight (CC0), src/dusk/ui/prelaunch.hpp, by way of FFCCEOT-Recomp. Adapted for Demon's Souls.
#pragma once

#include "button.hpp"
#include "document.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ui {

class ScopedEventListener;

// The start screen, over the game window at launch: the title, Play / Settings / Quit. The game boots behind it (its
// own logos and title screen are the backdrop); Play hides it and gives the game the input.
class Prelaunch : public Document {
public:
    Prelaunch();
    ~Prelaunch() override;

    void show() override;
    void hide(bool close) override;
    void update() override;
    bool focus() override;
    bool visible() const override;
    bool obscures_game() const override { return true; }
    // Drawn as the whole window, as the setup screens are: its scale follows the window's size, so the logo and the
    // choices keep their place in the smoke and the fades, which are drawn as shares of the window. At the display's
    // scale alone (100% on a 1440p display) a maximised window had them small in the middle, in thin fades.
    //   Not while a screen it opened covers it (Settings, Trophies, Mods, Seamless co-op): that screen takes the size
    // it has in the game, so a menu is one size wherever it opens, and UI scale's 50% is the same 50% at both.
    bool scales_with_window() const override { return !mCovered; }
    // The height its look lays it out for (the sheet's --start-room): a UI scale of 125% or more fills the window with
    // it, so the scale goes no further than the window holds of it, and the choices clear the lines at the foot.
    float room_height() const override { return mCovered ? 0.f : mRoomHeight; }
    // Covered, its choices go at once instead of fading (class covered): the scale changes as they go, and they would
    // shrink as they faded. Uncovered, they fade in at its own scale.
    void cover() override;
    void uncover() override;

protected:
    bool handle_nav_command(Rml::Event& event, NavCommand cmd) override;

private:
    void build_menu_buttons();
    void play();
    void show_update();
    void update_prompts();

    bool mEntranceAnimationStarted = false;
    std::vector<std::unique_ptr<Button>> mMenuButtons;
    size_t mUpdateIndex = 0;     // "Update to <version>" among them, shown once the check found one (updates.hpp)
    std::string mUpdateShown;    // the version it shows; empty: hidden
    std::unique_ptr<ScopedEventListener> mUpdateNoteListener;  // a click on the note at the top right: the same
    Rml::Element* mRoot = nullptr;
    Rml::Element* mVersion = nullptr;
    Rml::Element* mHint = nullptr;
    Rml::Element* mPrompts = nullptr;
    std::string mPromptsShown;
    float mRoomHeight = 0.f;  // --start-room, in dp (room_height())
    bool mCovered = false;    // a screen it opened is over it (cover, uncover)
};

}  // namespace ui
