// The start screen's sword (see start_sword.hpp): its looks, as motions.
#include "start_sword.hpp"

#include "souls_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>

namespace ui::start_sword {
namespace {

constexpr Look kLooks[] = {
    {"planted", "Planted behind the choices", Place::Window, false},
    {"beside", "Planted beside the title, with the soul flame", Place::Title, true},
    {"crest", "Across the logo", Place::Logo, false},
    {"rising", "Rising out of the smoke", Place::Window, false},
    {"drift", "Drifting in the smoke", Place::Window, false},
    {"joining",
     "Soulbrandt and Demonbrandt circle in the smoke and join into the Northern Regalia",  // (the game's sword)
     Place::Window, false, {"soulbrandt", "demonbrandt", "regalia"}},
    {"guardians", "One each side of the menu", Place::Window, false},
    {"column", "Down the middle, faint, with the soul flame", Place::Column, true},
    {"crossed", "Two crossed behind the choices, a black flame and a white one", Place::Menu, true,
     {"soulbrandt", "demonbrandt"}, {"black", "white"}},
};

// The column's look (Place::Column, prelaunch.rcss's #sword-column): the top of its element is room for the flame to
// rise into; the sword is in the rest, kColumnPad of it clear above and below.
constexpr double kColumnTop = 0.08;
constexpr double kColumnPad = 0.07;

// The crossed swords' box (Place::Menu, prelaunch.rcss's #sword-menu, 680 x 400 dp): the choices' middle at kMenuCross of its height, where the swords
// cross; their tips kMenuTips of its height above it (the choices' top and 5 dp, with six choices, 40 dp each). The
// rest is room for their halo and flames, which fade out before its edges (flame_shader.hpp).
constexpr double kMenuCross = 145.0 / 400.0;
constexpr double kMenuTips = 130.0 / 400.0;

// Beside the title (Place::Title, prelaunch.rcss's #sword-title: 160 x 276 dp beside a title 156 dp high, all of it
// smaller alike for a smaller title): the sword stands point down, as tall as the title (kTitleLength), its pommel level
// with the title's top (kTitlePommel down) and its point with its foot. The top kTitleTop is room for the flame's
// tongues; the map takes the rest, 50 dp of it above the pommel, as the logo's has room above its letters: a map cut
// off at the pommel cuts its halo off there, and the flame shows that edge as bands of light.
constexpr double kTitleTop = 30.0 / 276.0;
constexpr double kTitlePommel = 80.0 / 276.0;
constexpr double kTitleLength = 156.0 / 276.0;

// The room for the flame at the top of a look's element (a fraction of its height): the column's and the title's; the
// crossed swords' element has its own room above them (prelaunch.rcss), all of it in the map.
double flame_top(int look) {
    switch (kLooks[look].place) {
    case Place::Column: return kColumnTop;
    case Place::Title: return kTitleTop;
    default: return 0.0;
    }
}

// When a look with the flame is at rest, as its flame's map is made: the sword beside the title once it has struck and
// settled, its rocking at its middle; the others from their start.
double rest_time(int look) {
    return kLooks[look].place == Place::Title ? 10.0 : 0.0;
}

constexpr double kFall = 0.42;  // (the falling looks' fall, in seconds)

constexpr double kPi = 3.14159265358979323846;

struct M {
    float m[9];
};

M mul(const M& a, const M& b) {
    M r{};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++) r.m[i * 3 + j] += a.m[i * 3 + k] * b.m[k * 3 + j];
    return r;
}

M about_y(double deg) {
    const float c = float(std::cos(deg * kPi / 180.0)), s = float(std::sin(deg * kPi / 180.0));
    return {{c, 0, s, 0, 1, 0, -s, 0, c}};
}

M about_z(double deg) {
    const float c = float(std::cos(deg * kPi / 180.0)), s = float(std::sin(deg * kPi / 180.0));
    return {{c, -s, 0, s, c, 0, 0, 0, 1}};
}

// The sword's turn: `spin` degrees about its blade (28 puts its flat toward the light, as the cursor has it), turned
// over (the models have +Y up, the screen y down), its tip at `point` degrees (0 down the screen, 180 up, -90 to the
// right, -135 to the top right), then `yaw` degrees about the screen's upright (90: seen end on).
void turn(double spin, double point, double yaw, float out[9]) {
    static const M kOver = {{1, 0, 0, 0, -1, 0, 0, 0, -1}};
    const M r = mul(about_y(yaw), mul(about_z(point), mul(kOver, about_y(spin))));
    std::memcpy(out, r.m, sizeof r.m);
}

double clamp01(double x) {
    return std::clamp(x, 0.0, 1.0);
}

double ease_out(double x) {
    const double f = 1.0 - clamp01(x);
    return 1.0 - f * f * f;
}

// A slow swing between -1 and 1, once in `period` seconds.
double wave(double t, double period, double phase = 0.0) {
    return std::sin(2.0 * kPi * (t / period + phase));
}

void place(Pose& p, double x, double y, double length) {
    p.at[0] = float(x);
    p.at[1] = float(y);
    p.length = float(length);
}

}  // namespace

