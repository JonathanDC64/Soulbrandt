// Written for Demon's Souls. The mods folder as the Mods screens show it (mods_window.cpp for Alpha UI, mods_page.cpp
// for a look that draws the F1 menu's screens as its pages): the mods as read, in their order, and what turning one
// on or off, moving it or fixing it does. Every change goes to the mods folder's order file at once (mods.hpp).
#pragma once

#include "mods.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace ui {

struct ModsState {
    std::filesystem::path dir;           // the mods folder; empty when this run keeps no data folder
    std::vector<mods::Mod> all;          // as read, in its order (the order file's), on or off
    std::vector<mods::Overlap> overlaps;  // among the mods that are on
    unsigned generation = 1;             // moves with every change the screens show
    bool lookAgain = false;              // read the folder again (the screen's update, not inside a press)

    // The mods were given when the game started (a test run): they can't be changed here.
    static bool fixed();
    // Whether this session runs with the mod of that name.
    static bool in_use(const std::string& name);

    void load();
    // The order file, as `all` says now; a note over the game when it can't be written.
    void save();
    void refresh_overlaps();

    std::size_t usable() const;
    std::size_t on() const;
    // The usable mod next to all[i], above (step -1) or below (+1); all.size() when there is none.
    std::size_t neighbour(std::size_t i, int step) const;
    // What the list says differs from what this session runs with (which mods, or their order): a restart applies it.
    bool restart_needed() const;
    // all[i] is on, and this session doesn't run with it, or the other way round.
    bool pending(std::size_t i) const;

    // all[i] moves past the next usable mod above (-1) or below (+1): where it went, or all.size() when it can't (the
    // mods are fixed, or it is at that end already). Saves.
    std::size_t move(std::size_t i, int step);
    // all[i] on or off: whether it changed (not when the mods are fixed). Saves.
    bool toggle(std::size_t i);
    // all[i]'s loose files put in place (Problem::NotInPlace): "" when done, else why not. The folder is read again
    // after (lookAgain), and a note over the game says how it went.
    std::string fix(std::size_t i);

    // "3 of 4 mods on", "No mods yet"...
    std::string summary_line() const;
    // What all[i]'s note says when another mod on gives some of its files ("" when none does).
    std::string covered_by(std::size_t i) const;
};

// What the Mods screens tell a player new to mods, in "Good to know". Moving a mod names the buttons of the device the
// menus show (input::menu_device): L1 and R1, LB and RB, or Page Up and Page Down.
std::vector<std::string> mods_good_to_know();

}  // namespace ui
