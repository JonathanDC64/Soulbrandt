// Written for Demon's Souls. The button icons in the game's prompts (button_icons.hpp, docs/138).
//
// - The pages come from the player's own font files (font/*/dsfont24.ccm and .tpf under the game's USRDIR), read once
//   on a thread of this module's: the code map gives each button character's cell and page, the texture pack the
//   page's bytes. The renderer asks about each 512x512 texture of the page's format it uploads (rsx_texture_override.h);
//   one whose bytes are a page's is that page.
// - What to show: the Button icons row (DES_GLYPHS). Automatic follows cellPad's last-used device
//   (cellPad_host_glyph_device: a pad button or a stick pushed, or a key or mouse button). PlayStation is the game's own
//   page, untouched; Xbox, Nintendo and Keyboard and mouse are drawn here (button_pages.cpp) at the render scale
//   (RSX_SCALE, up to 4x), following PAD_MAP and the keys bound in Controls.
// - Drawing takes tens of milliseconds, so it happens on this module's thread; the game's page (or the last one drawn)
//   shows until the new one is ready. Up to six drawn pages are kept, so switching back is at once.
#include "button_icons.hpp"

#include "button_pages.hpp"
#include "controls_window.hpp"
#include "settings.hpp"
#include "ui_assets.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
// ps3recomp, libs/video/rsx_texture_override.h
typedef struct rsx_texture_override_image {
    const void* rgba;
    uint32_t width;
    uint32_t height;
    uint32_t levels;
} rsx_texture_override_image;
typedef int (*rsx_texture_override_fn)(void* user, const uint8_t* guest, uint32_t bytes, uint32_t width,
                                       uint32_t height, uint32_t fmt, rsx_texture_override_image* out);
int rsx_texture_override_watch(uint32_t width, uint32_t height, uint32_t fmt, rsx_texture_override_fn fn, void* user);
void rsx_texture_override_changed(void);
// ps3recomp, libs/input/cellPad.h: PAD_GLYPH_NONE 0, _KEYBOARD 1, _PLAYSTATION 2, _XBOX 3, _NINTENDO 4 (pad_glyph.h)
int cellPad_host_glyph_device(void);
}

