// settings_hooks.hpp for the game: each hook forwards to the UI.
#include "settings_hooks.hpp"

#include "app.hpp"
#include "input.hpp"
#include "modal.hpp"
#include "report.hpp"
#include "settings.hpp"
#include "souls_model.hpp"
#include "stick_preview.hpp"
#include "ui.hpp"
#include "update_window.hpp"
#include "updates.hpp"
#include "../../des_seamless.h"

#include <chrono>
#include <cstdint>
#include <memory>

// cellPad.h (declared here: des_ui does not take ps3recomp's include paths): the i-th controller connected now, its
// PAD_CONTROLLER name ("controller1", "playstation2") and what it is ("DualSense (USB)"); 0 past the last.
extern "C" int cellPad_host_controller(int i, char* id, int idn, char* name, int namen);
extern "C" uint8_t g_des_region;  // des_region.h

namespace ui::catalog::hooks {

std::string escape(std::string_view text) {
    return ui::escape(text);
}

void open_url(const char* url) {
    report::open_url(url);
}

void toast(const std::string& text, int seconds) {
    push_toast({.content = text, .duration = std::chrono::seconds(seconds)});
}

int region() {
    return g_des_region;
}

std::string menu_chord_label() {
    return input::chord_label(settings::get_int("pad_menu_chord"));
}

PadSupport pad_support() {
    const input::PadSupport pads = input::pad_support();
    return {pads.value, pads.help};
}

std::vector<Pad> connected_pads() {
    std::vector<Pad> out;
    char id[16], name[64];
    for (int i = 0; cellPad_host_controller(i, id, int(sizeof id), name, int(sizeof name)); ++i) out.push_back({id, name});
    return out;
}

int stick_default_zone() {
    return ui::stick_default_zone();
}

std::vector<Sword> swords() {
    std::vector<Sword> out;
    for (const models::Sword& s : models::swords()) out.push_back({s.name, s.label});
    return out;
}

void apply_scale() {
    ui::apply_scale();
}

void party_size_changed() {
    des::seamless::party_size_changed();
}

std::string update_status() {
    return updates::status_line();
}

bool update_possible() {
    return updates::possible();
}

bool update_checking() {
    return updates::state() == updates::State::Checking;
}

void update_check() {
    updates::start_check();
}

std::string update_found_version() {
    return updates::state() == updates::State::Found ? updates::found().version : std::string();
}

void open_update_notes() {
    push_document(std::make_unique<UpdateWindow>(UpdateWindow::Props{
        .open = UpdateWindow::Open::Notes, .release = updates::found(), .overGame = updates::over_game()}));
}

std::string update_kept_version() {
    return updates::kept_version();
}

// Go back to the version the last update kept (updates.hpp), asked first. Over the game, the program starts again with
// setup's screens alone for it: a running game cannot replace its own files.
void ask_go_back(const std::string& kept) {
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Go back to " + kept + "?",
        .bodyText = "The game starts again as version " + kept + ", with the game build it had. Your saves and settings "
                    "stay. Version " + updates::this_version() + " is kept, so you can update to it again.",
        .actions =
            {
                ModalAction{.label = "Cancel",
                    .onPressed =
                        [](Modal& modal) {
                            play_sound(kSoundWindowClose);
                            modal.pop();
                        }},
                ModalAction{.label = "Go back",
                    .onPressed =
                        [](Modal& modal) {
                            play_sound(kSoundClick);
                            modal.pop();
                            if (updates::over_game()) {
                                updates::restart_into("update-back", "");
                            } else {
                                push_document(std::make_unique<UpdateWindow>(
                                    UpdateWindow::Props{.open = UpdateWindow::Open::Back}));
                            }
                        }},
            },
        .onDismiss = [](Modal& modal) { modal.pop(); },
        .icon = "question-mark",
    }));
}

bool data_dir_known() {
    return !report::data_dir().empty();
}

}  // namespace ui::catalog::hooks