std::span<const Look> looks() {
    return kLooks;
}

int find(const std::string& id) {
    for (int i = 0; i < int(std::size(kLooks)); i++)
        if (id == kLooks[i].id) return i;
    return -1;
}

Shape shape(const ui::models::Model& m) {
    Shape s{};
    ui::models::bounds(m, s.lo, s.hi);
    // The blade: the vertices in the lower 40% of the length (toward the tip, at -Y), the middle of their x and z.
    const float below = s.lo[1] + 0.4f * (s.hi[1] - s.lo[1]);
    float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
    for (const ui::models::Mesh& mesh : ui::models::meshes(m))
        for (const ui::models::Vertex& v : mesh.vertices) {
            if (v.pos[1] > below) continue;
            lo[0] = std::min(lo[0], v.pos[0]), hi[0] = std::max(hi[0], v.pos[0]);
            lo[1] = std::min(lo[1], v.pos[2]), hi[1] = std::max(hi[1], v.pos[2]);
        }
    for (int k = 0; k < 2; k++) s.axis[k] = lo[k] <= hi[k] ? 0.5f * (lo[k] + hi[k]) : 0.5f * (s.lo[k * 2] + s.hi[k * 2]);
    return s;
}

int poses(int look, double t, const float box[4], const Shape shapes[kPoses], Pose out[kPoses], bool solid) {
    if (look < 0 || look >= int(std::size(kLooks)) || !(box[2] > 0.f) || !(box[3] > 0.f)) return 0;
    const double x = box[0], y = box[1], w = box[2], h = box[3];
    t = std::max(t, 0.0);
    for (int i = 0; i < kPoses; i++) {
        out[i] = Pose{};
        out[i].opacity = 1.f;
        out[i].glow = 1.f;
        out[i].bright = 1.f;
        for (int k = 0; k < 3; k++) out[i].pivot[k] = 0.5f * (shapes[i].lo[k] + shapes[i].hi[k]);  // (its middle)
    }
    const Shape& shape = shapes[0];
    Pose& p = out[0];
    // How much of a faint look shows once it is in: its own, or all of it when drawn solid.
    const auto faint = [solid](double v) { return solid ? 1.0 : v; };
    switch (look) {
    case 0: {  // Planted: falls point first from above the window in 0.42 s, strikes with a shake and a flare of its
               // glow, then stands, rocking a little on its blade so the light runs over its flat.
        const double fall = 0.42, length = 0.92 * h, rest = y + 0.53 * h, from = y - 0.6 * h;
        double cx = x + 0.5 * w, cy = rest;
        double glow = 0.85 + 0.15 * wave(t, 6.0);
        if (t < fall) {
            const double f = t / fall;
            cy = from + (rest - from) * f * f;  // (falling: faster and faster)
        } else {
            const double k = t - fall;
            cx += 0.006 * h * std::sin(2.0 * kPi * k / 0.07) * std::exp(-10.0 * k);
            glow += 1.8 * std::exp(-5.0 * k);
        }
        turn(28.0 + 14.0 * wave(t, 10.0), 0.0, 0.0, p.rotation);
        place(p, cx, cy, length);
        p.glow = float(glow);
        return 1;
    }
    case 1: {  // Beside the title: the planted one's motion (look 1), smaller. It falls point first from above the window
               // in kFall seconds, strikes with a shake and a flare of its glow, and stands point down, rocking a little
               // on its blade, at the title's right and as tall as it (prelaunch.rcss's #sword-title), its blade on the
               // element's middle line. The soul flame rises off it once it has struck (flame_in).
        const double length = kTitleLength * h, rest = y + (kTitlePommel + 0.5 * kTitleLength) * h;
        const double from = -0.6 * length;  // (its middle above the window's top: all of it out of sight)
        double cx = x + 0.5 * w, cy = rest;
        double glow = 0.85 + 0.15 * wave(t, 6.0);
        if (t < kFall) {
            const double f = t / kFall;
            cy = from + (rest - from) * f * f;  // (falling: faster and faster)
        } else {
            const double k = t - kFall;
            cx += 0.0065 * length * std::sin(2.0 * kPi * k / 0.07) * std::exp(-10.0 * k);
            glow += 1.8 * std::exp(-5.0 * k);
        }
        turn(28.0 + 14.0 * wave(t, 10.0), 0.0, 0.0, p.rotation);
        place(p, cx, cy, length);
        p.pivot[0] = shape.axis[0];
        p.pivot[2] = shape.axis[1];
        p.glow = float(glow);
        return 1;
    }
    case 2: {  // Across the logo: lying flat behind the letters, the tip to the right, half again as wide as the logo.
               // It swings round from end on in 0.9 s, then sways and rocks slowly.
        const double in = ease_out(t / 0.9);
        const double yaw = 88.0 * (1.0 - in) + 7.0 * wave(t, 13.0) * in;
        turn(28.0 + 20.0 * wave(t, 9.0), -90.0, yaw, p.rotation);
        place(p, x + 0.5 * w, y + 0.56 * h, 1.55 * w);
        p.opacity = float(ease_out(t / 0.35));
        return 1;
    }
    case 3: {  // Rising out of the smoke: point up, from below the window in 2.6 s until its tip is behind the logo,
               // then floating there.
        const double length = 0.66 * h, rest = y + 0.63 * h, from = y + h + 0.5 * length + 0.02 * h;
        const double f = ease_out(t / 2.6);
        turn(28.0 + 10.0 * wave(t, 11.0), 180.0, 0.0, p.rotation);
        place(p, x + 0.5 * w, from + (rest - from) * f + 0.006 * h * wave(t, 7.0) * f, length);
        p.opacity = 0.92f;
        p.glow = float(0.6 + 0.4 * f);
        return 1;
    }
    case 4: {  // Drifting in the smoke: far off and faint, larger than the window, across it from the bottom left to
               // the top right, drifting and turning very slowly.
        turn(28.0 + 8.0 * t, -135.0 + 4.0 * wave(t, 23.0), 0.0, p.rotation);
        place(p, x + 0.5 * w + 0.04 * w * wave(t, 31.0), y + 0.55 * h + 0.02 * h * wave(t, 19.0, 0.25), 1.35 * h);
        p.opacity = float(faint(0.32) * ease_out(t / 1.5));
        p.glow = 0.6f;
        return 1;
    }
    case 5: {  // Joining: soulbrandt and Demonbrandt as the drifting one (key 5, case 4: as large, at its angle, turning on
               // their blades as it does), from just outside the window at its left and right. They come in fading up
               // to its faintness and circle each other once, closer and closer, until they meet in kJoin seconds. The
               // Northern Regalia is the two of them on top of each other (in the game's models), so both share one
               // model space: the same scale and the same pivot (their joint box's middle), and one depth, so each
               // hides the other where it is in front. Two whole blades in one place go through each other, though,
               // so as they meet they fade into the game's own Regalia model (the two made one, its outline theirs),
               // in the same place, with a pulse of its glow, and it drifts on as the drifting one does.
        constexpr double kJoin = 10.0;
        const double p = clamp01(t / kJoin);
        // In the Regalia, Demonbrandt is turned half round on its blade from soulbrandt (each one's guard reaches out
        // to -x in its own model, the Regalia's to both sides): its x and z negated. Their joint box, in soulbrandt's.
        float lo[3], hi[3];
        for (int k = 0; k < 3; k++) {
            const float dlo = k == 1 ? shapes[1].lo[k] : -shapes[1].hi[k], dhi = k == 1 ? shapes[1].hi[k] : -shapes[1].lo[k];
            lo[k] = std::min(shapes[0].lo[k], dlo);
            hi[k] = std::max(shapes[0].hi[k], dhi);
        }
        const double along = hi[1] - lo[1], length = 1.35 * h;
        // The middle they circle: where the drifting one is.
        const double mx = x + 0.5 * w + 0.04 * w * wave(t, 31.0), my = y + 0.55 * h + 0.02 * h * wave(t, 19.0, 0.25);
        // Apart by this much at first, each just outside the window (half the span across of one is 0.48 h); closing
        // slower as they near, and circling faster.
        const double r = (0.5 * w + 0.5 * h) * std::pow(1.0 - p, 1.5);
        const double angle = kPi * (1.0 + 2.0 * p * p);  // (one turn; soulbrandt starts at the left)
        const double pulse = std::exp(-0.5 * (t - kJoin) * (t - kJoin) / 0.64);
        const double meet = clamp01((t - (kJoin - 1.1)) / 1.3), into = meet * meet * (3.0 - 2.0 * meet);
        for (int i = 0; i < 3; i++) {
            turn(28.0 + 8.0 * t + (i == 1 ? 180.0 : 0.0), -135.0 + 4.0 * wave(t, 23.0), 0.0, out[i].rotation);
            const double side = i == 0 ? 1.0 : -1.0;  // (Demonbrandt across the middle from soulbrandt)
            // The joint box's middle: in Demonbrandt's own model, as it is turned, x and z negated.
            for (int k = 0; k < 3; k++) out[i].pivot[k] = (i == 1 && k != 1 ? -0.5f : 0.5f) * (lo[k] + hi[k]);
            const double own = shapes[i].hi[1] - shapes[i].lo[1];  // (the renderer scales by length over this)
            const double off = i == 2 ? 0.0 : r;  // (the Regalia: where they meet)
            place(out[i], mx + side * off * std::cos(angle), my + side * 0.6 * off * std::sin(angle),
                  along > 0.0 ? length * own / along : length);
            out[i].opacity = float(faint(0.32) * (i == 2 ? into : ease_out(t / 3.0) * (1.0 - into)));
            out[i].glow = float(0.6 + 0.9 * pulse);
            out[i].apart = i == 2;
            // (the Regalia's brightness for the cursor, 1.7, burns its pale side white this large; 1.2)
            out[i].bright = i == 2 ? 0.7f : 1.f;
        }
        return 3;
    }
    case 6: {  // One each side of the menu: two of it, upright and point down, turning opposite ways, once in 30 s.
        for (int i = 0; i < 2; i++) {
            const double side = i == 0 ? 1.0 : -1.0;
            turn(side * (28.0 + 12.0 * t), 0.0, 0.0, out[i].rotation);
            place(out[i], x + (i == 0 ? 0.15 : 0.85) * w, y + 0.53 * h + 0.008 * h * wave(t, 6.0, 0.5 * i), 0.8 * h);
            out[i].opacity = float(ease_out(t / 0.9));
        }
        return 2;
    }
    case 7: {  // Down the middle: upright, point down, its blade on the column's middle line, still (the column floats,
               // prelaunch.rcss), faint as the drifting one, its glow breathing slowly. The soul flame rises off it
               // (flame_map: it must not move in its box, as the map is made once).
        const double top = y + kColumnTop * h, span = (1.0 - kColumnTop) * h;
        turn(28.0, 0.0, 0.0, p.rotation);
        place(p, x + 0.5 * w, top + 0.5 * span, (1.0 - 2.0 * kColumnPad) * span);
        p.pivot[0] = shape.axis[0];
        p.pivot[2] = shape.axis[1];
        p.opacity = float(faint(0.32 + 0.04 * wave(t, 7.0)));
        p.glow = float(0.6 + 0.15 * wave(t, 7.0));
        return 1;
    }
    case 8: {  // Crossed behind the choices: soulbrandt's hilt at the bottom left, Demonbrandt's at the bottom right,
               // points up, 55 degrees off upright, crossing a little above their middles on the choices' middle
               // (prelaunch.rcss's #sword-menu, a box centred on them). Their tips stop just above the
               // first choice, short of the logo. Each still; the element floats. soulbrandt turned over (spin 208),
               // so the spike of its guard is on its left, as Demonbrandt's is on its right. No glow of their own:
               // their flames (flame_map) are the light.
        const double tilt = 55.0 * kPi / 180.0, from_tip = 0.42;
        const double cx = x + 0.5 * w, cy = y + kMenuCross * h;
        const double length = kMenuTips * h / (from_tip * std::cos(tilt));
        for (int i = 0; i < 2; i++) {
            const double side = i == 0 ? 1.0 : -1.0;  // the tip's side: soulbrandt's to the right, Demonbrandt's left
            turn(i == 0 ? 208.0 : -28.0, 180.0 + side * 55.0, 0.0, out[i].rotation);
            // Its middle, (0.5 - from_tip) of its length from the crossing toward its hilt.
            const double back = (0.5 - from_tip) * length;
            place(out[i], cx - side * back * std::sin(tilt), cy + back * std::cos(tilt), length);
            out[i].pivot[0] = shapes[i].axis[0];
            out[i].pivot[2] = shapes[i].axis[1];
            out[i].opacity = float(faint(0.8 + 0.04 * wave(t, 7.0, 0.5 * i)));
            out[i].glow = 0.f;
            out[i].apart = i == 1;  // (Demonbrandt whole over soulbrandt where they cross: in one depth, their blades
                                    // would go through each other there)
        }
        return 2;
    }
    }
    return 0;
}