namespace ui::button_icons {
namespace {

namespace bp = button_pages;
namespace fs = std::filesystem;

constexpr size_t kKeep = 6;   // drawn pages kept

struct Built {
    std::string key;
    std::shared_ptr<bp::Image> image;
    uint64_t used = 0;
};

struct State {
    std::atomic<bool> started{false};
    std::atomic<bool> loaded{false};   // pages, below, is read and does not change after
    std::vector<bp::Page> pages;
    std::mutex mu;                      // everything below
    std::condition_variable cv;
    std::vector<char> seen;             // each page: the game has uploaded it
    unsigned seen_serial = 0;
    bp::Spec spec;                      // what to show (normalized; PlayStation: the game's own)
    unsigned spec_serial = 0;
    std::vector<Built> cache;
    std::shared_ptr<bp::Image> given;   // the image the renderer was last given, kept while it copies it
    uint64_t tick = 0;
    // The UI thread's own.
    int last_device = -1;
    unsigned frame = 0;
};

State& st() {
    static State* s = new State;   // never destroyed: the renderer's thread may still ask while the process ends
    return *s;
}

std::string key_of(const bp::Spec& spec, size_t page) { return bp::describe(spec) + " #" + std::to_string(page); }

bool read_file(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size <= 0 || size > (std::streamoff(64) << 20)) return false;
    out.resize(size_t(size));
    f.seekg(0);
    return bool(f.read(reinterpret_cast<char*>(out.data()), size));
}

// The pages with button pictures: the font folder's (the folder itself and each language's under it), then the
// menu's two with sprites (menu/menu.tpf; the disc has it beside menu.tpf.dcx, the same textures uncompressed).
std::vector<bp::Page> load_pages() {
    std::vector<bp::Page> out;
    const char* root = std::getenv("PS3_VFS_ROOT");
    if (!root || !*root) return out;
    const std::string r(root);
    const fs::path usrdir = fs::path(std::u8string(r.begin(), r.end())) / "PS3_GAME" / "USRDIR";
    const fs::path fonts = usrdir / "font";
    std::vector<fs::path> dirs{fonts};
    std::error_code ec;
    for (fs::directory_iterator it(fonts, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec)) dirs.push_back(it->path());
    std::sort(dirs.begin() + 1, dirs.end());
    for (const fs::path& d : dirs) {
        std::vector<uint8_t> ccm, tpf;
        if (!read_file(d / "dsfont24.ccm", ccm) || !read_file(d / "dsfont24.tpf", tpf)) continue;
        const std::u8string name = d.filename().u8string();
        const std::string from = d == fonts ? "font" : "font/" + std::string(name.begin(), name.end());
        for (bp::Page& p : bp::read_font(ccm, tpf, from)) {
            const bool again = std::any_of(out.begin(), out.end(), [&](const bp::Page& q) { return q.data == p.data; });
            if (!again) out.push_back(std::move(p));
        }
    }
    std::vector<uint8_t> menu;
    if (read_file(usrdir / "menu" / "menu.tpf", menu))
        for (bp::Page& p : bp::read_sprites(menu, "menu/menu.tpf")) out.push_back(std::move(p));
    return out;
}

// The renderer's question, on its thread: is this upload one of the pages, and is there an image for it?
int ask(void*, const uint8_t* guest, uint32_t bytes, uint32_t width, uint32_t height, uint32_t /*fmt*/,
        rsx_texture_override_image* out) {
    State& s = st();
    if (!s.loaded.load()) return 0;
    size_t id = 0;
    for (; id < s.pages.size(); id++) {
        const bp::Page& p = s.pages[id];
        if (p.width == width && p.height == height && bytes == p.level0 &&
            std::memcmp(guest, p.data.data(), p.level0) == 0)
            break;
    }
    if (id == s.pages.size()) return 0;
    std::lock_guard<std::mutex> lk(s.mu);
    if (!s.seen[id]) {
        s.seen[id] = 1;
        s.seen_serial++;
        s.cv.notify_one();
        std::fprintf(stderr, "[icons] the game's button page: %s, page %d\n", s.pages[id].from.c_str(),
                     s.pages[id].index);
    }
    if (s.spec.kind == bp::Kind::PlayStation) return 0;
    const std::string key = key_of(s.spec, id);
    for (Built& b : s.cache) {
        if (b.key != key) continue;
        b.used = ++s.tick;
        s.given = b.image;
        out->rgba = b.image->rgba.data();
        out->width = b.image->width;
        out->height = b.image->height;
        out->levels = b.image->levels;
        return 1;
    }
    return 0;   // being drawn: the renderer asks again once it is
}

void worker() {
    State& s = st();
    std::vector<bp::Page> pages = load_pages();
    if (pages.empty()) {
        std::fprintf(stderr, "[icons] no button page in the game's font files: the game's own icons show\n");
        return;
    }
    {
        std::lock_guard<std::mutex> lk(s.mu);
        s.pages = std::move(pages);
        s.seen.assign(s.pages.size(), 0);
    }
    s.loaded.store(true);
    std::vector<std::pair<uint32_t, uint32_t>> shapes;
    for (const bp::Page& p : s.pages) {
        bool known = false;
        for (const auto& sh : shapes) known = known || (sh.first == p.width && sh.second == p.height);
        if (!known) {
            shapes.push_back({p.width, p.height});
            rsx_texture_override_watch(p.width, p.height, p.fmt, ask, nullptr);
        }
    }
    const Asset* font = find_asset("res/fonts/AlegreyaSans-Bold.ttf");
    std::vector<std::vector<uint8_t>> decoded(s.pages.size());
    unsigned done_spec = ~0u, done_seen = ~0u;
    for (;;) {
        std::unique_lock<std::mutex> lk(s.mu);
        s.cv.wait(lk, [&] { return s.spec_serial != done_spec || s.seen_serial != done_seen; });
        done_spec = s.spec_serial;
        done_seen = s.seen_serial;
        const bp::Spec spec = s.spec;
        if (spec.kind == bp::Kind::PlayStation) continue;
        std::vector<size_t> todo;
        for (size_t i = 0; i < s.pages.size(); i++) {
            if (!s.seen[i]) continue;
            const std::string key = key_of(spec, i);
            if (std::none_of(s.cache.begin(), s.cache.end(), [&](const Built& b) { return b.key == key; }))
                todo.push_back(i);
        }
        lk.unlock();
        bool drew = false;
        for (size_t i : todo) {
            const auto t0 = std::chrono::steady_clock::now();
            if (decoded[i].empty()) decoded[i] = bp::decode(s.pages[i]);
            auto image = std::make_shared<bp::Image>(bp::draw(s.pages[i], decoded[i], spec,
                font ? reinterpret_cast<const unsigned char*>(font->data) : nullptr));
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            lk.lock();
            if (s.cache.size() >= kKeep) {
                s.cache.erase(std::min_element(s.cache.begin(), s.cache.end(),
                                               [](const Built& a, const Built& b) { return a.used < b.used; }));
            }
            s.cache.push_back({key_of(spec, i), std::move(image), ++s.tick});
            lk.unlock();
            std::fprintf(stderr, "[icons] drew the button page for %s in %.0f ms\n",
                         spec.kind == bp::Kind::Keyboard ? "the keyboard" : bp::describe(spec).c_str(), ms);
            drew = true;
        }
        if (drew) rsx_texture_override_changed();
    }
}

// A setting's value: its environment variable first, read each time (the live rows set it, and a test can change it
// through the debug console), then settings.ini's.
std::string value(const char* key) {
    const char* env = std::getenv(key);
    return env && *env ? std::string(env) : settings::get(key);
}

int pad_index(const std::string& name) {
    for (int i = 0; i < 16; i++) {
        const char* n = bp::kPadNames[i];
        if (name.size() == std::strlen(n) &&
            std::equal(name.begin(), name.end(), n, [](char a, char b) { return std::tolower((unsigned char)a) == b; }))
            return i;
    }
    return -1;
}

bp::Spec make_spec(int device) {
    bp::Spec s;
    const std::string choice = value("DES_GLYPHS");
    if (choice == "playstation") s.kind = bp::Kind::PlayStation;
    else if (choice == "xbox") s.kind = bp::Kind::Xbox;
    else if (choice == "nintendo") s.kind = bp::Kind::Nintendo;
    else if (choice == "keyboard") s.kind = bp::Kind::Keyboard;
    else s.kind = device == 1 ? bp::Kind::Keyboard : device == 3 ? bp::Kind::Xbox
                : device == 4 ? bp::Kind::Nintendo : bp::Kind::PlayStation;
    const int percent = std::atoi(value("RSX_SCALE").c_str());
    s.scale = std::clamp((percent + 99) / 100, 1, 4);
    for (int i = 0; i < 16; i++) s.pad_map[i] = i;
    // PAD_MAP: "game=pad" pairs, as cellPad reads them.
    const std::string map = value("PAD_MAP");
    for (size_t at = 0; at < map.size();) {
        size_t end = map.find_first_of(", ;", at);
        if (end == std::string::npos) end = map.size();
        const std::string pair = map.substr(at, end - at);
        if (const size_t eq = pair.find('='); eq != std::string::npos) {
            const int game = pad_index(pair.substr(0, eq)), pad = pad_index(pair.substr(eq + 1));
            if (game >= 0 && pad >= 0) s.pad_map[game] = pad;
        }
        at = end + 1;
    }
    for (int t = 0; t < bp::kKeyTargets; t++) {
        const KeyInput k = first_key(bp::kKeyTargetNames[t]);
        s.keys[t] = {k.in, k.mod, key_cap_label(k.in), k.mod ? key_cap_label(k.mod) : std::string()};
    }
    return bp::normalized(s);
}

}  // namespace

void start() {
    State& s = st();
    if (s.started.exchange(true)) return;
    std::thread(worker).detach();
}

void poll() {
    State& s = st();
    if (!s.loaded.load()) return;
    const int device = cellPad_host_glyph_device();
    if (device == s.last_device && ++s.frame % 30 != 0) return;   // the settings and bindings twice a second
    s.last_device = device;
    const bp::Spec spec = make_spec(device);
    bool ready = true;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        if (spec == s.spec) return;
        s.spec = spec;
        s.spec_serial++;
        for (size_t i = 0; i < s.pages.size() && spec.kind != bp::Kind::PlayStation; i++) {
            const std::string key = key_of(spec, i);
            if (s.seen[i] &&
                std::none_of(s.cache.begin(), s.cache.end(), [&](const Built& b) { return b.key == key; }))
                ready = false;
        }
    }
    s.cv.notify_one();
    static const char* const kNames[] = {"PlayStation (the game's own)", "Xbox", "Nintendo", "keyboard and mouse"};
    std::fprintf(stderr, "[icons] button icons: %s\n", kNames[int(spec.kind)]);
    if (ready) rsx_texture_override_changed();   // (otherwise the thread does, once it has drawn the page)
}

}  // namespace ui::button_icons
