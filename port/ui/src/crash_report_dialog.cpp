// The crash dialog's process, its part on every system (crash_report_dialog.hpp). Moved from crash_report.cpp:
// FFCCEOT-Recomp's report and dialog actions, adapted for Demon's Souls as that file says.
#include "crash_report_dialog.hpp"

#include "report.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace ui::crash_report {

std::string read_text(const std::filesystem::path& path) {
    const auto b = report::read_file(path);
    std::string text(b.begin(), b.end());
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());  // (the report was written in text mode)
    return text;
}

// "access violation writing 0000000000000000" or "no new frame for 14 s", from the first line of the report.
std::string what_happened(const std::string& txt) {
    std::string what = "The game stopped unexpectedly.";
    for (const char* mark : {kCrashMark, kHangMark, kLostMark}) {
        const size_t a = txt.find(mark);
        if (a == std::string::npos) continue;
        const size_t from = a + std::strlen(mark), b = txt.find(" ===", from);
        if (b != std::string::npos) what = txt.substr(from, b - from);
        break;
    }
    return what;
}

// The first `n` lines after the line `heading` starts in the report: the stack of a crash, the guest threads of a
// hang.
std::string top_of_stack(const std::string& txt, int n, const char* heading) {
    const size_t a = txt.find(heading);
    if (a == std::string::npos) return {};
    std::string out;
    size_t at = txt.find('\n', a) + 1;
    for (int i = 0; i < n && at < txt.size(); i++) {
        const size_t e = txt.find('\n', at);
        out += txt.substr(at, e == std::string::npos ? std::string::npos : e - at + 1);
        if (e == std::string::npos) break;
        at = e + 1;
    }
    return out;
}

crash_dialog::Content read_report(Dialog& d) {
    const std::string txt = read_text(d.txt);
    d.what = what_happened(txt);
    d.hang = txt.find(kHangMark) != std::string::npos;
    d.lost = txt.find(kLostMark) != std::string::npos;
    const bool running = d.hang || d.lost;
    return {d.what, running ? top_of_stack(txt, 30, "--- guest threads") : top_of_stack(txt, 14), d.hang, d.lost};
}

namespace {

// settings.ini with the online name, the matchmaking password, the custom server's address and its mark, and co-op's
// public address only as set (as settings::describe() shows them; this process has not read the settings).
std::string hide_private(const std::string& ini) {
    static const char* kHidden[] = {"DES_NET_NAME", "DES_NET_PASSWORD", "DES_NET_CUSTOM", "net_custom_here",
                                    "DES_SEAMLESS_ADDRESS"};
    std::string out;
    size_t at = 0;
    while (at < ini.size()) {
        size_t end = ini.find('\n', at);
        if (end == std::string::npos) end = ini.size();
        std::string line = ini.substr(at, end - at);
        for (const char* key : kHidden) {
            const size_t eq = line.find('=');
            if (line.rfind(key, 0) == 0 && eq != std::string::npos &&
                line.find_first_not_of(" \t\r", eq + 1) != std::string::npos &&
                line.find_first_not_of(" \t", std::strlen(key)) == eq)
                line = std::string(key) + " = (set)";
        }
        out += line + "\n";
        at = end + 1;
    }
    return out;
}

}  // namespace

// Zips what a bug report needs and opens the issue form, filled in, and the folder with the zip to attach to it.
// The text to show the player afterwards.
std::string make_report(const Dialog& d) {
    const std::string txt = report::redact(read_text(d.txt));
    std::string system = report::redact(report::system_info());
    // The renderer the game ran, from its report's renderer line (this process started none), after the game dump's.
    if (const size_t r = txt.find("\nrenderer "); r != std::string::npos && system.find("Renderer: ") == std::string::npos) {
        const size_t end = txt.find('\n', r + 1);
        const std::string line = "Renderer: " + txt.substr(r + 10, (end == std::string::npos ? txt.size() : end) - r - 10) + "\n";
        const size_t dump = system.find("Game dump");
        const size_t after = dump == std::string::npos ? std::string::npos : system.find('\n', dump);
        system.insert(after == std::string::npos ? system.size() : after + 1, line);
    }
    std::vector<report::File> files;
    files.push_back({d.lost ? "graphics-lost.txt" : d.hang ? "hang.txt" : "crash.txt", report::bytes(txt)});
    files.push_back({"system.txt", report::bytes(system)});
    // (their settings: as the game had them, with the defaults and what the environment set; else the file)
    if (auto in_use = report::read_file(d.data / L"logs" / L"settings.txt"); !in_use.empty())
        files.push_back({"settings.ini", report::bytes(report::redact(std::string(in_use.begin(), in_use.end())))});
    else if (auto ini = report::read_file(d.data / L"settings.ini"); !ini.empty())
        files.push_back({"settings.ini", report::bytes(report::redact(hide_private(std::string(ini.begin(), ini.end()))))});
    // (the game's command line: this process's is the dialog's)
    files.push_back({"environment.txt", report::bytes(report::redact(report::environment_info(d.command_line)))});
    auto log = d.txt;
    log.replace_extension(L".log");
    if (const std::string tail = read_text(log); !tail.empty()) files.push_back({"des.log", report::bytes(report::redact_log(tail))});
    log.replace_extension(L".coop.log");  // (the co-op log's, when the run had one)
    if (const std::string coop = read_text(log); !coop.empty()) files.push_back({"coop.log", report::bytes(report::redact_log(coop))});
    std::string save_note;  // (the save: this process has the game's environment and working folder)
    for (report::File& f : report::save_files(save_note)) files.push_back(std::move(f));
    const auto zip = report::write_bundle(report::narrow(d.txt.stem().wstring()), files);

    const bool running = d.hang || d.lost;
    // (What happened is the player's to write: what they were doing. The form asks for it, and for the zip.)
    std::string details = std::string(d.lost ? "## Graphics device lost\n" : d.hang ? "## Not responding\n" : "## Crash\n") +
                          d.what + "\n\n```\n" +
                          (running ? top_of_stack(txt, 24, "--- guest threads") : top_of_stack(txt, 20)) +
                          "```\n\n## System\n```\n" + system + report::redact(save_note) + "```\n\n";
    const std::string name = report::narrow(zip.filename().wstring());
    const char* holds = running ? "what every thread was doing, your settings, the game's recent messages and your save"
                                : "the full stack, your settings, the game's recent messages and your save";
    details += zip.empty() ? "(The report file could not be saved.)\n"
                           : "Report file: `" + name + "` (its folder is open). It has " + holds + ".\n";
    report::open_issue(
        report::issue_url((d.lost ? "Graphics lost: " : d.hang ? "Not responding: " : "Crash: ") + d.what.substr(0, 80),
                          "", details),
        zip);
    return zip.empty() ? "The report file could not be saved. The issue page is open in your browser."
                       : "The issue page is open in your browser, with the folder holding " + name +
                             ". Drag that file into the form's Report file box: it has " + holds + ". Thank you!";
}

}  // namespace ui::crash_report