std::string model_of(int look, int which, const std::string& fallback) {
    if (look < 0 || look >= int(std::size(kLooks)) || which < 0 || which >= kPoses || !kLooks[look].models[which])
        return fallback;
    return kLooks[look].models[which];
}

namespace {

// A Gaussian blur of a w x h plane (outside it is 0), across and then down.
void blur(std::vector<float>& a, int w, int h, float sigma) {
    if (!(sigma > 0.3f)) return;
    const int r = int(std::ceil(3.f * sigma));
    std::vector<float> k(size_t(2 * r + 1));
    float sum = 0.f;
    for (int i = -r; i <= r; i++) sum += k[size_t(i + r)] = std::exp(-0.5f * float(i * i) / (sigma * sigma));
    for (float& v : k) v /= sum;
    std::vector<float> b(a.size());
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float s = 0.f;
            for (int i = std::max(-r, -x); i <= std::min(r, w - 1 - x); i++) s += k[size_t(i + r)] * a[size_t(y) * w + x + i];
            b[size_t(y) * w + x] = s;
        }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float s = 0.f;
            for (int i = std::max(-r, -y); i <= std::min(r, h - 1 - y); i++) s += k[size_t(i + r)] * b[size_t(y + i) * w + x];
            a[size_t(y) * w + x] = s;
        }
}

}  // namespace

