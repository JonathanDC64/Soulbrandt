// Written for Demon's Souls. The frame rate counter (perf_overlay.hpp).
#include "perf_overlay.hpp"

#include "settings.hpp"
#include "ui.hpp"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/PropertyIdSet.h>
#include <RmlUi/Core/RenderManager.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

extern "C" {
uint64_t des_frame_count(void);        // des_options.cpp: the game's frames so far (its flip requests)
uint64_t des_frame_at_us(uint64_t n);  // when frame n came (µs, steady clock); 0 if it is not kept
int des_frame_target_mhz(void);        // the frame rate its pacing aims at (mHz); 0: none (unlocked)
}

namespace ui::perf {

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kColumns = 120;                 // the graph: the last 120 frames (4 s at 30 fps), one column each
constexpr uint64_t kRateUs = 1'000'000;       // the frame rate: its frames over the last second
constexpr uint64_t kLowUs = 10'000'000;       // the 1% low: the frames of the last 10 s
constexpr uint64_t kStaleUs = 1'000'000;      // no frame for this long: the game is not drawing (0 fps)
constexpr auto kTextEvery = std::chrono::milliseconds(500);  // the numbers change at most this often
constexpr auto kModeEvery = std::chrono::milliseconds(250);  // the setting is read again at most this often
// The graph is made again at most this often (each time the renderer takes a new mesh): at a high frame rate it moves
// by a few frames at a time.
constexpr auto kGraphEvery = std::chrono::milliseconds(15);
constexpr float kColumnAlpha = 0.45f;          // the columns against the line (FrameGraph)
// A frame this much longer than the line is late. A frame's time wanders a little either way at any rate (the game's
// thread wakes a little early or late); at 30 fps a late one misses a vblank: 50 ms, not 33.4. (The runtime's
// profile sessions count frames over 36.3 ms at 30.)
constexpr float kLate = 1.1f;

uint64_t now_us() {
    return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());
}

}  // namespace

// <frame-graph>: the frame times as columns from the bottom, a frame each, the oldest at the left, and a line across at
// half its height. The line is its `color`, the columns the same at kColumnAlpha. A late
// frame's column (kLate times the line or more) is `image-color` above the line. The mesh is made again only when the
// columns change, in the same buffers.
class FrameGraph final : public Rml::Element {
public:
    explicit FrameGraph(const Rml::String& tag) : Rml::Element(tag) {}

    // ms: each column's frame time, 0 for none; topMs: the time at the top; over: colour the part above the line.
    void set(const std::array<float, kColumns>& ms, float topMs, bool over) {
        mMs = ms;
        mTopMs = topMs;
        mOver = over;
        mStale = true;
    }

protected:
    void OnPropertyChange(const Rml::PropertyIdSet& changed) override {
        Rml::Element::OnPropertyChange(changed);
        if (changed.Contains(Rml::PropertyId::Color) || changed.Contains(Rml::PropertyId::ImageColor) ||
            changed.Contains(Rml::PropertyId::Opacity))
            mStale = true;
    }

    void OnRender() override {
        Rml::RenderManager* render = GetRenderManager();
        if (!render) return;
        const Rml::Vector2f box = GetBox().GetSize(Rml::BoxArea::Content);
        const float w = std::round(box.x), h = std::round(box.y);
        if (w < 1.f || h < 2.f) return;
        if (w != mDrawnW || h != mDrawnH) {
            mDrawnW = w;
            mDrawnH = h;
            mStale = true;
        }
        if (mStale) {
            mStale = false;
            Rml::Mesh mesh = mGeometry ? mGeometry.Release(Rml::Geometry::ReleaseMode::ClearMesh) : std::move(mSpare);
            mesh.vertices.clear();
            mesh.indices.clear();
            const Rml::ComputedValues& computed = GetComputedValues();
            const float opacity = computed.opacity();
            const Rml::ColourbPremultiplied stroke = computed.color().ToPremultiplied(opacity);
            const Rml::ColourbPremultiplied under = computed.color().ToPremultiplied(opacity * kColumnAlpha);
            const Rml::ColourbPremultiplied over = mOver ? computed.image_color().ToPremultiplied(opacity) : under;
            const float thick = std::max(1.f, std::round(Rml::ElementUtilities::GetDensityIndependentPixelRatio(this)));
            const float line = h - std::round(h * 0.5f);
            for (int c = 0; c < kColumns; c++) {
                if (mMs[c] <= 0.f || mTopMs <= 0.f) continue;
                const float x0 = std::round(w * float(c) / kColumns), x1 = std::round(w * float(c + 1) / kColumns);
                if (x1 <= x0) continue;
                const float top = h - std::max(1.f, std::round(h * std::min(mMs[c] / mTopMs, 1.f)));
                if (top < line && mMs[c] >= mTopMs * 0.5f * kLate) {
                    Rml::MeshUtilities::GenerateQuad(mesh, {x0, top}, {x1 - x0, line - top}, over);
                    Rml::MeshUtilities::GenerateQuad(mesh, {x0, line}, {x1 - x0, h - line}, under);
                } else {
                    Rml::MeshUtilities::GenerateQuad(mesh, {x0, top}, {x1 - x0, h - top}, under);
                }
            }
            if (mTopMs > 0.f) Rml::MeshUtilities::GenerateQuad(mesh, {0.f, line - thick}, {w, thick}, stroke);
            if (mesh)
                mGeometry = render->MakeGeometry(std::move(mesh));
            else
                mSpare = std::move(mesh);
        }
        mGeometry.Render(GetAbsoluteOffset(Rml::BoxArea::Content));
    }

private:
    std::array<float, kColumns> mMs{};
    float mTopMs = 0.f;
    bool mOver = false;
    bool mStale = true;
    float mDrawnW = 0.f, mDrawnH = 0.f;
    Rml::Geometry mGeometry;
    Rml::Mesh mSpare;  // the buffers, while there is no geometry
};

