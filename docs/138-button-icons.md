# 138. Button icons for the controller or keyboard in use

Date: 2026-11-07. Status: first version, checked in the game on the default renderer (below). The study is docs/125.

## What it does

The game's prompts ("Select", "Enter", "Two-hand", the tutorial's lines) and the HUD's item slot show the PS3's
buttons. They now show the buttons of the controller the player used last, or the keys they bound:

- **Automatic** (default): a press on a pad, or a stick or trigger pushed past half way, shows that pad's buttons.
  A key or mouse button shows the keyboard's. Stick drift, a resting trigger and mouse movement change nothing.
- **PlayStation**: the game's own icons. A button moved in Controls shows the game's own picture of the pad button
  that does it now.
- **Xbox**: A B X Y in their usual colours, LB RB LT RT, LS RS, View and Menu.
- **Nintendo**: the same letters in grey (SDL names a Nintendo pad's buttons by their letters, so A is Cross), L R
  ZL ZR, Minus and Plus.
- **Keyboard and mouse**: the keys set in Settings > Controls (E, Space, Esc...), the mouse with the button in use
  lit, arrow keys, WASD for moving and the mouse for the camera. A chord (Shift + left button) shows a small Shift key
  on the mouse.

Every pad's icons follow PAD_MAP: if Triangle is mapped to the pad's A, the prompt for Triangle shows A, and on a
PlayStation pad with Circle and Cross swapped, the prompt for Circle shows ×. The row:
**Settings > Game > Button icons** (DES_GLYPHS), live.

## How it works

1. **The game's text.** A prompt is text: `<?conclusion?>` becomes the character ④ of the menu font, and ④'s cell
   on the font page holds the picture of Cross (docs/125). Every button the game shows comes from one 512x512 page
   of that font.
2. **The page, from the player's files.** At start, `button_icons.cpp` reads `font/*/dsfont24.ccm` and `.tpf` under
   the game's USRDIR on a thread of its own. The code map says where each button character's cell is and on which
   page. The texture pack holds the page's bytes. Nothing of the game's is in the repository.
3. **The swap.** ps3recomp's renderer asks the port about each texture of the page's size and format when it uploads
   it (`rsx_texture_override.h`). One whose bytes are the page's gets the port's picture instead: RGBA, at the render
   scale (RSX_SCALE, up to 4x), with its smaller levels. The game keeps drawing ④; the picture in ④'s cell changed.
   Text already on screen changes on the next frame.
4. **The device.** cellPad records the last device used on port 0 and its kind (`cellPad_host_glyph_device`,
   `pad_glyph.h`): XInput is Xbox; DualShock 4 and DualSense over HID are PlayStation; SDL gives the kind (PS3, PS4,
   PS5, Switch Pro, Joy-Con, else Xbox). The port checks it each frame and the settings twice a second. A change has
   the renderer ask again.
5. **The sprites.** Three button pictures are not text: the HUD's item slot (□), its magic slot (□) and the message
   window's ○. They come from two textures of `menu/menu.tpf`, Menu08 and Menu00, which the port reads the same way
   (by name and size; the cells are where the menu's layout puts them, the same on the US and European discs). They
   are drawn at no more than 2x, since the pages are large. The item slot's picture is a symbol on a small dark
   square: the square stays, and the device's symbol is drawn bare on it, as the game draws its own.
6. **Drawing.** `button_pages.cpp` draws each cell from shapes and the UI's font (Alegreya Sans Bold), in the game's
   style, sampled from its own page: a dark glossy button with a rim lit from above and a soft highlight, light grey
   letters on a soft shadow, yellow arrows. Drawing a page takes tens of milliseconds, on that thread; the last page
   shows until the new one is ready, and six are kept.
7. **PlayStation with buttons moved.** With no button moved, nothing is drawn and the game's page shows. Once
   PAD_MAP moves one, each moved button's cell gets the game's own picture of the pad button that now does it, at the
   game's size: a cell the same size first (the font's □ for the font's △; Menu00's spare △ and × beside its □ and ○,
   which the game never shows, for those two). The item slot's square takes the symbol alone, without the disc under
   it, from the largest picture of that button, in its own colour, on the square's own backing.

## The art: drawn, not generated

The ask was art that matches the game's PS3 icons, maybe made with a free image model from the existing icons. The
best free models with a licence that allows shipping the output are **Qwen-Image-Edit-2511** (Apache 2.0) and
**FLUX.2 [klein] 4B** (Apache 2.0, about 13 GB of video memory). FLUX.2 [dev]'s licence does not allow commercial use.

Drawing them in code instead, because:

- **Licence.** Pictures made from the game's icons (image-to-image) are derived from the game's art. The project
  cannot ship those, whatever model makes them.
- **Letters.** Image models still get small letters wrong, and a 26-pixel cell is all letters: A, RB, ZL, Esc.
- **Bindings.** The keyboard's icons are whatever the player binds (any key, chords, mouse buttons). They have to be
  drawn when the bindings change, on the player's PC, with no model there.
- **Size.** Drawn shapes are sharp at any render scale.

No model, download or new dependency is needed. The look comes from colours measured on the game's own page. A model
could still help design a future icon, but its output would need redrawing as shapes.

## Limits and next steps

- **Wide key names.** A key's name is drawn inside a square cap, so "Space" is small. Wider cells would mean changing
  the font's glyph table in the game's memory (docs/125).
- **Menu bindings.** The keyboard page shows each button's first binding outside menus; `menu:` bindings are not
  shown yet.
- **Kept as the game draws them:**
  - the HUD's "L" and "R" over the hands, which name the hands, not buttons;
  - the embossed L2 and R2 of one key guide (`menu.tpf`'s Menu00).
- **Three lines name buttons in words**: "Right Stick: Change locked target" and two more. They are unchanged.
- **The legacy renderer** (RSX_BACKEND=legacy) does not ask, so it keeps the game's icons.

## Knobs

- DES_GLYPHS: auto (default), playstation, xbox, nintendo, keyboard. Read live from the environment, then
  settings.ini.
- PAD_GLYPH_LOG=1 (ps3recomp): log each change of the device.

## Checks

- ps3recomp `test_pad_glyph`: the rules for the last device used (presses, drift, triggers, ties, a pad unplugged).
- ps3recomp `test_rsx_texture_override`: the table of hooks (shapes, replacing, removing, a full table).
- `port/ui/tests/button_icons_check.cpp` (target `button_icons_check`): a made-up code map and texture pack read, the
  page decoded and drawn for each device at 1x, 2x and 4x, every cell drawn, the rest of the page kept. Then a made-up
  menu pack: its two sprite pages found by name and size (not a third texture), drawn at 2x at most, and the item
  slot's backing kept with the symbol drawn on it. PlayStation with buttons swapped: no page when no button is moved;
  the swapped cells take each other's pictures pixel for pixel and the others stay; Menu00's □ takes its spare Cross;
  the item slot's square takes Cross's symbol alone on its backing. With `--ccm` and `--tpf` it reads the player's own
  font files, and
  `--png <folder>` writes the pages to look at. Pages drawn from the player's files hold the game's art: never commit
  them.
- In the game (11-08), on the default renderer at RSX_SCALE=200, with DES_GLYPHS set live to each value:
  - the inventory's key guide showed Xbox's green A and red B, the keyboard's E and Space, Nintendo's grey A and B,
    and the game's own for PlayStation;
  - the HUD's item slot showed Xbox's blue X, the keyboard's R key, Nintendo's grey X, and the game's □.
  
  The font page and both menu pages were each found the first time they were used. The message window's ○ is on a
  page that was swapped, but no message was open in a shot. Shots with the game's art in them stay local.
