// The menus' looks on their own (looks.hpp, res/rml/looks): RmlUi with the UI's embedded files, no window, no
// renderer. It checks that:
// - every look in kLooks has its sheet, and every sheet in res/rml/looks belongs to a look;
// - each sheet's rules are all inside @media (theme: <its id>), maybe with more conditions after it
//   ("and (max-height: 640dp)"), so a look never changes another one;
// - RmlUi reads each sheet without a warning;
// - a theme switched on and off changes an element's style and changes it back, which is how the looks are switched
//   (looks::apply);
// - the F1 menu's bar, which slides by a transform in dp, sits right after a new dp ratio (UI scale). This needs
//   port/ui/cmake/rmlui_fixes.cmake's fix 1 in RmlUi.
//
// Prints a line per look, a line per failure and "ui_style_check: N looks, M failed"; exit 1 on a failure. Built on
// demand: cmake --build <build> --target ui_style_check, then ui_style_check.
#include "looks.hpp"
#include "ui_assets.hpp"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_fail = 0;
int g_warnings = 0;
double g_now = 0.0;  // (RmlUi's clock, in seconds: the transitions' check moves it on)

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL %s\n", what.c_str());
        g_fail++;
    }
}

class System final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return g_now; }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type <= Rml::Log::LT_WARNING) {
            std::printf("  [rml] %s\n", message.c_str());
            g_warnings++;
        }
        return true;
    }
};

// The UI's embedded files, as host.cpp serves them (paths relative to port/ui, "res/rml/...").
class Files final : public Rml::FileInterface {
    struct Handle {
        const ui::Asset* asset;
        size_t pos;
    };

public:
    Rml::FileHandle Open(const Rml::String& path) override {
        const ui::Asset* a = ui::find_asset(path.front() == '/' ? path.substr(1) : path);
        return a ? reinterpret_cast<Rml::FileHandle>(new Handle{a, 0}) : 0;
    }
    void Close(Rml::FileHandle file) override { delete reinterpret_cast<Handle*>(file); }
    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        auto* f = reinterpret_cast<Handle*>(file);
        const size_t n = std::min(size, f->asset->size - f->pos);
        std::memcpy(buffer, f->asset->data + f->pos, n);
        f->pos += n;
        return n;
    }
    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        auto* f = reinterpret_cast<Handle*>(file);
        const long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? long(f->pos) : long(f->asset->size);
        if (base + offset < 0 || size_t(base + offset) > f->asset->size) return false;
        f->pos = size_t(base + offset);
        return true;
    }
    size_t Tell(Rml::FileHandle file) override { return reinterpret_cast<Handle*>(file)->pos; }
};

// Draws nothing: the check reads styles and layout only.
class NoRender final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String&) override {
        dimensions = {1, 1};
        return 1;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 1; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

std::string text_of(const ui::Asset& a) {
    return std::string(reinterpret_cast<const char*>(a.data), a.size);
}

// The sheet without its comments.
std::string without_comments(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (s.compare(i, 2, "/*") == 0) {
            const size_t end = s.find("*/", i + 2);
            i = end == std::string::npos ? s.size() : end + 2;
        } else {
            out += s[i++];
        }
    }
    return out;
}

// True when everything in the sheet is inside @media (theme: id) blocks, "and (...)" conditions after it allowed.
bool all_inside_theme(const std::string& sheet, const std::string& id) {
    const std::string s = without_comments(sheet);
    size_t i = 0;
    const auto skip_space = [&] {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    };
    const auto take = [&](const std::string& word) {
        skip_space();
        if (s.compare(i, word.size(), word) != 0) return false;
        i += word.size();
        return true;
    };
    int blocks = 0;
    for (;;) {
        skip_space();
        if (i == s.size()) return blocks > 0;
        if (!take("@media") || !take("(") || !take("theme") || !take(":") || !take(id) || !take(")"))
            return false;
        // More conditions may follow the theme ("@media (theme: des) and (max-height: 640dp)"): still the look's own.
        while (take("and")) {
            if (!take("(")) return false;
            while (i < s.size() && s[i] != ')') i++;
            if (!take(")")) return false;
        }
        if (!take("{")) return false;
        for (int depth = 1; depth > 0; i++) {  // (to the block's closing brace)
            if (i == s.size()) return false;
            depth += s[i] == '{' ? 1 : s[i] == '}' ? -1 : 0;
        }
        blocks++;
    }
}

}  // namespace