namespace {
Rml::ElementInstancerGeneric<FrameGraph> sInstancer;
}  // namespace

void install() { Rml::Factory::RegisterElementInstancer("frame-graph", &sInstancer); }

namespace {

// The two settings, read again at most every kModeEvery.
struct Wanted {
    Mode mode = Mode::Off;
    Place place = Place::Right;
};

const Wanted& wanted() {
    static Wanted sWanted;
    static Clock::time_point sReadAt{};
    const Clock::time_point now = Clock::now();
    if (now >= sReadAt) {
        sReadAt = now + kModeEvery;
        const int v = settings::get_int("fps_counter");
        sWanted.mode = v == 1 ? Mode::Rate : v == 2 ? Mode::Times : Mode::Off;
        const std::string at = settings::get("fps_counter_at");
        sWanted.place = at == "centre" ? Place::Centre : at == "left" ? Place::Left : Place::Right;
    }
    return sWanted;
}

}  // namespace

Mode mode() { return wanted().mode; }

// perf > (perf-numbers > (perf-rate > (perf-fps, perf-unit), perf-low), frame-graph, perf-scale). Its sheet shows the
// graph, the 1% low and the scale only with the class times. The first of the overlay's elements, so its notes draw
// over it.
void Counter::build(Rml::Element* parent) {
    Rml::ElementDocument* doc = parent != nullptr ? parent->GetOwnerDocument() : nullptr;
    if (doc == nullptr) return;
    mBox = parent->InsertBefore(doc->CreateElement("perf"), parent->GetFirstChild());
    if (mBox == nullptr) return;
    Rml::Element* numbers = append(mBox, "perf-numbers");
    Rml::Element* rate = append(numbers, "perf-rate");
    mFps = append_text_element(rate, "perf-fps", "0");
    append_text_element(rate, "perf-unit", "fps");
    mLow = append_text_element(numbers, "perf-low", "1% low 0");
    mGraph = dynamic_cast<FrameGraph*>(append(mBox, "frame-graph"));
    mScale = append_text_element(mBox, "perf-scale", "");
}

void Counter::update(Rml::Element* parent, bool hidden) {
    const Mode m = wanted().mode;
    if (mBox == nullptr) {
        if (m == Mode::Off) return;
        build(parent);
        if (mBox == nullptr) return;
    }
    if (const Place place = wanted().place; place != mShownPlace) {
        mShownPlace = place;
        mBox->SetClass("centre", place == Place::Centre);
        mBox->SetClass("left", place == Place::Left);
    }
    if (m != mShownMode || hidden != mShownHidden) {
        mShownMode = m;
        mShownHidden = hidden;
        mBox->SetClass("off", m == Mode::Off || hidden);
        mBox->SetClass("times", m == Mode::Times);
        mShownFps = mShownLow = mShownTarget = -1;
        mTextAt = {};
        mGraphed = 0;
    }
    if (m == Mode::Off || hidden) return;
    take_frames();
    const Clock::time_point now = Clock::now();
    if (now >= mTextAt) {
        mTextAt = now + kTextEvery;
        update_text(m, now_us());
    }
    if (m == Mode::Times && mSeen != mGraphed && now >= mGraphAt) {
        mGraphAt = now + kGraphEvery;
        update_graph();
    }
}

