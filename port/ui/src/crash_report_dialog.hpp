// The crash dialog's process, its part on every system (crash_report.hpp): what a report it shows says, and its
// "Report on GitHub". Moved from crash_report.cpp, which starts the dialog on Windows; crash_report_posix.cpp starts
// it elsewhere. Only those two include this.
#pragma once

#include "crash_dialog.hpp"

#include <filesystem>
#include <string>

namespace ui::crash_report {

// A report's first line, after its empty one: a crash (with "stack:" after it), a hang (report_hang) or a lost
// graphics device (report_graphics_lost), each "<mark><what> ===", the snapshot's "--- guest threads" after the last two.
constexpr const char* kCrashMark = "=== CRASH: ";
constexpr const char* kHangMark = "=== NOT RESPONDING: ";
constexpr const char* kLostMark = "=== GRAPHICS LOST: ";

// What the dialog's process knows: from its command line on Windows, its environment elsewhere, and the report.
struct Dialog {
    std::filesystem::path txt, data;  // the report (crash-<time>.txt, hang-, graphics-) and the data folder
    unsigned long pid = 0;            // the game's process
    std::string what;
    bool hang = false;  // a hang report (report_hang): the game is still running
    bool lost = false;  // a lost graphics device (report_graphics_lost): the game is still running, drawing nothing
    std::string command_line;  // the game's, for the report's environment.txt
};

// The text of a file, without the carriage returns of one written in text mode.
std::string read_text(const std::filesystem::path& path);
// "access violation writing 0000000000000000" or "no new frame for 14 s", from the first line of the report.
std::string what_happened(const std::string& txt);
// The first `n` lines after the line `heading` starts in the report: the stack of a crash, the guest threads of a
// hang.
std::string top_of_stack(const std::string& txt, int n, const char* heading = "stack:\n");
// Reads the report at d.txt into d.what, d.hang and d.lost: the dialog's window shows that, with the stack of a
// crash, or what every thread was doing for a hang or a lost device.
crash_dialog::Content read_report(Dialog& d);
// Zips what a bug report needs and opens the issue form, filled in, and the folder with the zip to attach to it.
// The text to show the player afterwards.
std::string make_report(const Dialog& d);

}  // namespace ui::crash_report
