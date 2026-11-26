// Written for Demon's Souls. Crash reports off Windows (docs/76; crash_report.hpp, and crash_report.cpp for Windows):
// the log starts here (des_ui_early_init), and when the game ends on a fault nothing handled or on an abort, this
// writes crash-<time>.txt (what happened, the last HLE call and a backtrace) and crash-<time>.log (the end of the log)
// in the data folder's crashes folder; a hang (report_hang) or a lost graphics device (report_graphics_lost) writes
// hang-<time>.txt or graphics-<time>.txt and .log beside them.
//
// Then, as on Windows, a second process tells the player and offers what to do (crash_dialog.hpp). It is this program
// started again with DES_UI_OPEN=crash-dialog, DES_CRASH_REPORT=<the report> and DES_CRASH_PID=<the game's process>:
// setup's window-only start (des_ui.cpp, boot_main's PS3_LAUNCH_WINDOW) with the dialog's window in it (push_dialog
// below), on the renderer the game used. It has the arguments and the environment the game started with, so its
// Restart starts the game as it was started (start_again). Its output goes to the data folder's
// logs/crash-dialog.log, and it keeps none of the game's open files and sockets. If its window has not come up after
// kWindowWaitS, an SDL message box says where the report is. DES_NO_CRASH_DIALOG=1 writes the report alone.
//
// The faults come through the runtime's Win32 exception shim (runtime/platform/win32_compat.h): its unhandled filter
// runs only after the runtime's own handlers passed on the fault, as SetUnhandledExceptionFilter does on Windows.
// The runtime's previous filter and abort handler still run afterwards (they print the guest state).
#include "crash_report.hpp"

#include "app.hpp"
#include "crash_dialog.hpp"
#include "crash_report_dialog.hpp"
#include "modal.hpp"
#include "mouse.hpp"
#include "report.hpp"
#include "report_ack.hpp"
#include "ui.hpp"
#include "version.hpp"

#include <SDL3/SDL.h>
#include <execinfo.h>
#include <fcntl.h>
#include <sched.h>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

extern "C" uint32_t g_last_hle_nid;  // ppu_hle.cpp: the last HLE call (any thread)
extern "C" const char* g_last_hle_name;
extern "C" void ps3_diag_report(FILE* out, unsigned sample_sec);  // boot_main.cpp: what every thread is doing

// The shim's exception types (win32_compat.h), as far as they are read here.
namespace {
struct ExceptionRecord {
    uint32_t code;
    uint32_t flags;
    ExceptionRecord* record;
    void* address;
    uint32_t parameters;
    size_t information[15];
};
struct ExceptionPointers {
    ExceptionRecord* record;
    void* context;
};
using Filter = int32_t (*)(ExceptionPointers*);
}  // namespace

extern "C" Filter SetUnhandledExceptionFilter(Filter filter);