// The game's new frames, into mAt. After a pause (the counter off or under a menu) only the newest are read.
void Counter::take_frames() {
    const uint64_t count = des_frame_count();
    const uint64_t oldest = count > kKept - 64 ? count - (kKept - 64) : 0;  // (des_frame_at_us's margin)
    if (mSeen < oldest) mSeen = mFirst = oldest;
    for (; mSeen < count; ++mSeen) {
        const uint64_t t = des_frame_at_us(mSeen);
        if (t == 0) {  // (gone already: start again after it)
            mFirst = mSeen + 1;
            continue;
        }
        mAt[mSeen & (kKept - 1)] = t;
    }
    if (mSeen - mFirst > kKept) mFirst = mSeen - kKept;
}

void Counter::update_text(Mode m, uint64_t nowUs) {
    char text[48];
    const bool any = mSeen >= mFirst + 2;  // (a frame's time needs the one before it)
    const uint64_t last = any ? at(mSeen - 1) : 0;
    // The frame rate: the frames of the last second, by their own times; none for a while is 0.
    int fps = 0;
    if (any && nowUs >= last && nowUs - last < kStaleUs) {
        uint64_t k = mSeen - 1;
        while (k > mFirst && last - at(k - 1) <= kRateUs) --k;
        const uint64_t frames = mSeen - 1 - k;
        if (frames > 0 && last > at(k)) fps = int(std::lround(double(frames) * 1e6 / double(last - at(k))));
    }
    if (fps != mShownFps) {
        mShownFps = fps;
        std::snprintf(text, sizeof text, "%d", fps);
        set_text_content(mFps, text);
    }
    if (m != Mode::Times) return;

    // The frame times of the last 10 s: the 1% low (the frame rate of the slowest 1% of them, from their mean time)
    // and the median, a typical frame.
    size_t n = 0;
    if (any)
        for (uint64_t k = mSeen - 1; k > mFirst && last - at(k) < kLowUs && n < mScratch.size(); --k)
            mScratch[n++] = float(at(k) - at(k - 1)) / 1000.f;
    int low = 0;
    float median = 0.f;
    if (n > 0) {
        const size_t worst = std::max<size_t>(1, n / 100);
        const auto begin = mScratch.begin(), end = mScratch.begin() + ptrdiff_t(n);
        std::nth_element(begin, begin + ptrdiff_t(worst - 1), end, std::greater<float>());
        double sum = 0.0;
        for (size_t i = 0; i < worst; i++) sum += mScratch[i];
        if (sum > 0.0) low = int(std::lround(1000.0 * double(worst) / sum));
        std::nth_element(begin + ptrdiff_t(worst - 1), begin + ptrdiff_t(n / 2), end, std::greater<float>());
        median = mScratch[n / 2];
    }
    if (low != mShownLow) {
        mShownLow = low;
        std::snprintf(text, sizeof text, "1%% low %d", low);
        set_text_content(mLow, text);
    }

    // The line: the frame time the frame rate setting aims at. Unlocked, a typical frame's (the median, moved only
    // when it changes by a tenth).
    const int target = des_frame_target_mhz();
    float lineMs = 0.f;
    if (target > 0) {
        lineMs = 1.0e6f / float(target);
    } else if (median > 0.f) {
        const float held = mTopMs * 0.5f;
        lineMs = held > 0.f && std::fabs(median - held) < held * 0.1f ? held : median;
    }
    const int shown = target > 0 ? target : -int(std::lround(lineMs * 1000.f));  // (what the scale's text says)
    if (shown != mShownTarget || lineMs * 2.f != mTopMs) {
        mShownTarget = shown;
        mTopMs = lineMs * 2.f;
        if (lineMs > 0.f)
            std::snprintf(text, sizeof text, "%.1f ms", double(lineMs));
        else
            text[0] = 0;
        set_text_content(mScale, text);
        mGraphed = 0;  // (the graph again, at its new height)
    }
}

// The columns: the last kColumns frames' times, the newest at the right edge. (A frame a column, not a time: one
// column over a stretch of time would have to show the longest of its frames, and at a high frame rate the longest of
// several is always over the line.)
void Counter::update_graph() {
    mGraphed = mSeen;
    if (mGraph == nullptr) return;
    std::array<float, kColumns> ms{};
    if (mTopMs > 0.f) {
        uint64_t k = mSeen;
        for (int c = kColumns - 1; c >= 0 && k > mFirst + 1; c--, k--)
            ms[size_t(c)] = float(at(k - 1) - at(k - 2)) / 1000.f;
    }
    mGraph->set(ms, mTopMs, des_frame_target_mhz() > 0);
}

}  // namespace ui::perf
