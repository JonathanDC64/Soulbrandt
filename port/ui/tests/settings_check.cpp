// settings.cpp on its own (its report and privacy calls stubbed below): settings.ini files and environments in, the
// environment the game's runtime then reads out. For the Renderer row (Settings > Display, docs/120): RSX_BACKEND from
// the row reaches the runtime the way every Apply::Restart row does, an RSX_BACKEND from the environment wins, the data
// folder becomes RSX_STATE_DIR, and the row exists only in a build whose runtime has the Vulkan route. On Windows, on
// a GeForce card before the RTX 20 series (DES_RENDERER_GPU_ID gives the GPU's PCI IDs here; the other cases give
// none), the default is Vulkan, and a saved D3D12 becomes Vulkan once. The gpu-in-* cases are in that rule, the
// gpu-out-* cases are not. And a restart-only setting changed in the menu waits for the next start (restart_pending):
// not a live one, not one in effect already, not one pinned by the environment. And the Keyboard page's item slot keys
// (keys_for_pad): DES_ITEM_KEYS from the layout and PAD_KEYS's changes, without wheel notches and without an input a
// cellPad row has too, and none of the item rows in the PAD_KEYS cellPad reads. And seamless co-op's servers
// (TEST_SEAMLESS=host stands in for a hosting start): the endpoint for room codes (DES_SEAMLESS_INTRO) from the Server
// row, settings.ini's or the environment's value winning, and off in a test run; settings.ini's DES_SEAMLESS_STUN.
//
//   settings_check --row      a build with the row (DES_RSX_VULKAN: port/CMakeLists.txt found the Vulkan route)
//   settings_check --no-row   a build without it: nothing of it, and settings.ini's RSX_BACKEND is not applied
//
// Each case runs in a process of its own (the settings are read once a process), in a folder under the system's
// temporary folder that it removes. Prints one line a case and "settings_check: N passed, M failed"; exit 1 on a
// failure. Built on demand: cmake --build <build> --target settings_check.
#include "settings.hpp"
#include "../../des_seamless.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

extern "C" void ps3_setenv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value && *value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

// What settings.cpp needs of report.cpp and privacy.cpp. The data folder is TEST_DATA_DIR ("" = a test run's none).
namespace ui::report {
namespace {
std::filesystem::path g_dir;
bool g_dir_set = false;
}  // namespace
const std::filesystem::path& data_dir() {
    if (!g_dir_set) {
        const char* d = std::getenv("TEST_DATA_DIR");
        g_dir = d && *d ? std::filesystem::path(d) : std::filesystem::path();
        g_dir_set = true;
    }
    return g_dir;
}
void set_data_dir(const std::filesystem::path& dir) {
    g_dir = dir;
    g_dir_set = true;
}
std::string this_pc() { return "test-pc"; }
std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }  // (ASCII paths only here)
std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += char(c);
    return s;
}
FILE* open_file(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
    return _wfopen(path.c_str(), std::wstring(mode, mode + std::strlen(mode)).c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}
bool replace_file(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return !ec;
}
}  // namespace ui::report

namespace ui::privacy {
int version() { return 1; }
void set_needed(bool) {}
}  // namespace ui::privacy

// What settings.cpp needs of seamless co-op (des_seamless.cpp): off, or hosting with TEST_SEAMLESS=host.
namespace des::seamless {
Mode mode() {
    const char* v = std::getenv("TEST_SEAMLESS");
    return v && !std::strcmp(v, "host") ? Mode::Host : Mode::Off;
}
bool start(const std::string&, std::string*) { return mode() == Mode::Host; }
void held(const std::string&) {}
}  // namespace des::seamless

namespace {

namespace settings = ui::settings;

bool g_row = false;  // --row
int g_fail = 0;

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("    FAIL: %s\n", what.c_str());
        g_fail++;
    }
}

