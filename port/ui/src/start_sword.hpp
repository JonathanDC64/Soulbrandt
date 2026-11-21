// The start screen's sword (a prototype): one of the game's swords, large and moving, behind the logo and the choices.
// Each look is a motion: where the sword is, how it is turned and how strongly it shows, at a time since the look
// began. prelaunch.cpp picks the look (keys 1 to 9, Left / Right, L1 / R1); render_gpu.cpp draws it, from the
// decorator shader("model:<sword>@<look>") on an element that covers the window, the logo, or a column down the
// middle. The column's look has the logo's soul flame on it too: shader("flame:logo@<sword>@<look>"), whose map
// (flame_map) is made from the sword as the look places it.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ui::models {
struct Model;
}

namespace ui::start_sword {

// The element whose box the look takes (Menu: behind the choices; Title: beside the title, at its right).
enum class Place { Window, Logo, Column, Menu, Title };

// The most swords a look draws at once (its poses).
inline constexpr int kPoses = 3;

struct Look {
    const char* id;
    const char* label;
    Place place;
    bool flame;  // the soul flame rises off it (Place::Column, Place::Menu)
    // Its swords by model name, a pose each (null: the decorator's, "model:<sword>@<look>"), and the first two's
    // flames' looks (flame_shader.hpp; null: "logo").
    const char* models[kPoses] = {nullptr, nullptr, nullptr};
    const char* flames[2] = {nullptr, nullptr};
};

// The looks, in the order the keys pick them (1 is the first).
std::span<const Look> looks();

// The look by its id, or -1.
int find(const std::string& id);

// What a look needs of the model: its box, and the line of its blade (x and z, the middle of the blade's lower part),
// which the column's look puts on the window's middle.
struct Shape {
    float lo[3], hi[3];
    float axis[2];
};
Shape shape(const ui::models::Model& m);

// One sword in a look at one moment: its turn (model to screen, the blade along the model's -Y), the model's point
// that goes to `at` on the screen (pixels), its length in pixels, and how much of it and of its glow shows (0 to 1, and
// a factor of the sword's glow).
struct Pose {
    float rotation[9];
    float pivot[3];
    float at[2];
    float length;
    float opacity;
    float glow;
    bool apart;  // drawn over the others with a depth of its own (else they hide each other where one is in front)
    float bright;  // its brightness, times the sword's own (souls_model.cpp's kSwords: the cursor's)
};

// The poses of look `look` at `t` seconds since it began, in a box of pixels {left, top, width, height}, each pose's
// model's shape in shapes: how many (1 to kPoses), written to out; one with an opacity of 0 isn't drawn. 0 for a look
// that is not there. `solid`: a faint look is drawn whole instead (it still fades in as it would).
int poses(int look, double t, const float box[4], const Shape shapes[kPoses], Pose out[kPoses], bool solid = false);

// The soul flame's map for a look with the flame (flame_shader.hpp reads it as it reads the logo's): red the sword's
// outline softened, blue a wide halo round it, over the element less its top part (`top`, a fraction of its height),
// which the flame rises into. Made for an element of `width` x `height` pixels, `dp` pixels to the dp, at half that
// size, from the sword `sword` as the look places it (it doesn't move in the element): the pose whose model it is, or
// the first (the decorator's sword, for a look of one). The models are read by name (ui::models::get). False if the look
// has no flame.
struct FlameMap {
    std::vector<uint8_t> rgba;
    int width = 0, height = 0;
    float top = 0.f;
};
bool flame_map(int look, const std::string& sword, float width, float height, float dp, FlameMap& out);

// How much of a look's flame shows `t` seconds after the look began, 0 to 1: all of it, but for a sword that falls in,
// whose flame (its map made where the sword comes to rest) rises once it has struck.
float flame_in(int look, double t);

// The model of pose `which` of a look: its own, or `fallback` (the decorator's).
std::string model_of(int look, int which, const std::string& fallback);

}  // namespace ui::start_sword
