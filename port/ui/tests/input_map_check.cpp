// Written for Demon's Souls. The menus' keys on their own (CMakeLists.txt's input_map_check): a pad's buttons as keys
// with the chord rule (menu_pad.hpp), each key's command for the F1 menu and its pages (menu_input.hpp), what is held,
// and the prompts' buttons for each device (prompts.hpp). No window, no pads: frames of button states in, keys out.
// Prints a line per failure and "input_map_check: N checks, M failed"; exit 1 on a failure.
#include "button_pages.hpp"
#include "input.hpp"
#include "menu_input.hpp"
#include "menu_pad.hpp"
#include "prompts.hpp"

#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

using namespace ui::input;
using namespace ui::input::cell;
using ui::menu::Command;
using Rml::Input::KeyIdentifier;

int g_checks = 0, g_fail = 0;
std::string g_where;

void check(bool ok, const std::string& what) {
    g_checks++;
    if (ok) return;
    g_fail++;
    std::printf("FAIL %s: %s\n", g_where.c_str(), what.c_str());
}

// Frames of buttons through pad_keys from a fresh state: the keys of each frame.
std::vector<PadKeys> run(int chord, std::initializer_list<unsigned> frames, float ly = 0.f) {
    PadKeysState s;
    std::vector<PadKeys> out;
    for (const unsigned b : frames) {
        PadState pad;
        pad.buttons = b;
        pad.ly = ly;
        out.push_back(pad_keys(pad, chord, s));
    }
    return out;
}

// Which frames have the key down: "0110" for four frames.
std::string down(const std::vector<PadKeys>& frames, int key) {
    std::string s;
    for (const PadKeys& k : frames) s += k[key] ? '1' : '0';
    return s;
}

void check_keys(const std::string& where, const std::vector<PadKeys>& frames, int key, const char* want) {
    g_where = where;
    const std::string got = down(frames, key);
    check(got == want, std::string("frames ") + got + ", not " + want);
}

void check_pad() {
    // The plain buttons.
    const auto plain = run(0, {kCross, kCircle, kTriangle, kSquare, kR1, kL2, kStart, 0});
    check_keys("Cross", plain, kPadConfirm, "10000000");
    check_keys("Circle", plain, kPadCancel, "01000000");
    check_keys("Triangle", plain, kPadPreview, "00100000");
    check_keys("Square", plain, kPadCompare, "00010000");
    check_keys("R1", plain, kPadNext, "00001000");
    check_keys("L2", plain, kPadPrev, "00000100");
    check_keys("Start", plain, kPadMenu, "00000010");

    // L3 + R3 (chord 0): R3 alone hides while held; L3 joining makes the chord, which ends the hiding until R3 is up.
    const auto r3 = run(0, {kR3, kR3, kR3 | kL3, kR3, kR3, 0, kR3});
    check_keys("R3 then L3: Hide", r3, kPadHide, "1100001");
    check_keys("R3 then L3: Menu", r3, kPadMenu, "0010000");
    const auto l3 = run(0, {kL3, kL3 | kR3, kR3, 0});
    check_keys("L3 then R3: Hide", l3, kPadHide, "0000");
    check_keys("L3 then R3: Menu", l3, kPadMenu, "0100");
    check_keys("L3 alone: no Help", run(0, {kL3, 0}), kPadHelp, "00");
    // ...and Select is not in it: Help as it goes down.
    check_keys("Select, chord 0", run(0, {kSelect, kSelect, 0}), kPadHelp, "110");

    // Select + Start (chord 1): R3 hides at once, L3 or not; Select's Help is a tap as it comes up alone.
    check_keys("R3, chord 1", run(1, {kR3, kR3 | kL3, 0}), kPadHide, "110");
    check_keys("Select tap", run(1, {kSelect, kSelect, 0, 0}), kPadHelp, "0010");
    const auto chord1 = run(1, {kSelect, kSelect | kStart, kSelect, 0, 0, kSelect, 0, 0});
    check_keys("Select + Start: Menu", chord1, kPadMenu, "01000000");
    check_keys("Select + Start: no Help, then a tap", chord1, kPadHelp, "00000010");

    // The stick, with give: pressed past 0.5, let go under 0.37.
    PadKeysState s;
    std::string stick;
    for (const float y : {0.45f, 0.6f, 0.4f, 0.3f, -0.55f}) {
        PadState pad;
        pad.ly = y;
        const PadKeys k = pad_keys(pad, 0, s);
        stick += k[kPadDown] ? 'D' : k[kPadUp] ? 'U' : '-';
    }
    g_where = "stick";
    check(stick == "-DD-U", "frames " + stick + ", not -DD-U");

    g_where = "repeat";
    check(pad_key_repeats(kPadUp) && pad_key_repeats(kPadNext), "the arrows and the tabs repeat");
    check(!pad_key_repeats(kPadConfirm) && !pad_key_repeats(kPadPreview) && !pad_key_repeats(kPadHide) &&
              !pad_key_repeats(kPadHelp) && !pad_key_repeats(kPadMenu),
          "Confirm, Preview, Hide, Help and Menu don't");
}

