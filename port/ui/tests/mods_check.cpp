// mods.cpp on its own (docs/143): which of a mod's files change how the game plays, where in a mod folder the game's
// files are (laid out flat, in a wrapper folder, or in PS3_GAME\USRDIR), what makes a mod unusable, the order and the
// files two mods share, and the PS3_VFS_MODS that apply() sets from the mods that are on.
//
// Each case runs in made-up folders under the system's temporary folder, removed at the end. Prints one line a case
// and "mods_check: N passed, M failed"; exit 1 on a failure. The same on every platform (CI runs it on Windows and
// Linux, host.yml). Built on demand: cmake --build <build> --target mods_check.
#include "mods.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace ui::mods;

// boot_main.cpp's, for mods.cpp: the process's environment here.
extern "C" void ps3_setenv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value && *value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

namespace {

int g_passed = 0, g_failed = 0;
fs::path g_root;

void write(const fs::path& file, const std::string& text = "x") {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

// A case: its own folder under the root, made empty first, and no mods in the environment.
void run(const char* name, const std::function<std::string(const fs::path&)>& body) {
    std::error_code ec;
    const fs::path dir = g_root / name;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    ps3_setenv("PS3_VFS_MODS", "");
    ps3_setenv("DES_MODS_DIR", "");
    std::string problem;
    try {
        problem = body(dir);
    } catch (const std::exception& e) {
        problem = std::string("threw: ") + e.what();
    }
    if (problem.empty()) {
        ++g_passed;
        std::printf("ok    %s\n", name);
    } else {
        ++g_failed;
        std::printf("FAIL  %s: %s\n", name, problem.c_str());
    }
}

std::string expect(bool ok, const std::string& what) {
    return ok ? std::string() : what;
}

std::string all(std::initializer_list<std::string> problems) {
    for (const std::string& p : problems)
        if (!p.empty()) return p;
    return {};
}

}  // namespace

int main() {
    std::error_code ec;
    g_root = fs::temp_directory_path(ec) / ("mods_check-" + std::to_string(getpid()));
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root, ec);

    // ---- Look or gameplay, a file at a time ----
    run("kinds", [](const fs::path&) {
        struct Case {
            const char* rel;
            bool gameplay;
        };
        const Case cases[] = {
            {"menu/menu.tpf.dcx", false},
            {"chr/c2400/c2400.tpf", false},          // a texture, even among the characters
            {"other/systex_tex.tpf.dcx", false},
            {"parts/pc_a_1000.partsbnd.dcx", false},
            {"msg/naenglish/menu.msgbnd.dcx", false},
            {"font/naenglish/font.ccm", false},
            {"sfx/frpg_sfxbnd_m01.ffxbnd.dcx", false},
            {"sound/frpg_main.fsb", false},
            {"param/drawparam/m01_drawparam.parambnd.dcx", false},
            {"PARAM/DrawParam/M01.parambnd", false},   // any case
            {"map/m01_00_00_00/m0100b0a01.flver.dcx", false},
            {"map/mapstudio/m01_00_00_00.msb", true},
            {"map/m01_00_00_00/h0100b0a01.hkxbhd", true},
            {"param/gameparam/gameparamna.parambnd.dcx", true},
            {"chr/c0000.anibnd.dcx", true},
            {"script/talk/m01_00_00_00.talkesdbnd", true},
            {"obj/o0100.objbnd", true},
            {"loose.bin", true},                       // beside the game's folders: not known
            {"dds/x.dds", false},
        };
        for (const Case& c : cases)
            if (gameplay_file(c.rel) != c.gameplay)
                return std::string(c.rel) + (c.gameplay ? " should be gameplay" : " should be look only");
        return std::string();
    });

