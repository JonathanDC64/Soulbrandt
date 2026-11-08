// The button page on its own (button_pages.cpp): a made-up code map and texture pack read, the page decoded and drawn
// again for each device. No game data. Exit code 0 when every check passes.
//   button_icons_check [--ccm <dsfont24.ccm> --tpf <dsfont24.tpf>] [--png <folder>]
// --ccm and --tpf read the player's own files in place of the made-up ones; --png writes each page drawn (level 0) to
// the folder, to look at. Pages from the player's files hold the game's art: keep them out of the repository.
#include "button_pages.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace bp = ui::button_pages;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fail++;
}

void put16(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    v[at] = uint8_t(x >> 8);
    v[at + 1] = uint8_t(x);
}
void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    put16(v, at, x >> 16);
    put16(v, at + 2, x & 0xFFFF);
}
void putf(std::vector<uint8_t>& v, size_t at, float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    put32(v, at, x);
}

struct Made {
    uint32_t code;
    int x, y, w, h, page;
};
// The button characters at made-up places on page 0 (Select and Start wide, as the game's are), the copyright sign
// (not a button) beside them, and a letter on page 1.
std::vector<Made> made_cells() {
    const uint32_t codes[] = {0x2460, 0x2461, 0x2462, 0x2463, 0x2464, 0x2466, 0x2467, 0x2468, 0x2469, 0x246A,
                              0x246B, 0x246C, 0x246D, 0x246E, 0x2470, 0x2471, 0x32A4, 0x32A6, 0x32A7, 0x32A8};
    std::vector<Made> out;
    int i = 0;
    for (uint32_t c : codes) {
        out.push_back({c, 26 * (i % 19), 26 * (i / 19), 26, 26, 0});
        i++;
    }
    out.push_back({0x2465, 0, 52, 112, 26, 0});    // Select
    out.push_back({0x246F, 112, 52, 96, 26, 0});   // Start
    out.push_back({0x41, 0, 0, 13, 26, 1});        // 'A'
    return out;
}

std::vector<uint8_t> made_ccm(const std::vector<Made>& cells) {
    const size_t groups = 0x20, glyphs = groups + cells.size() * 12;
    std::vector<uint8_t> v(glyphs + cells.size() * 24);
    put32(v, 0, 0x00010000);
    put32(v, 4, uint32_t(v.size()));
    put16(v, 8, 26);
    put16(v, 0x0A, 512);
    put16(v, 0x0C, 512);
    put16(v, 0x0E, 32);
    put16(v, 0x10, uint32_t(cells.size()));
    put16(v, 0x12, uint32_t(cells.size()));
    put32(v, 0x14, uint32_t(groups));
    put32(v, 0x18, uint32_t(glyphs));
    for (size_t i = 0; i < cells.size(); i++) {
        const Made& m = cells[i];
        put32(v, groups + i * 12, m.code);
        put32(v, groups + i * 12 + 4, m.code);
        put32(v, groups + i * 12 + 8, uint32_t(i));
        const size_t g = glyphs + i * 24;
        putf(v, g, float(m.x) / 512.f);
        putf(v, g + 4, float(m.y) / 512.f);
        putf(v, g + 8, float(m.x + m.w) / 512.f);
        putf(v, g + 12, float(m.y + m.h) / 512.f);
        put16(v, g + 18, uint32_t(m.w));
        put16(v, g + 20, uint32_t(m.w));
        put16(v, g + 22, uint32_t(m.page));
    }
    return v;
}

constexpr uint32_t kLevels = 349552;   // a 512x512 DXT5 page and its smaller levels down to 1x1