void check_commands() {
    g_where = "commands";
    const struct {
        KeyIdentifier key;
        Command command;
    } kKeys[] = {
        {KeyIdentifier::KI_UP, Command::Up},           {KeyIdentifier::KI_DOWN, Command::Down},
        {KeyIdentifier::KI_LEFT, Command::Left},       {KeyIdentifier::KI_RIGHT, Command::Right},
        {KeyIdentifier::KI_RETURN, Command::Confirm},  {KeyIdentifier::KI_NUMPADENTER, Command::Confirm},
        {KeyIdentifier::KI_ESCAPE, Command::Back},     {KeyIdentifier::KI_F1, Command::Menu},
        {KeyIdentifier::KI_PRIOR, Command::PrevTab},   {KeyIdentifier::KI_NEXT, Command::NextTab},
        {KeyIdentifier::KI_Q, Command::PrevTab},       {KeyIdentifier::KI_E, Command::NextTab},
        {KeyIdentifier::KI_P, Command::Preview},       {KeyIdentifier::KI_H, Command::Help},
        {KeyIdentifier::KI_C, Command::Compare},       {KeyIdentifier::KI_LSHIFT, Command::Hide},
        {KeyIdentifier::KI_RSHIFT, Command::Hide},     {KeyIdentifier::KI_A, Command::None},
    };
    for (const auto& k : kKeys)
        check(ui::menu::command_for_key(k.key, false, false) == k.command, "key " + std::to_string(int(k.key)));
    check(ui::menu::command_for_key(KeyIdentifier::KI_F1, true, false) == Command::None, "Shift + F1 is nothing");

    // Typing: the letters, Shift, Left and Right are the text field's; Enter, Esc, Up, Down and the pages are not.
    for (const KeyIdentifier k : {KeyIdentifier::KI_Q, KeyIdentifier::KI_E, KeyIdentifier::KI_P, KeyIdentifier::KI_H,
                                  KeyIdentifier::KI_C, KeyIdentifier::KI_LSHIFT, KeyIdentifier::KI_LEFT,
                                  KeyIdentifier::KI_RIGHT})
        check(ui::menu::command_for_key(k, false, true) == Command::None, "typing: key " + std::to_string(int(k)));
    for (const KeyIdentifier k : {KeyIdentifier::KI_RETURN, KeyIdentifier::KI_ESCAPE, KeyIdentifier::KI_UP,
                                  KeyIdentifier::KI_DOWN, KeyIdentifier::KI_NEXT, KeyIdentifier::KI_F1})
        check(ui::menu::command_for_key(k, false, true) != Command::None, "typing keeps key " + std::to_string(int(k)));

    // Every key a pad presses (input.cpp) has the command its button stands for.
    const Command kPad[kPadKeyCount] = {Command::Up,      Command::Down,    Command::Left,    Command::Right,
                                        Command::Confirm, Command::Back,    Command::NextTab, Command::PrevTab,
                                        Command::Menu,    Command::Preview, Command::Help,    Command::Compare,
                                        Command::Hide};
    for (int k = 0; k < kPadKeyCount; k++)
        check(ui::menu::command_for_key(key_for(k), false, false) == kPad[k], "pad key " + std::to_string(k));

    check(ui::menu::held(Command::Compare) && ui::menu::held(Command::Hide), "Compare and Hide are held");
    check(!ui::menu::held(Command::Preview) && !ui::menu::held(Command::Help), "Preview and Help are not");
}

