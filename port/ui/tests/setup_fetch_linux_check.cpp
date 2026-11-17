// The first Python's fetch on a Linux with no curl (issue #521): setup_python.cpp with the data folder and the free
// space stubbed, and PATH, for each case, a folder that holds only the tools that case may use (links to the system's):
//   wget            tar, gzip (tar needs it for a .tar.gz) and wget: wget downloads it
//   python3         tar, gzip and python3: the system's own python3 downloads it
//   wget resume     the same, stopped once the .part file has 2 MB, then started again: it carries on from the file
//   python3 resume  the same with python3
//   old wget        a wget that refuses its options (a script that exits 2) and python3: python3 takes over
//   none            tar and gzip alone: the try ends at once, with the sentence that says what to install
//
//   setup_fetch_linux_check <a new folder>
//
// Each case has its own data folder under it. Prints one line a case and "setup_fetch_linux_check: N passed, M
// failed"; exit 1 on a failure. Needs tar, gzip, wget and python3 on the PC, and the network: it downloads the
// pinned python-build-standalone archive (35 MB) five times. Built on demand: cmake --build <build> --target
// setup_fetch_linux_check.
#include "setup_engine.hpp"
#include "setup_pc.hpp"
#include "setup_python.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {
fs::path g_data;  // the case's data folder
}

// What setup_python.cpp needs of setup_engine.cpp and setup_pc.cpp.
namespace ui::setup {
const fs::path& data_dir() {
    return g_data;
}
Space data_space() {
    return {};
}
}  // namespace ui::setup

namespace {

struct Try {
    std::vector<std::string> lines;
    bool ok = false, stopped = false;
};

// One try at the first Python in `data`, to its end. stopAt > 0: stop() once the .part file has that many bytes.
Try run(const fs::path& data, unsigned long long stopAt) {
    std::error_code ec;
    fs::create_directories(data, ec);
    g_data = data;
    Try t;
    std::mutex lock;
    std::condition_variable wake;
    bool ended = false;
    ui::setup::PythonFetch fetch;
    fetch.start(
        [&](std::string line) {
            std::lock_guard<std::mutex> l(lock);
            t.lines.push_back(std::move(line));
        },
        [&](bool ok, bool stopped) {
            std::lock_guard<std::mutex> l(lock);
            t.ok = ok;
            t.stopped = stopped;
            ended = true;
            wake.notify_all();
        });
    fs::path part = ui::setup::python_archive();
    part += ".part";
    std::unique_lock<std::mutex> l(lock);
    while (!ended) {
        wake.wait_for(l, std::chrono::milliseconds(50));
        if (stopAt && !ended) {
            l.unlock();
            const auto have = fs::file_size(part, ec);
            if (!ec && have >= stopAt) {
                fetch.stop();
                stopAt = 0;
            }
            l.lock();
        }
    }
    return t;  // (the fetch's destructor joins its thread)
}

bool has_line(const Try& t, const std::string& part) {
    for (const std::string& l : t.lines)
        if (l.find(part) != std::string::npos) return true;
    return false;
}

void show(const Try& t) {
    for (const std::string& l : t.lines)
        if (l.rfind("@progress", 0) != 0 && l.rfind("@overall", 0) != 0) std::printf("      | %s\n", l.c_str());
}

// The system's tool by name, from the usual folders.
fs::path tool(const char* name) {
    for (const char* dir : {"/usr/bin", "/bin", "/usr/local/bin"}) {
        const fs::path p = fs::path(dir) / name;
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) return p;
    }
    return {};
}

// PATH for a case: a folder of links to just these tools.
bool only(const fs::path& folder, std::initializer_list<const char*> names) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    for (const char* name : names) {
        const fs::path from = tool(name);
        if (from.empty()) {
            std::printf("no %s on this PC\n", name);
            return false;
        }
        fs::create_symlink(from, folder / name, ec);
        if (ec) return false;
    }
    return setenv("PATH", folder.c_str(), 1) == 0;
}

int g_passed = 0, g_failed = 0;

void result(const std::string& name, bool ok, const std::string& note) {
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), note.empty() ? "" : ": ", note.c_str());
    (ok ? g_passed : g_failed)++;
}

// A whole download with `downloader`, and one stopped at 2 MB and started again.
void cases(const fs::path& root, const char* downloader) {
    const std::string name = downloader;
    if (!only(root / ("bin-" + name), {"tar", "gzip", downloader})) {
        result(name, false, "no folder of tools");
        return;
    }
    Try t = run(root / name, 0);
    const std::string instead = "downloading with " + name + " instead";
    const bool ok = t.ok && ui::setup::python_unpacked() && has_line(t, "no curl on PATH") && has_line(t, instead);
    result(name, ok, "");
    if (!ok) show(t);

    t = run(root / (name + "-resume"), 2u << 20);
    fs::path part = ui::setup::python_archive();
    part += ".part";
    std::error_code ec;
    const auto left = fs::file_size(part, ec);
    const bool stoppedOk = t.stopped && !t.ok && !ec && left >= (2u << 20) && left < ui::setup::kPythonSize;
    const Try again = run(root / (name + "-resume"), 0);
    const bool resumeOk = stoppedOk && again.ok && ui::setup::python_unpacked();
    result(name + " resume", resumeOk, "stopped at " + std::to_string(ec ? 0 : left) + " bytes");
    if (!resumeOk) {
        show(t);
        show(again);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: setup_fetch_linux_check <a new folder>\n");
        return 2;
    }
    const fs::path root = fs::absolute(argv[1]);
    std::error_code ec;
    if (fs::exists(root, ec)) {
        std::fprintf(stderr, "setup_fetch_linux_check: %s exists; give it a new folder\n", root.c_str());
        return 2;
    }
    unsetenv("DES_SETUP_NO_SYSTEM_TOOLS");

    cases(root, "wget");
    cases(root, "python3");

    // old wget: a wget that refuses its options, then python3
    if (only(root / "bin-old", {"tar", "gzip", "python3"})) {
        {
            std::ofstream script(root / "bin-old" / "wget");
            script << "#!/bin/sh\necho \"wget: unrecognized option '--no-config'\" >&2\nexit 2\n";
        }
        fs::permissions(root / "bin-old" / "wget", fs::perms::owner_all, fs::perm_options::add, ec);
        const Try t = run(root / "old", 0);
        const bool ok = t.ok && ui::setup::python_unpacked() &&
                        has_line(t, "no curl on PATH, wget refused its options (exit 2): downloading with python3");
        result("old wget", ok, "");
        if (!ok) show(t);
    } else {
        result("old wget", false, "no folder of tools");
    }

    // none: tar and gzip alone
    if (only(root / "bin-none", {"tar", "gzip"})) {
        const auto start = std::chrono::steady_clock::now();
        const Try t = run(root / "none", 0);
        const auto took = std::chrono::steady_clock::now() - start;
        const bool ok = !t.ok && !t.stopped && has_line(t, "no curl on PATH, no wget on PATH, no python3 on PATH") &&
                        has_line(t, "it needs curl, wget or python3") && took < std::chrono::seconds(5);
        result("none", ok, "");
        if (!ok) show(t);
    } else {
        result("none", false, "no folder of tools");
    }

    std::printf("setup_fetch_linux_check: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