// Two 512x512 DXT5 pages, transparent but for one opaque red block on page 0 at block (100, 100).
std::vector<uint8_t> made_tpf() {
    const uint32_t data = 0x80;
    std::vector<uint8_t> v(data + 2 * kLevels + 32, 0);
    std::memcpy(v.data(), "TPF", 4);
    put32(v, 4, uint32_t(v.size()));
    put32(v, 8, 2);
    for (uint32_t i = 0; i < 2; i++) {
        const size_t e = 0x10 + i * 0x20;
        put32(v, e, data + i * kLevels);
        put32(v, e + 4, kLevels);
        v[e + 8] = 5;
        put16(v, e + 12, 512);
        put16(v, e + 14, 512);
    }
    uint8_t* b = &v[data + (size_t(100) * 128 + 100) * 16];
    b[0] = b[1] = 255;   // alpha: 255, every texel
    b[8] = 0x00;         // colour 0 = 0xF800 (red), little-endian; colour 1 the same; picks 0
    b[9] = 0xF8;
    b[10] = 0x00;
    b[11] = 0xF8;
    return v;
}

// A menu pack: "Menu00" (512x512), "Menu08" (1024x512) and "Other" (512x512), DXT5, one level each, transparent but
// for Menu08's item-slot square (79,426, 19x19): dark, at 203 of 255, as the game's backing is.
std::vector<uint8_t> made_menu_tpf() {
    const struct {
        const char* name;
        uint32_t w, h;
    } tex[] = {{"Menu00", 512, 512}, {"Menu08", 1024, 512}, {"Other", 512, 512}};
    const uint32_t names = 0x10 + 3 * 0x20, data = 0x100;
    uint32_t total = data;
    for (const auto& t : tex) total += t.w * t.h;   // DXT5: one byte a texel
    std::vector<uint8_t> v(total, 0);
    std::memcpy(v.data(), "TPF", 4);
    put32(v, 4, total);
    put32(v, 8, 3);
    uint32_t at = data, name_at = names;
    for (uint32_t i = 0; i < 3; i++) {
        const size_t e = 0x10 + i * 0x20;
        put32(v, e, at);
        put32(v, e + 4, tex[i].w * tex[i].h);
        v[e + 8] = 5;
        v[e + 10] = 1;
        put16(v, e + 12, tex[i].w);
        put16(v, e + 14, tex[i].h);
        put32(v, e + 24, name_at);
        std::memcpy(&v[name_at], tex[i].name, std::strlen(tex[i].name) + 1);
        name_at += 16;
        if (i == 1)
            for (uint32_t by = 424 / 4; by < 448 / 4; by++)
                for (uint32_t bx = 76 / 4; bx < 100 / 4; bx++) {
                    uint8_t* b = &v[at + (size_t(by) * (tex[i].w / 4) + bx) * 16];
                    b[0] = b[1] = 203;   // alpha
                    b[8] = 0x82;         // colour 0x1082 (16, 16, 16), both ends; picks 0
                    b[9] = 0x10;
                    b[10] = 0x82;
                    b[11] = 0x10;
                }
        at += tex[i].w * tex[i].h;
    }
    return v;
}

bool read(const char* path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return !out.empty();
}

const uint8_t* at(const bp::Image& im, uint32_t x, uint32_t y) { return &im.rgba[(size_t(y) * im.width + x) * 4]; }

// The most opaque texel in a cell of the drawn page (level 0).
int cell_alpha(const bp::Image& im, const bp::Cell& c, int scale) {
    int most = 0;
    for (int y = c.y * scale; y < (c.y + c.h) * scale; y++)
        for (int x = c.x * scale; x < (c.x + c.w) * scale; x++) most = std::max(most, int(at(im, uint32_t(x), uint32_t(y))[3]));
    return most;
}

}  // namespace

