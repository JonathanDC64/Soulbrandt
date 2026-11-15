// Written for Demon's Souls. The trophy set as the Trophies screens read it (trophies_window.cpp for Alpha UI,
// trophies_page.cpp for a look that draws the F1 menu's screens as its pages): the sceNpTrophy HLE's snapshot, which
// reads the set (the disc's TROPHY.TRP) and the unlock file under its lock, or what a test puts in its place.
#pragma once

#include <cstddef>
#include <functional>
#include <set>
#include <string>

// ---- What this uses of the runtime (libs/network/sceNpTrophy.h, declared here: des_ui does not take ps3recomp's
// include paths) ----
extern "C" {
typedef struct sceNpTrophySnapshotEntry {
    int trophyId;
    unsigned grade;  // SCE_NP_TROPHY_GRADE_*: 1 platinum, 2 gold, 3 silver, 4 bronze
    unsigned char hidden;
    unsigned char unlocked;
    unsigned char reserved[2];
    unsigned long long unix_time;  // 0 while locked
    char name[128];                // UTF-8
    char detail[1024];
} sceNpTrophySnapshotEntry;
typedef struct sceNpTrophySnapshot {
    char commId[16];
    char title[128];
    char detail[1024];
    unsigned count;  // entries used in trophies[], in id order
    unsigned unlocked;
    unsigned per_grade[5];  // indexed by grade
    unsigned unlocked_per_grade[5];
    int percent;
    sceNpTrophySnapshotEntry trophies[128];
} sceNpTrophySnapshot;
}
static_assert(sizeof(sceNpTrophySnapshotEntry) == 1176 && offsetof(sceNpTrophySnapshotEntry, name) == 24 &&
                  offsetof(sceNpTrophySnapshot, trophies) == 1224 &&
                  sizeof(sceNpTrophySnapshot) == 1224 + 128 * sizeof(sceNpTrophySnapshotEntry),
              "sceNpTrophySnapshot as libs/network/sceNpTrophy.h declares it");

namespace ui::trophy_set {

// The set as it is now, into out (148 KB: not the stack): whether there is one, with a trophy or more. Its count is
// at most 128.
bool read(sceNpTrophySnapshot& out);

// For the tests (ui_render_compare): read() fills out with this in place of the HLE's; an empty function puts the
// HLE's back.
void set_test_source(std::function<bool(sceNpTrophySnapshot&)> source);

// A fixed-size text field's text (it may fill the field without a terminator).
std::string text_of(const char* s, size_t max);

// When a trophy was unlocked, in local time: "18 Oct 2026, 21:04" ("" when the time can't be shown).
std::string unlock_date(unsigned long long unixTime);

// DES_TROPHY_HIDDEN=<id>,<id>,... (a test knob, unset by default): the Trophies screens treat those trophies as hidden,
// as if the set said so. Demon's Souls has no hidden trophy, so this is how a hidden row, and revealing it, can be
// seen. Nothing else sees it: not the HLE, the pop-ups or the unlock file. Read when a screen opens.
std::set<int> test_hidden();

}  // namespace ui::trophy_set
