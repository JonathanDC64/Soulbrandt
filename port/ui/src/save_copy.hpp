// Written for Demon's Souls. An earlier game folder's saves copied into this install's saves folder (docs/164 section
// 5): the player's saves and trophies, copied and checked file by file before any copy is used. The earlier folder is
// only read, and nothing already in this install is replaced.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ui::saves {

struct CopyResult {
    bool ok = false;
    std::string why;  // not ok: what went wrong, in a line for the player and the log
    size_t files = 0;
    uintmax_t bytes = 0;
};

// The folder beside <to>'s gamedata that copy_saves fills before it moves the copies into place.
inline constexpr const char* kCopyFolder = ".save-copy-in-progress";

// Copies <from>/gamedata's saves (gamedata/dev_hdd0/home: the saves and co-op's; gamedata/trophies) into
// <to>/gamedata, in this order:
//  1. every file listed; none may be in <to>/gamedata already (nothing there is replaced);
//  2. each copied into <to>/.save-copy-in-progress (made empty first: it holds only this function's copies), and its
//     size and SHA-256 checked against the original;
//  3. each moved into its place in <to>/gamedata (a rename on the same drive).
// A failure before step 3 leaves <to>/gamedata as it was, and the copies go. One during step 3 puts the copies already
// moved back first. <from> is never written.
CopyResult copy_saves(const std::filesystem::path& from, const std::filesystem::path& to);

}  // namespace ui::saves