    // ---- Where the game's files are ----
    run("flat", [](const fs::path& t) {
        write(t / "A" / "menu" / "menu.tpf.dcx");
        write(t / "A" / "msg" / "naenglish" / "menu.msgbnd.dcx");
        write(t / "A" / "readme.txt");    // the mod's notes: not its files
        write(t / "A" / "preview.jpg");
        write(t / "A" / "desktop.ini");
        const Mod m = read(t / "A");
        return all({expect(m.problem.empty(), "problem: " + m.problem), expect(m.root == t / "A", "root " + utf8(m.root)),
            expect(m.files == 2, "files " + std::to_string(m.files)), expect(m.kind == Kind::Look, "not look only"),
            expect(m.areas == std::vector<std::string>{"menu", "msg"}, "areas " + join(m.areas, ","))});
    });
    run("wrapper", [](const fs::path& t) {
        write(t / "A" / "Some Mod v1.2" / "parts" / "x.partsbnd.dcx");
        write(t / "A" / "Some Mod v1.2" / "map" / "m01_00_00_00" / "m.flver");
        write(t / "A" / "Install.txt");
        const Mod m = read(t / "A");
        return all({expect(m.problem.empty(), "problem: " + m.problem),
            expect(m.root == t / "A" / "Some Mod v1.2", "root " + utf8(m.root)),
            expect(m.files == 2, "files " + std::to_string(m.files)), expect(m.kind == Kind::Look, "not look only")});
    });
    run("usrdir", [](const fs::path& t) {
        write(t / "A" / "PS3_GAME" / "USRDIR" / "param" / "gameparam" / "gameparamna.parambnd.dcx");
        write(t / "A" / "PS3_GAME" / "USRDIR" / "menu" / "m.tpf");
        const Mod m = read(t / "A");
        return all({expect(m.problem.empty(), "problem: " + m.problem),
            expect(m.root == t / "A" / "PS3_GAME" / "USRDIR", "root " + utf8(m.root)),
            expect(m.kind == Kind::Gameplay, "not gameplay"),
            expect(m.gameplay == std::vector<std::string>{"param/gameparam/gameparamna.parambnd.dcx"},
                "gameplay files " + join(m.gameplay, ","))});
    });
    run("usrdir_any_case", [](const fs::path& t) {
        write(t / "A" / "BLUS30443" / "usrdir" / "sfx" / "x.ffxbnd");
        const Mod m = read(t / "A");
        return all({expect(m.problem.empty(), "problem: " + m.problem),
            expect(m.root == t / "A" / "BLUS30443" / "usrdir", "root " + utf8(m.root))});
    });
    run("deep_with_backup", [](const fs::path& t) {   // (a version's folder, and the game's files to undo it)
        write(t / "A" / "Other Mod 1.0" / "Modded" / "PS3_GAME" / "USRDIR" / "font" / "eu" / "f.tpf.dcx");
        write(t / "A" / "Other Mod 1.0" / "Original" / "PS3_GAME" / "USRDIR" / "font" / "eu" / "f.tpf.dcx");
        write(t / "A" / "Other Mod 1.0" / "Backup files" / "menu" / "m.tpf");
        const Mod m = read(t / "A");
        return all({expect(m.problem.empty(), "problem: " + m.problem),
            expect(m.root == t / "A" / "Other Mod 1.0" / "Modded" / "PS3_GAME" / "USRDIR", "root " + utf8(m.root)),
            expect(m.files == 1, "files " + std::to_string(m.files))});
    });
    run("unicode_name", [](const fs::path& t) {
        const fs::path dir = t / from_utf8("Texturas \xc3\xa9picas \xe6\x97\xa5\xe6\x9c\xac");
        write(dir / "menu" / "m.tpf");
        const Mod m = read(dir);
        return all({expect(m.problem.empty(), "problem: " + m.problem),
            expect(m.name == "Texturas \xc3\xa9picas \xe6\x97\xa5\xe6\x9c\xac", "name " + m.name),
            expect(m.files == 1, "files " + std::to_string(m.files))});
    });

    // ---- Mods that can't be used ----
    run("eboot", [](const fs::path& t) {
        write(t / "A" / "EBOOT.BIN");
        write(t / "A" / "menu" / "m.tpf");
        const Mod m = read(t / "A");
        return expect(m.problem.find("EBOOT.BIN") != std::string::npos, "no EBOOT.BIN problem: " + m.problem);
    });
    run("two_copies", [](const fs::path& t) {
        write(t / "A" / "v1" / "menu" / "m.tpf");
        write(t / "A" / "v2" / "menu" / "m.tpf");
        const Mod m = read(t / "A");
        return all({expect(m.problem.find("v1") != std::string::npos && m.problem.find("v2") != std::string::npos,
                        "the copies not named: " + m.problem),
            expect(m.root.empty(), "a root: " + utf8(m.root))});
    });
    run("no_game_files", [](const fs::path& t) {
        write(t / "A" / "textures" / "x.dds");
        const Mod m = read(t / "A");
        return all({expect(!m.problem.empty(), "no problem"), expect(m.root.empty(), "a root: " + utf8(m.root))});
    });
    run("empty_folders", [](const fs::path& t) {
        fs::create_directories(t / "A" / "menu");
        const Mod m = read(t / "A");
        return expect(m.problem == "It has no files.", "problem: " + m.problem);
    });
#ifndef _WIN32
    run("bar_in_name", [](const fs::path& t) {   // (Windows names can't have one)
        write(t / "A|B" / "menu" / "m.tpf");
        const Mod m = read(t / "A|B");
        return expect(m.problem.find('|') != std::string::npos, "no problem with the '|': " + m.problem);
    });
#endif

