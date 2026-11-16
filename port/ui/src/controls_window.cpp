// Adapted from FFCCEOT-Recomp's controls_window.cpp, itself after Dusklight's controller_config.cpp (CC0): the same
// layout (pages on the left, the bindings on the right, a binding is changed by pressing its row and then the button or
// key wanted). Adapted for Demon's Souls: the gamepad page is cellPad's PAD_MAP (the game's button = the pad button
// that presses it). The keyboard and mouse page is its PAD_KEYS, kept as changes to the Souls-style layout
// (settings.cpp, kKeysLayout): two rows an action, each a key, a mouse button, a wheel notch or one of those with
// Shift, Ctrl or Alt held (a chord). Both are kept in settings.ini. What the rows change, and how a row's new input is
// caught, are controls_model.hpp's, which DeS UI's page shares (controls_page.hpp).
#include "controls_window.hpp"

#include "keys.hpp"
#include "modal.hpp"
#include "pane.hpp"
#include "settings.hpp"
#include "platform.hpp"
#include "ui.hpp"

#include <string>

namespace ui {

using namespace controls;

KeyInput first_key(const char* target) {
    const int t = controls::target_index(target);
    if (t < 0) return {};
    for (const Binding& b : key_map()[t]) {
        if (!b.menu) return {b.in, b.mod};
    }
    return {};
}

std::string key_cap_label(int vk) {
    switch (vk) {
    case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT: return {};
    case VK_SPACE: return "Space";
    case VK_RETURN: return "Enter";
    case VK_TAB: return "Tab";
    case VK_ESCAPE: return "Esc";
    case VK_BACK: return "Bksp";
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return "Shift";
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "Ctrl";
    case VK_MENU: case VK_LMENU: case VK_RMENU: return "Alt";
    case VK_INSERT: return "Ins";
    case VK_DELETE: return "Del";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PgUp";
    case VK_NEXT: return "PgDn";
    case VK_CAPITAL: return "Caps";
    default: break;
    }
    if (vk >= kInMouse) return {};
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, char(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    std::string name = keys::key_name(vk);
    if (name.size() > 5) name.resize(5);
    return name;
}

ControlsWindow::ControlsWindow() {
    add_tab("Buttons", [this](Rml::Element* content) { build_tab(content); });
}

void ControlsWindow::build_tab(Rml::Element* content) {
    mCapture.cancel();
    auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
    auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

    leftPane.register_control(leftPane.add_group_button({.text = "Gamepad"}), rightPane, [this](Pane& pane) {
        mPage = Page::Gamepad;
        render_page(pane, Page::Gamepad);
    });
    leftPane.register_control(leftPane.add_group_button({.text = "Keyboard and Mouse"}), rightPane,
                              [this](Pane& pane) {
                                  mPage = Page::Keyboard;
                                  render_page(pane, Page::Keyboard);
                              });

    leftPane.add_section("Options");
    leftPane.register_control(
        leftPane.add_button("Restore Default Buttons").on_pressed([this] {
            play_sound(kSoundClick);
            const auto dismiss = [](Modal& modal) { modal.pop(); };
            push(std::make_unique<Modal>(Modal::Props{
                .title = "Restore Default Buttons",
                .bodyText = "Every gamepad button goes back to the game's own layout, and the keys and mouse buttons "
                            "to the Souls layout this port starts with.",
                .actions =
                    {
                        ModalAction{
                            .label = "Cancel",
                            .onPressed =
                                [dismiss](Modal& modal) {
                                    play_sound(kSoundWindowClose);
                                    dismiss(modal);
                                },
                        },
                        ModalAction{
                            .label = "Restore",
                            .onPressed =
                                [dismiss](Modal& modal) {
                                    play_sound(kSoundClick);
                                    restore_defaults();
                                    dismiss(modal);
                                },
                        },
                    },
                .onDismiss = dismiss,
                .icon = "question-mark",
            }));
        }),
        rightPane, [](Pane& pane) {
            pane.clear();
            pane.add_text("Puts all buttons and keys back to the defaults.");
        });
    render_page(rightPane, mPage);
}

void ControlsWindow::render_page(Pane& pane, Page page) {
    pane.clear();
    if (page == Page::Gamepad) {
        render_pad_page(pane);
    } else {
        render_key_page(pane);
    }
}

void ControlsWindow::render_pad_page(Pane& pane) {
    if (settings::pinned("PAD_MAP")) {
        pane.add_rml("<status-badge class=\"warning\">Set to " + escape(settings::pinned_value("PAD_MAP")) +
                     " when the game started (PAD_MAP), so these can't change it.</status-badge><br/>");
    }
    for (int r = 0; r < kPadCount; r++) {
        const int i = kPadRows[r];
        if (kPadSections[r]) pane.add_section(kPadSections[r]);
        pane.add_select_button({
                                   .key = kPadLabels[i],
                                   .getValue =
                                       [this, i]() -> Rml::String {
                                           if (mCapture.active() && mCapture.pad() && mCapture.target() == i)
                                               return mCapture.pending_label();
                                           return kPadLabels[pad_map()[i]];
                                       },
                                   .isModified = [i] { return pad_modified(i); },
                               })
            .on_pressed([this, i] {
                play_sound(kSoundClick);
                mCapture.start(i, true, 0);
            });
    }
    pane.add_rml("<br/>Choose a row, then press the button for it. Esc cancels.<br/><br/>(PAD_MAP)");
}

void ControlsWindow::render_key_page(Pane& pane) {
    if (settings::pinned("PAD_KEYS")) {
        pane.add_rml("<status-badge class=\"warning\">Set to " + escape(settings::pinned_value("PAD_KEYS")) +
                     " when the game started (PAD_KEYS), so these can't change it.</status-badge><br/>");
    }
    for (int t = 0; t < kTargetCount; t++) {
        if (kTargets[t].section) pane.add_section(kTargets[t].section);
        for (int slot = 0; slot < kSlots; slot++) {
            pane.add_select_button({
                                       .key = slot == 0 ? Rml::String(kTargets[t].label)
                                                        : Rml::String(kTargets[t].label) + ", 2nd",
                                       .getValue =
                                           [this, t, slot]() -> Rml::String {
                                               if (mCapture.active() && !mCapture.pad() && mCapture.target() == t &&
                                                   mCapture.slot() == slot)
                                                   return mCapture.pending_label();
                                               return slot_label(t, slot);
                                           },
                                       .isModified = [t, slot] { return key_modified(t, slot); },
                                   })
                .on_pressed([this, t, slot] {
                    play_sound(kSoundClick);
                    mCapture.start(t, false, slot);
                });
        }
    }
    pane.add_rml("<br/>Choose a row, then press a key, mouse button or wheel. Hold Shift, Ctrl or Alt for a "
                 "combination. Delete clears the row; Esc cancels.<br/><br/>(PAD_KEYS)");
}

void ControlsWindow::update() {
    mCapture.update();
    Window::update();
}

void ControlsWindow::hide(bool close) {
    mCapture.cancel();
    settings::save();
    Window::hide(close);
}

}  // namespace ui