namespace ui::crash_report {

namespace {

constexpr int kWindowWaitS = 30;  // the dialog's process: its window by then, or the message box

std::string g_dir;  // where the reports go (with a trailing slash)
bool g_installed = false;
bool g_dialog = false;  // start the dialog's process (DES_NO_CRASH_DIALOG=1: not)
volatile sig_atomic_t g_reported = 0, g_dialog_started = 0;
Filter g_prev_filter = nullptr;            // boot_main's, if any
void (*g_prev_abort)(int) = nullptr;       // boot_main's (prints a backtrace, then exits)
pid_t g_hang_dialog = 0;                   // the hang (or lost graphics) dialog's process, while it may be open
char g_txt[4096];                          // the crash report written (write_report), for its dialog

// The environment and the command line as the program started (start_again's), saved before main, before anything
// changed the environment. (Function statics: the saving may run before this file's other static initializers.)
std::vector<std::string>& started_environment() {
    static std::vector<std::string> vars;
    return vars;
}
std::vector<std::string>& started_arguments() {
    static std::vector<std::string> args;
    return args;
}
// And the CPUs it could run on (the player's own limit, if any): a new process gets those of the thread that starts
// it, not the program's (start_again).
struct StartedCpus {
    cpu_set_t set;
    bool known = false;
};
StartedCpus& started_cpus() {
    static StartedCpus cpus;
    return cpus;
}
__attribute__((constructor)) void save_start() {
    for (char** e = environ; e && *e; ++e) started_environment().emplace_back(*e);
    started_cpus().known = sched_getaffinity(0, sizeof(cpu_set_t), &started_cpus().set) == 0;
    std::ifstream cmdline("/proc/self/cmdline", std::ios::binary);  // the arguments, each ended by a 0
    for (std::string arg; std::getline(cmdline, arg, '\0');) started_arguments().push_back(arg);
}

// A start by setup's Play (start_again): the setup process is still closing. Wait for it to end (at most 10 s), so
// that this one gets des.log and shows the only window.
void wait_for_launcher() {
    const char* pid = std::getenv("DES_LAUNCH_AFTER");
    if (!pid || !*pid) return;
    const pid_t setup = pid_t(std::strtol(pid, nullptr, 10));
    unsetenv("DES_LAUNCH_AFTER");
    for (int i = 0; i < 200 && setup > 0 && kill(setup, 0) == 0; ++i) usleep(50000);
}

// "<kind>-<local time>" in the crashes folder.
std::string stem_of(const char* kind) {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char stem[64];
    std::snprintf(stem, sizeof stem, "%s-", kind);
    std::strftime(stem + std::strlen(stem), sizeof stem - std::strlen(stem), "%Y%m%d-%H%M%S", &tm);
    return g_dir + stem;
}

// The end of the log, into `path`, and the co-op log's (this run's, if it has one) as <report>.coop.log.
void write_log_tail(const std::string& path) {
    report::flush_log();
    const auto write = [](const std::string& to, const std::string& text) {
        if (FILE* f = std::fopen(to.c_str(), "w")) {
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
        }
    };
    if (const auto log = report::log_path(); !log.empty()) write(path, report::read_head_tail(log, 40, 192 << 10));
    if (const auto coop = report::coop_log_path(); !coop.empty() && path.size() > 4)
        write(path.substr(0, path.size() - 4) + ".coop.log", report::read_head_tail(coop, 200, 2 << 20));
}

// ---- Starting the dialog's process ----

// Made once, at install: a fault may come with the heap in any state, so starting the dialog then allocates nothing.
struct DialogStart {
    std::vector<std::string> vars;  // the environment the game started with, less what this one sets, plus it
    std::vector<char*> envp, argv;
    char report_var[4200] = {};     // DES_CRASH_REPORT=<the report>, filled in when there is one
    posix_spawn_file_actions_t files;
    posix_spawnattr_t attr;
    bool ready = false;
};
DialogStart g_start;

void prepare_dialog() {
    DialogStart& s = g_start;
    static const char* kSet[] = {"DES_UI_OPEN=", "DES_LAUNCH_AFTER=", "DES_UPDATE_TO=", "DES_CRASH_REPORT=",
                                 "DES_CRASH_PID="};
    for (const std::string& v : started_environment()) {
        bool drop = false;
        for (const char* name : kSet) drop = drop || v.rfind(name, 0) == 0;
        if (!drop) s.vars.push_back(v);
    }
    s.vars.push_back("DES_UI_OPEN=crash-dialog");
    s.vars.push_back("DES_CRASH_PID=" + std::to_string(getpid()));
    for (std::string& v : s.vars) s.envp.push_back(v.data());
    s.envp.push_back(s.report_var);
    s.envp.push_back(nullptr);
    for (std::string& a : started_arguments()) s.argv.push_back(a.data());
    if (s.argv.empty()) s.argv.push_back(const_cast<char*>("des_host"));
    s.argv.push_back(nullptr);
    // Its output to a log of its own: the game's standard output and error are the pipes of its log's threads
    // (report.cpp), which end with it. And none of the game's other files and sockets, held open while it is up.
    posix_spawn_file_actions_init(&s.files);
    const std::string log = (report::data_dir() / "logs" / "crash-dialog.log").string();
    posix_spawn_file_actions_addopen(&s.files, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&s.files, 1, 2);
    posix_spawn_file_actions_addclosefrom_np(&s.files, 3);
    // A fault's handler runs with its signal blocked, and a new process keeps the mask it was started with.
    posix_spawnattr_init(&s.attr);
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&s.attr, &none);
    posix_spawnattr_setflags(&s.attr, POSIX_SPAWN_SETSIGMASK);
    s.ready = true;
}

// Starts the dialog's process for the report at `txt`; its id, or 0. The program this one runs (/proc/self/exe:
// the same file, even replaced since), else the one where it was started.
pid_t start_dialog(const char* txt) {
    DialogStart& s = g_start;
    if (!s.ready || !txt || !*txt) return 0;
    std::snprintf(s.report_var, sizeof s.report_var, "DES_CRASH_REPORT=%s", txt);
    pid_t child = 0;
    int rc = posix_spawn(&child, "/proc/self/exe", &s.files, &s.attr, s.argv.data(), s.envp.data());
    if (rc != 0) {
        const std::string exe = report::program_now().string();
        if (!exe.empty()) rc = posix_spawn(&child, exe.c_str(), &s.files, &s.attr, s.argv.data(), s.envp.data());
    }
    if (rc != 0) {
        std::fprintf(stderr, "[crash] the dialog could not start: %s\n", std::strerror(rc));
        return 0;
    }
    std::fprintf(stderr, "[crash] the dialog's process: %ld (its log: logs/crash-dialog.log)\n", long(child));
    return child;
}

// The crash dialog, once, after the report.
void show_dialog() {
    if (!g_dialog || !g_txt[0] || g_dialog_started) return;
    g_dialog_started = 1;
    start_dialog(g_txt);
}

// The report, once: what happened, then a backtrace, then the end of the log beside it. Its first lines are as on
// Windows (the "=== CRASH:" line the dialog reads, then "stack:").
void write_report(const char* what, const char* notes = nullptr) {
    if (g_reported) return;
    g_reported = 1;
    mouse::release_now();
    const std::string base = stem_of("crash");
    std::fprintf(stderr, "[crash] %s; last HLE call %s (0x%08X)\n", what, g_last_hle_name ? g_last_hle_name : "?",
                 g_last_hle_nid);
    if (FILE* f = std::fopen((base + ".txt").c_str(), "w")) {
        std::fprintf(f, "\n%s%s ===\n", kCrashMark, what);
        std::fprintf(f, "version %s, last HLE call 0x%08X (%s), thread %ld\n", kVersion, g_last_hle_nid,
                     g_last_hle_name ? g_last_hle_name : "", long(syscall(SYS_gettid)));
        std::fprintf(f, "ps3recomp %s, game dump %s\n", kPs3recompVersion, *kDumpSha256 ? kDumpSha256 : "unknown");
        if (notes && *notes) std::fprintf(f, "%s\n", notes);
        {
            const char* why = nullptr;
            if (const char* r = report::renderer_name(&why))  // (the dialog's report takes it from here)
                std::fprintf(f, "renderer %s%s%s%s\n", r, why ? " (" : "", why ? why : "", why ? ")" : "");
        }
        std::fprintf(f, "stack:\n");
        std::fflush(f);
        void* frames[64];
        backtrace_symbols_fd(frames, backtrace(frames, 64), fileno(f));
        std::fclose(f);
        std::snprintf(g_txt, sizeof g_txt, "%s.txt", base.c_str());
    }
    write_log_tail(base + ".log");
    std::fprintf(stderr, "[crash] report: %s.txt\n", base.c_str());
    report::flush_log();
}

int32_t on_unhandled(ExceptionPointers* e) {
    char what[160];
    const ExceptionRecord* r = e ? e->record : nullptr;
    if (r && r->code == 0xC0000005u && r->parameters >= 2)
        std::snprintf(what, sizeof what, "access violation %s %p at %p", r->information[0] ? "writing" : "reading",
                      reinterpret_cast<void*>(r->information[1]), r->address);
    else
        std::snprintf(what, sizeof what, "exception 0x%08X at %p", r ? r->code : 0u, r ? r->address : nullptr);
    write_report(what);
    show_dialog();
    return g_prev_filter ? g_prev_filter(e) : 0;  // (0: EXCEPTION_CONTINUE_SEARCH, the process ends as it would have)
}

void on_abort(int sig) {
    write_report("abort (see the messages above it in the log)");
    show_dialog();
    if (g_prev_abort && g_prev_abort != SIG_DFL && g_prev_abort != SIG_IGN) g_prev_abort(sig);
    std::signal(SIGABRT, SIG_DFL);
    std::raise(SIGABRT);
}

// ---- The dialog's process ----

std::atomic<bool> g_window_up{false};  // push_dialog ran: the window-only start has its renderer and the UI

// Whether the process `pid` still runs: not when it is gone, when it has ended and waits for its parent to reap it
// (a zombie), or when the id is another user's now.
bool running(unsigned long pid) {
    if (pid == 0 || kill(pid_t(pid), 0) != 0) return false;
    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    std::getline(stat, line);
    const size_t name_end = line.rfind(')');  // (the process's name is in brackets, and may hold anything)
    return !(name_end != std::string::npos && name_end + 2 < line.size() && line[name_end + 2] == 'Z');
}

void wait_gone(unsigned long pid, int ms) {
    for (int waited = 0; waited < ms && running(pid); waited += 50) usleep(50000);
}

// A hang's "Close the game", and a lost device's before its restart: the game is still running.
void close_game(const Dialog& d) {
    if (!running(d.pid)) return;
    kill(pid_t(d.pid), SIGKILL);
    wait_gone(d.pid, 5000);
}

// The game again, as it was started (this process has its arguments and environment), once the crashed one is gone.
void restart(const Dialog& d) {
    wait_gone(d.pid, 10000);
    const char* skip = std::getenv("DES_SKIP_UI");  // (start_again sets it only for the restart for a setting)
    if (!start_again({}, skip && *skip && std::strcmp(skip, "0") != 0))
        std::fprintf(stderr, "[crash-dialog] the game could not be started again\n");
}

// No window after kWindowWaitS (its renderer did not start): a plain message box, then the end of this process.
void watch_window(std::string txt, std::string title) {
    for (int i = 0; i < kWindowWaitS * 10 && !g_window_up.load(); i++) usleep(100000);
    if (g_window_up.load()) return;
    std::fprintf(stderr, "[crash-dialog] no window after %d s: a message box\n", kWindowWaitS);
    const std::string text = "A report was saved in " + std::filesystem::path(txt).parent_path().string() +
                             ".\nTo report it, attach the files in that folder to a new issue at " +
                             std::string(report::kRepositoryUrl);
    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), nullptr))
        std::fprintf(stderr, "[crash-dialog] no message box either: %s\n", SDL_GetError());
    std::fflush(stderr);
    _exit(0);
}

}  // namespace

