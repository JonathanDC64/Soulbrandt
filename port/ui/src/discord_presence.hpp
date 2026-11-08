// Written for Demon's Souls (docs/137).
// The Discord status: while the game runs and the Discord app is open on this PC, the player's profile shows that
// they play this program, the area they are in, whether they are online, the time played, and a button to the
// project's page. Settings > Network > Discord status (DES_DISCORD) turns it off, live. Discord itself clears the
// status when the game closes (its connection ends).
#pragma once

namespace ui::discord {

// des_ui_early_init: starts the thread that talks to Discord. Not in a test run (unless DES_DISCORD is given on the
// command line), and not without an application id (kAppId, or DES_DISCORD_APP). Once.
void start();

// Each of the UI's frames (the render thread, where the settings are read): hands the row's value and the online
// setting to the thread. Cheap: it reads them once a second.
void poll();

// The area's name for a map number (mNN of the game's files); nullptr for one this program does not know.
const char* area_name(int map);

}  // namespace ui::discord
