// Written for Demon's Souls. The system's fonts for Japanese, Chinese and Korean text (text_fonts.hpp, docs/149).
//
// Each language has a list of font files to try, the first that loads is added to RmlUi as a fallback face under a
// family of its own, and RmlUi takes from it the characters the UI's fonts lack. Windows 10 and 11 have all of the
// first ones (Yu Gothic, Malgun Gothic, Microsoft JhengHei) in every edition. Elsewhere it is Noto Sans CJK (one file
// for all of them: faces 0 Japanese, 1 Korean, 2 Simplified and 3 Traditional Chinese) where distributions install
// it, or the older Droid Sans Fallback and WenQuanYi fonts, which have all three. On Windows the Linux paths are
// tried last through Z:, the host's root under Wine and Proton, whose Windows folder has none of these fonts.
//
// The file is memory-mapped and stays mapped for the run (FreeType reads the font where it lies, and RmlUi keeps the
// face until Rml::Shutdown): a font of 14-21 MB costs only the pages of the characters drawn.
#include "text_fonts.hpp"

#include "host.hpp"

#include <RmlUi/Core.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ui::host {
namespace {

enum Lang { kJapanese, kKorean, kChinese, kLangs };
constexpr const char* kLangName[kLangs] = {"Japanese", "Korean", "Chinese"};
constexpr const char* kFamily[kLangs] = {"des-system-ja", "des-system-ko", "des-system-zh"};

struct Candidate {
    const char* file;  // in the Windows fonts folder (a bare name), or a whole path
    int face;          // in a font collection (.ttc)
};

// The Windows fonts, best first: for Japanese, Japanese fonts and then the Chinese ones (they have kana too); for
// Chinese, Traditional (the Asian disc's), then Simplified, then the Japanese fonts; for Korean, the fonts with hangul.
constexpr Candidate kWindows[kLangs][6] = {
    {{"YuGothM.ttc", 0}, {"YuGothR.ttc", 0}, {"meiryo.ttc", 0}, {"msgothic.ttc", 0}, {"msjh.ttc", 0}, {"msyh.ttc", 0}},
    {{"malgun.ttf", 0}, {"gulim.ttc", 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}},
    {{"msjh.ttc", 0}, {"msyh.ttc", 0}, {"mingliu.ttc", 0}, {"simsun.ttc", 0}, {"YuGothM.ttc", 0}, {"msgothic.ttc", 0}},
};

// The Linux fonts: Noto Sans CJK where Debian and Ubuntu, Arch and SteamOS, and Fedora put it, then the older ones.
constexpr const char* kNoto[] = {
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc",
};
constexpr int kNotoFace[kLangs] = {0, 1, 3};
constexpr const char* kOlder[] = {
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/wenquanyi/wqy-zenhei/wqy-zenhei.ttc",
};

struct Mapped {
    const unsigned char* data = nullptr;
    size_t size = 0;
};

#ifdef _WIN32

std::wstring wide(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? size_t(n) : 1, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    w.resize(w.size() - 1);  // (the NUL)
    return w;
}

std::string fonts_folder() {
    wchar_t buf[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return "C:\\Windows\\Fonts\\";
    const int len = WideCharToMultiByte(CP_UTF8, 0, buf, int(n), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(len > 0 ? len : 0), '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, buf, int(n), out.data(), len, nullptr, nullptr);
    return out + "\\Fonts\\";
}

Mapped map_file(const std::string& path) {
    Mapped m;
    const HANDLE file = CreateFileW(wide(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return m;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 12 && size.QuadPart < (1ll << 31)) {
        if (const HANDLE map = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr)) {
            if (void* view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0)) {
                m.data = static_cast<const unsigned char*>(view);
                m.size = size_t(size.QuadPart);
            }
            CloseHandle(map);  // (the view keeps the mapping)
        }
    }
    CloseHandle(file);
    return m;
}

void unmap(const Mapped& m) {
    if (m.data) UnmapViewOfFile(m.data);
}

#else

Mapped map_file(const std::string& path) {
    Mapped m;
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return m;
    struct stat st {};
    if (fstat(fd, &st) == 0 && st.st_size > 12 && st.st_size < (1ll << 31)) {
        void* view = mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
        if (view != MAP_FAILED) {
            m.data = static_cast<const unsigned char*>(view);
            m.size = size_t(st.st_size);
        }
    }
    close(fd);  // (the mapping stays)
    return m;
}

void unmap(const Mapped& m) {
    if (m.data) munmap(const_cast<unsigned char*>(m.data), m.size);
}

#endif

// What is loaded, for the RmlUi that is up now. The mappings outlive it: a next init uses them again.
struct Fonts {
    bool tried[kLangs] = {};
    bool loaded[kLangs] = {};
    std::set<std::pair<std::string, int>> faces;  // (file, face) loaded, under any language
    std::map<std::string, Mapped> mapped;         // file -> its mapping, for the run
};
Fonts sFonts;

// The files to try for a language, best first.
std::vector<std::pair<std::string, int>> candidates(Lang lang) {
    std::vector<std::pair<std::string, int>> out;
    if (const char* own = std::getenv("DES_UI_CJK_FONT"); own && *own) out.emplace_back(own, 0);
    std::string root;
#ifdef _WIN32
    const std::string folder = fonts_folder();
    for (const Candidate& c : kWindows[lang])
        if (c.file) out.emplace_back(folder + c.file, c.face);
    root = "Z:";  // (Wine and Proton: the host's root)
#endif
    for (const char* p : kNoto) out.emplace_back(root + p, kNotoFace[lang]);
    for (const char* p : kOlder) out.emplace_back(root + p, 0);
#ifdef _WIN32
    for (auto& c : out)  // (Z:/usr/share/... as Z:\usr\share\...)
        for (char& ch : c.first)
            if (ch == '/') ch = '\\';
#endif
    return out;
}

bool load_face(Lang lang, const std::string& file, int face) {
    if (sFonts.faces.count({file, face})) return true;  // (the same face for another language: it is there already)
    auto it = sFonts.mapped.find(file);
    if (it == sFonts.mapped.end()) {
        const Mapped m = map_file(file);
        if (!m.data) return false;
        it = sFonts.mapped.emplace(file, m).first;
    }
    const Mapped& m = it->second;
    bool ok = Rml::LoadFontFace({m.data, m.size}, kFamily[lang], Rml::Style::FontStyle::Normal,
                                Rml::Style::FontWeight::Normal, true, face);
    if (!ok && face != 0) {  // (a collection with fewer faces than this list says: its first)
        face = 0;
        ok = sFonts.faces.count({file, face}) ||
             Rml::LoadFontFace({m.data, m.size}, kFamily[lang], Rml::Style::FontStyle::Normal,
                               Rml::Style::FontWeight::Normal, true, face);
    }
    if (!ok) {
        bool used = false;  // (unmapped unless another face of it is loaded)
        for (const auto& f : sFonts.faces) used = used || f.first == file;
        if (!used) {
            unmap(m);
            sFonts.mapped.erase(it);
        }
        return false;
    }
    sFonts.faces.insert({file, face});
    std::fprintf(stderr, "[ui] %s text: drawn with the system's font %s (face %d)\n", kLangName[lang], file.c_str(), face);
    return true;
}

void load(Lang lang) {
    if (sFonts.tried[lang]) return;
    sFonts.tried[lang] = true;
    for (const auto& [file, face] : candidates(lang)) {
        if (load_face(lang, file, face)) {
            sFonts.loaded[lang] = true;
            return;
        }
    }
    std::fprintf(stderr, "[ui] %s text, but none of the system's fonts for it was found, so it shows blank "
                         "(DES_UI_CJK_FONT=<font file> names one)\n", kLangName[lang]);
}

}  // namespace

void cover_text(std::string_view utf8) {
    const unsigned scripts = text_fonts::scripts_of(utf8);
    if (scripts == 0 || !is_initialized()) return;
    if (scripts & text_fonts::kKana) load(kJapanese);
    if (scripts & text_fonts::kHangul) load(kKorean);
    if ((scripts & text_fonts::kHan) && !sFonts.loaded[kJapanese] && !sFonts.loaded[kKorean] &&
        !sFonts.loaded[kChinese])
        load(kChinese);
}

void forget_text_fonts() {
    for (int l = 0; l < kLangs; l++) sFonts.tried[l] = sFonts.loaded[l] = false;
    sFonts.faces.clear();
}

}  // namespace ui::host
