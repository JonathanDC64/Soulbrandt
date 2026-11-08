// Written for Demon's Souls. The button icons in the game's prompts, for the controller or keyboard the player uses
// (docs/138). The game draws a prompt's buttons as characters of its menu font; the font page holding them is drawn
// again here (button_pages.hpp) and the renderer shows it in place of the game's (ps3recomp's
// rsx_texture_override.h), so every prompt changes on the next frame.
#pragma once

namespace ui::button_icons {

// Reads the player's font files on a thread of its own, then has the renderer ask about the button page.
void start();
// Each frame, on the UI thread: the device used last (cellPad), the Button icons row (DES_GLYPHS), the bindings and
// the render scale. A change draws the page again and has the renderer take it.
void poll();

}  // namespace ui::button_icons
