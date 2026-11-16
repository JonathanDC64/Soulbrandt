// Written for Demon's Souls. The menus' pictures of buttons (prompts.hpp's Art): <menu-art>, an element that draws its
// button the way the game's prompts look (button_pages.hpp's single buttons, docs/138) at the size the layout gives it,
// so it stays sharp at any UI scale. A look's sheet gives it a height, and its width follows the picture (a key with a
// word on it is wider); or the sheet hides it and shows the button's name instead.
//   <menu-art device="xbox" button="cross"/>             one of a pad's buttons (cellPad's names, button_pages::kPadNames)
//   <menu-art device="playstation" dpad="up down"/>      the D-pad, these ways lit
//   <menu-art device="keyboard" key="13" cap="Enter"/>   a key: its virtual-key code and the name on its cap
#pragma once

#include "prompts.hpp"

#include <string>

namespace ui::button_art {

// The element for one of a glyph's pictures, as `device` shows it; empty for none.
std::string rml(prompts::Device device, const prompts::Art& art);

// Registers <menu-art> with RmlUi: once it has started (host.cpp), before a document is loaded.
void install();

}  // namespace ui::button_art
