// Written for Demon's Souls. The frame rate counter (Settings, Display, Frame rate counter): the game's frame rate at
// the top of the screen (Counter position: right, centre or left), and with frame times a graph of how long each of
// its last 120 frames took (4 s at 30 fps), with a line at the frame time its frame rate setting aims at. For seeing
// whether the game keeps its frame rate, with a mod or a setting changed.
//
// The frames are the game's own: the times of its flip requests (des_options.cpp's des_frame_at_us), the clock the
// game's frame step is measured by, so a menu of this UI drawn between them does not count. The counter is part of
// the overlay document (overlay.hpp). It shows while no menu of this UI is up, and keeps the UI awake each frame then
// (des_ui.cpp's frame); with the setting off it costs one check a frame.
#pragma once

#include <RmlUi/Core/Types.h>

#include <array>
#include <chrono>
#include <cstdint>

namespace ui::perf {

enum class Mode { Off, Rate, Times };
enum class Place { Right, Centre, Left };  // along the top edge

// The setting (fps_counter: 0, 1 the frame rate, 2 with frame times), read again at most every quarter second, as is
// its place (fps_counter_at: "", centre, left). The render thread's.
Mode mode();

// Registers <frame-graph> with RmlUi: once it has started (host.cpp), before a document is loaded.
void install();

class FrameGraph;

// The counter's elements in the overlay document, made the first time the setting is on.
class Counter {
public:
    // hidden: a menu is up (the counter waits under it).
    void update(Rml::Element* parent, bool hidden);

private:
    static constexpr size_t kKept = 4096;  // the game's frames kept here, the newest last (a power of two)

    void build(Rml::Element* parent);
    void place_area(Rml::Element* parent);
    void take_frames();
    void update_text(Mode mode, uint64_t nowUs);
    void update_graph();
    uint64_t at(uint64_t n) const { return mAt[n & (kKept - 1)]; }

    Rml::Element* mArea = nullptr;  // the picture's part of the window, which holds mBox
    Rml::Element* mBox = nullptr;
    Rml::Element* mFps = nullptr;
    Rml::Element* mLow = nullptr;
    Rml::Element* mScale = nullptr;  // the line's frame time, beside it
    FrameGraph* mGraph = nullptr;

    std::array<float, 4> mPicture{};  // perf-area's place (px: left, top, right, bottom)
    std::array<uint64_t, kKept> mAt{};  // when the game's frame n came (µs, steady clock), at mAt[n % kKept]
    uint64_t mFirst = 0;                // the first frame kept
    uint64_t mSeen = 0;                 // frames read so far (the next to read)
    uint64_t mGraphed = 0;              // mSeen when the graph was last made
    std::array<float, kKept> mScratch{};  // frame times, for the 1% low and the median

    Mode mShownMode = Mode::Off;
    Place mShownPlace = Place::Right;
    bool mShownHidden = false;
    int mShownFps = -1;
    int mShownLow = -1;
    int mShownTarget = -1;  // mHz
    float mTopMs = 0.f;     // the graph's height in ms, while no target sets it
    std::chrono::steady_clock::time_point mTextAt{};
    std::chrono::steady_clock::time_point mGraphAt{};
};

}  // namespace ui::perf