bool flame_map(int look, const std::string& sword, float width, float height, float dp, FlameMap& out) {
    if (look < 0 || look >= int(std::size(kLooks)) || !kLooks[look].flame || !(width >= 2.f) || !(height >= 2.f) ||
        !(dp > 0.f))
        return false;
    // Its pose: the one whose model the sword is (the first, for a look of one sword); and each pose's model's shape.
    const int which = model_of(look, 1, sword) == sword && model_of(look, 0, sword) != sword ? 1 : 0;
    const ui::models::Model* models[kPoses];
    for (int i = 0; i < kPoses; i++) models[i] = ui::models::get(model_of(look, i, sword));
    if (!models[which]) return false;
    const ui::models::Model& m = *models[which];
    Shape shapes[kPoses];
    for (int i = 0; i < kPoses; i++) shapes[i] = shape(models[i] ? *models[i] : m);
    const Shape& s = shapes[which];
    // At half the element's size (as the logo's map), less the room above; never more than 768 rows.
    const double top = flame_top(look);
    const float k = std::min(0.5f, 768.f / (height * float(1.0 - top)));
    const int w = std::max(1, int(std::lround(width * k)));
    const int h = std::max(1, int(std::lround(height * float(1.0 - top) * k)));
    // The sword as the look places it, in the map's pixels: the element's box, its top above the map's.
    const float box[4] = {0.f, -float(top) * height * k, width * k, height * k};
    Pose poses_[kPoses];
    if (poses(look, rest_time(look), box, shapes, poses_) <= which) return false;
    const Pose* p = &poses_[which];
    const float along = s.hi[1] - s.lo[1];
    if (!(along > 0.f)) return false;
    const float scale = p[0].length / along;
    const float* R = p[0].rotation;

    // The outline: each triangle filled (its pixels' middles inside it).
    std::vector<float> fill(size_t(w) * size_t(h), 0.f);
    for (const ui::models::Mesh& mesh : ui::models::meshes(m)) {
        std::vector<std::array<float, 2>> at(mesh.vertices.size());
        for (size_t i = 0; i < mesh.vertices.size(); i++) {
            const float* v = mesh.vertices[i].pos;
            const float d[3] = {v[0] - p[0].pivot[0], v[1] - p[0].pivot[1], v[2] - p[0].pivot[2]};
            for (int r = 0; r < 2; r++)
                at[i][size_t(r)] = (R[r * 3] * d[0] + R[r * 3 + 1] * d[1] + R[r * 3 + 2] * d[2]) * scale + p[0].at[r];
        }
        for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const auto& a = at[mesh.indices[t]];
            const auto& b = at[mesh.indices[t + 1]];
            const auto& c = at[mesh.indices[t + 2]];
            const float area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
            if (std::fabs(area) < 1e-6f) continue;
            const int x0 = std::max(0, int(std::floor(std::min({a[0], b[0], c[0]}))));
            const int x1 = std::min(w - 1, int(std::ceil(std::max({a[0], b[0], c[0]}))));
            const int y0 = std::max(0, int(std::floor(std::min({a[1], b[1], c[1]}))));
            const int y1 = std::min(h - 1, int(std::ceil(std::max({a[1], b[1], c[1]}))));
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    const float px = float(x) + 0.5f, py = float(y) + 0.5f;
                    const float e0 = (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
                    const float e1 = (c[0] - b[0]) * (py - b[1]) - (c[1] - b[1]) * (px - b[0]);
                    const float e2 = (a[0] - c[0]) * (py - c[1]) - (a[1] - c[1]) * (px - c[0]);
                    if ((e0 >= 0.f && e1 >= 0.f && e2 >= 0.f) || (e0 <= 0.f && e1 <= 0.f && e2 <= 0.f))
                        fill[size_t(y) * w + x] = 1.f;
                }
        }
    }
    // Red: the outline softened by a pixel; blue: a halo of 40 / 3 dp (the logo's), brought up so its middle is full.
    std::vector<float> red = fill, blue = fill;
    blur(red, w, h, 1.f);
    blur(blue, w, h, 40.f / 3.f * dp * k);
    float peak = 1e-6f;
    for (float v : blue) peak = std::max(peak, v);
    out.width = w;
    out.height = h;
    out.top = float(top);
    out.rgba.assign(size_t(w) * size_t(h) * 4, 0);
    for (size_t i = 0; i < fill.size(); i++) {
        out.rgba[i * 4 + 0] = uint8_t(std::lround(255.f * std::clamp(red[i], 0.f, 1.f)));
        out.rgba[i * 4 + 2] = uint8_t(std::lround(255.f * std::clamp(blue[i] / peak * 1.6f, 0.f, 1.f)));
        out.rgba[i * 4 + 3] = 255;
    }
    return true;
}

float flame_in(int look, double t) {
    if (look < 0 || look >= int(std::size(kLooks)) || kLooks[look].place != Place::Title) return 1.f;
    const double f = clamp01((t - kFall) / 1.2);
    return float(f * f * (3.0 - 2.0 * f));
}

}  // namespace ui::start_sword
