// Settings' tabs, sections and rows as data: what each row shows, which setting it changes and how, and its help.
// The settings window builds its panes from it (settings_window.cpp); nothing here knows how a row is drawn, so any
// look can draw the same rows. No RmlUi: what the rows read or do outside settings.cpp goes through settings_hooks.hpp,
// and tests/settings_catalog_check.cpp checks the whole table on its own.
//
// Adding a row: one RowDesc in settings_catalog.cpp, in its tab and section, with the key's Def in settings.cpp's
// kDefs. Write its help without the "(KEY)" line (the window adds it; kHideKey for a row that shows none) and its
// choices without "(original)" (Choice::original marks the game's own). Then run settings_catalog_check.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ui::catalog {

// Text a row shows: fixed, or made each time it is shown (from what the game or the PC says then).
class Text {
public:
    Text() = default;
    Text(const char* text) : mFixed(text) {}
    Text(std::string text) : mFixed(std::move(text)) {}
    template <class F>
        requires std::is_invocable_r_v<std::string, F&>
    Text(F make) : mMake(std::move(make)) {}

    std::string operator()() const { return mMake ? mMake() : mFixed; }

private:
    std::string mFixed;
    std::function<std::string()> mMake;
};

// A line above a row's help that says why the row does what it does now. RML.
struct Note {
    enum class Kind { Info, Warning };
    Kind kind = Kind::Info;
    std::string text;
};

// The sound a button makes when pressed (sound.hpp's kSoundClick, kSoundWindowOpen).
enum class Sound { None, Click, Open };

// A button beside a row's help (Open the website, Check now).
struct Button {
    std::string text;
    Sound sound = Sound::Click;
    std::function<void()> press;
    std::function<bool()> disabled;  // none: never
};

// One choice of a ChoiceRow. original: the PS3 game's own, shown as "<label> (original)".
struct Choice {
    std::string value;
    std::string label;
    bool original = false;
};

// Picks one value of a list. A value in settings.ini that is not in the list shows as "Custom (<value>)".
struct ChoiceRow {
    std::vector<Choice> choices;
    // Read each time the row is drawn, in place of choices, given the value in use (the gamepads connected now).
    std::function<std::vector<Choice>(const std::string& current)> live;
    std::string blank;  // the choice an empty value counts as ("" none)
    // A last choice for any value not in the list (Frame rate's Custom): shown chosen while the value is none of the
    // others, and pressed, it sets value().
    struct Custom {
        std::string label;
        std::function<std::string()> value;
    };
    std::optional<Custom> custom;
    // A note below the choices, shown while visible() holds; checked as the row is drawn.
    struct LiveNote {
        Note note;
        std::function<bool()> visible;
    };
    std::optional<LiveNote> liveNote;
    std::function<void()> changed;  // after a choice is set
};

// On or off: "1" or "0". inverted: On stores "0" (AUDIO_MUTE shown as Sound).
struct ToggleRow {
    bool inverted = false;
};

// A number, stepped or typed. get and set stand between the number shown and the setting's text; set is never called
// while the setting is pinned.
struct NumberRow {
    int min = 0;
    int max = 100;
    int step = 1;
    std::string suffix;
    std::function<int()> get;
    std::function<void(int)> set;
    std::function<std::string(int)> format;  // the text for a value, in place of the value and suffix
    std::function<bool()> modified;          // none: the setting is not at its default
    int stick = -1;  // a stick's deadzone (0 left, 1 right): a slider and the stick drawn beside the row
};

// Typed text. set stores what was typed (tidied, checked); none: as typed. Never called while the setting is pinned.
struct TextRow {
    int maxLength = -1;
    std::function<void(const std::string&)> set;
};

// Shows a value and says what it is.
struct InfoRow {
    Text value;
    bool everyFrame = false;  // read each frame the row is drawn; else once, when its tab is built
};

// Opens another page or asks a question. The row's label is the button's text.
struct ActionRow {
    enum class Opens { HudEditor, Controls, Players, Privacy, DeleteData };
    Opens opens;
};

using RowKind = std::variant<ChoiceRow, ToggleRow, NumberRow, TextRow, InfoRow, ActionRow>;

enum RowFlag : unsigned {
    kHideKey = 1u << 0,  // the help ends without "(KEY)": a setting of the UI's own, not a run option
};

struct RowDesc {
    std::string label;
    std::string key;  // the setting it changes (settings.hpp); "" for a row that only shows or opens something
    RowKind kind;
    Text help;        // RML
    Text footnote;    // RML, below the help on a line of its own (a credit's licence)
    std::function<std::vector<Note>()> notes;  // above the help, while they apply
    // Locked for a reason other than the setting being pinned (a developer cheat session), and the value shown then.
    std::function<bool()> locked;
    Text lockedValue;
    std::function<std::vector<Button>()> buttons;  // below the help, made when the row is shown
    unsigned flags = 0;                            // RowFlag
    std::function<bool()> when;                    // shown only where it holds (a disc's languages); none: always
};

struct SectionDesc {
    std::string title;  // "" for rows without a heading (a tab's first rows)
    std::vector<RowDesc> rows;
    std::function<bool()> when;  // its heading shown only where it holds; none: always
};

struct TabDesc {
    std::string title;
    std::vector<SectionDesc> sections;
};

// Settings' tabs in order, made the first time they are asked for.
const std::vector<TabDesc>& tabs();

// The label a choice shows: "16:9 (original)".
std::string choice_label(const Choice& c);

// The help a row shows: its help, then "(KEY)" on a line of its own (unless kHideKey). RML.
std::string help_with_key(const RowDesc& row);

}  // namespace ui::catalog