void check_holds() {
    g_where = "holds";
    ui::menu::Holds h;
    check(h.down(KeyIdentifier::KI_LSHIFT, Command::Hide), "Shift down starts Hide");
    check(!h.down(KeyIdentifier::KI_LSHIFT, Command::Hide), "the key's repeat changes nothing");
    check(!h.down(KeyIdentifier::KI_RSHIFT, Command::Hide), "the other Shift changes nothing");
    check(!h.up(KeyIdentifier::KI_LSHIFT) && h.held(Command::Hide), "one Shift up: still hiding");
    check(h.up(KeyIdentifier::KI_RSHIFT) && !h.held(Command::Hide), "the last up ends it");
    check(!h.up(KeyIdentifier::KI_RSHIFT), "a key not held changes nothing");
    check(!h.down(KeyIdentifier::KI_P, Command::Preview) && !h.held(Command::Preview), "Preview is not held");
    h.down(KeyIdentifier::KI_C, Command::Compare);
    h.down(KeyIdentifier::KI_LSHIFT, Command::Hide);
    check(h.held(Command::Compare) && h.held(Command::Hide), "both held");
    check(h.release_all() && !h.held(Command::Compare) && !h.held(Command::Hide), "release_all lets go of both");
    check(!h.release_all(), "and then has nothing to let go of");
}

