// Adapted from Dusklight (CC0), src/dusk/ui/settings.cpp, by way of FFCCEOT-Recomp's settings_window.cpp: the same
// window, tabs, rows and help panes. Adapted for Demon's Souls: the rows are the port's run options (README, "Running"),
// stored in settings.ini under their environment variable names (settings.hpp), and listed in settings_catalog.hpp. A
// row's value is shown on the left, its choices or its help on the right. A row whose option was set in the environment
// when the game started is disabled (the environment wins) and says so.
#include "settings_window.hpp"

#include "bool_button.hpp"
#include "controls_window.hpp"
#include "hud_editor.hpp"
#include "modal.hpp"
#include "number_button.hpp"
#include "pane.hpp"
#include "players_window.hpp"
#include "privacy_window.hpp"
#include "settings.hpp"
#include "settings_catalog.hpp"
#include "settings_values.hpp"
#include "slider.hpp"
#include "stick_preview.hpp"
#include "string_button.hpp"
#include "ui.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace ui {
namespace {

using catalog::RowDesc;

// What heads a row's help: the option is set from outside, or it takes effect at the next start.
Rml::String with_note(const Rml::String& key, Rml::String help) {
    const settings::Def* d = settings::find(key);
    if (settings::pinned(key)) {
        const Rml::String value = settings::pinned_value(key);
        return "<status-badge class=\"warning\">Set to " + escape(value.empty() ? Rml::String("(empty)") : value) +
               " when the game started (" + escape(key) + "), so this row can't change it.</status-badge><br/>" + help;
    }
    if (d && d->apply == settings::Apply::Restart) {
        return "<status-badge class=\"info\">Takes effect the next time you start the game.</status-badge><br/>" + help;
    }
    return help;
}

Rml::String badge(const catalog::Note& note) {
    return Rml::String("<status-badge class=\"") + (note.kind == catalog::Note::Kind::Warning ? "warning" : "info") +
           "\">" + note.text + "</status-badge>";
}

// A row's help as its pane shows it: the row's own notes, the pinned or restart note, the help and its "(KEY)".
Rml::String full_help(const RowDesc& row) {
    Rml::String notes;
    if (row.notes)
        for (const catalog::Note& n : row.notes()) notes += badge(n) + "<br/>";
    const Rml::String help = catalog::help_with_key(row);
    return notes + (row.key.empty() ? help : with_note(row.key, help));
}

void play_button_sound(catalog::Sound sound) {
    switch (sound) {
    case catalog::Sound::Click: play_sound(kSoundClick); break;
    case catalog::Sound::Open: play_sound(kSoundWindowOpen); break;
    case catalog::Sound::None: break;
    }
}

// The buttons below a row's help.
void add_buttons(Pane& pane, const RowDesc& row) {
    if (!row.buttons) return;
    for (catalog::Button& b : row.buttons()) {
        auto press = [sound = b.sound, press = b.press] {
            play_button_sound(sound);
            if (press) press();
        };
        if (b.disabled)
            pane.add_button({.text = b.text, .isDisabled = b.disabled}).on_pressed(std::move(press));
        else
            pane.add_button(b.text).on_pressed(std::move(press));
    }
}

// RML in a pane that shows only while `visible` holds, checked at each update: a note that goes away as soon as what
// it asks for is done (the Server row's, once Custom is pressed), without rebuilding the pane from inside the press.
class LiveNote : public Component {
public:
    LiveNote(Rml::Element* parent, const Rml::String& rml, std::function<bool()> visible)
        : Component(append(parent, "div")), mVisible(std::move(visible)) {
        mRoot->SetInnerRML(rml);
        update();
    }
    void update() override {
        set_display(mRoot, mVisible() ? Rml::Style::Display::Block : Rml::Style::Display::None);
        Component::update();
    }

private:
    std::function<bool()> mVisible;
};

// ps3emu/ps3_net_signin.h (declared here: des_ui does not take ps3recomp's include paths). Its outcome comes back
// through ps3_net_erase_result, which net_notices.cpp shows.
extern "C" int ps3_net_player_erase(void);

// Delete my data (the server's docs/social.md), asked first.
void ask_erase() {
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Delete your data on the server?",
        .bodyText = "This deletes your name, your characters' online records, your messages and bloodstains, and the "
                    "rest the online server keeps about you (Privacy note). Online play turns off, so the game goes "
                    "back to its title screen. It cannot be undone.",
        .actions =
            {
                ModalAction{.label = "Cancel",
                    .onPressed =
                        [](Modal& modal) {
                            play_sound(kSoundWindowClose);
                            modal.pop();
                        }},
                ModalAction{.label = "Delete",
                    .onPressed =
                        [](Modal& modal) {
                            play_sound(kSoundClick);
                            modal.pop();
                            if (ps3_net_player_erase() != 0) {
                                push_toast({
                                    .type = "warning",
                                    .title = "Online",
                                    .content = "Not signed in to the online server, so nothing was deleted. Play "
                                               "online, then try again.",
                                    .duration = std::chrono::seconds(10),
                                });
                            }
                        }},
            },
        .onDismiss = [](Modal& modal) { modal.pop(); },
        .icon = "warning",
    }));
}