int main() {
    System system;
    Files files;
    NoRender render;
    Rml::SetSystemInterface(&system);
    Rml::SetFileInterface(&files);
    Rml::SetRenderInterface(&render);
    if (!Rml::Initialise()) {
        std::printf("ui_style_check: RmlUi did not start\n");
        return 2;
    }

    int looks = 0;
    for (const ui::looks::Look& look : ui::looks::kLooks) {
        const std::string id(look.id);
        const std::string path = "res/rml/looks/" + id + ".rcss";
        std::printf("%s (%s%s)\n", id.c_str(), std::string(look.label).c_str(), look.released ? "" : ", not released");
        looks++;
        const ui::Asset* a = ui::find_asset(path);
        check(a != nullptr, id + ": no " + path);
        if (!a) continue;
        check(all_inside_theme(text_of(*a), id), id + ": a rule outside @media (theme: " + id + ")");
        const int before = g_warnings;
        check(Rml::Factory::InstanceStyleSheetFile(path) != nullptr, id + ": RmlUi did not read its sheet");
        check(g_warnings == before, id + ": RmlUi warned about its sheet");
    }
    check(ui::looks::find(ui::looks::kDefault) && ui::looks::find(ui::looks::kDefault)->released,
        "the default look is not a released one");
    for (size_t i = 0; i < ui::kAssetCount; i++) {
        const std::string path = ui::kAssets[i].path;
        if (path.rfind("res/rml/looks/", 0) != 0) continue;
        const std::string id = path.substr(14, path.size() - 14 - 5);
        check(path.ends_with(".rcss") && ui::looks::find(id), path + " belongs to no look in looks.hpp");
    }

    // A theme switched on, then off: the element's style follows (the looks' sheets stand on this).
    Rml::Context* context = Rml::CreateContext("check", {1280, 720});
    Rml::ElementDocument* doc = context ? context->LoadDocumentFromMemory(R"(<rml><head><style>
        body { display: block; width: 100%; }
        #probe { display: block; width: 100px; height: 10px; }
        @media (theme: probe) { #probe { width: 7px; } }
        </style></head><body><div id="probe"/></body></rml>)")
                                        : nullptr;
    check(doc != nullptr, "the probe document did not load");
    if (doc) {
        doc->Show();
        const auto width = [&] {
            context->Update();
            return doc->GetElementById("probe")->GetBox().GetSize().x;
        };
        const float plain = width();
        context->ActivateTheme("probe", true);
        const float themed = width();
        context->ActivateTheme("probe", false);
        const float back = width();
        check(plain == 100.f && themed == 7.f && back == 100.f,
            "the probe's width " + std::to_string(plain) + ", themed " + std::to_string(themed) + ", after " +
                std::to_string(back) + " (100, 7, 100)");
    }

    // The F1 menu's bar (Alpha UI: menu.rcss's menu-entries) after a new dp ratio (a new UI scale). The steps follow the
    // game:
    // - the document and its sheets are made as menu_shell.cpp and document.cpp make them;
    // - the body's classes go as MenuShell::draw writes them: closed, the bar open (entries), then a page (Settings).
    //   Closed and on a page, the bar sits its height and a dp up (translateY(-65dp));
    // - each frame is an update and a draw;
    // - a new ratio is set as host.cpp's render sets it: the ratio, then each document's sheets again
    //   (restyle_documents).
    // At each ratio the bar must be 65dp up, before a transition and after one.
    //
    // Without rmlui_fixes.cmake's fix 1, opening the bar turned the sheet's own translateY(-65dp) into px at that
    // moment's ratio, for good. After a larger UI scale, the bar then stayed partly in view under Settings.
    Rml::ElementDocument* menu = context ? context->LoadDocumentFromMemory(R"(<rml><head>
        <link type="text/rcss" href="res/rml/theme.rcss" />
        <link type="text/rcss" href="res/rml/controls.rcss" />
        <link type="text/rcss" href="res/rml/menu.rcss" />
        </head><body><menu-entries id="entries" /></body></rml>)")
                                         : nullptr;
    check(menu != nullptr, "the menu's document did not load");
    if (menu) {
        if (auto sheets = Rml::Factory::InstanceStyleSheetFile("res/rml/global.rcss")) {
            if (const auto* local = menu->GetStyleSheetContainer()) sheets = sheets->CombineStyleSheetContainer(*local);
            for (const ui::looks::Look& look : ui::looks::kLooks)
                if (auto own = Rml::Factory::InstanceStyleSheetFile("res/rml/looks/" + std::string(look.id) + ".rcss"))
                    sheets = sheets->CombineStyleSheetContainer(*own);
            menu->SetStyleSheetContainer(std::move(sheets));
        }
        for (const ui::looks::Look& look : ui::looks::kLooks)
            context->ActivateTheme(std::string(look.id), look.id == "alpha");
        menu->Show();
        Rml::Element* bar = menu->GetElementById("entries");
        // One frame. RmlUi works a transform out when it draws (NoRender draws nothing).
        const auto frame = [&] {
            context->Update();
            context->Render();
        };
        // How far the bar's transform moves it, in px. Project takes a window point into the element's own space, so
        // a translation t takes (0, 0) to -t.
        const auto bar_y = [&] {
            frame();
            Rml::Vector2f p(0.f, 0.f);
            bar->Project(p);
            return -p.y;
        };
        // New classes on the body, then half a second in 50 ms frames, for the transition to end. RmlUi moves an
        // animation on by at most 0.1 s per update (ElementAnimation.cpp).
        const auto go = [&](const char* classes) {
            menu->SetClassNames(classes);
            for (int i = 0; i < 10; i++) {
                g_now += 0.05;
                frame();
            }
        };
        static const Rml::StyleSheetContainer kNothing;
        go("closed motion");
        for (const float ratio : {1.0f, 2.0f, 1.5f}) {
            context->SetDensityIndependentPixelRatio(ratio);
            if (const Rml::StyleSheetContainer* sheet = menu->GetStyleSheetContainer())
                menu->SetStyleSheetContainer(sheet->CombineStyleSheetContainer(kNothing));
            for (int pass = 0; pass < 2; pass++) {  // (before and after a transition at this ratio)
                const float y = bar_y();
                check(std::fabs(y + 65.f * ratio) < 0.01f, "the F1 bar at dp ratio " + std::to_string(ratio) +
                                                               (pass ? ", after it opened and closed" : "") + ": " +
                                                               std::to_string(y) + " px (" +
                                                               std::to_string(-65.f * ratio) + ")");
                go("entries motion");
                go("page motion");
            }
        }
    }

    Rml::Shutdown();
    std::printf("ui_style_check: %d looks, %d failed\n", looks, g_fail);
    return g_fail ? 1 : 0;
}