int main(int argc, char** argv) {
    const char* ccm_path = nullptr;
    const char* tpf_path = nullptr;
    const char* png = nullptr;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--ccm")) ccm_path = argv[i + 1];
        else if (!std::strcmp(argv[i], "--tpf")) tpf_path = argv[i + 1];
        else if (!std::strcmp(argv[i], "--png")) png = argv[i + 1];
    }
    std::vector<uint8_t> font;
    check(read(DES_UI_FONT, font), "the UI's font read (letters on the pages)");
    const bool own = ccm_path && tpf_path;

    std::vector<uint8_t> ccm, tpf;
    if (own) {
        check(read(ccm_path, ccm) && read(tpf_path, tpf), "the player's font files read");
    } else {
        ccm = made_ccm(made_cells());
        tpf = made_tpf();
    }
    const std::vector<bp::Page> pages = bp::read_font(ccm, tpf, "test");
    check(pages.size() == 1, "one page with button cells (not the letters' page)");
    if (pages.empty()) return 1;
    const bp::Page& page = pages[0];
    check(page.cells.size() == 21, "its 21 button cells (the copyright sign is not one)");
    check(page.fmt == 0x88 && page.level0 == 512u * 512u && page.width == 512 && page.height == 512,
          "a 512x512 DXT5 page, its level 0 the first 256 KB");
    if (!own) {
        check(page.index == 0 && page.data.size() == kLevels, "page 0, all its levels kept");
        bool wide = false;
        for (const bp::Cell& c : page.cells) wide = wide || (c.role == bp::kSelect && c.w == 112 && c.y == 52);
        check(wide, "Select's cell where the code map puts it, 112 wide");
        check(bp::read_font(std::vector<uint8_t>(ccm.begin(), ccm.begin() + 0x30), tpf, "cut").empty() &&
              bp::read_font(ccm, std::vector<uint8_t>(tpf.begin(), tpf.begin() + 0x20), "cut").empty(),
              "cut files read as no pages");
    }

    const std::vector<uint8_t> base = bp::decode(page);
    check(base.size() == 512u * 512u * 4, "the page decoded");
    if (!own) {
        const uint8_t* red = &base[(size_t(401) * 512 + 402) * 4];
        check(red[0] == 255 && red[1] == 0 && red[2] == 0 && red[3] == 255, "the red block decoded red and opaque");
        check(base[3] == 0, "the rest transparent");
    }

    bp::Spec xbox;
    xbox.kind = bp::Kind::Xbox;
    xbox.scale = 2;
    for (int i = 0; i < 16; i++) xbox.pad_map[i] = i;
    xbox.keys[bp::kKeyCross] = {'E', 0, "E", ""};
    const bp::Spec xn = bp::normalized(xbox);
    check(xn.keys[bp::kKeyCross].in == 0 && xn.pad_map[3] == 3, "a pad's page does not depend on the keys");
    bp::Spec ps = xbox;
    ps.kind = bp::Kind::PlayStation;
    check(bp::normalized(ps).scale == 1 && bp::describe(bp::normalized(ps)) == "PlayStation x1",
          "the game's own page: one, whatever the scale");

    const bp::Image xi = bp::draw(page, base, xn, font.empty() ? nullptr : font.data());
    size_t want = 0;
    for (uint32_t s = 1024; s; s /= 2) want += size_t(s) * s * 4;
    check(xi.width == 1024 && xi.height == 1024 && xi.levels == 11 && xi.rgba.size() == want,
          "drawn at 2x: 1024x1024 and 11 levels");
    bool all = true;
    for (const bp::Cell& c : page.cells) all = all && cell_alpha(xi, c, 2) > 200;
    check(all, "every button cell drawn");
    if (!own) {
        const uint8_t* red = at(xi, 805, 803);
        check(red[0] > 240 && red[1] < 16 && red[3] > 240, "the rest of the page kept (the red block, at 2x)");
        check(at(xi, 1000, 1000)[3] == 0, "empty places stay empty");
    }

    bp::Spec nin = xn;
    nin.kind = bp::Kind::Nintendo;
    nin.scale = 1;
    std::swap(nin.pad_map[12], nin.pad_map[14]);   // PAD_MAP triangle=cross, cross=triangle
    const bp::Image ni = bp::draw(page, base, bp::normalized(nin), font.empty() ? nullptr : font.data());
    check(ni.width == 512 && ni.levels == 10, "drawn at 1x: 512x512 and 10 levels");
    check(bp::describe(bp::normalized(nin)) == "Nintendo x1 triangle=cross cross=triangle", "PAD_MAP in the page's name");

    bp::Spec kb;
    kb.kind = bp::Kind::Keyboard;
    kb.scale = 4;
    const bp::Key keys[bp::kKeyTargets] = {
        {'F', 0, "F", ""}, {'R', 0, "R", ""}, {0x20, 0, "Space", ""}, {'E', 0, "E", ""}, {'G', 0, "G", ""},
        {0x1B, 0, "Esc", ""}, {0x100, 0, "", ""}, {0x100, 0x10, "", "Shift"}, {0x101, 0, "", ""},
        {0x101, 0xA2, "", "Ctrl"}, {'Q', 0, "Q", ""}, {'C', 0, "C", ""}, {0x26, 0, "", ""}, {0x28, 0, "", ""},
        {0x25, 0, "", ""}, {0x27, 0, "", ""}, {'W', 0, "W", ""}, {'S', 0, "S", ""}, {'A', 0, "A", ""},
        {'D', 0, "D", ""}};
    for (int t = 0; t < bp::kKeyTargets; t++) kb.keys[t] = keys[t];
    const bp::Image ki = bp::draw(page, base, bp::normalized(kb), font.empty() ? nullptr : font.data());
    all = ki.width == 2048 && ki.levels == 12;
    for (const bp::Cell& c : page.cells) all = all && cell_alpha(ki, c, 4) > 200;
    check(all, "the keyboard's page at 4x, every cell drawn");

    // The menu's sprites: two pages found by name and size, drawn at 2x at most.
    const std::vector<bp::Page> menu = bp::read_sprites(made_menu_tpf(), "menu");
    check(menu.size() == 2, "two menu pages with sprites (not the other texture)");
    if (menu.size() == 2) {
        const bp::Page& m0 = menu[0].width == 512 ? menu[0] : menu[1];
        const bp::Page& m8 = menu[0].width == 1024 ? menu[0] : menu[1];
        check(m0.cells.size() == 2 && m8.cells.size() == 1 && m8.cells[0].tile && m8.max_scale == 2,
              "Menu00's two round buttons, Menu08's item-slot square");
        check(m8.level0 == 1024u * 512u && m8.data.size() == m8.level0, "Menu08 as stored: one level");
        bp::Spec big = xn;
        big.scale = 4;
        const std::vector<uint8_t> mb = bp::decode(m8);
        const bp::Image mi = bp::draw(m8, mb, bp::normalized(big), font.empty() ? nullptr : font.data());
        check(mi.width == 2048 && mi.height == 1024, "a menu page at 4x drawn at 2x");
        const bp::Cell& t = m8.cells[0];
        const uint8_t* edge = at(mi, uint32_t(t.x * 2 + 1), uint32_t(t.y * 2 + 1));
        const uint8_t* back = at(mi, uint32_t((t.x + 2) * 2 + 1), uint32_t((t.y + 2) * 2 + 1));
        check(edge[3] > 190 && edge[3] < 215 && edge[0] < 40 && back[3] > 190 && back[3] < 215 && back[0] < 40,
              "the square's backing kept, dark at the game's alpha");
        int bright = 0;
        for (int y = (t.y + 3) * 2; y < (t.y + t.h - 3) * 2; y++)
            for (int x = (t.x + 3) * 2; x < (t.x + t.w - 3) * 2; x++) {
                const uint8_t* p = at(mi, uint32_t(x), uint32_t(y));
                bright = std::max(bright, int(std::max(p[0], std::max(p[1], p[2]))));
            }
        check(font.empty() || bright > 120, "the Xbox X drawn on the square");
    }

    if (png) {
        const struct {
            const char* name;
            const bp::Image* im;
        } out[] = {{"xbox_2x.png", &xi}, {"nintendo_1x.png", &ni}, {"keyboard_4x.png", &ki}};
        for (const auto& o : out) {
            const std::string path = std::string(png) + "/" + o.name;
            // The top of the page, where the cells are: 128 rows of the game's at the page's scale.
            const int rows = int(o.im->height) / 4;
            const bool ok = stbi_write_png(path.c_str(), int(o.im->width), rows, 4, o.im->rgba.data(), int(o.im->width) * 4);
            std::printf("%s %s\n", ok ? "wrote" : "could not write", path.c_str());
        }
    }
    std::printf("%s\n", g_fail ? "FAILED" : "ALL PASS");
    return g_fail ? 1 : 0;
}
