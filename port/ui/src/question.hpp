// Written for Demon's Souls. The menus' questions (Quit, a report, a restart for a setting), asked one way wherever they
// come from: what they say and their answers here, how they look in the look's style sheets (looks.hpp).
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ui {

struct Answer {
    std::string label;
    std::function<void()> chosen;  // once the question has gone; none: the answer only closes it
};

// A question and its answers, in the order shown: the action first and the safe answer after it, as the game's own
// questions have them. The safe answer has the focus first, and Back (Esc, B, Circle, F1) gives it.
struct Question {
    std::string title;  // the question itself: "Quit to the desktop?"
    std::string body;   // a line or two under it, plain text
    std::vector<Answer> answers;
    int safe = 0;         // which answer is the safe one
    bool danger = false;  // the action loses something (Quit)
};

// Shows the question over whatever is open. False, and nothing shown, while another one asked here is still up (a
// second press of Quit), or with no answers.
bool ask(Question question);

}  // namespace ui
