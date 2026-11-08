// Written for Demon's Souls. The button page read from the player's font files and drawn again (button_pages.hpp).
#include "button_pages.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

// stb_truetype's code, static to this file: the rest of the UI draws its text with FreeType. Its functions this file
// does not call are not warnings.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace ui::button_pages {

const char* const kKeyTargetNames[kKeyTargets] = {
    "triangle", "square", "circle", "cross", "select", "start", "r1", "r2", "l1", "l2", "r3", "l3",
    "up", "down", "left", "right", "ls_up", "ls_down", "ls_left", "ls_right"};

const char* const kPadNames[16] = {"select", "l3", "r3", "start", "up", "right", "down", "left",
                                   "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square"};

namespace {

// kPadNames indexes.
enum Pad { kPSelect, kPL3, kPR3, kPStart, kPUp, kPRight, kPDown, kPLeft, kPL2, kPR2, kPL1, kPR1,
           kPTriangle, kPCircle, kPCross, kPSquare };

// Big-endian reads; past the end reads 0 and sets bad.
struct Be {
    const std::vector<uint8_t>& d;
    bool bad = false;
    uint32_t u8(size_t o) {
        if (o >= d.size()) {
            bad = true;
            return 0;
        }
        return d[o];
    }
    uint32_t u16(size_t o) { return u8(o) << 8 | u8(o + 1); }
    uint32_t u32(size_t o) { return u16(o) << 16 | u16(o + 2); }
    float f32(size_t o) {
        const uint32_t v = u32(o);
        float f;
        std::memcpy(&f, &v, sizeof f);
        return f;
    }
};

struct CharRole {
    uint32_t code;
    Role role;
};
// The button characters (the game's file 90, docs/125). U+246C, the copyright sign the title screen uses, is left as
// the game drew it, as is everything else on the page.
constexpr CharRole kChars[] = {
    {0x2460, kTriangle}, {0x2461, kSquare}, {0x2462, kCircle}, {0x2463, kCross},  {0x2464, kDpad},
    {0x2465, kSelect},   {0x2466, kR1},     {0x2467, kR2},     {0x2468, kL1},     {0x2469, kL2},
    {0x246A, kR3},       {0x246B, kL3},     {0x246D, kDpadLR}, {0x246E, kDpadUD}, {0x246F, kStart},
    {0x2470, kRStick},   {0x2471, kLStick}, {0x32A4, kUp},     {0x32A6, kDown},   {0x32A7, kLeft},
    {0x32A8, kRight},
};

// The button sprites of the menu's layout (menu/menu.drb draws them from menu/menu.tpf; docs/125): the texture by
// name and size, and the cell. The US and European discs' textures are the same. The layout's other button pictures
// are the hands' "L" and "R" (words, not buttons) and the embossed L2 and R2 of one key guide, kept as they are.
struct Sprite {
    const char* texture;
    uint32_t width, height;
    Cell cell;
};
const Sprite kSprites[] = {
    {"Menu08", 1024, 512, {kSquare, 79, 426, 19, 19, true}},    // the HUD's item slot (F20-00, FE_key_Square)
    {"Menu00", 512, 512, {kSquare, 160, 0, 32, 32, false}},     // the HUD's magic slot (F20-00, key)
    {"Menu00", 512, 512, {kCircle, 160, 32, 32, 32, false}},    // the message window (F10-00 and F10-01, key_circle)
    // Beside them, never shown: Triangle and Cross the same size, for the two above when PAD_MAP moves a button.
    {"Menu00", 512, 512, {kTriangle, 192, 0, 32, 32, false, true}},
    {"Menu00", 512, 512, {kCross, 192, 32, 32, 32, false, true}},
};

// The texture pack, big-endian: "TPF\0", the count at 8, then 32 bytes a texture (data offset, size, format, cube,
// ..., width and height at 12, its name's offset at 24). The count; 0 for a file that is not one.
uint32_t tpf_count(const std::vector<uint8_t>& tpf) {
    if (tpf.size() < 0x10 || std::memcmp(tpf.data(), "TPF", 4) != 0) return 0;
    Be t{tpf};
    return std::min<uint32_t>(t.u32(8), 4096);
}

// Texture i into p (its format, shape, place and bytes) and its name; false for one the renderer would not upload
// as DXT, a cube, or one past the file's end.
bool tpf_page(const std::vector<uint8_t>& tpf, uint32_t i, Page& p, std::string& name) {
    Be t{tpf};
    const size_t e = 0x10 + size_t(i) * 0x20;
    const uint32_t off = t.u32(e), size = t.u32(e + 4), format = t.u8(e + 8), cube = t.u8(e + 9);
    const uint32_t w = t.u16(e + 12), h = t.u16(e + 14), at = t.u32(e + 24);
    if (t.bad) return false;
    name.clear();
    for (size_t k = at; k < tpf.size() && tpf[k] && name.size() < 64; k++) name += char(tpf[k]);
    uint32_t block = 0;
    if (format == 5) {
        p.fmt = 0x88;
        block = 16;
    } else if (format == 3) {
        p.fmt = 0x87;
        block = 16;
    } else if (format == 0 || format == 1) {
        p.fmt = 0x86;
        block = 8;
    }
    if (!block || cube || !w || !h || size_t(off) + size > tpf.size()) return false;
    p.level0 = ((w + 3) / 4) * ((h + 3) / 4) * block;
    if (size < p.level0) return false;
    p.index = int(i);
    p.width = w;
    p.height = h;
    p.data.assign(tpf.begin() + off, tpf.begin() + off + size);
    return true;
}

// ---- DXT ----

void rgb565(uint32_t v, int out[4]) {
    out[0] = int((v >> 11) & 31) * 255 / 31;
    out[1] = int((v >> 5) & 63) * 255 / 63;
    out[2] = int(v & 31) * 255 / 31;
    out[3] = 255;
}

// A 4x4 block's colour part: two 16-bit colours (little-endian), then sixteen 2-bit picks.
void color_block(const uint8_t* b, bool dxt1, uint8_t px[16][4]) {
    const uint32_t c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8;
    int p[4][4];
    rgb565(c0, p[0]);
    rgb565(c1, p[1]);
    for (int k = 0; k < 4; k++) {
        if (c0 > c1 || !dxt1) {
            p[2][k] = (2 * p[0][k] + p[1][k]) / 3;
            p[3][k] = (p[0][k] + 2 * p[1][k]) / 3;
        } else {
            p[2][k] = (p[0][k] + p[1][k]) / 2;
            p[3][k] = 0;   // transparent black
        }
    }
    const uint32_t picks = b[4] | b[5] << 8 | b[6] << 16 | uint32_t(b[7]) << 24;
    for (int i = 0; i < 16; i++) {
        const int k = (picks >> (2 * i)) & 3;
        for (int ch = 0; ch < 4; ch++) px[i][ch] = uint8_t(p[k][ch]);
    }
}

// DXT5's alpha part: two alphas, then sixteen 3-bit picks.
void alpha_block(const uint8_t* b, uint8_t px[16][4]) {
    int a[8] = {b[0], b[1]};
    if (a[0] > a[1]) {
        for (int i = 1; i < 7; i++) a[i + 1] = ((7 - i) * a[0] + i * a[1]) / 7;
    } else {
        for (int i = 1; i < 5; i++) a[i + 1] = ((5 - i) * a[0] + i * a[1]) / 5;
        a[6] = 0;
        a[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; i++) px[i][3] = uint8_t(a[(bits >> (3 * i)) & 7]);
}

}  // namespace

std::vector<Page> read_font(const std::vector<uint8_t>& ccm, const std::vector<uint8_t>& tpf, const std::string& from) {
    std::vector<Page> out;
    // The code map, big-endian: a header, the code groups (first code, last code, its glyph: 12 bytes) and the
    // glyphs (24 bytes: the cell's corners as fractions of the page, four floats; then x offset, width, advance and
    // page, 16 bits each).
    Be c{ccm};
    if (ccm.size() < 0x20 || c.u32(0) != 0x00010000u) return out;
    const uint32_t tex_w = c.u16(0x0A), tex_h = c.u16(0x0C);
    const uint32_t ngroups = c.u16(0x10), nglyphs = c.u16(0x12);
    const uint32_t groups = c.u32(0x14), glyphs = c.u32(0x18);
    if (!tex_w || !tex_h) return out;
    struct Found {
        uint32_t page;
        Cell cell;
    };
    std::vector<Found> found;
    for (const CharRole& ch : kChars) {
        long glyph = -1;
        for (uint32_t g = 0; g < ngroups && !c.bad; g++) {
            const size_t e = groups + size_t(g) * 12;
            const uint32_t first = c.u32(e), last = c.u32(e + 4);
            if (ch.code >= first && ch.code <= last) {
                glyph = long(c.u32(e + 8) + (ch.code - first));
                break;
            }
        }
        if (glyph < 0 || uint32_t(glyph) >= nglyphs) continue;
        const size_t e = glyphs + size_t(glyph) * 24;
        const float u0 = c.f32(e), v0 = c.f32(e + 4), u1 = c.f32(e + 8), v1 = c.f32(e + 12);
        const uint32_t page = c.u16(e + 22);
        if (c.bad) return {};
        const Cell cell{ch.role, int(std::lround(u0 * float(tex_w))), int(std::lround(v0 * float(tex_h))),
                        int(std::lround((u1 - u0) * float(tex_w))), int(std::lround((v1 - v0) * float(tex_h)))};
        if (cell.w <= 0 || cell.h <= 0 || cell.x < 0 || cell.y < 0 || cell.x + cell.w > int(tex_w) ||
            cell.y + cell.h > int(tex_h))
            continue;
        found.push_back({page, cell});
    }
    if (found.empty()) return out;
    const uint32_t count = tpf_count(tpf);
    for (uint32_t i = 0; i < count; i++) {
        Page p;
        for (const Found& f : found)
            if (f.page == i) p.cells.push_back(f.cell);
        if (p.cells.empty()) continue;
        std::string name;
        if (!tpf_page(tpf, i, p, name) || p.width != tex_w || p.height != tex_h) continue;
        p.from = from;
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<Page> read_sprites(const std::vector<uint8_t>& tpf, const std::string& from) {
    std::vector<Page> out;
    const uint32_t count = tpf_count(tpf);
    for (uint32_t i = 0; i < count; i++) {
        Page p;
        std::string name;
        if (!tpf_page(tpf, i, p, name)) continue;
        for (const Sprite& s : kSprites)
            if (name == s.texture && p.width == s.width && p.height == s.height) p.cells.push_back(s.cell);
        if (std::all_of(p.cells.begin(), p.cells.end(), [](const Cell& c) { return c.spare; })) continue;
        p.from = from;
        p.max_scale = 2;   // (a 1024x512 page at 4x would be 43 MB for one 19-pixel square)
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<uint8_t> decode(const Page& p) {
    const bool dxt1 = p.fmt == 0x86, dxt3 = p.fmt == 0x87;
    if (!dxt1 && !dxt3 && p.fmt != 0x88) return {};
    const uint32_t bw = (p.width + 3) / 4, bh = (p.height + 3) / 4, bs = dxt1 ? 8 : 16;
    if (p.data.size() < size_t(bw) * bh * bs) return {};
    std::vector<uint8_t> out(size_t(p.width) * p.height * 4);
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            const uint8_t* b = p.data.data() + (size_t(by) * bw + bx) * bs;
            uint8_t px[16][4];
            color_block(dxt1 ? b : b + 8, dxt1, px);
            if (dxt3) {
                for (int i = 0; i < 16; i++) px[i][3] = uint8_t(((b[i / 2] >> (4 * (i & 1))) & 15) * 17);
            } else if (!dxt1) {
                alpha_block(b, px);
            }
            for (int i = 0; i < 16; i++) {
                const uint32_t x = bx * 4 + uint32_t(i % 4), y = by * 4 + uint32_t(i / 4);
                if (x < p.width && y < p.height) std::memcpy(&out[(size_t(y) * p.width + x) * 4], px[i], 4);
            }
        }
    return out;
}

Spec normalized(Spec s) {
    if (s.kind == Kind::Keyboard) s.pad_map = {};
    if (s.kind != Kind::Keyboard) s.keys = {};
    if (s.kind == Kind::PlayStation) s.scale = 1;   // (the game's own pictures, moved: no larger than they are)
    return s;
}

bool untouched(const Spec& s) {
    if (s.kind != Kind::PlayStation) return false;
    for (int i = 0; i < 16; i++)
        if (s.pad_map[i] != i && s.pad_map[i] >= 0 && s.pad_map[i] < 16) return false;
    return true;
}

std::string describe(const Spec& s) {
    static const char* const kNames[] = {"PlayStation", "Xbox", "Nintendo", "keyboard"};
    std::string out = kNames[int(s.kind)];
    out += " x" + std::to_string(s.scale);
    if (s.kind != Kind::Keyboard) {
        for (int i = 0; i < 16; i++)
            if (s.pad_map[i] != i && s.pad_map[i] >= 0 && s.pad_map[i] < 16)
                out += std::string(" ") + kPadNames[i] + "=" + kPadNames[s.pad_map[i]];
    }
    if (s.kind == Kind::Keyboard) {
        char text[48];
        for (int t = 0; t < kKeyTargets; t++) {
            std::snprintf(text, sizeof text, " %s=%X+%X", kKeyTargetNames[t], unsigned(s.keys[t].mod),
                          unsigned(s.keys[t].in));
            out += text;
        }
    }
    return out;
}

namespace {

// ---- Drawing ----

struct Rgba {
    float r, g, b, a;  // straight, 0 to 1
};
constexpr Rgba rgb(int r, int g, int b, float a = 1.f) { return {float(r) / 255.f, float(g) / 255.f, float(b) / 255.f, a}; }
Rgba mix(Rgba x, Rgba y, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
}

// The game's look, sampled from its own page: a dark button with a rim lit at the top and dark at the bottom and a
// soft highlight over its upper half, light grey letters on a soft shadow, yellow arrows. The pads' letters are the
// usual colours, toned to sit beside the game's teal, pink and blue.
constexpr Rgba kBase = rgb(24, 24, 24), kRimHi = rgb(132, 130, 132), kRimLo = rgb(8, 6, 8);
constexpr Rgba kGloss = rgb(110, 110, 112), kLetter = rgb(178, 176, 178), kShadow = rgb(0, 0, 0, 0.63f);
constexpr Rgba kYellow = rgb(236, 226, 112), kDim = rgb(96, 94, 96), kLine = rgb(6, 6, 6);
constexpr Rgba kXboxA = rgb(96, 178, 84), kXboxB = rgb(210, 72, 66), kXboxX = rgb(78, 132, 210);
constexpr Rgba kXboxY = rgb(226, 196, 72);

// Premultiplied RGBA, 8 bits a channel.
struct Canvas {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    void over(int x, int y, Rgba c, float cover) {
        const float a = c.a * cover;
        if (a <= 0.f || x < 0 || y < 0 || x >= w || y >= h) return;
        uint8_t* p = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
        const float k = 1.f - a;
        const float src[4] = {c.r * a, c.g * a, c.b * a, a};
        for (int ch = 0; ch < 4; ch++) p[ch] = uint8_t(std::lround(std::clamp(src[ch] * 255.f + float(p[ch]) * k, 0.f, 255.f)));
    }
};

struct Box {
    float x0, y0, x1, y1;
    float w() const { return x1 - x0; }
    float h() const { return y1 - y0; }
    float cx() const { return (x0 + x1) * 0.5f; }
    float cy() const { return (y0 + y1) * 0.5f; }
};
Box centred(float cx, float cy, float w, float h) { return {cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2}; }

// Shapes as signed distances in canvas pixels (negative inside).
float sd_circle(float x, float y, float cx, float cy, float r) { return std::hypot(x - cx, y - cy) - r; }
// A box with rounded corners: rt the top two, rb the bottom two.
float sd_rbox(float x, float y, const Box& b, float rt, float rb) {
    const float hx = b.w() / 2, hy = b.h() / 2;
    float r = y < b.cy() ? rt : rb;
    r = std::min(r, std::min(hx, hy));
    const float qx = std::fabs(x - b.cx()) - hx + r, qy = std::fabs(y - b.cy()) - hy + r;
    return std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) - r;
}
// A triangle: the largest distance to its sides' lines, which is exact at the sides, where the edge is drawn.
float sd_tri(float x, float y, const float v[3][2]) {
    const float gx = (v[0][0] + v[1][0] + v[2][0]) / 3, gy = (v[0][1] + v[1][1] + v[2][1]) / 3;
    float d = -1e9f;
    for (int i = 0; i < 3; i++) {
        const float* a = v[i];
        const float* b = v[(i + 1) % 3];
        float nx = b[1] - a[1], ny = a[0] - b[0];
        const float len = std::hypot(nx, ny);
        if (len <= 0) continue;
        nx /= len;
        ny /= len;
        if (nx * (gx - a[0]) + ny * (gy - a[1]) > 0) {
            nx = -nx;
            ny = -ny;
        }
        d = std::max(d, nx * (x - a[0]) + ny * (y - a[1]));
    }
    return d;
}

// Paints a shape over `clip`, its edge smoothed over a pixel.
template <typename Sd, typename Paint>
void fill(Canvas& cv, const Box& clip, Sd sd, Paint paint) {
    const int x0 = std::max(0, int(std::floor(clip.x0))), x1 = std::min(cv.w, int(std::ceil(clip.x1)));
    const int y0 = std::max(0, int(std::floor(clip.y0))), y1 = std::min(cv.h, int(std::ceil(clip.y1)));
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            const float fx = float(x) + 0.5f, fy = float(y) + 0.5f;
            const float cover = std::clamp(0.5f - sd(fx, fy), 0.f, 1.f);
            if (cover > 0.f) cv.over(x, y, paint(fx, fy), cover);
        }
}

struct Ctx {
    Canvas& cv;
    const stbtt_fontinfo* font;  // null: no letters
    float u;                     // one of the game's pixels on the canvas (the scale)
};

// A button the game's way. shape(x, y, inset) is the outline's distance with the outline moved in by `inset`.
template <typename Shape>
void gloss(Ctx& c, const Box& b, Shape shape) {
    const float top = b.y0, h = b.h(), u = c.u;
    fill(c.cv, b, [&](float x, float y) { return shape(x, y, 0.f); },
         [&](float, float y) { return mix(kRimHi, kRimLo, (y - top) / h); });
    fill(c.cv, b, [&](float x, float y) { return shape(x, y, u); }, [&](float, float) { return kBase; });
    fill(c.cv, b, [&](float x, float y) { return shape(x, y, 1.75f * u); },
         [&](float, float y) {
             const float t = (y - top - u) / (h * 0.5f);
             Rgba g = kGloss;
             g.a = t >= 1.f ? 0.f : 0.59f * std::pow(1.f - std::max(t, 0.f), 1.6f);
             return g;
         });
}

void disc(Ctx& c, const Box& b) {
    const float r = 0.48f * std::min(b.w(), b.h()), cx = b.cx(), cy = b.cy();
    gloss(c, b, [=](float x, float y, float in) { return sd_circle(x, y, cx, cy, r - in); });
}

void cap(Ctx& c, const Box& b, float rt, float rb) {
    gloss(c, b, [=](float x, float y, float in) {
        const Box i{b.x0 + in, b.y0 + in, b.x1 - in, b.y1 - in};
        return sd_rbox(x, y, i, std::max(rt - in, 0.f), std::max(rb - in, 0.f));
    });
}

// A triangle pointing `dir` (0 up, 1 right, 2 down, 3 left), `size` across, centred on (cx, cy).
void arrow(Ctx& c, float cx, float cy, float size, int dir, Rgba color) {
    const float s = size / 2;
    float v[3][2] = {{0, -s}, {s, s * 0.8f}, {-s, s * 0.8f}};   // up
    for (auto& p : v) {
        const float x = p[0], y = p[1];
        switch (dir & 3) {
        case 1: p[0] = -y; p[1] = x; break;
        case 2: p[0] = -x; p[1] = -y; break;
        case 3: p[0] = y; p[1] = -x; break;
        default: break;
        }
        p[0] += cx;
        p[1] += cy;
    }
    const Box clip = centred(cx, cy, size + 2, size + 2);
    fill(c.cv, clip, [&](float x, float y) { return sd_tri(x, y, v); }, [&](float, float) { return color; });
}

// Text in the box: its capitals `cap` pixels high, narrowed to fit, centred (left-aligned with left = true), on the
// game's soft shadow.
void text(Ctx& c, const Box& b, const std::string& s, float cap, Rgba color, bool left = false) {
    if (!c.font || s.empty()) return;
    const stbtt_fontinfo* f = c.font;
    int x0, y0, x1, y1;
    stbtt_GetCodepointBox(f, 'H', &x0, &y0, &x1, &y1);
    float scale = y1 > 0 ? cap / float(y1) : stbtt_ScaleForPixelHeight(f, cap * 1.4f);
    // The width at this scale, and the ink's extent, then narrower if it does not fit.
    const auto measure = [&](float sc, float& w, float& top, float& bottom) {
        w = 0;
        top = 1e9f;
        bottom = -1e9f;
        for (size_t i = 0; i < s.size(); i++) {
            const int cp = static_cast<unsigned char>(s[i]);
            int adv, lsb;
            stbtt_GetCodepointHMetrics(f, cp, &adv, &lsb);
            int bx0, by0, bx1, by1;
            stbtt_GetCodepointBitmapBox(f, cp, sc, sc, &bx0, &by0, &bx1, &by1);
            if (by1 > by0) {
                top = std::min(top, float(by0));
                bottom = std::max(bottom, float(by1));
            }
            w += float(adv) * sc;
            if (i + 1 < s.size()) w += float(stbtt_GetCodepointKernAdvance(f, cp, static_cast<unsigned char>(s[i + 1]))) * sc;
        }
    };
    float w, top, bottom;
    measure(scale, w, top, bottom);
    const float room = b.w() * (left ? 1.f : 0.84f);
    if (w > room && w > 0) {
        scale *= room / w;
        measure(scale, w, top, bottom);
    }
    if (top > bottom) return;
    const float base_y = b.cy() - (top + bottom) / 2;   // the ink centred
    const float start_x = left ? b.x0 : b.cx() - w / 2;
    for (int pass = 0; pass < 2; pass++) {
        const float dx = pass ? 0.f : 0.5f * c.u, dy = pass ? 0.f : 0.75f * c.u;
        const Rgba col = pass ? color : kShadow;
        float pen = start_x + dx;
        for (size_t i = 0; i < s.size(); i++) {
            const int cp = static_cast<unsigned char>(s[i]);
            int adv, lsb;
            stbtt_GetCodepointHMetrics(f, cp, &adv, &lsb);
            const float fx = std::floor(pen), sx = pen - fx;
            const float fy = std::floor(base_y + dy), sy = base_y + dy - fy;
            int bx0, by0, bx1, by1;
            stbtt_GetCodepointBitmapBoxSubpixel(f, cp, scale, scale, sx, sy, &bx0, &by0, &bx1, &by1);
            const int gw = bx1 - bx0, gh = by1 - by0;
            if (gw > 0 && gh > 0) {
                std::vector<unsigned char> g(size_t(gw) * size_t(gh));
                stbtt_MakeCodepointBitmapSubpixel(f, g.data(), gw, gh, gw, scale, scale, sx, sy, cp);
                for (int y = 0; y < gh; y++)
                    for (int x = 0; x < gw; x++) {
                        const unsigned char a = g[size_t(y) * size_t(gw) + size_t(x)];
                        if (a) c.cv.over(int(fx) + bx0 + x, int(fy) + by0 + y, col, float(a) / 255.f);
                    }
            }
            pen += float(adv) * scale;
            if (i + 1 < s.size()) pen += float(stbtt_GetCodepointKernAdvance(f, cp, static_cast<unsigned char>(s[i + 1]))) * scale;
        }
    }
}

// ---- The pads ----

void face(Ctx& c, const Box& b, const char* letter, Rgba color) {
    disc(c, b);
    text(c, b, letter, 0.40f * b.h(), color);
}

void shoulder(Ctx& c, const Box& b, const char* label, bool trigger) {
    const float w = 0.94f * b.w(), h = (trigger ? 0.86f : 0.70f) * b.h();
    const Box k = centred(b.cx(), b.cy(), w, h);
    cap(c, k, trigger ? 0.45f * w : 0.24f * h, 0.24f * h);
    text(c, k, label, 0.34f * b.h(), kLetter);
}

// A stick: its letter, with the four ways it moves in yellow (moving), or with no arrows (pressed in).
void stick(Ctx& c, const Box& b, const char* label, bool moving) {
    const float d = std::min(b.w(), b.h());
    const Box k = centred(b.cx(), b.cy(), d, d);
    disc(c, moving ? centred(k.cx(), k.cy(), 0.86f * d, 0.86f * d) : k);
    text(c, k, label, (moving ? 0.34f : 0.32f) * d, kLetter);
    if (moving) {
        const float a = 0.20f * d, r = 0.40f * d;
        arrow(c, k.cx(), k.cy() - r, a, 0, kYellow);
        arrow(c, k.cx() + r, k.cy(), a, 1, kYellow);
        arrow(c, k.cx(), k.cy() + r, a, 2, kYellow);
        arrow(c, k.cx() - r, k.cy(), a, 3, kYellow);
    }
}

// The D-pad, the ways in `lit` (bit 0 up, 1 right, 2 down, 3 left) in yellow.
void dpad(Ctx& c, const Box& b, unsigned lit) {
    const float d = 0.94f * std::min(b.w(), b.h()), arm = 0.36f * d, r = 0.08f * d;
    const Box hz = centred(b.cx(), b.cy(), d, arm), vt = centred(b.cx(), b.cy(), arm, d);
    gloss(c, centred(b.cx(), b.cy(), d, d), [=](float x, float y, float in) {
        const Box h{hz.x0 + in, hz.y0 + in, hz.x1 - in, hz.y1 - in}, v{vt.x0 + in, vt.y0 + in, vt.x1 - in, vt.y1 - in};
        return std::min(sd_rbox(x, y, h, r, r), sd_rbox(x, y, v, r, r));
    });
    const float a = 0.22f * d, at = 0.30f * d;
    const float dx[4] = {0, at, 0, -at}, dy[4] = {-at, 0, at, 0};
    for (int i = 0; i < 4; i++) arrow(c, b.cx() + dx[i], b.cy() + dy[i], a, i, (lit >> i) & 1 ? kYellow : kDim);
}

// Select and Start: a small button with the pad's picture on it, then the button's name when the cell is wide.
void menu_button(Ctx& c, const Box& b, Kind kind, bool start) {
    const float d = b.h();
    const Box k = {b.x0, b.y0, b.x0 + d, b.y1};
    disc(c, k);
    const float u = d / 26.f, cx = k.cx(), cy = k.cy();
    const auto bar = [&](float x, float y, float w, float h) {
        const Box r = centred(x, y, w, h);
        fill(c.cv, r, [&](float px, float py) { return sd_rbox(px, py, r, h / 2, h / 2); },
             [&](float, float) { return kLetter; });
    };
    if (kind == Kind::Nintendo) {
        bar(cx, cy, 10 * u, 2.4f * u);
        if (start) bar(cx, cy, 2.4f * u, 10 * u);
    } else if (start) {   // Xbox's Menu: three lines
        for (int i = -1; i <= 1; i++) bar(cx, cy + float(i) * 3.2f * u, 10 * u, 1.8f * u);
    } else {              // Xbox's View: two windows, one over the other
        for (int i = 0; i < 2; i++) {
            const Box r = centred(cx - 1.4f * u + float(i) * 2.8f * u, cy - 1.4f * u + float(i) * 2.8f * u, 7 * u, 6 * u);
            fill(c.cv, r, [&](float px, float py) { return std::fabs(sd_rbox(px, py, r, 1.2f * u, 1.2f * u) + 0.8f * u) - 0.8f * u; },
                 [&](float, float) { return kLetter; });
        }
    }
    if (b.w() > 1.5f * d) {
        const char* word = kind == Kind::Nintendo ? (start ? "PLUS" : "MINUS") : (start ? "MENU" : "VIEW");
        const Box rest{k.x1 + 0.12f * d, b.y0, b.x1, b.y1};
        text(c, rest, word, 0.44f * d, kLetter, true);
    }
}

// Pad button p (kPadNames) as the device shows it.
void pad_button(Ctx& c, const Box& b, Kind kind, int p) {
    const bool x = kind == Kind::Xbox;
    // SDL names a Nintendo pad's buttons by their letters (SDL2's default), so A presses Cross as it does on an Xbox
    // pad: the same letters, without the colours.
    switch (p) {
    case kPCross: face(c, b, "A", x ? kXboxA : kLetter); break;
    case kPCircle: face(c, b, "B", x ? kXboxB : kLetter); break;
    case kPSquare: face(c, b, "X", x ? kXboxX : kLetter); break;
    case kPTriangle: face(c, b, "Y", x ? kXboxY : kLetter); break;
    case kPR1: shoulder(c, b, x ? "RB" : "R", false); break;
    case kPR2: shoulder(c, b, x ? "RT" : "ZR", true); break;
    case kPL1: shoulder(c, b, x ? "LB" : "L", false); break;
    case kPL2: shoulder(c, b, x ? "LT" : "ZL", true); break;
    case kPR3: stick(c, b, "RS", false); break;
    case kPL3: stick(c, b, "LS", false); break;
    case kPUp: dpad(c, b, 1); break;
    case kPRight: dpad(c, b, 2); break;
    case kPDown: dpad(c, b, 4); break;
    case kPLeft: dpad(c, b, 8); break;
    case kPSelect: menu_button(c, b, kind, false); break;
    case kPStart: menu_button(c, b, kind, true); break;
    default: break;
    }
}

int role_button(Role r) {
    switch (r) {
    case kTriangle: return kPTriangle;
    case kSquare: return kPSquare;
    case kCircle: return kPCircle;
    case kCross: return kPCross;
    case kSelect: return kPSelect;
    case kStart: return kPStart;
    case kR1: return kPR1;
    case kR2: return kPR2;
    case kL1: return kPL1;
    case kL2: return kPL2;
    case kR3: return kPR3;
    case kL3: return kPL3;
    case kUp: return kPUp;
    case kDown: return kPDown;
    case kLeft: return kPLeft;
    case kRight: return kPRight;
    default: return -1;
    }
}

void draw_pad_cell(Ctx& c, const Box& b, Role role, const Spec& s) {
    switch (role) {
    case kDpad: dpad(c, b, 15); return;
    case kDpadLR: dpad(c, b, 10); return;
    case kDpadUD: dpad(c, b, 5); return;
    case kRStick: stick(c, b, "R", true); return;
    case kLStick: stick(c, b, "L", true); return;
    default: break;
    }
    const int g = role_button(role);
    if (g < 0) return;
    const int p = s.pad_map[g] >= 0 && s.pad_map[g] < 16 ? s.pad_map[g] : g;
    // A wide cell (Select's, Start's) with another button in it: the button at its left.
    const bool wide = b.w() > 1.5f * b.h();
    if (wide && p != kPSelect && p != kPStart) pad_button(c, {b.x0, b.y0, b.x0 + b.h(), b.y1}, s.kind, p);
    else pad_button(c, b, s.kind, p);
}

// ---- The keyboard and mouse ----

constexpr int kMouse = 0x100, kWheelUp = 0x105, kWheelDown = 0x106;
constexpr int kVkLeft = 0x25, kVkUp = 0x26, kVkRight = 0x27, kVkDown = 0x28;

// A mouse seen from above, the button in use lit (0 left, 1 right, 2 middle, 3 the wheel up, 4 down; -1 none).
void mouse(Ctx& c, const Box& b, int lit) {
    const float d = std::min(b.w(), b.h());
    const Box body = centred(b.cx(), b.cy(), 0.62f * d, 0.92f * d);
    const float r = 0.30f * body.w();
    cap(c, body, r, r);
    const float split = body.y0 + 0.44f * body.h(), lw = std::max(1.f, 0.06f * d);
    if (lit == 0 || lit == 1) {
        const Box half = lit == 0 ? Box{body.x0, body.y0, body.cx(), split} : Box{body.cx(), body.y0, body.x1, split};
        const float in = 1.2f * c.u;
        fill(c.cv, half, [&](float x, float y) {
            const Box i{body.x0 + in, body.y0 + in, body.x1 - in, body.y1 - in};
            return sd_rbox(x, y, i, std::max(r - in, 0.f), std::max(r - in, 0.f));
        }, [&](float, float) { return kYellow; });
    }
    const Box vline = {body.cx() - lw / 2, body.y0, body.cx() + lw / 2, split};
    const Box hline = {body.x0, split - lw / 2, body.x1, split + lw / 2};
    for (const Box& l : {vline, hline})
        fill(c.cv, l, [&](float x, float y) {
            const float in = 0.5f * c.u;
            const Box i{body.x0 + in, body.y0 + in, body.x1 - in, body.y1 - in};
            return std::max(sd_rbox(x, y, l, 0, 0), sd_rbox(x, y, i, std::max(r - in, 0.f), std::max(r - in, 0.f)));
        }, [&](float, float) { return kLine; });
    if (lit >= 2) {
        const Box wheel = centred(body.cx(), body.y0 + 0.24f * body.h(), 0.18f * body.w(), 0.26f * body.h());
        fill(c.cv, wheel, [&](float x, float y) { return sd_rbox(x, y, wheel, wheel.w() / 2, wheel.w() / 2); },
             [&](float, float) { return kYellow; });
        if (lit >= 3) arrow(c, body.x1 + 0.12f * d, body.y0 + 0.24f * body.h(), 0.2f * d, lit == 3 ? 0 : 2, kYellow);
    }
}

// One key or mouse button, in the box.
void input(Ctx& c, const Box& b, int in, const std::string& label) {
    const float d = std::min(b.w(), b.h());
    const Box k = centred(b.cx(), b.cy(), 0.92f * d, 0.92f * d);
    if (in >= kMouse && in < kMouse + 3) return mouse(c, b, in - kMouse);
    if (in == kWheelUp || in == kWheelDown) return mouse(c, b, in == kWheelUp ? 3 : 4);
    cap(c, k, 0.22f * d, 0.22f * d);
    if (in == kVkUp || in == kVkRight || in == kVkDown || in == kVkLeft) {
        const int dir = in == kVkUp ? 0 : in == kVkRight ? 1 : in == kVkDown ? 2 : 3;
        arrow(c, k.cx(), k.cy(), 0.40f * d, dir, kLetter);
        return;
    }
    std::string l = label;
    if (l.empty() && in >= kMouse + 3 && in < kMouse + 5) l = "M" + std::to_string(in - kMouse + 1);
    if (l.empty()) l = in ? "?" : "-";
    text(c, k, l, (l.size() == 1 ? 0.44f : 0.30f) * d, kLetter);
}

// A binding: its input, with a chord's modifier as a small key above at the left.
void key(Ctx& c, const Box& b, const Key& k) {
    if (!k.mod) return input(c, b, k.in, k.label);
    const float d = std::min(b.w(), b.h());
    const Box sq = centred(b.cx(), b.cy(), d, d);
    input(c, {sq.x0 + 0.30f * d, sq.y0 + 0.30f * d, sq.x1, sq.y1}, k.in, k.label);
    input(c, {sq.x0, sq.y0, sq.x0 + 0.52f * d, sq.y0 + 0.52f * d}, k.mod, k.mod_label);
}

// Four keys as the arrow keys sit: one above, three below.
void four(Ctx& c, const Box& b, const Key& up, const Key& left, const Key& down, const Key& right) {
    const float q = std::min(b.w() / 3, b.h() / 2);
    const float cx = b.cx(), cy = b.cy();
    key(c, centred(cx, cy - q / 2, q, q), up);
    key(c, centred(cx - q, cy + q / 2, q, q), left);
    key(c, centred(cx, cy + q / 2, q, q), down);
    key(c, centred(cx + q, cy + q / 2, q, q), right);
}

void draw_key_cell(Ctx& c, const Box& b, Role role, const Spec& s) {
    const auto& k = s.keys;
    const float d = std::min(b.w(), b.h());
    const Box sq = {b.x0, b.y0, b.x0 + d, b.y0 + d};   // a wide cell's left end
    switch (role) {
    case kTriangle: key(c, b, k[kKeyTriangle]); return;
    case kSquare: key(c, b, k[kKeySquare]); return;
    case kCircle: key(c, b, k[kKeyCircle]); return;
    case kCross: key(c, b, k[kKeyCross]); return;
    case kSelect: key(c, b.w() > 1.5f * d ? sq : b, k[kKeySelect]); return;
    case kStart: key(c, b.w() > 1.5f * d ? sq : b, k[kKeyStart]); return;
    case kR1: key(c, b, k[kKeyR1]); return;
    case kR2: key(c, b, k[kKeyR2]); return;
    case kL1: key(c, b, k[kKeyL1]); return;
    case kL2: key(c, b, k[kKeyL2]); return;
    case kR3: key(c, b, k[kKeyR3]); return;
    case kL3: key(c, b, k[kKeyL3]); return;
    case kUp: key(c, b, k[kKeyUp]); return;
    case kDown: key(c, b, k[kKeyDown]); return;
    case kLeft: key(c, b, k[kKeyLeft]); return;
    case kRight: key(c, b, k[kKeyRight]); return;
    case kDpad: four(c, b, k[kKeyUp], k[kKeyLeft], k[kKeyDown], k[kKeyRight]); return;
    case kLStick: four(c, b, k[kKeyLsUp], k[kKeyLsLeft], k[kKeyLsDown], k[kKeyLsRight]); return;
    case kDpadLR:
        key(c, {b.cx() - d / 2, b.cy() - d / 4, b.cx(), b.cy() + d / 4}, k[kKeyLeft]);
        key(c, {b.cx(), b.cy() - d / 4, b.cx() + d / 2, b.cy() + d / 4}, k[kKeyRight]);
        return;
    case kDpadUD:
        key(c, {b.cx() - d / 4, b.cy() - d / 2, b.cx() + d / 4, b.cy()}, k[kKeyUp]);
        key(c, {b.cx() - d / 4, b.cy(), b.cx() + d / 4, b.cy() + d / 2}, k[kKeyDown]);
        return;
    case kRStick: {   // the camera: the mouse moving
        mouse(c, centred(b.cx(), b.cy(), 0.8f * d, 0.8f * d), -1);
        const float a = 0.20f * d;
        arrow(c, b.cx() - 0.40f * d, b.cy(), a, 3, kYellow);
        arrow(c, b.cx() + 0.40f * d, b.cy(), a, 1, kYellow);
        return;
    }
    default: return;
    }
}

// A sprite's square (Cell::tile): the game draws the button's symbol bare on it, so the pads' face buttons are their
// letters alone, in their colours; another button (PAD_MAP), and the keyboard's keys, as drawn elsewhere.
void draw_tile_cell(Ctx& c, const Box& b, Role role, const Spec& s) {
    if (s.kind == Kind::Keyboard) return draw_key_cell(c, b, role, s);
    const int g = role_button(role);
    const int p = g < 0 ? -1 : s.pad_map[g] >= 0 && s.pad_map[g] < 16 ? s.pad_map[g] : g;
    const char* letter = p == kPCross ? "A" : p == kPCircle ? "B" : p == kPSquare ? "X" : p == kPTriangle ? "Y" : nullptr;
    if (!letter) return draw_pad_cell(c, b, role, s);
    const bool x = s.kind == Kind::Xbox;
    const Rgba col = !x ? kLetter : p == kPCross ? kXboxA : p == kPCircle ? kXboxB : p == kPSquare ? kXboxX : kXboxY;
    text(c, b, letter, 0.62f * b.h(), col);
}

// ---- PlayStation with buttons moved: the game's own pictures, each where the button that now reads it is ----

int pad_at(const Spec& s, int g) { return s.pad_map[g] >= 0 && s.pad_map[g] < 16 ? s.pad_map[g] : g; }
bool face_button(int p) { return p == kPCross || p == kPCircle || p == kPSquare || p == kPTriangle; }

// A rectangle of one of the game's pages, read premultiplied; outside it reads clear (the font's next character is
// not part of it). symbol: only a face button's coloured symbol, without the dark disc under it, all in the symbol's
// own colour (the item slot draws its symbol bare on its square).
struct Picture {
    const Page* page = nullptr;
    const std::vector<uint8_t>* base = nullptr;
    int x = 0, y = 0, w = 0, h = 0;
    bool symbol = false;
    float colour[3] = {0, 0, 0};

    const uint8_t* px(int u, int v) const { return &(*base)[(size_t(v) * page->width + size_t(u)) * 4]; }
    // How much of a pixel is the symbol: its colourfulness (the disc and rim are grey), times its alpha.
    static float weight(const uint8_t* p) {
        const int chroma = std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]});
        return std::clamp((float(chroma) - 40.f) / 50.f, 0.f, 1.f) * float(p[3]) / 255.f;
    }
    void at(int u, int v, float out[4]) const {
        if (u < x || v < y || u >= x + w || v >= y + h) {
            out[0] = out[1] = out[2] = out[3] = 0.f;
            return;
        }
        const uint8_t* p = px(u, v);
        const float a = symbol ? weight(p) : float(p[3]) / 255.f;
        for (int ch = 0; ch < 3; ch++) out[ch] = (symbol ? colour[ch] : float(p[ch]) / 255.f) * a;
        out[3] = a;
    }
    // Narrowed to the symbol, and its colour found (the mean of its solid part). False when the cell has none.
    bool to_symbol() {
        int x0 = x + w, y0 = y + h, x1 = -1, y1 = -1;
        double sum[3] = {0, 0, 0}, n = 0;
        for (int v = y; v < y + h; v++)
            for (int u = x; u < x + w; u++) {
                const float k = weight(px(u, v));
                if (k < 0.25f) continue;
                x0 = std::min(x0, u), y0 = std::min(y0, v), x1 = std::max(x1, u), y1 = std::max(y1, v);
                if (k < 0.9f) continue;
                for (int ch = 0; ch < 3; ch++) sum[ch] += px(u, v)[ch];
                n++;
            }
        if (x1 < 0 || n == 0) return false;
        for (int ch = 0; ch < 3; ch++) colour[ch] = float(sum[ch] / n / 255.0);
        x = x0, y = y0, w = x1 - x0 + 1, h = y1 - y0 + 1;
        symbol = true;
        return true;
    }
};