bool dialog_process() {
    static const bool yes = [] {
        const char* open = std::getenv("DES_UI_OPEN");
        return open && std::strcmp(open, "crash-dialog") == 0;
    }();
    return yes;
}

void push_dialog() {
    static Dialog d;
    static crash_dialog::Content content;
    static crash_dialog::Actions actions;
    const char* txt = std::getenv("DES_CRASH_REPORT");
    const char* pid = std::getenv("DES_CRASH_PID");
    d.txt = txt ? txt : "";
    d.pid = pid ? std::strtoul(pid, nullptr, 10) : 0;
    d.data = report::data_dir();
    for (const std::string& a : started_arguments()) d.command_line += (d.command_line.empty() ? "" : " ") + a;
    content = read_report(d);
    actions.report = [] { return make_report(d); };
    actions.restart = [] {
        if (d.lost) close_game(d);  // (the game is still running, with nothing to draw with)
        restart(d);
    };
    actions.close_game = [] { close_game(d); };
    crash_dialog::push_window(content, actions, [] {
        if (ui::app().quit) ui::app().quit();
    });
    g_window_up.store(true);
    std::fprintf(stderr, "[crash-dialog] the window for %s (process %lu)\n", d.txt.string().c_str(), d.pid);

    // The test knobs of crash_dialog.hpp, but at once: this window has its first frame already.
    const char* auto_details = std::getenv("DES_CRASH_DIALOG_DETAILS");
    const char* auto_report = std::getenv("DES_CRASH_DIALOG_REPORT");
    auto* modal = dynamic_cast<Modal*>(top_document());
    if (modal && auto_report && *auto_report && *auto_report != '0') {
        if (std::strcmp(auto_report, "ask") == 0)  // (as the button: the first report's question first)
            with_zip_ack([modal] { modal->set_body_text(actions.report()); });
        else
            modal->set_body_text(actions.report());
    }
    if (modal && auto_details && *auto_details && *auto_details != '0') crash_dialog::show_details(content, *modal);
    if (const char* ms = std::getenv("DES_CRASH_DIALOG_EXIT_MS"); ms && std::atoll(ms) > 0) {
        const long long after = std::atoll(ms);
        std::thread([after] {
            std::this_thread::sleep_for(std::chrono::milliseconds(after));
            if (ui::app().quit) ui::app().quit();
        }).detach();
    }
}

