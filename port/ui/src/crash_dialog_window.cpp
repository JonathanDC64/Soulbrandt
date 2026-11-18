// The crash dialog's window (crash_dialog.hpp): the crash's, the hang's or the lost device's, with its details.
// Adapted for Demon's Souls from FFCCEOT-Recomp's crash_dialog.cpp (the modal and its actions). It was crash_dialog.cpp's,
// which keeps the window of its own the dialog has on Windows; off Windows the dialog's process draws it in setup's
// window-only start (crash_report_posix.cpp), and ui_render_compare draws it for its pictures.
#include "crash_dialog.hpp"

#include "looks.hpp"
#include "modal.hpp"
#include "report_ack.hpp"
#include "ui.hpp"

#include <memory>

namespace ui::crash_dialog {

// Show details: the stack, or a hang's threads, over the dialog.
void show_details(const Content& content, Modal& modal) {
    modal.push(std::make_unique<Modal>(Modal::Props{
        .title = "Details",
        .bodyRml = escape_lines(content.details),
        .actions = {ModalAction{.label = "Close", .onPressed = [](Modal& d) { d.pop(); }}},
        .onDismiss = [](Modal& d) { d.pop(); },
        .variant = "long-body",  // (window.rcss: the thread list scrolls, Close stays in view)
    }));
}

void push_window(const Content& content, const Actions& actions, std::function<void()> closed) {
    const auto dismiss = [closed](Modal&) {
        if (closed) closed();
    };
    const auto details = [&content](Modal& modal) { show_details(content, modal); };
    // A look whose screens are pages (DeS UI) lists the actions as rows, the last under them, as its questions with
    // named answers; Alpha UI keeps them side by side.
    const bool rows = looks::current().pages;
    if (content.lost) push_document(std::make_unique<Modal>(Modal::Props{
        .title = "The game can no longer draw",
        .bodyRml = "<b>" + escape(content.what) +
                   ".</b> The game lost its graphics device, so its picture stays as it was while it runs on. "
                   "Restart the game to go on.<br/><br/>"
                   "A report was saved on this PC. Reporting the problem opens a new issue on GitHub with the "
                   "details filled in, and a zip to attach with what each part of the game was doing, your settings, "
                   "the game's recent messages and your save (your characters and their names). It has no game "
                   "files; nothing is sent until you submit the issue.",
        .actions =
            {
                ModalAction{
                    .label = "Restart game",
                    .onPressed =
                        [&actions, closed](Modal&) {
                            if (actions.restart) actions.restart();
                            if (closed) closed();
                        },
                },
                ModalAction{
                    .label = "Report on GitHub",
                    .onPressed =
                        [&actions](Modal& modal) {
                            with_zip_ack([&actions, &modal] {
                                if (actions.report) modal.set_body_text(actions.report());
                            });
                        },
                },
                ModalAction{.label = "Show details", .onPressed = details},
                ModalAction{
                    .label = "Close the game",
                    .onPressed =
                        [&actions, closed](Modal&) {
                            if (actions.close_game) actions.close_game();
                            if (closed) closed();
                        },
                },
            },
        .onDismiss = dismiss,
        .variant = "danger",
        .icon = "error",
        .isVertical = rows,
        .focusAction = 1,  // Report on GitHub: a report is the one thing the player can't do later
    }));
    else if (content.hang) push_document(std::make_unique<Modal>(Modal::Props{
        .title = "The game is not responding",
        .bodyRml = "<b>" + escape(content.what) +
                   ".</b> The menu cannot open. The game may still recover: keep waiting, or close it.<br/><br/>"
                   "A report was saved on this PC. Reporting the problem opens a new issue on GitHub with the "
                   "details filled in, and a zip to attach with what each part of the game was doing, your settings, "
                   "the game's recent messages and your save (your characters and their names). It has no game "
                   "files; nothing is sent until you submit the issue.",
        .actions =
            {
                ModalAction{.label = "Keep waiting", .onPressed = dismiss},
                ModalAction{
                    .label = "Report on GitHub",
                    .onPressed =
                        [&actions](Modal& modal) {
                            with_zip_ack([&actions, &modal] {
                                if (actions.report) modal.set_body_text(actions.report());
                            });
                        },
                },
                ModalAction{.label = "Show details", .onPressed = details},
                ModalAction{
                    .label = "Close the game",
                    .onPressed =
                        [&actions, closed](Modal&) {
                            if (actions.close_game) actions.close_game();
                            if (closed) closed();
                        },
                },
            },
        .onDismiss = dismiss,
        .variant = "danger",
        .icon = "error",
        .isVertical = rows,
        .focusAction = 1,  // Report on GitHub: a report is the one thing the player can't do later
    }));
    else push_document(std::make_unique<Modal>(Modal::Props{
        .title = "The game has crashed",
        .bodyRml = "<b>" + escape(content.what) +
                   "</b><br/><br/>A report was saved on this PC. Reporting the problem opens a new issue on GitHub "
                   "with the details filled in, and a zip to attach with the stack, your settings (settings.ini), "
                   "the game's recent messages and your save (your characters and their names). It has no game "
                   "files; nothing is sent until you submit the issue.",
        .actions =
            {
                ModalAction{
                    .label = "Restart game",
                    .onPressed =
                        [&actions, closed](Modal&) {
                            if (actions.restart) actions.restart();
                            if (closed) closed();
                        },
                },
                ModalAction{
                    .label = "Report on GitHub",
                    .onPressed =
                        [&actions](Modal& modal) {
                            with_zip_ack([&actions, &modal] {
                                if (actions.report) modal.set_body_text(actions.report());
                            });
                        },
                },
                ModalAction{.label = "Show details", .onPressed = details},
                ModalAction{.label = "Close", .onPressed = dismiss},
            },
        .onDismiss = dismiss,
        .variant = "danger",
        .icon = "error",
        .isVertical = rows,
        .focusAction = 1,  // Report on GitHub: a report is the one thing the player can't do later
    }));
}

}  // namespace ui::crash_dialog
