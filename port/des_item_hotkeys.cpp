/* The Keyboard page's Item slot 1 to 5 (issue #410): a key for each of the five item slots, which switches straight
 * to the item in that slot, as Down would reach it one slot at a time, and does not use it. 1 to 5 by default
 * (settings.cpp's layout); keys_for_pad passes them as DES_ITEM_KEYS, the five slots' bindings separated by commas.
 *
 * The game's side (US addresses):
 * - The HUD's four equipment slots are dialogs of one class (vtable 0x018697B0; des_hud.cpp). Each frame its update
 *   (vtable +0x44, 0x007ADB80, which nothing calls but the table) calls the D-pad step 0x007AB920. The step does
 *   nothing while a menu is up: 0x00720F80's bits (less 0x80), menu variable 0x79, the talk menu's state (the menus'
 *   +0x38) at 1, or menu variable 0xAD or 0x34. Then the dialog's +0xF4 says which slot it shows (1: the items),
 *   and on a press of that direction (0x00F55A18(dlg, 0x81), which first needs the dialog to take input: bit 31 of
 *   +0xA0) the items' step 0x00902A68(PGD + 0x2A0) moves the slot in use (+0x28) to the next filled one (the five
 *   entries from +0x0C, -1 empty), +0xFC = 1 starts the HUD's switch animation (0x007AA998 shows it and clears it)
 *   and 0x0072D970(0) plays the menu tick.
 * - So 0x007ADB80 is wrapped. On the items' dialog, a key's press that frame, with the step's own tests, sets +0x28
 *   to its slot at once and then does what the step does after it; the update then runs as before. An empty slot,
 *   or the one in use, does nothing. The game reads the slot in use as it reads one Down reached: Square uses that
 *   item.
 *
 * The keys count while the keyboard reaches the game (cellPad_host_keys_live: the window in front, no menu of the
 * port's open, the keys not kept out of a pad). In a scripted run (PS3_NO_USER_INPUT) DES_ITEM_KEY_TEST=<1-5>
 * stands for a press of that slot's key, once for each new value. DES_ITEM_KEYS_LOG=1 prints a line a press. */

#include "ps3emu/ppu_lift.h"
#include "des_region.h"      /* DES_AT: the running disc's addresses */
#include "des_game_calls.h"  /* the lifted functions below (the game DLL's with DES_SPLIT) */
#include "des_keybinds.h"
#include "ps3emu/ps3_knob.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>

#ifndef PS3_GAME_SPLIT   /* DES_SPLIT: the game DLL's (ps3emu/game_split.h) */
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
#endif
extern "C" int cellPad_host_keys_live(void);   /* cellPad.c: the keyboard's keys reach the game now */

