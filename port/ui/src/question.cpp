// Written for Demon's Souls (question.hpp).
#include "question.hpp"

#include "modal.hpp"
#include "ui.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace ui {
namespace {

const Document* sAsked = nullptr;  // the question up now

// The question as a modal. However it goes (an answer, Back, or every document closed at once), ask() can ask again
// once it is gone.
class QuestionModal final : public Modal {
public:
    using Modal::Modal;
    ~QuestionModal() override {
        if (sAsked == this) sAsked = nullptr;
    }
};

}  // namespace

bool asking() {
    return sAsked && !sAsked->closed();
}

bool ask(Question question) {
    // (one closed with every document at once, ui::close_all_documents, is still in the stack until the next frame:
    // it is asked no more)
    if ((sAsked && !sAsked->closed()) || question.answers.empty()) return false;
    const int safe = std::clamp(question.safe, 0, static_cast<int>(question.answers.size()) - 1);
    // An answer closes the question first, then does its part: what it starts (a restart, Quit) finds it gone.
    const auto answer = [](Modal& modal, const std::function<void()>& chosen) {
        if (sAsked == &modal) sAsked = nullptr;
        modal.pop();
        if (chosen) chosen();
    };
    std::vector<ModalAction> actions;
    for (int i = 0; i < static_cast<int>(question.answers.size()); i++) {
        Answer& a = question.answers[static_cast<size_t>(i)];
        actions.push_back(ModalAction{
            .label = std::move(a.label),
            .onPressed = [answer, chosen = a.chosen, sound = i == safe ? kSoundWindowClose : kSoundClick](Modal& m) {
                play_sound(sound);
                answer(m, chosen);
            },
        });
    }
    sAsked = &push_document(std::make_unique<QuestionModal>(Modal::Props{
        .title = std::move(question.title),
        .bodyText = std::move(question.body),
        .actions = std::move(actions),
        // Back: the safe answer (Modal plays its sound)
        .onDismiss = [answer, chosen = question.answers[static_cast<size_t>(safe)].chosen](Modal& m) { answer(m, chosen); },
        .variant = question.danger ? "danger" : "",
        .icon = "question-mark",
        .focusAction = safe,
    }));
    return true;
}

}  // namespace ui