std::string read_text(const std::filesystem::path& p) {
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// One case, in this process: the case's folder is the working one (the parent made it, with settings.ini when the case
// has one, and set the environment).
int run_case(const std::string& name) {
    const std::string data = env("TEST_DATA_DIR");
    settings::load_and_apply_env();
    const bool has = settings::find("RSX_BACKEND") != nullptr;
    check(has == g_row, std::string("the Renderer row is ") + (has ? "there" : "not there"));
    if (name == "no-ini") {
#if defined(_WIN32)
        const std::string def = "d3d12";  // the runtime's default (rsx_out.c), which the row shows
#else
        const std::string def = "vulkan";
#endif
        check(env("RSX_BACKEND").empty(), "RSX_BACKEND unset (the runtime's default, " + def + "): " + env("RSX_BACKEND"));
        check(settings::get("RSX_BACKEND") == (g_row ? def : ""), "the row's default: " + settings::get("RSX_BACKEND"));
        check(env("RSX_STATE_DIR") == (g_row ? data : ""), "RSX_STATE_DIR: " + env("RSX_STATE_DIR"));
        const std::string d = settings::describe();
        check((d.find("RSX_BACKEND = " + def + "\n") != std::string::npos) == g_row, "settings.txt's line");
    } else if (name == "ini-vulkan") {
        check(env("RSX_BACKEND") == (g_row ? "vulkan" : ""), "RSX_BACKEND from settings.ini: " + env("RSX_BACKEND"));
        check(!settings::pinned("RSX_BACKEND"), "the row is not pinned");
        check(env("RSX_STATE_DIR") == (g_row ? data : ""), "RSX_STATE_DIR: " + env("RSX_STATE_DIR"));
    } else if (name == "ini-d3d12") {
        check(env("RSX_BACKEND") == (g_row ? "d3d12" : ""), "RSX_BACKEND from settings.ini: " + env("RSX_BACKEND"));
    } else if (name == "env-wins") {  // RSX_BACKEND=d3d12 in the environment, vulkan in settings.ini
        check(env("RSX_BACKEND") == "d3d12", "the environment's RSX_BACKEND kept: " + env("RSX_BACKEND"));
        check(settings::pinned("RSX_BACKEND") == g_row, "the row shows as set from outside");
        check(!g_row || settings::pinned_value("RSX_BACKEND") == "d3d12", "its value: " + settings::pinned_value("RSX_BACKEND"));
    } else if (name == "env-vulkan") {  // RSX_BACKEND=vulkan in the environment, no settings.ini
        check(env("RSX_BACKEND") == "vulkan", "the environment's RSX_BACKEND kept: " + env("RSX_BACKEND"));
    } else if (name == "env-hal") {  // a developer's RSX_BACKEND=hal-d3d12 (no row value of its own)
        check(env("RSX_BACKEND") == "hal-d3d12", "the environment's RSX_BACKEND kept: " + env("RSX_BACKEND"));
    } else if (name == "state-env-wins") {  // RSX_STATE_DIR in the environment
        check(env("RSX_STATE_DIR") == "elsewhere", "the environment's RSX_STATE_DIR kept: " + env("RSX_STATE_DIR"));
    } else if (name == "test-run") {  // no data folder
        check(env("RSX_STATE_DIR").empty(), "no RSX_STATE_DIR in a test run: " + env("RSX_STATE_DIR"));
    } else if (name.rfind("gpu-", 0) == 0) {  // gpu-<in|out>-<GPU>-<kind>
#if defined(_WIN32)
        const bool rule = g_row && name.rfind("gpu-in-", 0) == 0;
#else
        const bool rule = false;  // (Windows only: elsewhere Vulkan is every GPU's default)
#endif
        const std::string kind = name.substr(name.rfind('-') + 1);
        const std::string ini = read_text(std::filesystem::path(data) / "settings.ini");
        if (kind == "saved") {
            // 0.1.0's settings.ini: "d3d12" whether picked or not
            const std::string want = !g_row ? "" : rule ? "vulkan" : "d3d12";
            check(env("RSX_BACKEND") == want, "RSX_BACKEND " + want + ": " + env("RSX_BACKEND"));
            check((ini.find("RSX_BACKEND = vulkan\n") != std::string::npos) == rule, "settings.ini's line switched");
            check((ini.find("renderer_rule = 1\n") != std::string::npos) == rule, "the rule noted");
            check(!rule || settings::is_default("RSX_BACKEND"), "Vulkan shows as the default");
        } else if (kind == "picked") {  // D3D12 picked after the check: it stays
            check(env("RSX_BACKEND") == (g_row ? "d3d12" : ""), "RSX_BACKEND d3d12 kept: " + env("RSX_BACKEND"));
            check(ini.find("RSX_BACKEND = d3d12\n") != std::string::npos, "settings.ini kept");
            check(!rule || !settings::is_default("RSX_BACKEND"), "D3D12 shows as changed");
        } else if (kind == "noini") {  // never changed a setting
            check(env("RSX_BACKEND") == (rule ? "vulkan" : ""), "RSX_BACKEND: " + env("RSX_BACKEND"));
#if defined(_WIN32)
            const std::string def = rule ? "vulkan" : "d3d12";
#else
            const std::string def = "vulkan";
#endif
            check(settings::get("RSX_BACKEND") == (g_row ? def : ""), "the row's default: " + settings::get("RSX_BACKEND"));
            const std::string d = settings::describe();
            check((d.find("RSX_BACKEND = " + def + "\n") != std::string::npos) == g_row, "settings.txt's line: " + d);
        } else if (kind == "envwins") {  // RSX_BACKEND=d3d12 in the environment
            check(env("RSX_BACKEND") == "d3d12", "the environment's RSX_BACKEND kept: " + env("RSX_BACKEND"));
            check(ini.find("renderer_rule") == std::string::npos, "nothing noted");
        } else {
            std::printf("    unknown case %s\n", name.c_str());
            return 2;
        }
    } else if (name == "restart-waits") {  // restart-only settings changed in the menu wait for the next start
        using Keys = std::vector<std::string>;
        check(settings::restart_pending().empty(), "nothing waits at the start");
        settings::set("DES_LANGUAGE", "ja");
        settings::set("DES_FPS", "60");  // (live: never waits)
        check(settings::restart_pending() == Keys{"DES_LANGUAGE"}, "Language waits, alone");
        settings::set("DES_LANGUAGE", "");
        check(settings::restart_pending().empty(), "changed back: nothing waits");
        settings::set("DES_ONLINE", "0");
        settings::now_in_effect("DES_ONLINE");  // (as after the server deleted this player's data)
        check(settings::restart_pending().empty(), "in effect already: Online does not wait");
        settings::set("DES_ONLINE", "1");
        check(settings::restart_pending() == Keys{"DES_ONLINE"}, "Online changed again: it waits");
    } else if (name == "restart-pinned") {  // DES_LANGUAGE=en in the environment: the next start has it again
        settings::set("DES_LANGUAGE", "ja");
        check(settings::restart_pending().empty(), "a pinned setting never waits");
    } else if (name.rfind("keys-", 0) == 0) {  // the item slot keys
        const std::string pad = env("PAD_KEYS");
        check(pad.find("item") == std::string::npos, "no item rows for cellPad: " + pad);
        const std::string want = name == "keys-default" ? "1,2,3,4,5"
                                 : name == "keys-taken" ? ",2,shift+3,4,5"   // 1 is L2's; the wheel notch is left out
                                                        : ",2,3,4,6";        // keys-changed: slot 1 none, slot 5 on 6
        check(env("DES_ITEM_KEYS") == want, "DES_ITEM_KEYS " + want + ": " + env("DES_ITEM_KEYS"));
        if (name == "keys-taken") check(pad.find("l2=1|lctrl") != std::string::npos, "L2 keeps 1: " + pad);
    } else if (name.rfind("coop-", 0) == 0) {  // a seamless co-op host: its servers
        const std::string want = name == "coop-project"   ? std::string(settings::kNetProjectHost) + ":36100"
                                 : name == "coop-local"   ? "127.0.0.1:36100"
                                 : name == "coop-env"     ? "127.0.0.1:36200"
                                 : name == "coop-ini-own" ? "192.168.1.20:36100"
                                                          : "0";  // coop-off, coop-test-run
        check(env("DES_SEAMLESS_INTRO") == want, "DES_SEAMLESS_INTRO " + want + ": " + env("DES_SEAMLESS_INTRO"));
        check(env("DES_SEAMLESS_STUN") == (name == "coop-off" ? "0" : ""), "DES_SEAMLESS_STUN: " +
                                                                               env("DES_SEAMLESS_STUN"));
    } else if (name == "set-saves") {  // the row changed in the menu: kept for the next start, not applied now
        settings::set("RSX_BACKEND", "vulkan");
        check(env("RSX_BACKEND").empty(), "not applied before a restart: " + env("RSX_BACKEND"));
        const std::string ini = read_text(std::filesystem::path(data) / "settings.ini");
        check((ini.find("RSX_BACKEND = vulkan\n") != std::string::npos) == g_row, "settings.ini's line");
    } else {
        std::printf("    unknown case %s\n", name.c_str());
        return 2;
    }
    return g_fail ? 1 : 0;
}

struct Case {
    const char* name;
    const char* ini;  // settings.ini's text, or null for none
    const char* env[2][2];
    bool data = true;  // a data folder (else a test run's none)
};

const Case kCases[] = {
    {"no-ini", nullptr, {}},
    {"ini-vulkan", "RSX_BACKEND = vulkan\n", {}},
    {"ini-d3d12", "RSX_BACKEND = d3d12\n", {}},
    {"env-wins", "RSX_BACKEND = vulkan\n", {{"RSX_BACKEND", "d3d12"}}},
    {"env-vulkan", nullptr, {{"RSX_BACKEND", "vulkan"}}},
    {"env-hal", "RSX_BACKEND = vulkan\n", {{"RSX_BACKEND", "hal-d3d12"}}},
    {"state-env-wins", nullptr, {{"RSX_STATE_DIR", "elsewhere"}}},
    {"test-run", nullptr, {}, false},
    {"set-saves", nullptr, {}},
    {"restart-waits", nullptr, {}},
    {"restart-pinned", nullptr, {{"DES_LANGUAGE", "en"}}},
    {"keys-default", nullptr, {}},
    {"keys-taken", "keys_layout = souls\nPAD_KEYS = l2=1|lctrl,item3=shift+3|wheelup\n", {}},   // from before the rows
    {"keys-changed", "keys_layout = souls\nPAD_KEYS = item1=none,item5=6\n", {}},
    // 0.1.0's settings.ini ("d3d12" whether picked or not), by GPU
    {"gpu-in-1070-saved", "RSX_BACKEND = d3d12\n", {{"DES_RENDERER_GPU_ID", "10de:1b81"}}},        // GTX 1070 (#205, #372)
    {"gpu-in-1060-saved", "RSX_BACKEND = d3d12\n", {{"DES_RENDERER_GPU_ID", "10de:1c03"}}},        // GTX 1060 6GB (#398)
    {"gpu-in-1060laptop-saved", "RSX_BACKEND = d3d12\n", {{"DES_RENDERER_GPU_ID", "10de:1c20"}}},  // GTX 1060, laptop
    {"gpu-in-1070ti-saved", "RSX_BACKEND = d3d12\n", {{"DES_RENDERER_GPU_ID", "10de:1b82"}}},      // GTX 1070 Ti
    {"gpu-in-980-saved", "RSX_BACKEND = d3d12\n", {{"DES_RENDERER_GPU_ID", "10de:13c0"}}},         // GTX 980, Maxwell
    {"gpu-in-titanv-saved", "RSX_BACKEND = d3d12\n", {{"DES_RENDERER_GPU_ID", "10de:1d81"}}},      // Titan V, Volta
    {"gpu-out-2080ti-saved", "RSX_BACKEND = d3d12\n", {{"DES_RENDERER_GPU_ID", "10de:1e04"}}},     // RTX 2080 Ti, Turing
    {"gpu-out-1650-saved", "RSX_BACKEND = d3d12\n", {{"DES_RENDERER_GPU_ID", "10de:1f82"}}},       // GTX 1650, Turing
    {"gpu-out-5070ti-saved", "RSX_BACKEND = d3d12\n", {{"DES_RENDERER_GPU_ID", "10de:2c05"}}},     // RTX 5070 Ti
    {"gpu-out-intel-saved", "RSX_BACKEND = d3d12\n", {{"DES_RENDERER_GPU_ID", "8086:0166"}}},      // not NVIDIA, a low ID
    {"gpu-out-nameonly-saved", "RSX_BACKEND = d3d12\n", {{"DES_RENDERER_GPU", "NVIDIA GeForce GTX 1070"}}},  // no IDs
    {"gpu-in-1070-picked", "RSX_BACKEND = d3d12\nrenderer_rule = 1\n", {{"DES_RENDERER_GPU_ID", "10de:1b81"}}},
    {"gpu-in-1070-noini", nullptr, {{"DES_RENDERER_GPU_ID", "10de:1b81"}}},
    {"gpu-out-2080ti-noini", nullptr, {{"DES_RENDERER_GPU_ID", "10de:1e04"}}},
    {"gpu-in-1070-envwins", nullptr, {{"DES_RENDERER_GPU_ID", "10de:1b81"}, {"RSX_BACKEND", "d3d12"}}},
    {"coop-project", "DES_NET_NAME = tester\n", {{"TEST_SEAMLESS", "host"}}},
    {"coop-local", "DES_NET_NAME = tester\nDES_NET_SERVER = local\n", {{"TEST_SEAMLESS", "host"}}},
    {"coop-off", "DES_NET_NAME = tester\nDES_SEAMLESS_INTRO = 0\nDES_SEAMLESS_STUN = 0\n", {{"TEST_SEAMLESS", "host"}}},
    {"coop-ini-own", "DES_NET_NAME = tester\nDES_SEAMLESS_INTRO = 192.168.1.20:36100\n", {{"TEST_SEAMLESS", "host"}}},
    {"coop-env", "DES_NET_NAME = tester\nDES_SEAMLESS_INTRO = 0\n",
     {{"TEST_SEAMLESS", "host"}, {"DES_SEAMLESS_INTRO", "127.0.0.1:36200"}}},
    {"coop-test-run", "DES_NET_NAME = tester\n", {{"TEST_SEAMLESS", "host"}, {"PS3_PIDFILE", "test.pid"}}},
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || (std::strcmp(argv[1], "--row") && std::strcmp(argv[1], "--no-row"))) {
        std::fprintf(stderr, "usage: settings_check --row|--no-row\n");
        return 2;
    }
    g_row = !std::strcmp(argv[1], "--row");
    if (argc >= 4 && !std::strcmp(argv[2], "--case")) return run_case(argv[3]);

    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec) / ("settings_check_" + std::to_string(getpid()));
    int passed = 0, failed = 0;
    for (const Case& c : kCases) {
        const auto dir = root / c.name;
        std::filesystem::create_directories(dir, ec);
        if (c.ini) std::ofstream(dir / "settings.ini") << c.ini;
        for (const char* k :
            {"RSX_BACKEND", "RSX_STATE_DIR", "DES_SETTINGS", "DES_DATA_DIR", "PS3_PIDFILE", "DES_LANGUAGE", "PAD_KEYS",
             "DES_ITEM_KEYS", "TEST_SEAMLESS", "DES_SEAMLESS_INTRO", "DES_SEAMLESS_STUN", "PS3_NP_ONLINE_ID",
             "DES_KIND"})
            ps3_setenv(k, "");
        ps3_setenv("DES_RENDERER_GPU", "Test GPU");  // (not this PC's: a case gives the rule's GPU itself)
        ps3_setenv("DES_RENDERER_GPU_ID", "");
        ps3_setenv("TEST_DATA_DIR", c.data ? dir.string().c_str() : "");
        ps3_setenv("DES_SETTINGS", c.data ? (dir / "settings.ini").string().c_str() : "");
        for (const auto& e : c.env)
            if (e[0]) ps3_setenv(e[0], e[1]);
#ifdef _WIN32
        const std::string cmd = "\"\"" + std::string(argv[0]) + "\" " + argv[1] + " --case " + c.name + "\"";  // (cmd /c)
#else
        const std::string cmd = "'" + std::string(argv[0]) + "' " + argv[1] + " --case " + c.name;
#endif
        const int rc = std::system(cmd.c_str());
        std::printf("%s %s\n", rc == 0 ? "PASS" : "FAIL", c.name);
        (rc == 0 ? passed : failed)++;
    }
    std::filesystem::remove_all(root, ec);
    std::printf("settings_check (%s): %d passed, %d failed\n", g_row ? "with the Renderer row" : "without it", passed,
                failed);
    return failed ? 1 : 0;
}
