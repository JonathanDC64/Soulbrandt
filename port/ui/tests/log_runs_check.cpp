// log_runs.hpp on its own: the runs the logs keep. In a new folder under the system's temporary one: seven runs leave
// the last one and the four before it, a log that can't be moved moves nothing, and which names the main copy keeps.
// Exit 0 when every case holds.
#include "log_runs.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
namespace runs = ui::report::runs;

namespace {

int g_failed = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        g_failed++;
    }
}

void write(const fs::path& p, const std::string& text) {
    std::ofstream(p, std::ios::binary) << text;
}

std::string read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

bool move(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (!fs::exists(from, ec)) return false;
    fs::rename(from, to, ec);   // (replaces `to`, as report.cpp's replace_file)
    return !ec;
}

}  // namespace

int main() {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) /
                         ("log_runs_check_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir, ec);
    if (ec) {
        std::printf("FAIL: no folder for the test\n");
        return 1;
    }
    const fs::path log = dir / "des.log";

    check(runs::older(log, 1) == dir / "des.prev.log", "older 1 is des.prev.log");
    check(runs::older(log, 4) == dir / "des.prev4.log", "older 4 is des.prev4.log");
    check(runs::older(dir / "coop.log", 2) == dir / "coop.prev2.log", "coop's older 2 is coop.prev2.log");

    runs::keep(log, move);   // no log yet: nothing to move
    check(!fs::exists(runs::older(log, 1)), "no .prev from no log");

    for (int run = 1; run <= 7; run++) {   // each run starts by keeping the one before, then writes its own
        runs::keep(log, move);
        write(log, "run " + std::to_string(run));
    }
    check(read(log) == "run 7", "des.log is the last run");
    for (int k = 1; k <= runs::kOlder; k++)
        check(read(runs::older(log, k)) == "run " + std::to_string(7 - k), "each older run in its place");
    check(!fs::exists(dir / "des.prev5.log"), "no fifth older run");
    check(!fs::exists(dir / "des.moving.log"), "nothing left moving");

    // A log that can't be moved (another copy has it open): nothing moves
    runs::keep(log, [&](const fs::path& from, const fs::path& to) { return from != log && move(from, to); });
    check(read(log) == "run 7" && read(runs::older(log, 1)) == "run 6" && read(runs::older(log, 4)) == "run 3",
          "a log that can't move leaves every run where it was");

    // Gaps (an older one deleted by hand) close up from the newest
    fs::remove(runs::older(log, 2), ec);
    runs::keep(log, move);
    write(log, "run 8");
    check(read(runs::older(log, 1)) == "run 7" && read(runs::older(log, 2)) == "run 6", "a gap: the newest move on");

    check(runs::kept(L"des.log") && runs::kept(L"coop.log") && runs::kept(L"des.prev.log") &&
              runs::kept(L"des.prev4.log") && runs::kept(L"coop.prev3.log") && runs::kept(L"des.moving.log"),
          "the main copy's logs are kept");
    check(!runs::kept(L"des.1234.log") && !runs::kept(L"coop.5678.log"), "another copy's logs aren't");

    fs::remove_all(dir, ec);
    if (g_failed) return 1;
    std::printf("log_runs_check: every case holds\n");
    return 0;
}
