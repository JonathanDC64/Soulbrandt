// Written for Demon's Souls. See ack.hpp.
#include "ack.hpp"

#include "modal.hpp"
#include "pane.hpp"
#include "report.hpp"
#include "sound.hpp"
#include "ui.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <utility>

namespace ui {
namespace {

// The answer's file. None when there is no data folder (a test run keeps nothing).
std::filesystem::path marker(const wchar_t* file) {
    const auto& dir = report::data_dir();
    return dir.empty() || !file ? std::filesystem::path{} : dir / file;
}

bool answered(const wchar_t* file) {
    std::error_code ec;
    const auto path = marker(file);
    return !path.empty() && std::filesystem::exists(path, ec);
}

void keep_answer(const wchar_t* file, const char* kept) {
    const auto path = marker(file);
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (FILE* f = report::open_file(path, "wb")) {
        std::fputs(kept, f);
        std::fclose(f);
    }
}

}  // namespace

void with_ack(Ack ack, std::function<void()> then) {
    if (answered(ack.file)) {
        if (then) then();
        return;
    }
    const auto ticked = std::make_shared<bool>(false);
    Modal::Props props{
        .title = ack.title,
        .bodyRml = ack.bodyRml,
        .actions =
            {
                ModalAction{
                    .label = "Continue",
                    .onPressed =
                        [ticked, file = ack.file, kept = ack.kept, then = std::move(then)](Modal& m) {
                            if (!*ticked) return;
                            play_sound(kSoundClick);
                            keep_answer(file, kept);
                            const auto go = then;  // (the modal and this lambda go with pop)
                            m.pop();
                            if (go) go();
                        },
                    .isDisabled = [ticked] { return !*ticked; },
                },
                ModalAction{
                    .label = "Cancel",
                    .onPressed =
                        [](Modal& m) {
                            play_sound(kSoundWindowClose);
                            m.pop();
                        },
                },
            },
        .onDismiss = [](Modal& m) { m.pop(); },
        .icon = ack.icon,
    };
    if (!ack.bodyText.empty()) props.bodyText = ack.bodyText;
    auto owned = std::make_unique<Modal>(std::move(props));
    // The tick: a button whose box fills when pressed (Material Symbols' check_box and check_box_outline_blank), with
    // its state in words too for a look that draws it as a row and its value (No, Yes). Made before the question
    // shows, so the focus starts on it.
    auto& tick = owned->content_pane().add_button(ControlledButton::Props{
        .isSelected = [ticked] { return *ticked; },
    });
    tick.root()->SetClass("ack-tick", true);
    Rml::Element* box = append(tick.root(), "icon");
    box->SetClass("check-box-blank", true);
    append_text_element(tick.root(), "tick-label", ack.tick);
    Rml::Element* says = append_text_element(tick.root(), "tick-value", "No");
    tick.on_pressed([ticked, box, says] {
        *ticked = !*ticked;
        box->SetClass("check-box", *ticked);
        box->SetClass("check-box-blank", !*ticked);
        set_text_content(says, *ticked ? "Yes" : "No");
        play_sound(*ticked ? kSoundItemEnable : kSoundItemDisable);
    });
    // Over what is showing, covering it as a window's own push does (the crash dialog's text would show through).
    if (Document* top = top_document())
        top->push(std::move(owned));
    else
        push_document(std::move(owned));
}

}  // namespace ui