bool hang_dialog_open() {
    if (g_hang_dialog <= 0) return false;
    int status = 0;
    if (waitpid(g_hang_dialog, &status, WNOHANG) == 0) return true;
    g_hang_dialog = 0;  // (it ended, and is reaped here)
    return false;
}

namespace {

// A report of a game that is still running: <kind>-<time>.txt (`mark`, `what`, then `note` and the runtime's
// snapshot) and the end of the log beside it, then the dialog.
void report_running(const char* kind, const char* mark, const std::string& what, const char* note) {
    if (!g_installed || hang_dialog_open()) return;
    const std::string base = stem_of(kind);
    FILE* out = std::fopen((base + ".txt").c_str(), "w");
    if (!out) return;
    std::fprintf(out, "\n%s%s ===\n", mark, what.c_str());
    std::fprintf(out, "version %s, ps3recomp %s, game dump %s\n", kVersion, kPs3recompVersion,
                 *kDumpSha256 ? kDumpSha256 : "unknown");
    {
        const char* why = nullptr;
        if (const char* r = report::renderer_name(&why))
            std::fprintf(out, "renderer %s%s%s%s\n", r, why ? " (" : "", why ? why : "", why ? ")" : "");
    }
    std::fprintf(out, "%s\n\n", note);
    ps3_diag_report(out, 2);
    std::fclose(out);
    write_log_tail(base + ".log");
    std::fprintf(stderr, "[report] %s report: %s.txt\n", kind, base.c_str());
    if (g_dialog) g_hang_dialog = start_dialog((base + ".txt").c_str());
}

}  // namespace

