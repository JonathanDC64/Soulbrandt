// Written for Demon's Souls. A question asked once on a PC, with a tick: the window says what the player needs to know
// first, and goes on only after they tick that they have read it. The answer is a file in the data folder (report.hpp's
// data_dir()), so the game, its crash dialog and setup share it. The first report's "attach the zip" (report_ack.hpp)
// and co-op's notice (coop_window.cpp) are two.
#pragma once

#include <functional>
#include <string>

namespace ui {

struct Ack {
    const wchar_t* file = nullptr;  // the answer's file in the data folder
    const char* kept = "";          // what that file says, for whoever finds it
    std::string title;
    std::string bodyText;  // the body as plain text; or, when this is empty,
    std::string bodyRml;   // as RML
    std::string tick;      // the tick's line, "I'll ..."
    std::string icon = "question-mark";
    // A warning to see each time, with the tick to stop it (co-op's mods warning): the first button goes on without
    // the tick too, and keeps the answer only with it. `go` and `back` name the two buttons.
    bool tickOptional = false;
    std::string go = "Continue";
    std::string back = "Cancel";
    // The answer counts only for this (which mods are on, say): a different key asks again. The file holds it.
    std::string key;
};

// Runs `then` at once when the player has ticked this before on this PC (with this key). Otherwise shows the
// question: Continue waits for the tick (unless it is optional), keeps the answer when ticked and runs `then`; Cancel
// (and Back) runs nothing. With no data folder (a test run) it asks each time and keeps nothing.
void with_ack(Ack ack, std::function<void()> then);

}  // namespace ui
