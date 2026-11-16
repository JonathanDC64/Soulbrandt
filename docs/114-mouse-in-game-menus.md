# 114. The mouse in the game's own menus

Date: 2026-10-27. Status: experimental, off by default (Settings > Controls > Mouse in game menus (experimental)).

- While one of the game's menus is up, the mouse works in it: the title, its prompts, the profile list, character
  creation, the pause menu and its pages, the inventory and its command menu, and any other screen built the same
  way (section 2).
- The pointer is the **Large Sword of Moonlight**, as in our own menus (docs/112), or the system's arrow when
  Settings > Interface > Sword cursor is off. It shows when the mouse moves over a menu of the game, and hides on a
  key or a gamepad button, or when the menu closes. While it is hidden the system's cursor is hidden too.
- The part under the pointer gets a **soft glow**: a thin pale outline with a glow about it, fading in quickly. The
  game's own selection does not follow the pointer; it moves on a click.
- **Left click** picks the part under it and confirms it, in one click. A click on a tab only switches to it. A click
  on empty space does nothing. On PRESS START BUTTON a click anywhere is Start.
- **Right click** is Circle (back), anywhere.
- **The wheel**: one notch is one row of a list (the list scrolls as with the D-pad), one tab along a bar of tabs,
  and on a row with arrows (◀ Male ▶) it turns the value, up to the right.
- **The arrows** (◀ ▶): a click steps once, and holding the button down repeats, at the game's own rate. A value
  with arrows (the options, Gender and Class) is two halves: a click on its left half is ◀, on its right half ▶,
  on any row, so the options' values change in one click though the game draws their arrows on the selected row only.