// The game's picture of pad button p for `want`: a cell of the same size, on the same page, first; for a sprite's square
// the largest (its symbol is shrunk to fit). Squares are never taken from: their symbol sits on a backing.
bool find_picture(int p, const Page& page, const std::vector<uint8_t>& base, const Cell& want,
                  const std::vector<Source>& pictures, Picture& out) {
    int64_t best = -1;
    const auto look = [&](const Page& pg, const std::vector<uint8_t>& b) {
        if (b.size() != size_t(pg.width) * pg.height * 4) return;
        for (const Cell& c : pg.cells) {
            if (c.tile || role_button(c.role) != p) continue;
            const int64_t area = int64_t(c.w) * c.h, same = int64_t(1) << 40;
            const int64_t score = want.tile ? area : (c.w == want.w && c.h == want.h ? 4 * same : 0) +
                                                     (c.h == want.h ? 2 * same : 0) + (&pg == &page ? same : 0) + area;
            if (score <= best) continue;
            best = score;
            out = Picture{&pg, &b, c.x, c.y, c.w, c.h};
        }
    };
    look(page, base);
    for (const Source& s : pictures)
        if (s.page && s.base && s.page != &page) look(*s.page, *s.base);
    return best >= 0;
}

// pic drawn over the canvas as large as fits in b, centred (b in the canvas's pixels; the page at 1x). A cell the same
// size as the picture is a copy, pixel for pixel.
void put(Canvas& cv, const Picture& pic, const Box& b) {
    const float k = std::min(b.w() / float(pic.w), b.h() / float(pic.h));
    if (!(k > 0.f)) return;
    const float dw = float(pic.w) * k, dh = float(pic.h) * k;
    const float x0 = b.cx() - dw / 2, y0 = b.cy() - dh / 2;
    const int n = std::clamp(int(std::ceil(1.f / k - 0.01f)), 1, 4);   // samples a side, when shrunk
    const auto bilinear = [&](float sx, float sy, float out[4]) {
        const int u = int(std::floor(sx)), v = int(std::floor(sy));
        const float fx = sx - float(u), fy = sy - float(v);
        float p00[4], p10[4], p01[4], p11[4];
        pic.at(u, v, p00);
        pic.at(u + 1, v, p10);
        pic.at(u, v + 1, p01);
        pic.at(u + 1, v + 1, p11);
        for (int ch = 0; ch < 4; ch++)
            out[ch] = (p00[ch] * (1 - fx) + p10[ch] * fx) * (1 - fy) + (p01[ch] * (1 - fx) + p11[ch] * fx) * fy;
    };
    for (int y = std::max(0, int(std::floor(y0))); y < std::min(cv.h, int(std::ceil(y0 + dh))); y++)
        for (int x = std::max(0, int(std::floor(x0))); x < std::min(cv.w, int(std::ceil(x0 + dw))); x++) {
            float acc[4] = {0, 0, 0, 0};
            for (int j = 0; j < n; j++)
                for (int i = 0; i < n; i++) {
                    const float dx = float(x) + (float(i) + 0.5f) / float(n), dy = float(y) + (float(j) + 0.5f) / float(n);
                    float s[4];
                    bilinear(float(pic.x) + (dx - x0) / k - 0.5f, float(pic.y) + (dy - y0) / k - 0.5f, s);
                    for (int ch = 0; ch < 4; ch++) acc[ch] += s[ch];
                }
            uint8_t* o = &cv.px[(size_t(y) * size_t(cv.w) + size_t(x)) * 4];
            const float a = acc[3] / float(n * n), keep = 1.f - a;
            for (int ch = 0; ch < 4; ch++)
                o[ch] = uint8_t(std::lround(std::clamp(acc[ch] / float(n * n) * 255.f + float(o[ch]) * keep, 0.f, 255.f)));
        }
}

