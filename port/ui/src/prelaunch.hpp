// Adapted from Dusklight (CC0), src/dusk/ui/prelaunch.hpp, by way of FFCCEOT-Recomp. Adapted for Demon's Souls.
#pragma once

#include "button.hpp"
#include "document.hpp"

#include <filesystem>
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
    // The height its look lays it out for (the sheet's --start-room): a UI scale of 125% or more fills the window with
    // it, so the scale goes no further than the window holds of it, and the choices clear the lines at the foot.
    float room_height() const override { return mRoomHeight; }

protected:
    bool handle_nav_command(Rml::Event& event, NavCommand cmd) override;

private:
    void build_menu_buttons();
    void play();
    void show_update();
    void update_prompts();
    // The saves' question at this start, once (save_offer.hpp, docs/164 section 5): how the copy the last start asked
    // for went, or the offer to copy an earlier game folder's saves, or to find that folder.
    void ask_saves();
    void pick_saves_folder();  // the system's folder dialog, for the earlier game folder
    void saves_picked(const std::filesystem::path& folder);
    // The sword's look (start_sword.hpp): 1 to its count, 0 none, any other wraps; it starts after `delay` seconds.
    void set_sword_look(int look, double delay = 0.0);

    bool mEntranceAnimationStarted = false;
    int mSwordLook = 0;
    int mSwordShown = 0;  // (how many times a look was set: each decorator value is new)
    bool mSwordOn = false;  // the sword's looks are tried (DES_START_SWORD): off, the start screen is as it was
    bool mSwordSolid = false;  // a faint look drawn whole (T, or DES_START_SWORD_SOLID=1)
    Rml::Element* mSwordWindow = nullptr;  // the sword's element over the window, over the logo, down the middle
    Rml::Element* mSwordLogo = nullptr;
    Rml::Element* mSwordColumn = nullptr;
    Rml::Element* mSwordMenu = nullptr;  // (behind the choices)
    Rml::Element* mSwordTitle = nullptr;  // (beside the title)
    Rml::Element* mSwordLabel = nullptr;
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
    bool mSavesAsked = false;
    struct SavesPick;                       // the folder dialog's answer, from the dialog's thread
    std::shared_ptr<SavesPick> mSavesPick;  // (shared with that thread: this screen may close first)
};

}  // namespace ui