// One row of a tab, on the left, and what it shows on the right when it has the focus.
class RowBuilder {
public:
    RowBuilder(Window& window, Pane& left, Pane& right) : mWindow(window), mLeft(left), mRight(right) {}

    void build(const RowDesc& row) {
        std::visit([&](const auto& kind) { add(row, kind); }, row.kind);
    }

private:
    // On or off: "1" or "0". inverted: On means "0" (AUDIO_MUTE shown as Sound).
    void add(const RowDesc& row, const catalog::ToggleRow& kind) {
        const Rml::String key = row.key;
        const bool inverted = kind.inverted;
        const auto value_of = [key, inverted] {
            const std::string v = settings::in_use(key);
            const bool on = !v.empty() && v != "0";
            return inverted ? !on : on;
        };
        const auto& locked = row.locked;
        BoolButton::Props props{
            .key = row.label,
            .getValue = value_of,
            .setValue =
                [key, inverted, value_of, locked](bool value) {
                    if (settings::pinned(key) || (locked && locked()) || value == value_of()) return;
                    settings::set(key, (value != inverted) ? "1" : "0");
                },
            .isModified = [key] { return !settings::is_default(key); },
        };
        if (locked) {  // (a row locked by more than the environment: its value says why)
            props.isDisabled = [key, locked] { return settings::pinned(key) || locked(); };
            props.valueOverride = [locked, text = row.lockedValue] { return locked() ? text() : Rml::String(); };
        }
        auto& button = mLeft.add_child<BoolButton>(std::move(props));
        mLeft.register_control(button, mRight, [&row](Pane& pane) {
            pane.add_rml(full_help(row));
            add_buttons(pane, row);
        });
    }

    // The row's choices as they are now, to f: its fixed list as it stands, or the live one made now.
    template <class F>
    static auto with_choices(const catalog::ChoiceRow& kind, const Rml::String& key, F&& f) {
        return kind.live ? f(kind.live(settings::in_use(key))) : f(kind.choices);
    }

    static const catalog::Choice* find_choice(const std::vector<catalog::Choice>& list, const std::string& value) {
        const auto it = std::find_if(list.begin(), list.end(), [&](const catalog::Choice& c) { return c.value == value; });
        return it == list.end() ? nullptr : &*it;
    }

    // A list of choices on the right, the current one marked. A value from settings.ini that is not in the list shows
    // as itself; the row's Custom choice, when it has one, is marked for it.
    void add(const RowDesc& row, const catalog::ChoiceRow& kind) {
        const Rml::String key = row.key;
        const auto current = [key, blank = kind.blank] {
            const std::string v = settings::in_use(key);
            return v.empty() && !blank.empty() ? blank : v;
        };
        const auto listed = [&kind, key, current] {
            return with_choices(kind, key, [&](const auto& list) { return find_choice(list, current()) != nullptr; });
        };
        auto& button = mLeft.add_select_button({
            .key = row.label,
            .getValue =
                [&kind, current, key] {
                    return with_choices(kind, key, [&](const auto& list) {
                        const catalog::Choice* c = find_choice(list, current());
                        return c ? catalog::choice_label(*c) : "Custom (" + settings::in_use(key) + ")";
                    });
                },
            .isModified = [key] { return !settings::is_default(key); },
        });
        mLeft.register_control(button, mRight, [&row, &kind, key, current, listed](Pane& pane) {
            const std::vector<catalog::Choice> choices = kind.live ? kind.live(settings::in_use(key)) : kind.choices;
            for (const catalog::Choice& c : choices) {
                pane.add_button({
                                    .text = catalog::choice_label(c),
                                    .isSelected = [current, value = c.value] { return current() == value; },
                                    .isDisabled = [key] { return settings::pinned(key); },
                                })
                    .on_pressed([key, value = c.value, &kind] {
                        play_sound(kSoundItemChange);
                        settings::set(key, value);
                        if (kind.changed) kind.changed();
                    });
            }
            if (kind.custom) {
                pane.add_button({
                                    .text = kind.custom->label,
                                    .isSelected = [listed] { return !listed(); },
                                    .isDisabled = [key] { return settings::pinned(key); },
                                })
                    .on_pressed([key, &kind] {
                        play_sound(kSoundItemChange);
                        settings::set(key, kind.custom->value());
                    });
            }
            if (kind.liveNote) pane.add_child<LiveNote>("<br/>" + badge(kind.liveNote->note), kind.liveNote->visible);
            if (const Rml::String help = full_help(row); !help.empty()) pane.add_rml("<br/>" + help);
            add_buttons(pane, row);
        });
    }