// The moved button p's picture in cell `cell` (box b, the page at 1x). A sprite's square gets a face button's symbol
// bare, the size of the game's (inset 3 pixels), or another button's whole picture.
void put_moved(Canvas& cv, const Page& page, const std::vector<uint8_t>& base, const Cell& cell, const Box& b, int p,
               const std::vector<Source>& pictures) {
    Picture pic;
    if (!find_picture(p, page, base, cell, pictures, pic)) return;
    if (cell.tile) {
        if (face_button(p) && pic.to_symbol()) return put(cv, pic, {b.x0 + 3, b.y0 + 3, b.x1 - 3, b.y1 - 3});
        return put(cv, pic, {b.x0 + 2, b.y0 + 2, b.x1 - 2, b.y1 - 2});
    }
    // A wide cell (Select's, Start's) with another button in it: the button at its left, as for the pads.
    const bool wide = b.w() > 1.5f * b.h(), pic_wide = float(pic.w) > 1.5f * float(pic.h);
    if (wide && !pic_wide) return put(cv, pic, {b.x0, b.y0, b.x0 + b.h(), b.y1});
    put(cv, pic, b);
}

}  // namespace

Image draw(const Page& page, const std::vector<uint8_t>& base, const Spec& spec, const unsigned char* font,
           const std::vector<Source>& pictures) {
    const int S = std::clamp(spec.scale, 1, std::clamp(page.max_scale, 1, 4));
    const int W = int(page.width) * S, H = int(page.height) * S;
    Canvas cv;
    cv.w = W;
    cv.h = H;
    cv.px.assign(size_t(W) * size_t(H) * 4, 0);
    // The game's page, premultiplied, enlarged (bilinear), so whatever else is on it stays as it was.
    if (base.size() == size_t(page.width) * page.height * 4) {
        const int bw = int(page.width), bh = int(page.height);
        const auto at = [&](int x, int y, int ch) {
            x = std::clamp(x, 0, bw - 1);
            y = std::clamp(y, 0, bh - 1);
            const uint8_t* p = &base[(size_t(y) * size_t(bw) + size_t(x)) * 4];
            return ch == 3 ? float(p[3]) : float(p[ch]) * float(p[3]) / 255.f;
        };
        for (int y = 0; y < H; y++) {
            const float sy = (float(y) + 0.5f) / float(S) - 0.5f;
            const int y0 = int(std::floor(sy));
            const float fy = sy - float(y0);
            for (int x = 0; x < W; x++) {
                const float sx = (float(x) + 0.5f) / float(S) - 0.5f;
                const int x0 = int(std::floor(sx));
                const float fx = sx - float(x0);
                uint8_t* o = &cv.px[(size_t(y) * size_t(W) + size_t(x)) * 4];
                for (int ch = 0; ch < 4; ch++) {
                    const float v = (at(x0, y0, ch) * (1 - fx) + at(x0 + 1, y0, ch) * fx) * (1 - fy) +
                                    (at(x0, y0 + 1, ch) * (1 - fx) + at(x0 + 1, y0 + 1, ch) * fx) * fy;
                    o[ch] = uint8_t(std::lround(std::clamp(v, 0.f, 255.f)));
                }
            }
        }
    }
    stbtt_fontinfo info;
    const bool have_font = font && stbtt_InitFont(&info, font, stbtt_GetFontOffsetForIndex(font, 0));
    Ctx c{cv, have_font ? &info : nullptr, float(S)};
    const bool have_base = base.size() == size_t(page.width) * page.height * 4;
    for (const Cell& cell : page.cells) {
        if (cell.spare) continue;
        const Box b{float(cell.x * S), float(cell.y * S), float((cell.x + cell.w) * S), float((cell.y + cell.h) * S)};
        c.u = b.h() / 26.f;
        int moved = -1;   // PlayStation: the pad button that now reads this one, the cell left as it is when none
        if (spec.kind == Kind::PlayStation) {
            const int g = role_button(cell.role);
            if (g < 0 || pad_at(spec, g) == g || !have_base) continue;
            moved = pad_at(spec, g);
        }
        if (cell.tile) {
            // The square's inside painted with its own backing (taken 2 pixels in, clear of the symbol, which the
            // game insets 3), its edge left as the game drew it; then the device's symbol on it.
            constexpr int kIn = 2;
            uint8_t back[4] = {0, 0, 0, 0};
            if (base.size() == size_t(page.width) * page.height * 4 && cell.w > 2 * kIn && cell.h > 2 * kIn) {
                const uint8_t* p = &base[(size_t(cell.y + kIn) * page.width + size_t(cell.x + kIn)) * 4];
                for (int ch = 0; ch < 3; ch++) back[ch] = uint8_t((int(p[ch]) * int(p[3]) + 127) / 255);
                back[3] = p[3];
            }
            for (int y = (cell.y + kIn) * S; y < (cell.y + cell.h - kIn) * S; y++)
                for (int x = (cell.x + kIn) * S; x < (cell.x + cell.w - kIn) * S; x++)
                    std::memcpy(&cv.px[(size_t(y) * size_t(W) + size_t(x)) * 4], back, 4);
            const float in = float(kIn * S);
            if (moved >= 0) put_moved(cv, page, base, cell, b, moved, pictures);
            else draw_tile_cell(c, {b.x0 + in, b.y0 + in, b.x1 - in, b.y1 - in}, cell.role, spec);
            continue;
        }
        for (int y = cell.y * S; y < (cell.y + cell.h) * S; y++)
            std::memset(&cv.px[(size_t(y) * size_t(W) + size_t(cell.x * S)) * 4], 0, size_t(cell.w * S) * 4);
        if (moved >= 0) put_moved(cv, page, base, cell, b, moved, pictures);
        else if (spec.kind == Kind::Keyboard) draw_key_cell(c, b, cell.role, spec);
        else if (spec.kind == Kind::Xbox || spec.kind == Kind::Nintendo) draw_pad_cell(c, b, cell.role, spec);
    }
    // Level 0, then each level half the last (a box filter on the premultiplied colours), each straight alpha.
    Image out;
    out.width = uint32_t(W);
    out.height = uint32_t(H);
    std::vector<uint8_t> cur = std::move(cv.px);
    int w = W, h = H;
    for (;;) {
        const size_t at = out.rgba.size();
        out.rgba.resize(at + cur.size());
        for (size_t i = 0; i < cur.size(); i += 4) {
            const uint8_t a = cur[i + 3];
            for (int ch = 0; ch < 3; ch++)
                out.rgba[at + i + size_t(ch)] = a ? uint8_t(std::min(255, (int(cur[i + size_t(ch)]) * 255 + a / 2) / a)) : 0;
            out.rgba[at + i + 3] = a;
        }
        out.levels++;
        if (w == 1 && h == 1) break;
        const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
        std::vector<uint8_t> next(size_t(nw) * size_t(nh) * 4);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
                for (int ch = 0; ch < 4; ch++) {
                    int sum = 0, n = 0;
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++) {
                            const int sx = std::min(w - 1, 2 * x + dx), sy = std::min(h - 1, 2 * y + dy);
                            sum += cur[(size_t(sy) * size_t(w) + size_t(sx)) * 4 + size_t(ch)];
                            n++;
                        }
                    next[(size_t(y) * size_t(nw) + size_t(x)) * 4 + size_t(ch)] = uint8_t((sum + n / 2) / n);
                }
        cur = std::move(next);
        w = nw;
        h = nh;
    }
    return out;
}

}  // namespace ui::button_pages
