// Written for Demon's Souls. The setup screens' side of the setup engine's line protocol (docs/88 section 4; the
// engine's side is tools/setup/des_setup/protocol.py): one line of its output read into a Line, and what a run has said
// so far kept in a Status for the screens to draw. Nothing here starts a process or draws: setup_engine.hpp runs the
// engine and setup_window.hpp draws.
#pragma once

#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ui::setup {

// The protocol's version: the engine's first line says which it speaks ("@setup 2"). 2 added the RPCS3 route (docs/90
// 3.8): the unlock and import phases, @file, @rpcs3, @user, @library, @found, @setting, and the rpcs3 and import error
// codes.
inline constexpr int kProtocol = 2;

// One line of the engine's output.
struct Line {
    enum class Kind {
        Text, Setup, Plan, Step, Progress, Overall, Check, Disc, DiscFile, File, Rpcs3, User, Library, Install,
        InstallWhy, Found, Setting, Awake, Memory, Log, Error, Done, Release, Note
    };
    Kind kind = Kind::Text;
    std::string key;                // Step, Progress, File: the step. Check, Disc, Rpcs3, Library, Install: the state
                                    // or kind.
                                    // DiscFile: missing, case or size. Error: the code. Memory: short or ok. Done: the
                                    // result. User: the 8-digit ID. Found: what (save, current-save, trophies,
                                    // settings). Setting: its settings.ini name.
    std::string key2;               // Step: start, done or skip. Check: the file. Error: the step. Found: its state.
                                    // Install: disc, store or iso.
    long long n = 0, m = 0;         // Progress, Overall: n/m. Setup: n is the version. User: n is RPCS3's user count.
                                    // Release: n is its time (Unix seconds, 0: not known). Release's key: its version.
                                    // Disc: n is the files in the disc's list, m the ones missing.
    long long k = 0;                // Disc: the files of another size.
    std::vector<std::string> list;  // Plan: the phases. Found: the words after its state. Install: title, version,
                                    // chosen.
    std::string text;               // Check, Disc, Error, InstallWhy: the sentence. File, DiscFile, Rpcs3, Log, Library,
                                    // Install: the path.
                                    // User: the name.
                                    // Setting: the value. Release: its page. Note: the line, its indent kept (empty for
                                    // an empty line). A plain line: all of it.
};

// One line, without its line break. A line that starts with '@' but has a keyword this host does not know, or is
// malformed, reads as a plain line.
Line parse_line(std::string_view line);

// What a run of the engine has said so far.
struct Status {
    enum class State { Todo, Running, Done, Skipped, Failed };
    struct Step {
        std::string name;
        State state = State::Todo;
    };

    int protocol = 0;
    std::vector<Step> steps;  // the plan
    std::string progress;     // the last count's step ("ppu", "download", ...), and the count
    long long progressN = 0, progressM = 0;
    std::string file;         // the file the current step works on (@file: the one RPCS3 unlocks)
    std::string rpcs3State;   // the RPCS3 in use (@rpcs3): own, shared or missing
    std::string rpcs3Path;    // and its folder
    // What the RPCS3 route's check found in RPCS3 (docs/90 3.8).
    std::string userId, userName;              // RPCS3's active user (@user)
    long long users = 0;                       // and how many users RPCS3 has
    std::string libraryKind, libraryPath;      // Demon's Souls in RPCS3's library (@library): folder, iso, store, none
    struct Found {
        std::string state;                     // ok, none, other-user or busy
        std::vector<std::string> values;       // save: characters, saved; current-save: saved; trophies: earned/total;
                                               // settings: their count ("-" where unknown, times in Unix seconds)
    };
    std::map<std::string, Found> found;        // by what: save, current-save, trophies, settings (@found)
    // The copies of the game in RPCS3 to choose from (@install): none unless RPCS3 has more than one.
    struct Install {
        std::string state;    // ready, decrypt, update, incomplete, bad or iso
        std::string kind;     // disc, store or iso
        std::string title;    // the disc's TITLE_ID, which a PlayStation Store copy also says
        std::string version;  // empty: not known
        std::string path;     // its folder or file
        std::string why;      // why it is not ready, a sentence for the player
        bool chosen = false;  // the copy this check is of
    };
    std::vector<Install> installs;
    std::vector<std::pair<std::string, std::string>> settings;  // the settings that would come across (@setting)
    int overall = 0;          // the whole run, per mille
    bool awake = false;       // the engine keeps the PC from sleeping
    bool memoryShort = false; // the compile runs fewer jobs than it could, for want of memory (@memory)
    std::vector<Line> checks;  // the checked files
    // Every file of the disc copy against the disc's list (@disc, tools/setup/des_setup/disc.py): a warning only, so
    // the result stays ok. Empty state: the engine said nothing about it.
    struct Disc {
        std::string state;  // complete, incomplete or unknown (setup has no list for this disc)
        long long files = 0, missing = 0, differ = 0;
        std::string sentence;
        std::vector<std::pair<std::string, std::string>> list;  // (missing, case or size; path), the first ones
        bool incomplete() const { return state == "incomplete"; }
    } disc;
    std::string errorCode, errorStep, errorText, logPath;
    std::string result;            // ok, fix or failed (the last line's), stopped (Cancel); empty while it runs
    std::deque<std::string> tail;  // the last plain lines, for Details
    // update-check (update.py, docs/105): the newer release, and its notes (Markdown, a line each).
    struct Release {
        std::string version, page;
        long long when = 0;
    } release;
    std::vector<std::string> notes;

    void apply(const Line& line);
    // The engine's process has ended: `stopped` if Cancel ended it. One that ended without its last line failed.
    void ended(int exit_status, bool stopped);
    const Step* current() const;  // the running step, or null
    bool store() const;           // the game checked is a PlayStation Store copy (the @check line STORE)
    std::string game_name() const;  // "Demon's Souls, US disc, version 1.00", for the game checked
    bool finished() const { return !result.empty(); }
};

// The time left, from how fast the whole run's bar moves: nothing for the first minute, or until enough of the bar has
// moved to judge, then smoothed, and counted down between the bar's moves.
class Eta {
public:
    void reset() { *this = Eta{}; }
    void add(int overall, unsigned long long now_ms);
    std::string text(unsigned long long now_ms) const;  // "About 25 minutes left", or empty while it cannot tell

private:
    unsigned long long mFirstMs = 0, mStartMs = 0, mLeftAt = 0;
    int mStart = -1, mLast = -1;
    double mLeftMs = -1.0;
};

}  // namespace ui::setup
