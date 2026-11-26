// Written for Demon's Souls. The start screen's offer to copy an earlier game folder's saves into a new install
// (docs/164 section 5), and the copy itself. The game reads its saves and trophies as it boots behind the start
// screen, so no copy is made in a process whose game has started: Copy starts this program again, and the new process
// copies in its launch step (des_ui.cpp), before its own game starts. The start screen it shows then says how it went.
#pragma once

#include "save_copy.hpp"

#include <filesystem>
#include <string>

namespace ui::saves {

// This install has no saves of its own yet: a player's start whose saves are in the one data folder or a portable
// install's folder (report::saves_pick), with none there. Never for a test run (setup's harness, a save root of its
// own, DES_KEEP_CWD), whose saves are its own business.
bool missing();
// Where this install keeps its saves (the folder that holds gamedata).
std::filesystem::path home();

// What the start screen asks at this start.
struct Offer {
    enum class Kind {
        None,    // nothing
        Folder,  // `folder` (game-folders.txt names it) has saves: copy them?
        Find,    // this PC ran an earlier version, from a folder the list does not name with saves: find it?
    } kind = Kind::None;
    std::filesystem::path folder;
};
// Only while missing(), and never once the player answered Never for this install (or for that folder).
Offer offer();
// Never: `folder` is not offered again (this install's home(): nothing is asked here again).
void never(const std::filesystem::path& folder);
// Copy: this program starts again with DES_SAVE_COPY_FROM=<folder>, and this one closes. False, and nothing closes,
// when the new one cannot start.
bool restart_to_copy(const std::filesystem::path& folder);

// The launch step's part (des_ui_launch, after the saves' folder is the working folder): the copy DES_SAVE_COPY_FROM
// asks for, while this install still has no saves (a later start that still has the variable copies nothing).
void copy_at_launch();
// What copy_at_launch did at this start, for the start screen to say; null when it did nothing. from: the folder.
const CopyResult* launch_copy();
const std::filesystem::path& launch_copy_from();

}  // namespace ui::saves