void report_hang(long long ms_since_frame) {
    report_running("hang", kHangMark, "no new frame for " + std::to_string(ms_since_frame / 1000) + " s",
                   "The menu could not open: the render thread did not answer.");
}

void report_graphics_lost(const std::string& what) {
    report_running("graphics", kLostMark, what,
                   "The graphics device was lost: nothing more can be drawn, while the game runs on.");
}

void install() {
    // The dialog's process: no reports of its own (a dialog for the dialog, and its own des.log over the game's),
    // only the message box if its window does not come up.
    if (dialog_process()) {
        const char* txt = std::getenv("DES_CRASH_REPORT");
        const std::string report = txt ? txt : "";
        const std::string text = read_text(report);
        const char* title = text.find(kLostMark) != std::string::npos   ? DES_PRODUCT_NAME " can no longer draw"
                            : text.find(kHangMark) != std::string::npos ? DES_PRODUCT_NAME " is not responding"
                                                                        : DES_PRODUCT_NAME " has crashed";
        std::thread(watch_window, report, std::string(title)).detach();
        return;
    }
    wait_for_launcher();
    if (g_installed || report::data_dir().empty()) return;  // test runs: nothing
    g_installed = true;
    const char* no_dialog = std::getenv("DES_NO_CRASH_DIALOG");
    g_dialog = !(no_dialog && *no_dialog && std::strcmp(no_dialog, "0") != 0);
    report::start_log();
    std::error_code ec;
    const auto dir = report::data_dir() / "crashes";
    std::filesystem::create_directories(dir, ec);
    g_dir = dir.string() + "/";
    if (g_dialog) prepare_dialog();
    g_prev_filter = SetUnhandledExceptionFilter(on_unhandled);
    g_prev_abort = std::signal(SIGABRT, on_abort);
}

