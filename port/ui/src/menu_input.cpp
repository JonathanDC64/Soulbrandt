// Written for Demon's Souls. The keys as the F1 menu and its pages read them (menu_input.hpp).
#include "menu_input.hpp"

#include <algorithm>

namespace ui::menu {

using Rml::Input::KeyIdentifier;

Command command_for_key(KeyIdentifier key, bool shift, bool typing) noexcept {
    switch (key) {
    case KeyIdentifier::KI_UP: return Command::Up;
    case KeyIdentifier::KI_DOWN: return Command::Down;
    case KeyIdentifier::KI_RETURN:
    case KeyIdentifier::KI_NUMPADENTER: return Command::Confirm;
    case KeyIdentifier::KI_ESCAPE: return Command::Back;
    case KeyIdentifier::KI_F1: return shift ? Command::None : Command::Menu;  // (as map_nav_event has it)
    case KeyIdentifier::KI_PRIOR: return Command::PrevTab;
    case KeyIdentifier::KI_NEXT: return Command::NextTab;
    default: break;
    }
    if (typing) return Command::None;  // (Left and Right move the text cursor)
    switch (key) {
    case KeyIdentifier::KI_LEFT: return Command::Left;
    case KeyIdentifier::KI_RIGHT: return Command::Right;
    case KeyIdentifier::KI_Q: return Command::PrevTab;
    case KeyIdentifier::KI_E: return Command::NextTab;
    case KeyIdentifier::KI_P: return Command::Preview;
    case KeyIdentifier::KI_H: return Command::Help;
    case KeyIdentifier::KI_C: return Command::Compare;
    case KeyIdentifier::KI_LSHIFT:
    case KeyIdentifier::KI_RSHIFT: return Command::Hide;
    default: return Command::None;
    }
}

bool Holds::down(KeyIdentifier key, Command c) {
    if (!menu::held(c)) return false;
    const bool was = held(c);
    if (std::none_of(mHeld.begin(), mHeld.end(), [&](const Held& h) { return h.key == key; })) mHeld.push_back({key, c});
    return !was;
}

bool Holds::up(KeyIdentifier key) {
    const auto it = std::find_if(mHeld.begin(), mHeld.end(), [&](const Held& h) { return h.key == key; });
    if (it == mHeld.end()) return false;
    const Command c = it->command;
    mHeld.erase(it);
    return !held(c);
}

bool Holds::held(Command c) const noexcept {
    return std::any_of(mHeld.begin(), mHeld.end(), [&](const Held& h) { return h.command == c; });
}

bool Holds::release_all() noexcept {
    const bool any = !mHeld.empty();
    mHeld.clear();
    return any;
}

}  // namespace ui::menu
