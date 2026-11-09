// See stick_preview.hpp.
#include "stick_preview.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <string>

namespace ui {

// cellPad.h (declared here: des_ui does not take ps3recomp's include paths). The pad on the game's port, read when
// asked (the game may be held behind this menu, or not running yet), and what the runtime makes of a stick position.
extern "C" int cellPad_host_stick_raw(int stick, float* x, float* y);
extern "C" void cellPad_host_stick_stage(int path, int stick, float x, float y, unsigned char* out_x,
    unsigned char* out_y);

namespace {

// The game's own stick function, as both sticks go through it: each byte to -1..1, then lengths up to 0.37 are nothing
// and the rest runs to full at 0.95.
constexpr float kGameZone = 0.37f;
constexpr float kGameGain = 1.7241f;

struct Point {
    float x, y;
};

Point game_stick(unsigned char bx, unsigned char by) {
    const float x = (bx / 255.f - 0.5f) * 2.f;
    const float y = (by / 255.f - 0.5f) * 2.f;
    const float len = std::hypot(x, y);
    if (len <= kGameZone) {
        return {0.f, 0.f};
    }
    const float out = std::min((len - kGameZone) * kGameGain, 1.f);
    return {x / len * out, y / len * out};
}

// Which deadzone the connected controller goes through (cellPad_host_stick_raw). Rows ask every frame, so a slow
// answer (an empty XInput slot) is kept for a moment; controllers come and go at human speed.
int connected_path() {
    using clock = std::chrono::steady_clock;
    static clock::time_point next{};
    static int path = 0;
    const auto now = clock::now();
    if (now >= next) {
        float x, y;
        path = cellPad_host_stick_raw(0, &x, &y);
        next = now + std::chrono::milliseconds(500);
    }
    return path;
}

// Where the default deadzone first gets past the game's zone on a straight push (the side that needs one byte more).
// The runtime's own numbers, once for each path: only its environment knobs could change them.
int default_zone_of(int path) {
    static int cached[3];
    const int p = path == 2 ? 2 : 1;
    if (cached[p] == 0) {
        float lo = 0.f, hi = 1.f;
        for (int i = 0; i < 16; ++i) {
            const float mid = (lo + hi) * 0.5f;
            unsigned char bx = 128, by = 128;
            cellPad_host_stick_stage(p, -1, -mid, 0.f, &bx, &by);
            (game_stick(bx, by).x != 0.f ? hi : lo) = mid;
        }
        cached[p] = int(std::lround(hi * 100.f));
    }
    return cached[p];
}

std::atomic<bool> sTakesLeft{false};

void place(Rml::Element* el, float at[2], float x, float y) {
    if (x == at[0] && y == at[1]) {
        return;
    }
    at[0] = x;
    at[1] = y;
    el->SetProperty(Rml::PropertyId::Left, Rml::Property(50.f + 50.f * x, Rml::Unit::PERCENT));
    el->SetProperty(Rml::PropertyId::Top, Rml::Property(50.f + 50.f * y, Rml::Unit::PERCENT));
}

}  // namespace

int stick_default_zone() {
    return default_zone_of(connected_path());
}

bool stick_preview_takes_left() {
    return sTakesLeft.exchange(false);
}

StickPreview::StickPreview(Rml::Element* parent, Props props)
    : Component(append(parent, "stick-preview")), mProps(std::move(props)) {
    auto* ring = append(mRoot, "stick-ring");
    mZone = append(ring, "stick-zone");
    mDot = append(ring, "stick-dot");
    mGame = append(ring, "stick-dot");
    mGame->SetClass("game", true);

    auto* legend = append(mRoot, "stick-legend");
    auto* yours = append(legend, "line");
    append(yours, "stick-dot");
    mYours = append(yours, "span");
    auto* gets = append(legend, "line");
    append(gets, "stick-dot")->SetClass("game", true);
    mGets = append(gets, "span");
    mNote = append(legend, "note");
    update();
}

void StickPreview::update() {
    if (!mRoot->IsVisible(true)) {
        return;  // (a hidden window reads no pads)
    }
    float x = 0.f, y = 0.f;
    const int path = cellPad_host_stick_raw(mProps.stick, &x, &y);
    if (path != 0 && mProps.stick == 0) {
        sTakesLeft.store(true);
    }

    // What the game gets: the runtime's deadzone in force, then the game's own.
    unsigned char bx = 128, by = 128;
    if (path != 0) {
        cellPad_host_stick_stage(path, mProps.stick, x, y, &bx, &by);
    }
    const Point game = game_stick(bx, by);
    place(mGame, mGameAt, game.x, game.y);

    // The stick, held to its ring (a square gate's corners go past it).
    const float len = std::hypot(x, y);
    const float scale = len > 1.f ? 1.f / len : 1.f;
    place(mDot, mDotAt, x * scale, y * scale);

    const int zone = mProps.zone();
    if (zone != mShownZone) {
        mShownZone = zone;
        mZone->SetClass("none", zone <= 0);
        mZone->SetProperty(Rml::PropertyId::Width, Rml::Property(float(zone), Rml::Unit::PERCENT));
        mZone->SetProperty(Rml::PropertyId::Height, Rml::Property(float(zone), Rml::Unit::PERCENT));
        const float edge = 50.f - zone * 0.5f;
        mZone->SetProperty(Rml::PropertyId::Left, Rml::Property(edge, Rml::Unit::PERCENT));
        mZone->SetProperty(Rml::PropertyId::Top, Rml::Property(edge, Rml::Unit::PERCENT));
    }

    const int yours = int(std::lround(std::min(len, 1.f) * 100.f));
    if (yours != mShownYours) {
        mShownYours = yours;
        set_text_content(mYours, std::format("Your stick: {}%", yours));
    }
    const int gets = int(std::lround(std::hypot(game.x, game.y) * 100.f));
    if (gets != mShownGets) {
        mShownGets = gets;
        set_text_content(mGets, std::format("The game gets: {}%", gets));
    }

    const int def = default_zone_of(path);
    if (path != mShownPath || def != mShownDefault) {
        mShownPath = path;
        mShownDefault = def;
        Rml::String note = path == 0 ? Rml::String("No gamepad connected.")
                                     : std::format("Default for this controller: {}%.", def);
        if (path != 0 && mProps.stick == 0) {
            note += " The D-pad moves in this menu while you try the left stick.";
        }
        set_text_content(mNote, note);
    }

    Component::update();
}

}  // namespace ui