bool start_again(const std::vector<std::string>& set, bool play) {
    // (DES_SAVE_COPY_FROM: the copy that start asked for is made once, save_offer.hpp)
    std::vector<std::string> drops = {"DES_UI_OPEN=", "DES_SKIP_UI=", "DES_LAUNCH_AFTER=", "DES_UPDATE_TO=",
                                      "DES_CRASH_REPORT=", "DES_CRASH_PID=", "DES_SAVE_COPY_FROM="};
    for (const std::string& v : set) drops.push_back(v.substr(0, v.find('=') + 1));
    std::vector<std::string> vars;
    for (const std::string& v : started_environment()) {
        bool drop = false;
        for (const std::string& name : drops) drop = drop || v.rfind(name, 0) == 0;
        if (!drop) vars.push_back(v);
    }
    if (play) vars.push_back("DES_SKIP_UI=1");
    vars.insert(vars.end(), set.begin(), set.end());
    vars.push_back("DES_LAUNCH_AFTER=" + std::to_string(getpid()));
    std::vector<std::string> args = started_arguments();
    if (args.empty()) return false;
    std::vector<char*> envp, argv;
    for (std::string& v : vars) envp.push_back(v.data());
    envp.push_back(nullptr);
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    // What the new one gets of this one's files: standard output and error as they were when this one started (a
    // terminal, the desktop's journal), not the log's pipe that report::start_log put there, which nothing reads once
    // this one has closed (after 1 MB in it, the new one's every thread that prints would wait: #670). Nothing else
    // either: no lock on des.log, no end of that pipe.
    posix_spawn_file_actions_t files;
    posix_spawn_file_actions_init(&files);
    for (int fd : {1, 2}) {
        const int original = report::original_output(fd);
        if (original < 0) posix_spawn_file_actions_addopen(&files, fd, "/dev/null", O_WRONLY, 0);
        else if (original != fd) posix_spawn_file_actions_adddup2(&files, original, fd);
    }
    posix_spawn_file_actions_addclosefrom_np(&files, 3);
    // The program's file where it was started (report::program_now, read at the start): after an update that path
    // holds the new version, while /proc/self/exe follows this one's file into .previous/. /proc/self/exe only if
    // the path cannot be started.
    // The new one runs on the CPUs this one started with: this thread is put on them while it starts it, then back.
    // A new process gets the CPUs of the thread that starts it, and this is often the one that draws and runs the
    // menus' presses, which RSX_CORE (on by default) puts alone on one CPU (ps3recomp's plat_core_reserve). Every
    // game the Co-op window's Host, Join or Leave started ran on that one CPU: a tester's i5-12400F (12 CPUs) got
    // about 35 fps in co-op where it gets 240 alone, the game 1.00 core busy with its threads waiting for a CPU, and
    // RSX_CORE "not used here" (11-23).
    cpu_set_t was;
    const bool moved = started_cpus().known && sched_getaffinity(0, sizeof was, &was) == 0 &&
                       !CPU_EQUAL(&was, &started_cpus().set) &&
                       sched_setaffinity(0, sizeof(cpu_set_t), &started_cpus().set) == 0;
    pid_t child = 0;
    const std::string exe = report::program_now().string();
    const bool started =
        (!exe.empty() && posix_spawn(&child, exe.c_str(), &files, nullptr, argv.data(), envp.data()) == 0) ||
        posix_spawn(&child, "/proc/self/exe", &files, nullptr, argv.data(), envp.data()) == 0;
    posix_spawn_file_actions_destroy(&files);
    if (moved) sched_setaffinity(0, sizeof was, &was);
    if (started && moved)
        std::fprintf(stderr, "[ui] the game starts again on the %d CPU(s) it started with, not this thread's %d\n",
                     CPU_COUNT(&started_cpus().set), CPU_COUNT(&was));
    if (!started) return false;
    // A test run's PS3_PIDFILE (each process appends itself, late in its boot): the new one goes in now, so the script
    // that started this one sees the game at once, to wait for it or stop it.
    if (const char* pids = std::getenv("PS3_PIDFILE"); pids && *pids)
        if (FILE* f = std::fopen(pids, "a")) {
            std::fprintf(f, "%ld\n", static_cast<long>(child));
            std::fclose(f);
        }
    return true;
}

bool reports_on() { return g_installed; }

void report_guest_exit(const char* what, const char* notes) {
    write_report(what, notes);
    show_dialog();
}

}  // namespace ui::crash_report

extern "C" int des_ui_crash_reports_on(void) { return ui::crash_report::reports_on() ? 1 : 0; }
extern "C" void des_ui_report_guest_exit(const char* what, const char* notes) {
    ui::crash_report::report_guest_exit(what, notes);
}