namespace {

constexpr uint32_t kShortcutUpdate = 0x007ADB80u;  /* the equipment slots' update (their vtable +0x44) */
constexpr uint32_t kShortcutVt = 0x018697B0u;
constexpr uint32_t kDlgShows = 0xF4u;              /* the slot it shows: */
constexpr int32_t kShowsItems = 1;                 /*   the items */
constexpr uint32_t kDlgInput = 0xA0u;              /* bit 31: the dialog takes input */
constexpr uint32_t kDlgItemSwitched = 0xFCu;       /* starts the HUD's switch animation */

constexpr uint32_t kToc = 0x019B5040u;
constexpr uint32_t kPgdRoot = 0x01B4EF9Cu;         /* PGD = *(*this + 8) */
constexpr uint32_t kPgdItems = 0x2A0u;             /* the item slots: */
constexpr uint32_t kItemEntries = 0x0Cu;           /*   five entries, -1 empty */
constexpr uint32_t kItemInUse = 0x28u;             /*   the one in use */
constexpr int kSlots = 5;

/* The menus' object (*(*(*(TOC - 0x72F0) - 0x8000))): menu variable n at +0x14 + 4n (0x00720F20 reads it); the
 * talk menu's state at +0x38 (0x00721418 asks whether it is 1). */
constexpr uint32_t kMenusSlot = 0x019ADD50u;
constexpr uint32_t kTalkState = 0x38u;
constexpr int kVarBusy[] = {0x79, 0xAD, 0x34};
constexpr uint32_t kMenuBitsOff = 0x80u;           /* the one bit of 0x00720F80's the step lets through */

bool guest_ok(uint32_t a) { return a >= 0x00010000u && a < 0xD0000000u && !(a & 3u); }

uint32_t menus()
{
    const uint32_t t = vm_read32(DES_AT(kMenusSlot));
    const uint32_t h = guest_ok(t) ? vm_read32(t - 0x8000u) : 0;
    const uint32_t m = guest_ok(h) ? vm_read32(h) : 0;
    return guest_ok(m) ? m : 0;
}

uint32_t item_slots()
{
    const uint32_t root = vm_read32(DES_AT(kPgdRoot));
    const uint32_t pgd = guest_ok(root) ? vm_read32(root + 8) : 0;
    return guest_ok(pgd) ? pgd + kPgdItems : 0;
}

/* A game function with up to eight arguments (r3-r10), on the thread's own registers, which are put back after
 * (des_respec.cpp's call_at): returns r3. */
uint32_t call_game(ppu_context* ctx, void (*fn)(ppu_context*), std::initializer_list<int64_t> a)
{
    uint64_t g[32];
    double f[32];
    uint8_t v[32][16];
    memcpy(g, ctx->gpr, sizeof g);
    memcpy(f, ctx->fpr, sizeof f);
    memcpy(v, ctx->vr, sizeof v);
    const uint64_t lr = ctx->lr, ctr = ctx->ctr;
    const uint32_t cr = ppu_cr_pack(ctx), xer = ctx->xer;
    ctx->gpr[1] -= 0x1000;
    ctx->gpr[2] = DES_AT(kToc);
    int i = 0;
    for (int64_t x : a)
        if (i < 8) ctx->gpr[3 + i++] = (uint64_t)x;
    ctx->lr = DES_AT(kShortcutUpdate);
    fn(ctx);
    while (g_trampoline_fn) {
        void (*tf)(void*) = g_trampoline_fn;
        g_trampoline_fn = nullptr;
        tf(ctx);
    }
    const uint32_t r3 = (uint32_t)ctx->gpr[3];
    memcpy(ctx->gpr, g, sizeof g);
    memcpy(ctx->fpr, f, sizeof f);
    memcpy(ctx->vr, v, sizeof v);
    ctx->lr = lr;
    ctx->ctr = ctr;
    ppu_cr_unpack(ctx, cr);
    ctx->xer = xer;
    return r3;
}

struct ItemKeys {
    DesKeys slot[kSlots];
};

/* DES_ITEM_KEYS: the five slots' bindings, separated by commas ("1,2,3,4,5"; an empty one has none). */
ItemKeys item_keys(const char* e)
{
    ItemKeys k{};
    for (int i = 0; i < kSlots && e; i++) {
        const char* comma = std::strchr(e, ',');
        k.slot[i] = des_keys_parse(e, comma ? comma : e + std::strlen(e));
        e = comma ? comma + 1 : nullptr;
    }
    return k;
}

/* The slot (0-4) whose key went down since the last frame, or -1. */
int pressed()
{
    static ItemKeys keys{};
    static int test = 0, test_seen = 0, scripted = 0;
    static bool down[kSlots];
    PS3_KNOB(keys, item_keys(std::getenv("DES_ITEM_KEYS")));
    PS3_KNOB(scripted, ps3_env_int("PS3_NO_USER_INPUT", 0));
    if (scripted) {
        PS3_KNOB(test, ps3_env_int("DES_ITEM_KEY_TEST", 0));
        if (test == test_seen) return -1;
        test_seen = test;
        return test >= 1 && test <= kSlots ? test - 1 : -1;
    }
    const bool live = cellPad_host_keys_live() != 0;
    int out = -1;
    for (int i = 0; i < kSlots; i++) {
        const bool held = live && des_keys_held(keys.slot[i]);
        if (held && !down[i] && out < 0) out = i;
        down[i] = held;
    }
    return out;
}

/* No menu is up: the D-pad step's tests (0x007AB920), and the dialog takes input (0x00F55A18's). */
const char* busy(ppu_context* ctx, uint32_t dlg)
{
    const uint32_t m = menus();
    if (!m) return "no menus yet";
    if (call_game(ctx, func_00720F80, {}) & ~kMenuBitsOff) return "a menu is open";
    for (int var : kVarBusy)
        if (vm_read32(m + 0x14 + 4 * var)) return "a menu is open";
    if (vm_read32(m + kTalkState) == 1) return "a talk menu is open";
    if ((int32_t)vm_read32(dlg + kDlgInput) >= 0) return "the HUD takes no input now";
    return nullptr;
}

/* For 0x007ADB80, r3 = the dialog. */
void shortcut_update(ppu_context* ctx)
{
    static int log = 0;
    PS3_KNOB(log, ps3_env_int("DES_ITEM_KEYS_LOG", 0));
    const uint32_t dlg = (uint32_t)ctx->gpr[3];
    int slot = -1;
    if (guest_ok(dlg) && vm_read32(dlg) == DES_AT(kShortcutVt) && (int32_t)vm_read32(dlg + kDlgShows) == kShowsItems)
        slot = pressed();
    if (slot >= 0) {
        const uint32_t items = item_slots();
        const char* no = busy(ctx, dlg);
        const int32_t was = items ? (int32_t)vm_read32(items + kItemInUse) : -1;
        if (!no && !items) no = "no character yet";
        if (!no && (int32_t)vm_read32(items + kItemEntries + 4 * slot) == -1) no = "the slot is empty";
        if (!no && was == slot) no = "already in use";
        if (!no) {
            vm_write32(items + kItemInUse, (uint32_t)slot);
            vm_write8(dlg + kDlgItemSwitched, 1);
            call_game(ctx, func_0072D970, {0});
        }
        if (log)
            fprintf(stderr, "[items] key for slot %d: %s (slot in use %d)\n", slot + 1, no ? no : "switched",
                    (int)(no ? was : slot) + 1);
    }
    func_007ADB80(ctx);
}

}  // namespace

/* From ps3_title_options_init, before the guest runs. */
extern "C" void des_item_keys_init(void)
{
    DES_REGISTER_FUNCTION(kShortcutUpdate, shortcut_update);
}