- **Sliders** (Edit Appearance's age and gender): a click on the track moves the thumb there, and while the button is
  down the thumb follows the pointer. The wheel steps it, up to the right.
- Settings > Controls > **Mouse in game menus (experimental)** (`DES_MENU_MOUSE`, off by default) turns it on. With mouse look on,
  the mouse is the menu's while a menu of the game is up: the camera does not turn behind the pause menu.

## 1. Knobs

| Knob | Default | What it does |
|---|---|---|
| `DES_MENU_MOUSE` | `0` | `1`: the mouse in the game's menus (experimental). `0`: the game's menus take no mouse, and the buttons and wheel are the keyboard layout's. Its row is Mouse in game menus (experimental). Live. |
| `DES_MENU_MOUSE_PRESS` | `1` | Frames (and pad packets) each of the clicks' presses is held, and let go after (section 3). |
| `DES_MENU_MOUSE_WALK` | `0` | `1`: every click walks, also in the menus a click selects at once (section 3.1). For a test against the walk. |
| `DES_MENU_MOUSE_LOG` | `0` | `1`: the menus found, each time they change and once their spots come to rest after a slide, and each walk. It also lists the menus left out, with why (`under` a higher menu's spots, `beneath` a menu over it) and the menu they went for, and the pass's draws. When the mouse stops in a menu, that tells a menu over it from one not found. `2`: also the arrow pieces on screen. `3`: also every visible dialog with its pieces, each time the set changes (the census used to learn a screen). |

Tests click with the UI's `DES_UI_MOUSE` (des_ui.cpp): over a menu of the game, `x,y` moves the pointer there (back
buffer pixels), `down`/`up` are the left button, `rdown`/`rup` the right one, `wheelup`/`wheeldown` a notch. They go
through the same code as the window's messages (`game_menu.cpp`), which `PS3_NO_USER_INPUT=1` otherwise keeps from
the user's mouse.

## 2. What a menu is, to the game

Found in the census runs (`DES_MENU_MOUSE_LOG=3`, section 1); nothing here is a table of screens.

- **Dialogs.** Every menu screen is a tree of EzMenu dialogs, all drawn flat through one function, `0x00F60BD0`
  (des_hud.cpp already wraps it for the HUD). A dialog's children are at `+0x0C` (array) and `+0x10` (count), its
  rect at `+0x74` (left, top, right, bottom), its layout at `+0x84` (the space its pieces are in), and `+0x94` is
  set while it is visible. A dialog's rect is in its host's layout. The roots are in the 1280x720 menu canvas.
- **Where a dialog is drawn.** The dialogs are drawn one after another, never one inside another's draw. The place
  comes from the renderer the draw is given (`r4`): its current matrix, at `+0x30` (4x4 floats by rows, read by
  its vtable `+0x4C`, `0x00717E30`), maps the dialog's host's layout to the canvas as the draw begins. For a root
  it is the identity; for the inventory's tabs it is the inventory's offset, 78,148. It includes the host piece's
  slide: while the inventory slides in, the tabs' matrix runs from about -400 to 78 on x, though no rect in either
  dialog moves. So the canvas place of a dialog's pieces is that matrix, then the dialog's own layout-to-rect
  map. (Before, it was composed from the hosts up the chain, and a wrong host put a whole menu somewhere else.)
  Nothing else in the dialog holds its place: `+0x04` is a set of eight animation channels, empty in the menus
  seen.
- **Pieces.** A piece's name (UTF-16, at `*(*(piece+0x1C))`) is the layout file's, bit `0x40000000` of `+0x18` is
  set while it is drawn, and `+0x48` points at the dialog it hosts (or at an object whose first word points just
  past that dialog). Its rect in its dialog's layout comes from the game's own `0x00F62E68` (des_hud.cpp,
  `obj_rect`).
- **Hosts the pointer does not name.** Some dialogs hang off an object the rule above does not read (the
  inventory's tabs, in `tab_select`). Their host is the piece with a host pointer of its own and the dialog's rect,
  in a dialog drawn before it.
- **Hosts with no pointer at all.** When the inventory opens on a category other than the first (it remembers the
  last one), its pieces' `+0x48` words hold no pointer: `tab_select`'s read `0x00F301FF`, the list's `I02-02`
  `0xE0000000`. Then only the rect is left: a drawn piece with the dialog's rect is its host. Without that the tabs
  had no host, so their spots sat at the canvas's top left corner. Being a menu that was not part of the inventory,
  they also left the inventory's rows out: the sword showed over the inventory, with no glow and no clicks.
- **Which host wins.** A host always draws before what it holds, so only dialogs drawn earlier are looked at. Best
  first:
  1. a piece whose host word is the dialog;
  2. an object whose first word points just past the dialog, and whose piece has the dialog's rect;
  3. a piece with a host of its own and the dialog's rect;
  4. an object pointing just past the dialog, whatever its rect;
  5. a drawn piece with the dialog's rect, whatever its host word holds.

  The rules run twice. The first time each one takes only a piece at the dialog's place in the canvas: the piece's
  rect through its dialog's canvas map is the dialog's rect through the dialog's own matrix. The second time, for
  the dialogs still without a host, any piece. Host words go stale and the same rect is in many dialogs. Before
  the place check:
  - rule 3 gave the inventory's ◀ ▶ arrows to the pause menu's arrow guides, and a list's second row to its first;
  - rule 1 gave a row's grey-out to another row's icon;
  - rule 4 gave a pause menu that the game had made anew, after a while in play, to one of its bar's small arrow
    guides.

  The second pass is for the frames of a slide: then the host piece's rect stays put while its dialog moves, so the
  place misses.

  The host is still what makes a menu (the cursor's host piece) and what tells a menu part of another from one over
  it (**Which menus count**, below). The place no longer depends on it.

  Rule 4 alone used to win, and it named the wrong host after Edit Appearance. Going back makes a new Create
  Profile dialog where an old one was, and the stats panel on its right holds an object that still points there.
  Create Profile was then placed in the panel's layout, 777 px right and 155 down, and nothing under the pointer
  was its.
- **A menu.** Each menu with a selection has a cursor dialog hosted by a piece whose name ends in `cursor`
  (`cursor`, `list_cursor`, `tab_cursor`, `stay_cursor`). The cursor dialog takes the selected spot's rect: a
  `DsMenuDlgCursor` (vtable `0x01867F50`, 32x32 layout), a plain dialog with the same 32x32 layout (the profile
  list), or one of the spot's own size (the title's NEW GAME / LOAD GAME, 500x35). Where no piece holds the cursor
  dialog (the inventory's `list_cursor`), or a hidden one does, the spot it sits on names its menu, with the drawn
  `...cursor` piece of the spots' name or its `stay_` one. A pause menu that the game makes anew (after Use Item
  closes it) has one cursor dialog, held by the bar's `cursor` piece. With a page open that piece is hidden and
  `stay_cursor` is shown, so before this the bar had no menu in a page. The tabs of the bar drawn behind it
  (`0x01883208`, four 64 px spots from 768) then took the glow and the clicks, 31 px off the icons.
- **Spots.** The spots are the drawn pieces of the cursor's host dialog named after the host piece and their place:
  `cursor0_0`, `cursor1_0`, `list_cursor2_0`, `tab_cursor0_3`, `cursor_button0_0` (row, then column). The spot whose
  rect is the cursor dialog's is the selected one.
- **Which menus count.** The host piece is drawn only while its menu has the pad: the pause menu's bar hides
  `cursor` while a page is open and draws `stay_cursor` on that page's tab instead (a *stay* menu: its tabs turn
  the pages with L2/R2). Two more rules leave out what the player cannot use:
  - A menu with spots under a higher menu's spots is beneath it: the Maiden in Black's bar (soul, skill, spell,
    storage) is drawn in the Nexus under the pause menu's, hidden by its plate.
  - A higher menu that is not part of a lower one (neither's dialog holds the other's) has the pad: a prompt over
    the profile list, the inventory's command menu over the list. The inventory's tabs are part of the inventory,
    so both count. A stay menu always counts.
- **Arrows.** Drawn pieces named `arrow_left`, `arrow_right` (`arrow_up`, `arrow_down`) in a dialog a menu's dialog
  holds are that menu's arrows: the pause menu's bar, the inventory's categories, Gender and Class in character
  creation. Their spots are made at least 28x32 (the pieces are a few pixels wide). Arrows parked above the screen
  (the pause menu's `guide` arrows) are left out.
- **Values with arrows.** A value and its arrows sit in a piece named `arrow<n>_<m>` (Options' `arrow0_0` to
  `arrow10_0`, Create Profile's Gender and Class, Edit Appearance's Origin, Hairstyle, Hair Color). Its numbers are
  not the row's (Edit Appearance's are 0_0, 0_1, 0_2 on rows 0, 5, 6), so it goes by place, as the arrows do. Its
  spot is a value field: its left half the ◀, its right half the ▶. The Options page draws `arrow_left` and
  `arrow_right` on the selected row only, so without the fields only that row's value could be clicked.
- **Sliders.** A dialog with a drawn track piece `move` (wider than it is tall: a list's scroll bar is not one) and a
  thumb `slider` on it, framed by `edge`, is a slider of the menu whose dialog holds it (Edit Appearance's
  `T03_slider00` and `T03_slider01`, class `0x01868418`). Its spot is the frame; the thumb's middle is where its
  value is. Like the arrows, it belongs to the grid spot on its row.
- **The title.** PRESS START BUTTON has no cursor: while the title dialog (vtable `0x0186A968`) draws
  `text_press_start_button` and no menu is up, that line is the one spot, and a click anywhere is Start.

The model is made once a pass over the menus, from the pass before's draws, on the game's thread, and only when a
visible dialog has a piece named `...cursor` (kept per dialog by its list of pieces, so a pass with no menu costs two
reads a dialog). A pass is a frame's draws: it begins with the first draw after the game's thread has read the pad.
Where it reads none (a loading screen), a pass ends when a dialog is drawn again: each is drawn once a pass.

**Where a frame begins.** Which menu is on top goes by the draw order, so a pass must begin where the game's frame
does. Each frame the game reads the pad once (cellPad's title hook, `des_mm_pad_buttons`, on the same thread), then
draws its own layers (0x00192310: the pause menu, the inventory, System) and then the system's (0x0018FEB0: the
yes/no prompts, the autosave icon) over them. Each layer is a list drawn by 0x00F7F3E0 from 0x0071CA28. Cut where a
dialog is drawn again, a pass began at the oldest dialog still drawn: the system's first layer, made at boot. Its
prompts then came first, beneath the menus they are drawn over. They were left out, as spots under a higher
menu's, and a click on OK went to the row beneath: Load Profile's "Load profile and exit current game?", Quit
Game's prompt, the inventory's Auto Sort and Drop. Measured at `DES_FPS=unlocked` in the inventory with the Auto Sort
prompt up:
- The draws' one long gap, about 15 ms (the rest of the frame), came between the system's last layer and the game's
  first.
- The gap before the old pass's first draw was 0.1 to 0.5 ms.
- The pad was read once a pass, in that long gap.

A pass is not a flip. The first version ended a frame when the flip count changed, and the flip is counted on
another thread (the flip request). At 30 fps that fell between passes. Unlocked (hundreds of frames a second) it
fell in the middle of one, about 2 frames in 5 on the title's screens: a dialog showed up twice in a frame, or the
top menu was missing. Measured over 500 frames at a time with `DES_FPS=unlocked`, the number of menus changed 200 to
280 times on the title's screens, against 0 to 3 times (real changes only) by passes. The menus coming and going was
behind four reports:
- The pointer flickered between the system's and the sword.
- The pointer and the glow went away while the mouse stood still. A frame without the menu hid them until the next
  move.
- With mouse look on, the cursor was pulled to the middle of the window while the pause menu was up. Each frame
  without a menu gave the mouse back to mouse look, which holds the cursor there.
- Two menus fought over the glow: a pass without the top menu kept the one under it.

If no pass is drawn for 30 flips, the menu counts as gone (`des_menu_mouse_open`): nothing is left up after the game
stops drawing its menus.

## 3. A click: walking the game's cursor

A click on a spot walks the game's own cursor there with the pad's presses, then presses Cross. Pressing is all it
does, so every screen keeps its own rules, sounds and help text. In the menus of section 3.1 the spot is selected at
once instead, and the walk only presses Cross.

- The presses go in through cellPad's title hook (`des_mm_pad_buttons`, beside DES_AUTO_CONTINUE's in
  des_options.cpp). Each is held for `DES_MENU_MOUSE_PRESS` frames *and* as many pad packets the game took
  (`g_pad0_reads - g_pad0_empty_reads`), then let go for as long: one press each, at any frame rate. The default,
  one frame down and one up, is the fastest the game counts: a row every 2 frames (66 ms at 30 fps, a few ms
  unlocked). A walk of 10 rows on the Options page took one round at 30 fps. (It was 2 frames each, a row every 4.)
- A walk is the game's cursor stepping, row by row, with the game's tick each time.
- A walk goes in rounds. A round presses down or up as many times as the rows differ, then the column buttons as
  many times as the columns differ, then waits until the cursor has stayed on one spot for 3 frames. Then it looks
  again. It ends on the spot (Cross, unless it is a tab or a stay menu), after 8 rounds, or when nothing moves the
  cursor.
- The pause menu's bar with a page open (a stay menu) turns one page a round. While a page turns the game takes no
  page button: it takes the next about 0.2 game seconds after the last turn unlocked, and up to 0.37 s at 30 fps.
  A round of several presses turned one page (two at 30 fps), and the walk took the presses that went unheard for a
  button that does not work there. So a click three tabs away turned one page, at most two, and stopped (issue
  #582). Now a press that turned no page is pressed again while less than 1 game second has
  passed since the page last turned or opened; after that it counts as not working, as in any menu. A page that
  takes no page button at all (the inventory's command menu is up) gets one press of each pair, as before.
- What each menu's buttons do is learnt as it walks, by the owner's class and the host piece's name: right and left,
  or R1 and L1, or R2 and L2 for the columns (tried in that order when one does not move the cursor), and whether
  the spots' rows run across (down and up change the second number). The pause menu's stay bar starts with R2/L2.
- An arrow (or a value field's half) on the cursor's row presses its button, held while the mouse button is down:
  the game repeats it as it does a held D-pad. One on another row (Class ▶ while the cursor is on Name) walks there
  first, then presses.
- A slider: walked to its row first if the cursor is on another, then its thumb is slid to the pointer with the
  column buttons. Far from it the button is held (the game repeats it), and let go a step short; near it, one press
  at a time, the first teaching how far a press moves it (6 px on the face's sliders). While the mouse button is down
  the pointer's moves (`des_menu_mouse_drag`) move the target. It stops where a press no longer moves the thumb (the
  track's end). If holding moves it only once, it goes on one press at a time.
- The wheel: down and up in a list; along a bar of tabs (down is the next); on a row with arrows or a slider, the
  column buttons (up is the right one), walking to that row first if need be (notches during that walk add up).
  Over no spot it goes to the top menu that is not tabs (the inventory's list, not its categories).
- Right click is Circle, and drops a walk under way.

### 3.1 Selecting at once

The game has no one "select item N": each menu class keeps its selection in its own place, and its input code
moves it, plays the tick (`0x0072D970(0)`) and places the cursor itself. Many keep it in a table of menu
variables, not in the dialog (`0x00720F20` reads one, `0x00720F50` writes one). So a memory diff of the dialogs
found the pause menu's bar at +0x184, which is only the tab it placed last: writing it does nothing.

A click in one of these menus selects the spot as that class's input code would, with one tick
(`select_now`), but only while the menu's dialog takes input (bit 31 of its +0xA0, which the game's input query
0x00F55A18 asks). The walk then waits for the cursor to show there and presses Cross. If it does not show within 10
passes, the walk walks. Until it shows, the mouse's next events wait, so a click right after one reads the cursor
where it is then.
- **The pause menu's bar, while it has the pad** (DsMenuDlgStartTab, vtable 0x018692C0): its tab is menu variable
  0x4B. Its step (0x0079B2C0) sets it, then runs its vt+0x80 (the help line), +0x90 and +0x88 (the cursor). The
  bytes +0x180, +0x181 and +0x182 have its next update run the same three. The step goes round 0 to 4 and skips no
  tab.
- **The bar with a page open** (the same class, a stay menu): a click on another tab goes to that page at once.
  - The pages are menu variables too. Inventory is 2, Equipment 3, Status 0x15, World Tendency 0x16 and System 0x2A
    (Options 0x13). A page opens when its variable is set, and closes when it is set back.
  - The game's page switch (`0x007234E0`, with the page's variable: 2, 3, 0x15, 0x16 or 0x13) sets the bar's tab,
    plays the tick and opens the page. But it closes only that page's two neighbours, the only pages R2 and L2 come
    from. So a jump also closes the page it leaves with that page's own close: `0x0081FA40(0)`, `0x00736BB0(0)`,
    `0x0082A440`, `0x00841A90`, and for System `0x007C8260` then `0x008ABD18(0)`. Without it, from World Tendency to
    the inventory, World Tendency's statues stayed drawn over the inventory.
  - Only while the page left would take R2 and L2:
    - its variable is at its idle value: 2 for the inventory, 1 for the others, 3 for System. The first is 1 while
      the inventory opens, and 3 while Status or World Tendency open, for 0.16 to 0.24 game seconds.
    - each menu shown besides the bar is a part of the page: the inventory's list (DsMenuDlgInventory, 0x0186C318)
      and categories (0x01866570), the equipment's slots (0x01866068), System. Those of the page also have the pad.
      So the command menu, a prompt, an equipment slot's item list, Options and Status's Stat Guide keep the walk.
  - The switch refuses the inventory while its variable is not 0 (and Status likewise); then the walk walks.
- **System** (DsMenuDlgSystem, vtable 0x0186EF60): its row is +0xFC (0 to 2), and its update places the cursor and
  the help line from it every frame. Its input keeps to row 0 while +0xF5 is set, and steps over row 1 (Load
  Profile) while +0xF7 is. A click on a row it would not go to walks instead.
- **Lists** (DsMenuDlgListbox and the classes built on it: the inventory, an equipment slot's items, Create
  Profile's rows, ...). A list is
  told by its vt+0x8C, the list's own cursor placement (0x007814F0), so a class that keeps it counts, with no
  table of screens.
  - Its fields: +0x100 is the row in its window of +0xF4 rows, showing items from +0x108 of +0x104.
  - Its step (0x00780E80) sets the row and the byte +0x111, then calls vt+0x84(this, up, moved), the class's "row
    moved" (the inventory's sets its refresh flag, the cursor's move animation and the tick). The update then places
    the cursor and takes the item at +0x108 plus the row (menu variable 0x72).
  - A click on a row of the window that holds an item needs no scrolling, so it does the same. A row past the last
    item walks (and stops, as before). A list shown as a grid of icons (spots with a column) walks.
- **Button groups** (DsMenuDlgButtonGroup and the classes that keep its vt+0x88, the hook before a step, or its
  input at vt+0x8C, 0x00777C90: the inventory's command menu, the equipment's slots (DsMenuDlgEquip), ...).
  - Its fields: the row is +0xF4 and the column +0xF8, of +0xFC rows by +0x100 columns.
  - Its step (0x00777A18) skips spots with no `cursor<r>_<c>` piece (a clicked spot always has one; a greyed
    button such as Drop has one too, as with the pad), sets the row, and calls vt+0x84(this, moved). That is the
    class's "moved": the command menu's and the equipment's play the cursor's move animation and the tick, the
    plain group's does nothing.
  - Its update places the cursor only after a step, so 0x00776F68 places it. The command menu's update also sets
    its help line from the row every frame, and the equipment's shows the slot of the row and column (its grid is 2
    by 9, with no slot at row 1, column 6).
  - The equipment's own hook (0x007317C0) changes only a step from row 6, column 0, which its grid does not have.
- **Options** (DsMenuDlgOption, vtable 0x0186A398).
  - Its row is menu variable 0x51 (0 to 12; OK is 11, DEFAULT 12, drawn as row 11's column 1).
  - Its step up (0x007CC598(this, _, from, wrap)) sets it to the row above `from`, with the help line for that row,
    and redraws every row's arrows. Called from the row below the clicked one, it selects that one; 0x007CA320 then
    places the cursor.
  - DEFAULT is a step right of OK and is walked.
- **The profile list** (DsMenuDlgSelectProfile, vtable 0x0186BE58: LOAD GAME's, and System's Load Profile): its row
  is +0xF8 (0 to 3). Its step goes one row and skips none (empty slots too), and its update places the cursor from
  it every frame.
- **The prompts** (vtable 0x01869FB8: the yes/no of Auto Sort, Quit Game, Create Profile, ..., and the notices with
  one OK). The button is +0x110, counted from 1 (button `c` is `c + 1`).
  - Its steps (vt+0x8C right, +0x90 left) move it by one, with the tick. Its update places the cursor from it every
    frame while the prompt is open (+0xF8 is 0).
  - Its kind is menu variable 0x11 (`0x00720F20` reads it). Kinds 0 and 1 take no left or right, and with 6 and 9
    the update places nothing, so those walk.
  - Only when +0x110 matches the spot the cursor is on: any other numbering walks.
- A click on an arrow or a value's half on another row (section 3) goes to that row the same way first, then
  presses the arrow: an Options value changes in one click with no walk.

Every other menu still walks. Among them: the title's NEW GAME / LOAD GAME (one step; the menu the port sees there
is the cursor's dialog, not the title's), Options' DEFAULT, and lists shown as a grid of icons.

## 4. The UI's side (port/ui/src/game_menu.cpp)

- Each presented frame `game_menu::poll` asks the game's side whether a menu is up (`des_menu_mouse_open`). While
  one is and the pointer shows, the UI runs every frame (no fast path) to draw the sword and the glow. A menu counts
  as closed only once the game's side has said so for 4 presented frames running. So one frame without it neither
  hides the pointer nor gives the mouse back to mouse look.
- **Pointer to canvas.** The window's position goes to back buffer pixels (des_ui.cpp, `map_mouse`), then through
  the picture's rect (hud_editor.hpp, `picture_rect_of`: RSX_ASPECT's shape, in the middle) to the canvas the
  picture shows (`des_menu_mouse_view`: 1280x720, wider by the HUD's side width on a wider picture).
- **Mouse ownership.** mouse.cpp and mouse_sdl.cpp let the mouse go while a menu of the game is open ("a menu of the
  game is open"): no mouse look, and the buttons and wheel do not go to the keyboard layout (left click is not R1
  in a menu). des_ui.cpp's window procedure sends the mouse messages to `game_menu` instead (while no document of the
  UI is up). On Windows, mouse.cpp asks for WM_MOUSELEAVE, so the sword goes when the cursor leaves the window.
- **A lost button up.** An arrow held down holds the game's button, and every press after it waits. If the left
  button's up never reaches the menu (let go over another window, or while the menu was not taking the mouse),
  `game_menu::frame` sees the button up and lets go itself. It does so only once it has seen the button down
  through `ui::keys::key_down`: a platform that does not report the mouse's buttons there leaves it to the messages.
- **The glow** is an element of the overlay document (`game-glow` in overlay.rcss), placed over the spot's box each
  frame (`game_menu::glow`) and faded with its `open` attribute.
- **The sword** is host.cpp's, at the pointer (`set_game_pointer`); the lizard stays off over the game's menus.

## 5. Screens tried

On the pinned regression save, by `DES_UI_MOUSE` (the steps and the shots are in the session's scratch, not here):

| Screen | Tried |
|---|---|
| PRESS START BUTTON | glow, click anywhere is Start |
| Title menu (NEW GAME / LOAD GAME) | glow, click walks up and confirms |
| "Cannot connect to the server" and "Start the game in Offline Mode" | glow on OK, click |
| Profile list | glow, wheel down and up, click loads the profile; at LOAD GAME and in System's Load Profile, a click on an empty row selects it at once (Cross on it does nothing) |
| Create Profile | a click on Class's label selects it at once (with its help line); a click on Gender's right half selects Gender at once and steps it, and the game asks its gender question (as it does after a walk); click on Class ▶ steps it; wheel over Gender; the gender prompt's OK / CANCEL; a click on the profile under the prompt does nothing |
| Edit Appearance | glow on a slider; a click on Age's track from the Origin row walks down 3 and slides the thumb there; a drag moves it back; 3 wheel notches over Gender walk there and step it 3 times |
| Pause menu bar | glow on a tab, click opens its page, ◀ ▶ click and hold, wheel; no glow in the gaps between tabs |
| Pause menu pages | with a page open, a click on another tab goes to its page at once, any distance (from World Tendency to the inventory with no statues left, from the equipment to System); from Options and with Status's Stat Guide open it walks (L2), and with the command menu open nothing turns, as with the pad; two clicks in one step: the second waits for the first page, then walks one page; right click closes the page; the same after Use Item, when the game has made the pause menu anew (in a level, from a copy of a player's save) |
| Inventory | glow on a row (its whole width), wheel, click opens the command menu, right click closes it, click on a category; closed on the armor category and opened again (it opens there), glow and clicks on its rows and tabs |
| Equipment | a click on a slot selects it at once and opens its item list (from the first slot: a bolt slot, then an item slot; the same lists as in a walk-only run); glow on a row, wheel, right click back |
| System | a click on the pause bar's last tab selects it at once and opens System; a click on Options opens it; a click on Quit Game selects it at once |
| Yes/no prompts in play | Auto Sort's "Automatically resort all items?": glow on OK, OK (from CANCEL) is selected at once and sorts; Quit Game's: CANCEL, and OK (selected at once) goes to the title; Load Profile's "Load profile and exit current game?": CANCEL |
| Options | glow on a value; a click on Camera Speed's right half goes there at once and steps it 5 to 6, its left half back to 5; 2 wheel notches over Music walk there and step it 10 to 8; a click on a label selects its row at once, and only that; right click brings the game's "Cancel option changes and return?" |

Drop's "Please select the number of items to drop." with its ▲ ▼ arrows was tried by hand in play: the
arrows step the number. Not tried yet: the shops, the blacksmith, storage and the Maiden in Black's level up
(they need the Nexus's characters). They are built the same way, so they should work; the census
(`DES_MENU_MOUSE_LOG=3`) shows what a screen has if one does not.
