// setup_rpcs3.cpp on its own: the player's RPCS3 as setup's card finds it when the player picks its folder
// (rpcs3_at): the program, the folder the card shows, a portable install, and Demon's Souls in RPCS3's game list, in
// each platform's own layout. Windows: rpcs3.exe in a folder of its own, its settings beside it. Linux: rpcs3 in a
// folder, its settings in ~/.config/rpcs3. A Mac: RPCS3.app (the folder that holds it, or the app itself), its
// settings in ~/Library/Application Support/rpcs3, and portable/ beside the app (File.cpp: RPCS3's executable path is
// the app there).
//
// Each case runs in made-up folders under the system's temporary folder, removed at the end; HOME points there (not
// on Windows). Prints one line a case and "setup_rpcs3_check: N passed, M failed"; exit 1 on a failure. CI runs it
// on Windows, Linux (host.yml) and a Mac (mac-native.yml). Built on demand: cmake --build <build> --target
// setup_rpcs3_check.
#include "setup_rpcs3.hpp"

#include "path_utf8.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace ui::setup;

namespace {

int gPassed = 0, gFailed = 0;

void expect(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    (ok ? gPassed : gFailed)++;
}

void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

std::string str(const fs::path& p) {
    return ui::path_utf8(p);
}

#ifndef _WIN32
void set_env(const char* name, const std::string& value) {
    setenv(name, value.c_str(), 1);
}
#endif

void unset_env(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

}  // namespace

int main() {
    std::error_code ec;
    const fs::path base = fs::temp_directory_path() / ("setup_rpcs3_check-" + std::to_string(getpid()));
    fs::remove_all(base, ec);
    const fs::path home = base / "home", disc = base / "games" / "BLUS30443";
    put(disc / "PS3_GAME" / "PARAM.SFO", "a stand-in");
    const std::string games = "BLUS30443: " + str(disc) + "/\n";
    unset_env("RPCS3_CONFIG_DIR");
    unset_env("XDG_CONFIG_HOME");
#ifndef _WIN32
    set_env("HOME", str(home));
#endif

#ifdef _WIN32
    const fs::path folder = base / "RPCS3", program = folder / "rpcs3.exe";
    const fs::path settings = folder;  // (no RPCS3_CONFIG_DIR: the program's folder)
    const fs::path portableAt = folder;
    put(program, "a stand-in");
    put(settings / "config" / "games.yml", games);
    expect(rpcs3_program_shown() == "rpcs3.exe", "the pages name rpcs3.exe");
#elif defined(__APPLE__)
    const fs::path apps = base / "Applications", app = apps / "RPCS3.app";
    const fs::path folder = app, program = app / "Contents" / "MacOS" / "rpcs3";
    const fs::path settings = home / "Library" / "Application Support" / "rpcs3";
    const fs::path portableAt = apps;
    put(program, "a stand-in");
    put(settings / "games.yml", games);
    expect(rpcs3_program_shown() == "RPCS3.app", "the pages name RPCS3.app");
    {
        const Rpcs3 r = rpcs3_at(apps);
        expect(r.found() && r.program == program && r.folder == app,
               "the folder that holds RPCS3.app: its program, and the app on the card (" + str(r.program) + ", " +
                   str(r.folder) + ")");
    }
    {
        const fs::path other = base / "Downloads" / "rpcs3-v0.0.38-macos.app" / "Contents" / "MacOS" / "rpcs3";
        put(other, "a stand-in");
        const Rpcs3 r = rpcs3_at(base / "Downloads");
        expect(r.found() && r.program == other, "an app by another name: " + str(r.program));
    }
    {
        put(base / "Other" / "Safari.app" / "Contents" / "MacOS" / "Safari", "a stand-in");
        expect(!rpcs3_at(base / "Other" / "Safari.app").found() && !rpcs3_at(base / "Other").found(),
               "another app is not RPCS3");
    }
#else
    const fs::path folder = base / "RPCS3", program = folder / "rpcs3";
    const fs::path settings = home / ".config" / "rpcs3";
    const fs::path portableAt = folder;
    put(program, "a stand-in");
    put(settings / "games.yml", games);
    expect(rpcs3_program_shown() == "rpcs3 or RPCS3's AppImage", "the pages name rpcs3 or its AppImage");
#endif

    for (const fs::path& given : {folder, program}) {
        const Rpcs3 r = rpcs3_at(given);
        expect(r.found() && r.program == program && r.folder == folder && !r.portable,
               "picked " + str(given) + ": " + str(r.program) + ", on the card " + str(r.folder));
        expect(r.library == Rpcs3::Library::Folder && r.game == disc,
               "Demon's Souls from RPCS3's game list: " + str(r.game));
        expect(rpcs3_arg(r) == str(program), "the engine gets the program: " + rpcs3_arg(r));
    }
    fs::create_directories(base / "empty");
    expect(!rpcs3_at(base / "empty").found(), "an empty folder holds no RPCS3");

    // portable/ where RPCS3 looks for it (beside the app, on a Mac): its settings win.
    const fs::path other = base / "games" / "BLES00932";
    put(other / "PS3_GAME" / "PARAM.SFO", "a stand-in");
#ifdef _WIN32
    put(portableAt / "portable" / "config" / "games.yml", "BLES00932: " + str(other) + "/\n");
#else
    put(portableAt / "portable" / "games.yml", "BLES00932: " + str(other) + "/\n");
#endif
    {
        const Rpcs3 r = rpcs3_at(folder);
        expect(r.found() && r.portable, "portable/ beside " + str(portableAt));
        expect(r.library == Rpcs3::Library::Folder && r.game == other, "the portable folder's game list: " + str(r.game));
    }

    fs::remove_all(base, ec);
    std::printf("setup_rpcs3_check: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed ? 1 : 0;
}