    // A number, stepped on the left; a stick's deadzone also gets a slider and the stick drawn on the right.
    void add(const RowDesc& row, const catalog::NumberRow& kind) {
        const Rml::String key = row.key;
        const auto set_value = [key, set = kind.set](int v) {
            if (settings::pinned(key)) return;
            set(v);
        };
        auto& button = mLeft.add_child<NumberButton>(NumberButton::Props{
            .key = row.label,
            .getValue = kind.get,
            .setValue = set_value,
            .isDisabled = [key] { return settings::pinned(key); },
            .isModified = kind.modified ? kind.modified : std::function<bool()>([key] { return !settings::is_default(key); }),
            .min = kind.min,
            .max = kind.max,
            .step = kind.step,
            .suffix = kind.suffix,
            .format = kind.format,
        });
        mLeft.register_control(button, mRight, [&row, &kind, key, set_value](Pane& pane) {
            if (kind.stick >= 0) {
                pane.add_child<Slider>(Slider::Props{
                    .min = kind.min,
                    .max = kind.max,
                    .suffix = kind.suffix,
                    .getValue = kind.get,
                    .setValue = set_value,
                    .isDisabled = [key] { return settings::pinned(key); },
                });
                pane.add_child<StickPreview>(StickPreview::Props{.stick = kind.stick, .zone = kind.get});
            }
            pane.add_rml(full_help(row));
            add_buttons(pane, row);
        });
    }

    // Typed text.
    void add(const RowDesc& row, const catalog::TextRow& kind) {
        const Rml::String key = row.key;
        auto& button = mLeft.add_child<StringButton>(StringButton::Props{
            .key = row.label,
            .getValue = [key] { return Rml::String(settings::in_use(key)); },
            .setValue =
                [key, set = kind.set](Rml::String value) {
                    if (settings::pinned(key)) return;
                    if (set)
                        set(value);
                    else
                        settings::set(key, value);
                },
            .isDisabled = [key] { return settings::pinned(key); },
            .isModified = [key] { return !settings::is_default(key); },
            .maxLength = kind.maxLength,
        });
        mLeft.register_control(button, mRight, [&row](Pane& pane) {
            pane.add_rml(full_help(row));
            add_buttons(pane, row);
        });
    }

    // A value, and what it is.
    void add(const RowDesc& row, const catalog::InfoRow& kind) {
        std::function<Rml::String()> value;
        if (kind.everyFrame)
            value = [&kind] { return kind.value(); };
        else
            value = [v = kind.value()] { return v; };
        mLeft.register_control(mLeft.add_select_button({.key = row.label, .getValue = std::move(value)}), mRight,
            [&row](Pane& pane) {
                pane.add_rml(full_help(row));
                if (const Rml::String note = row.footnote(); !note.empty()) pane.add_rml("<br/>" + note);
                add_buttons(pane, row);
            });
    }

    // A button that opens another page, or asks.
    void add(const RowDesc& row, const catalog::ActionRow& kind) {
        using Opens = catalog::ActionRow::Opens;
        Window& window = mWindow;
        mLeft.register_control(
            mLeft.add_group_button({.text = row.label}).on_pressed([&window, opens = kind.opens] {
                switch (opens) {
                case Opens::HudEditor:
                    play_sound(kSoundClick);
                    window.push(std::make_unique<HudEditor>());
                    break;
                case Opens::Controls:
                    play_sound(kSoundClick);
                    window.push(std::make_unique<ControlsWindow>());
                    break;
                case Opens::Players:
                    play_sound(kSoundClick);
                    window.push(std::make_unique<PlayersWindow>());
                    break;
                case Opens::Privacy:
                    play_sound(kSoundWindowOpen);
                    window.push(std::make_unique<PrivacyWindow>(PrivacyWindow::Mode::Read));
                    break;
                case Opens::DeleteData:
                    play_sound(kSoundWindowOpen);
                    ask_erase();
                    break;
                }
            }),
            mRight, [&row](Pane& pane) {
                pane.add_rml(full_help(row));
                add_buttons(pane, row);
            });
    }

    Window& mWindow;
    Pane& mLeft;
    Pane& mRight;
};

}  // namespace

SettingsWindow::SettingsWindow(bool prelaunch) : mPrelaunch(prelaunch) {
    for (const catalog::TabDesc& tab : catalog::tabs()) {
        add_tab(tab.title, [this, &tab](Rml::Element* content) {
            auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
            auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);
            RowBuilder rows(*this, leftPane, rightPane);
            for (const catalog::SectionDesc& section : tab.sections) {
                if (!section.title.empty() && (!section.when || section.when())) leftPane.add_section(section.title);
                for (const RowDesc& row : section.rows)
                    if (!row.when || row.when()) rows.build(row);
            }
        });
    }
}

void SettingsWindow::update() {
    Window::update();
}

void SettingsWindow::hide(bool close) {
    settings::save();
    Window::hide(close);
}

}  // namespace ui
