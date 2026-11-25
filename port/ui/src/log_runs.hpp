// The runs the logs keep (report.cpp): des.log and coop.log, and the four runs before each, beside them. A run's log
// becomes des.prev.log when the next one starts, and the older ones move back one: des.prev2.log to des.prev4.log, the
// oldest dropped (coop.log's the same). Header-only, so tests/log_runs_check.cpp checks it on its own.
#pragma once

#include <filesystem>
#include <string>

namespace ui::report::runs {

constexpr int kOlder = 4;

// The log of the run `back` runs before `log`'s (1: its .prev), beside it.
inline std::filesystem::path older(const std::filesystem::path& log, int back) {
    std::wstring name = log.stem().wstring() + L".prev";
    if (back > 1) name += std::to_wstring(back);
    return log.parent_path() / (name + L".log");
}

// A new run starts: `log` becomes its .prev, and each older one moves back one. Nothing moves if `log` isn't there or
// can't be moved (another copy of the game has it open). move(from, to) replaces `to` with `from`, false if it can't.
template <class Move>
void keep(const std::filesystem::path& log, Move move) {
    const auto moving = log.parent_path() / (log.stem().wstring() + L".moving.log");
    if (!move(log, moving)) return;
    for (int k = kOlder; k > 1; --k) move(older(log, k - 1), older(log, k));
    move(moving, older(log, 1));
}

// A log file name the main copy keeps (another copy's des.<process>.log and coop.<process>.log go after a day).
inline bool kept(const std::wstring& name) {
    return name == L"des.log" || name == L"coop.log" || name.rfind(L"des.prev", 0) == 0 ||
           name.rfind(L"coop.prev", 0) == 0 || name.rfind(L"des.moving", 0) == 0 || name.rfind(L"coop.moving", 0) == 0;
}

}  // namespace ui::report::runs