    // ---- The mods folder ----
    run("scan_order", [](const fs::path& t) {
        write(t / "mods" / "b mod" / "menu" / "m.tpf");
        write(t / "mods" / "a mod" / "param" / "p.parambnd");
        write(t / "mods" / "notes.txt");   // (a file, not a mod)
        const std::vector<Mod> all_ = scan(t / "mods", "b mod|gone");
        return all({expect(all_.size() == 2, std::to_string(all_.size()) + " mods"),
            expect(all_.size() == 2 && all_[0].name == "a mod" && all_[1].name == "b mod", "not A to Z"),
            expect(all_.size() == 2 && !all_[0].enabled && all_[1].enabled, "enabled marks wrong")});
    });
    run("scan_missing_folder", [](const fs::path& t) {
        return all({expect(scan(t / "nothing", "x").empty(), "mods from nowhere"), expect(scan({}, "x").empty(), "mods from \"\"")});
    });
    run("folder", [](const fs::path& t) {
        const std::string a = utf8(folder(t)), b = utf8(t / "mods");
        ps3_setenv("DES_MODS_DIR", utf8(t / "elsewhere").c_str());
        const std::string c = utf8(folder(t));
        ps3_setenv("DES_MODS_DIR", "");
        return all({expect(a == b, "folder " + a), expect(c == utf8(t / "elsewhere"), "DES_MODS_DIR ignored: " + c),
            expect(folder({}).empty(), "a folder without a data folder")});
    });
    run("conflicts", [](const fs::path& t) {
        write(t / "mods" / "A" / "menu" / "same.tpf");
        write(t / "mods" / "A" / "menu" / "a_only.tpf");
        write(t / "mods" / "B" / "Menu" / "Same.tpf");   // the same file, another case
        write(t / "mods" / "B" / "readme.txt");
        write(t / "mods" / "A" / "readme.txt");          // (notes beside the folders: not a conflict)
        const std::vector<std::string> c = conflicts(scan(t / "mods", ""));
        return expect(c == std::vector<std::string>{"menu/same.tpf: A, B"}, "conflicts: " + join(c, " / "));
    });

    // ---- At start ----
    run("apply_on", [](const fs::path& t) {
        write(t / "mods" / "B gameplay" / "param" / "gameparam" / "g.parambnd.dcx");
        write(t / "mods" / "A look" / "x" / "PS3_GAME" / "USRDIR" / "menu" / "m.tpf");
        write(t / "mods" / "C off" / "menu" / "m.tpf");
        apply(t, "B gameplay|A look|Gone");
        const std::string want = utf8(t / "mods" / "A look" / "x" / "PS3_GAME" / "USRDIR") + "|" +
                                 utf8(t / "mods" / "B gameplay");
        return all({expect(env("PS3_VFS_MODS") == want, "PS3_VFS_MODS " + env("PS3_VFS_MODS")),
            expect(in_use().size() == 2, std::to_string(in_use().size()) + " in use"),
            expect(gameplay_in_use(), "no gameplay mod"), expect(gameplay_names() == "B gameplay", gameplay_names()),
            expect(!from_environment(), "from the environment"),
            expect(summary() == "2: A look (look, 1 files), B gameplay (gameplay, 1 files)", summary())});
    });
    run("apply_look_only", [](const fs::path& t) {
        write(t / "mods" / "A" / "menu" / "m.tpf");
        write(t / "mods" / "B" / "param" / "p.parambnd");   // a gameplay mod, but off
        apply(t, "A");
        return all({expect(!gameplay_in_use(), "gameplay"), expect(in_use().size() == 1, "in use")});
    });
    run("apply_unusable_left_out", [](const fs::path& t) {
        write(t / "mods" / "A" / "EBOOT.BIN");
        write(t / "mods" / "A" / "param" / "p.parambnd");
        apply(t, "A");
        return all({expect(env("PS3_VFS_MODS").empty(), "PS3_VFS_MODS " + env("PS3_VFS_MODS")),
            expect(in_use().empty(), "in use"), expect(!gameplay_in_use(), "gameplay"),
            expect(summary() == "none", summary())});
    });
    run("apply_none", [](const fs::path& t) {
        write(t / "mods" / "A" / "menu" / "m.tpf");
        apply(t, "");
        apply({}, "A");   // (no data folder: no mods folder)
        return all({expect(env("PS3_VFS_MODS").empty(), "PS3_VFS_MODS " + env("PS3_VFS_MODS")),
            expect(in_use().empty(), "in use")});
    });
    run("apply_from_environment", [](const fs::path& t) {
        write(t / "x" / "param" / "p.parambnd");
        write(t / "mods" / "A" / "menu" / "m.tpf");
        ps3_setenv("PS3_VFS_MODS", utf8(t / "x").c_str());
        apply(t, "A");   // (DES_MODS is not read then)
        return all({expect(from_environment(), "not from the environment"),
            expect(env("PS3_VFS_MODS") == utf8(t / "x"), "PS3_VFS_MODS changed: " + env("PS3_VFS_MODS")),
            expect(in_use().size() == 1 && in_use()[0].name == "x", "in use"),
            expect(gameplay_in_use(), "not gameplay")});
    });

    fs::remove_all(g_root, ec);
    std::printf("mods_check: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
