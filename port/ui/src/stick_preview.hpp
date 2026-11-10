// The live picture of a stick in Settings, under its deadzone slider: how far it is pushed, where it starts counting
// and what the game then gets. Written for Demon's Souls.
#pragma once

#include "component.hpp"

#include <functional>

namespace ui {

// Where the character first moves with the default deadzone of the controller on the game's port, in percent of the
// stick's travel (about 45 on XInput and SDL's pads, about 39 on a PlayStation pad over HID). With no controller, the
// first of those.
int stick_default_zone();

// A preview of the left stick is on screen with a pad connected, so the stick moves its dot and not the menu. Asked
// once a frame: the answer clears itself until the preview's next update.
bool stick_preview_takes_left();

class StickPreview : public Component {
public:
    struct Props {
        int stick = 0;                // 0 left, 1 right
        std::function<int()> zone;    // the percent of its travel where the stick starts counting
    };

    StickPreview(Rml::Element* parent, Props props);

    void update() override;

private:
    Props mProps;
    Rml::Element* mZone = nullptr;
    Rml::Element* mDot = nullptr;
    Rml::Element* mGame = nullptr;
    Rml::Element* mYours = nullptr;
    Rml::Element* mGets = nullptr;
    Rml::Element* mNote = nullptr;
    int mShownZone = -1;
    int mShownPath = -1;
    int mShownDefault = -1;
    int mShownYours = -1;
    int mShownGets = -1;
    float mDotAt[2] = {2.f, 2.f};
    float mGameAt[2] = {2.f, 2.f};
};

}  // namespace ui
