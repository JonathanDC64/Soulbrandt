// The menus' looks on their own (looks.hpp, res/rml/looks): RmlUi with the UI's embedded files, no window, no
// renderer. It checks that:
// - every look in kLooks has its sheet, and every sheet in res/rml/looks belongs to a look;
// - each sheet's rules are all inside @media (theme: <its id>), so a look never changes another one;
// - RmlUi reads each sheet without a warning;
// - a theme switched on and off changes an element's style and changes it back, which is how the looks are switched
//   (looks::apply).
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

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL %s\n", what.c_str());
        g_fail++;
    }
}

class System final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return 0.0; }
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

// True when everything in the sheet is inside @media (theme: id) blocks.
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
        if (!take("@media") || !take("(") || !take("theme") || !take(":") || !take(id) || !take(")") || !take("{"))
            return false;
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

    Rml::Shutdown();
    std::printf("ui_style_check: %d looks, %d failed\n", looks, g_fail);
    return g_fail ? 1 : 0;
}