void check_prompts() {
    using ui::prompts::Device;
    using ui::prompts::Key;
    g_where = "prompts";
    for (int d = 0; d <= int(Device::SteamDeck); d++)
        for (int k = 0; k < int(Key::Count); k++) {
            const ui::prompts::Glyph g = ui::prompts::glyph(Device(d), Key(k));
            check(!g.text.empty() && !g.classes.empty(),
                  "device " + std::to_string(d) + " key " + std::to_string(k) + " has a button");
        }
    check(ui::prompts::glyph(Device::PlayStation, Key::Confirm).text == "✕", "PlayStation confirms with Cross");
    check(ui::prompts::glyph(Device::Xbox, Key::Back).text == "B", "Xbox goes back with B");
    check(ui::prompts::glyph(Device::Nintendo, Key::Confirm).text == "B" &&
              ui::prompts::glyph(Device::Nintendo, Key::Back).text == "A",
          "Nintendo: B confirms and A goes back, the buttons where Cross and Circle are (SDL3's are positional)");
    check(ui::prompts::glyph(Device::Keyboard, Key::Hide).text == "Shift", "the keyboard hides with Shift");
    check(ui::prompts::glyph(Device::Xbox, Key::Help).text == "View", "Xbox's Help is View");
    check(ui::prompts::glyph(Device::SteamDeck, Key::Confirm).text == "A" &&
              ui::prompts::glyph(Device::SteamDeck, Key::Help).text == "View" &&
              ui::prompts::glyph(Device::SteamDeck, Key::NextTab).text == "R1" &&
              ui::prompts::glyph(Device::SteamDeck, Key::Hide).text == "R3",
          "the Steam Deck: A, View, R1 and R3");
    check(ui::prompts::glyph(Device(int(Device::SteamDeck) + 1), Key::Confirm).text.empty(), "no device past the last");

    // Each button's picture (button_art.hpp): a pad's name one of cellPad's buttons or the D-pad, the keyboard's keys.
    using ui::prompts::Art;
    for (int d = 0; d <= int(Device::SteamDeck); d++)
        for (int k = 0; k < int(Key::Count); k++) {
            const ui::prompts::Glyph g = ui::prompts::glyph(Device(d), Key(k));
            const std::string what = "device " + std::to_string(d) + " key " + std::to_string(k);
            check(g.art[0].is != Art::Is::None, what + " has a picture");
            for (const Art& a : g.art) {
                if (a.is == Art::Is::None) continue;
                if (Device(d) == Device::Keyboard) {
                    check(a.is == Art::Is::Key && a.code > 0, what + ": a key");
                } else if (a.is == Art::Is::Button) {
                    bool known = false;
                    for (const char* name : ui::button_pages::kPadNames) known = known || a.name == name;
                    check(known, what + ": one of cellPad's buttons");
                } else {
                    check(a.is == Art::Is::Dpad && a.code > 0 && a.code < 16, what + ": the D-pad");
                }
            }
        }
    const auto art = [](Device d, Key k, int i) { return ui::prompts::glyph(d, k).art[i]; };
    check(art(Device::Keyboard, Key::Move, 1).is == Art::Is::Key && art(Device::Keyboard, Key::Move, 1).name.empty(),
          "the keyboard's Move: two arrow keys");
    check(art(Device::Keyboard, Key::Confirm, 0).name == "Enter", "Enter's cap");
    check(art(Device::Xbox, Key::Confirm, 0).name == "cross" && art(Device::Nintendo, Key::Confirm, 0).name == "cross",
          "Confirm is Cross, on an Xbox pad (A) and a Nintendo pad (B)");
    check(art(Device::PlayStation, Key::Change, 0).is == Art::Is::Dpad && art(Device::PlayStation, Key::Change, 0).code == 10,
          "Change: the D-pad's left and right");
    const Art padRight = art(Device::Xbox, Key::Right, 0), keyRight = art(Device::Keyboard, Key::Right, 0);
    check(padRight.is == Art::Is::Dpad && padRight.code == 2 && keyRight.is == Art::Is::Key && keyRight.code == 0x27 &&
              art(Device::Keyboard, Key::Right, 1).is == Art::Is::None,
          "Right: the D-pad's right alone, the right arrow key alone");

    // The device: the Button icons row first, then what was used last (PAD_GLYPH_*).
    check(ui::prompts::resolve("xbox", 1) == Device::Xbox, "Button icons: Xbox, whatever was used");
    check(ui::prompts::resolve("keyboard", 2) == Device::Keyboard, "Button icons: keyboard");
    check(ui::prompts::resolve("auto", 1) == Device::Keyboard, "auto, the keyboard used last");
    check(ui::prompts::resolve("auto", 3) == Device::Xbox, "auto, an Xbox pad");
    check(ui::prompts::resolve("auto", 4) == Device::Nintendo, "auto, a Nintendo pad");
    check(ui::prompts::resolve("auto", 2) == Device::PlayStation, "auto, a PlayStation pad");
    check(ui::prompts::resolve("auto", 0) == Device::PlayStation, "auto, nothing yet: the game's own");
    check(ui::prompts::resolve("", 3) == Device::Xbox, "no setting: as auto");
    check(ui::prompts::resolve("steamdeck", 2) == Device::SteamDeck, "Button icons: Steam Deck, whatever was used");
    check(ui::prompts::resolve("auto", 3, true) == Device::SteamDeck, "auto on a Steam Deck: an Xbox-style pad is the Deck");
    check(ui::prompts::resolve("auto", 2, true) == Device::PlayStation && ui::prompts::resolve("auto", 4, true) == Device::Nintendo &&
              ui::prompts::resolve("auto", 1, true) == Device::Keyboard && ui::prompts::resolve("auto", 0, true) == Device::PlayStation,
          "auto on a Steam Deck: other pads and the keyboard as anywhere");
    check(ui::prompts::resolve("xbox", 3, true) == Device::Xbox, "Xbox chosen on a Steam Deck stays Xbox");
}

}  // namespace

int main() {
    check_pad();
    check_commands();
    check_holds();
    check_prompts();
    std::printf("input_map_check: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
