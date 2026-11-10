/* Stat respec at the Nexus statue and the Maiden in Black (docs/47 section 1).
 *
 * DES_RESPEC=1 (Settings, Game; off by default): Cross in front of the statue
 * that a save which quit in the Nexus starts facing opens the shop's command
 * menu (the one with Upgrade Weapon ... Leave) with the rows in kRows:
 * Respecialization and Leave. No prompt shows: the HUD draws the action
 * prompt only for a target the game's action search found (docs/47 1.9).
 * Respecialization asks first, then puts the stats back to the starting
 * class's, gives back the souls the levels cost, and opens the game's level-up
 * screen to spend them again. The screen only raises stats, so nothing goes
 * below the class's values.
 *
 * The Maiden in Black's menu (Seek Soul Power, Talk, Leave) gets the same
 * Respecialization after Seek Soul Power. Its YES ends in her own Seek Soul
 * Power, so she opens the level-up screen; NO puts her menu back.
 *
 * The game's pieces, all called from here (no hook in the lift):
 * - The talk scripts' commands (0x004CC3A8, ids as the scripts number them):
 *   20 ClearTalkListData (0x00723450), 19 AddTalkListData (0x00723498, an
 *   entry {text id, row}), 10 the menu (0x00721490 the talk partner, then
 *   0x007212A8: slot 9 to 1, the menus open page 17400), 17 the yes/no dialog
 *   (0x007216D0, slot 0x11 the kind, 0x007216F0 its texts), 31 the level-up
 *   screen (0x007DC7A0: slot 0x17 to 1). The menu's answer: slot 9 goes to 4
 *   and menus+0xABC holds the row (0 for Circle); the dialog's: slot 0x11 back
 *   to 0 and menus+0x82C 1 for YES.
 * - 0x00226778(x), the cost of the level the screen sells to stat sum x.
 * - 0x0090FCD0(r3, pgd), the game's reset (a penalty path's): the class's
 *   stats (CharaInitParam 1000 + PGD+0xB6), base HP, MP and stamina, PGD+0x88
 *   (levels bought) to 0, every spell unattuned. Souls and items stay.
 * - 0x007E4248(), what the level-up screen runs after a raise: the live
 *   character's HP, MP and stamina from the new stats.
 * - The Maiden's talk script (t0615.esd, her entity id) builds her menu with
 *   {15000025 Seek Soul Power, row 1}, the menu's request, {15000000 Talk, 3}
 *   and {15000005 Leave, 4}, all in one of its updates. It then waits for the
 *   list's result (function 23: menus+0xABC): 1 her level-up screen, 3 Talk,
 *   4 Leave, 0 Circle. Another row keeps it waiting.
 *
 * The texts the game does not have go in its own tables: a row's into an
 * unused id of the talk menu's FMG (category 0x1E), the menu's title in place
 * of the missing-name text ("?NpcName?", while the menu is open). A respec
 * whose souls would pass the cap is refused, and nothing changes.
 *
 * DES_APPEARANCE=1 (Settings, Game; off by default) adds Change Appearance to
 * the statue's rows: the game's own Edit Appearance from character creation
 * (docs/47 section 1.10). Its page script is only in the title's menu bundle,
 * so the title's menu step runs beside the in-game one while it is open. It
 * starts once the statue menu's title bar has gone (about a second after the
 * menu): started sooner, the title bar stayed up behind the editor and could
 * show after it. The editor writes straight into PGD. Back then asks whether
 * to keep the changes: YES reloads the Nexus, which rebuilds the character
 * with the new look; NO puts PGD back as it was. With nothing changed there is
 * no question. */

#include "ps3emu/ppu_lift.h"
#include "des_region.h"      /* DES_AT: the running disc's addresses */
#include "des_game_calls.h"
#include "ps3emu/ps3_knob.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#ifndef PS3_GAME_SPLIT   /* DES_SPLIT: the game DLL's (ps3emu/game_split.h) */
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
#endif
extern "C" void ds_malloc(ppu_context* ctx);              /* des_heap.cpp: the guest heap */
extern "C" void ps3_indirect_call(ppu_context* ctx);      /* ppu_loader.cpp: a call to the guest address in CTR */
extern "C" volatile unsigned g_pad0_buttons;              /* cellPad.c: the buttons the game last read */
extern "C" int des_hud_menu_title_shown(void);            /* des_hud.cpp: the in-game menus' title bar is up */

namespace {

constexpr uint32_t kToc       = 0x019B5040u;
constexpr uint32_t kPgdRoot   = 0x01B4EF9Cu;   /* PGD = *(*this + 8) */
constexpr uint32_t kPgdStats  = 0x38u;         /* eight stats, 8 bytes apart (two copies each) */
constexpr uint32_t kPgdSouls  = 0x78u;
constexpr uint32_t kPgdLevels = 0x88u;         /* levels bought */
constexpr uint32_t kPgdClass  = 0xB6u;         /* halfword */
constexpr int64_t  kSoulsCap  = 999999999;     /* the constant at 0x016FF700 */
constexpr uint32_t kChrMan    = 0x01B3D6E0u;   /* *this + 0x1BC: the player's character */

/* PGD's current and max HP, MP and stamina; the character's current values (its max 4 bytes on). */
constexpr uint32_t kPgdBars[3][2] = {{0x0Cu, 0x10u}, {0x18u, 0x1Cu}, {0x28u, 0x2Cu}};
constexpr uint32_t kChrBars[3]    = {0x3C4u, 0x3CCu, 0x3D4u};

/* The menus' object (*(*(*(TOC - 0x72F0) - 0x8000))): its slots at +0x14 + 4n (0x00720F50 sets them). */
constexpr uint32_t kSlotTalk = 9, kSlotDialog = 0x11, kSlotLevelUp = 0x17;
constexpr uint32_t kMenuRow = 0xABCu, kDialogAnswer = 0x82Cu;
constexpr int32_t  kTalkAnswered = 4, kDialogYesNo = 2, kDialogOk = 1;

/* The texts in the talk menu's FMG (category 0x1E). */
constexpr int32_t kFmgTalk = 0x1E;
constexpr int32_t kTextYes = 10020000, kTextNo = 10020001, kTextLeave = 15000005, kTextSeekSoulPower = 15000025;

/* The game's steps. A step object starts through the step manager (0x001381E8), which gives back its task. */
constexpr uint32_t kStepMan = 0x01B387B8u, kStepHeap = 0x01AD62C8u;   /* *this: the manager, the heap steps are made on */
constexpr uint32_t kTaskState = 0x0Cu, kTaskRefs = 0x10u, kStepState = 8u;
constexpr uint32_t kTaskEnded = 3;
/* TitleMenuStep, the title's menus: its state once the bundle is in, and the byte that starts its end. FaceGenStep,
 * the editor: its constructor leaves the job pointer unset, zero on the title only because that heap is fresh. */
constexpr uint32_t kMenuRunning = 7, kMenuClose = 0x28u, kFaceJob = 0x28u;
/* Menu slots: 5 stops the player's buttons, 0x25 is the editor's request and state, 0x26 keeps its frame up, 0x4E and
 * 0x4F the help strip. */
constexpr uint32_t kSlotNoControl = 5, kSlotFace = 0x25, kSlotFaceFrame = 0x26, kSlotHelp = 0x4E, kSlotLegend = 0x4F;
constexpr int32_t kPlayerEntity = 10000;   /* the player's event id, for the scripts' EnableLogic (0x00443838) */

/* The talk list (menus+0xAB4): *(+4) a vector, its entries {text id, row} from *(+4) to *(+8). */
constexpr uint32_t kTalkList = 0xAB4u;
constexpr int kMaxEntries = 16;

/* The Maiden's Seek Soul Power row, and ours in her menu: one her script has no use for. */
constexpr int32_t kMaidenSeekRow = 1, kMaidenRespecRow = 70;
const char kRespecText[] = "Respecialization";
const char kLookText[] = "Change Appearance";

/* The statue, in the Nexus (map m01): a save that quit there starts at (0, 479.46, -41.69), facing it (-z); the
 * player stops against it at z -41.8. Cross from in front of it, within kReach of (kStatueX, kStatueZ). */
constexpr int   kNexus   = 1;
constexpr float kStatueX = 0.0f, kStatueY = 479.5f, kStatueZ = -43.0f, kReach = 2.2f;

bool guest_ok(uint32_t a) { return a >= 0x00010000u && a < 0xD0000000u && !(a & 3u); }

uint32_t pgd()
{
    const uint32_t root = vm_read32(DES_AT(kPgdRoot));
    return guest_ok(root) && guest_ok(vm_read32(root + 8)) ? vm_read32(root + 8) : 0;
}

uint32_t player_chr()
{
    const uint32_t man = vm_read32(DES_AT(kChrMan));
    const uint32_t c = guest_ok(man) ? vm_read32(man + 0x1BC) : 0;
    return guest_ok(c) ? c : 0;
}

uint32_t menus()
{
    const uint32_t t = vm_read32(DES_AT(0x019ADD50u));
    const uint32_t h = guest_ok(t) ? vm_read32(t - 0x8000u) : 0;
    const uint32_t m = guest_ok(h) ? vm_read32(h) : 0;
    return guest_ok(m) ? m : 0;
}

int32_t slot(uint32_t m, uint32_t n) { return (int32_t)vm_read32(m + 0x14 + 4 * n); }

float readf(uint32_t a)
{
    const uint32_t w = vm_read32(a);
    float f;
    memcpy(&f, &w, 4);
    return f;
}

void drain(ppu_context* ctx)
{
    while (g_trampoline_fn) {
        void (*tf)(void*) = g_trampoline_fn;
        g_trampoline_fn = nullptr;
        tf(ctx);
    }
}

/* Calls a game function with up to eight arguments (r3-r10) and returns r3, on the thread's own registers, which
 * are put back after (des_souls_use.cpp's). */
uint32_t call_at(ppu_context* ctx, void (*fn)(ppu_context*), uint32_t toc, uint32_t code, std::initializer_list<int64_t> a)
{
    uint64_t g[32]; double f[32]; uint8_t v[32][16];
    memcpy(g, ctx->gpr, sizeof g); memcpy(f, ctx->fpr, sizeof f); memcpy(v, ctx->vr, sizeof v);
    const uint64_t lr = ctx->lr, ctr = ctx->ctr;
    const uint32_t cr = ppu_cr_pack(ctx), xer = ctx->xer;
    ctx->gpr[1] -= 0x1000; ctx->gpr[2] = toc;
    ctx->ctr = code;
    int i = 0;
    for (int64_t x : a) if (i < 8) ctx->gpr[3 + i++] = (uint64_t)x;
    ctx->lr = DES_AT(0x00724E40u);
    fn(ctx);
    drain(ctx);
    const uint32_t r3 = (uint32_t)ctx->gpr[3];
    memcpy(ctx->gpr, g, sizeof g); memcpy(ctx->fpr, f, sizeof f); memcpy(ctx->vr, v, sizeof v);
    ctx->lr = lr; ctx->ctr = ctr; ppu_cr_unpack(ctx, cr); ctx->xer = xer;
    return r3;
}

uint32_t call_game(ppu_context* ctx, void (*fn)(ppu_context*), std::initializer_list<int64_t> a)
{
    return call_at(ctx, fn, DES_AT(kToc), (uint32_t)ctx->ctr, a);
}

/* The virtual function at off in obj's table: the entry is a descriptor {code, TOC}, called as the game calls it. */
uint32_t call_virtual(ppu_context* ctx, uint32_t obj, uint32_t off, std::initializer_list<int64_t> a)
{
    const uint32_t d = vm_read32(vm_read32(obj) + off);
    return call_at(ctx, ps3_indirect_call, vm_read32(d + 4), vm_read32(d), a);
}

/* size bytes of the guest heap, for good (des_heap.cpp's ds_malloc); 0 if none. */
uint32_t guest_alloc(ppu_context* ctx, uint32_t size)
{
    const uint64_t r3 = ctx->gpr[3];
    ctx->gpr[3] = size;
    ds_malloc(ctx);
    const uint32_t a = (uint32_t)ctx->gpr[3];
    ctx->gpr[3] = r3;
    return a;
}

/* text as the game's UTF-16 (big-endian) in the guest heap, once per call site's static; 0 if none. */
uint32_t guest_text(ppu_context* ctx, const char* text)
{
    const uint32_t n = (uint32_t)strlen(text) + 1;
    const uint32_t t = guest_alloc(ctx, 2 * n);
    for (uint32_t i = 0; t && i < n; i++) vm_write16(t + 2 * i, (uint16_t)(uint8_t)text[i]);
    return t;
}

bool on()
{
    static int s_on = 0;
    PS3_KNOB(s_on, ps3_env_int("DES_RESPEC", 0));
    return s_on != 0;
}

bool look_on()
{
    static int s_on = 0;
    PS3_KNOB(s_on, ps3_env_int("DES_APPEARANCE", 0));
    return s_on != 0;
}

bool logging()
{
    static int s_log = 0;
    PS3_KNOB(s_log, ps3_env_int("DES_RESPEC_LOG", 0));
    return s_log != 0;
}

std::atomic<int> s_map{-1};   /* the map whose script archive opened last (des_respec_on_open) */

int32_t stat_sum(uint32_t p)
{
    int32_t s = 0;
    for (uint32_t i = 0; i < 8; i++) s += (int32_t)vm_read32(p + kPgdStats + 8 * i);
    return s;
}

/* The class's CharaInitParam row (0x0090FCD0's: 0x00597108 maps the class to a row, 0x00597388 finds it), whose
 * first eight words are its stats in PGD order; 0 if none. */
uint32_t class_row(ppu_context* ctx, uint32_t p)
{
    static uint32_t s_out = 0;   /* the getter's {id, row} */
    if (!s_out && !(s_out = guest_alloc(ctx, 16))) return 0;
    const int32_t row = (int32_t)call_game(ctx, func_00597108, {(int16_t)vm_read16(p + kPgdClass)});
    vm_write32(s_out, 0xFFFFFFFFu); vm_write32(s_out + 4, 0);
    call_game(ctx, func_00597388, {s_out, row});
    const uint32_t r = vm_read32(s_out + 4);
    return guest_ok(r) ? r : 0;
}

/* The souls the levels from stat sum lo up to hi cost, as the screen sells them: the level to sum x costs cost(x). */
int64_t refund(ppu_context* ctx, int32_t lo, int32_t hi)
{
    int64_t s = 0;
    for (int32_t x = lo + 1; x <= hi; x++) s += call_game(ctx, func_00226778, {x});
    return s;
}

/* The game's text id in the talk menu's FMG for an own text: an id of the group that holds Leave that has no text
 * (offset 0), its offset set to the text (offsets are from the FMG's start, modulo 2^32, as 0x005C2848 adds them).
 * k picks the k-th such id, so each own text keeps its own. -1 if none. The lookup (0x005C2848): groups of
 * {offset index, first id, last id} from +0x1C (count at +0x0C), the offsets at *(+0x14). */
int32_t own_text_id(ppu_context* ctx, int k, uint32_t text)
{
    const uint32_t fmg = call_game(ctx, func_005C2810, {kFmgTalk});
    if (!guest_ok(fmg) || !text) return -1;
    const uint32_t groups = vm_read32(fmg + 0x0C), offs = vm_read32(fmg + 0x14);
    if (!guest_ok(offs)) return -1;
    for (uint32_t g = 0; g < groups && g < 0x10000u; g++) {
        const uint32_t gi = fmg + 0x1C + 12 * g;
        const uint32_t oi = vm_read32(gi), first = vm_read32(gi + 4), last = vm_read32(gi + 8);
        if ((uint32_t)kTextLeave < first || (uint32_t)kTextLeave > last) continue;
        for (uint32_t id = last; id > (uint32_t)kTextLeave; id--) {
            const uint32_t at = offs + 4 * (oi + id - first), off = vm_read32(at);
            if (off == text - fmg) { if (k-- == 0) return (int32_t)id; continue; }
            if (off) continue;
            if (k-- == 0) {
                vm_write32(at, text - fmg);
                return (int32_t)id;
            }
        }
    }
    return -1;
}

/* ---- The menu ---- */

void ask_respec(ppu_context* ctx);
void open_editor(ppu_context* ctx);

/* The statue's rows, in order (the talk list's row number is the position + 1). own: a text of ours (own_text_id),
 * else game: the game's text id. choose: what the row does, nullptr to close (Leave). shown: the row's knob, nullptr
 * for always. */
struct Row { const char* own; int32_t game; void (*choose)(ppu_context*); bool (*shown)(); };
constexpr Row kRows[] = {
    {kRespecText, 0, ask_respec, on},
    {kLookText, 0, open_editor, look_on},
    {nullptr, kTextLeave, nullptr, nullptr},
};
constexpr int kRowCount = (int)(sizeof kRows / sizeof kRows[0]);

/* An own text in the guest heap, one copy per text for good, so the statue and the Maiden share its id. */
uint32_t own_text(ppu_context* ctx, const char* s)
{
    static const char* s_keys[4];
    static uint32_t s_vals[4];
    for (int i = 0; i < 4; i++) {
        if (s_keys[i] == s) return s_vals[i];
        if (!s_keys[i]) {
            if ((s_vals[i] = guest_text(ctx, s))) s_keys[i] = s;
            return s_vals[i];
        }
    }
    return 0;
}

/* AddTalkListData's entry {text id, row}, then command 10's argument {0, -1, 0} 8 bytes on. */
uint32_t talk_entry(ppu_context* ctx)
{
    static uint32_t s_entry = 0;
    return s_entry ? s_entry : (s_entry = guest_alloc(ctx, 32));
}

void add_row(ppu_context* ctx, int32_t id, int32_t row)
{
    const uint32_t e = talk_entry(ctx);
    vm_write32(e, (uint32_t)id);
    vm_write32(e + 4, (uint32_t)row);
    call_game(ctx, func_00723498, {e});
}

void show_menu(ppu_context* ctx)
{
    const uint32_t e = talk_entry(ctx);
    vm_write8(e + 8, 0);
    vm_write32(e + 12, 0xFFFFFFFFu);
    vm_write32(e + 16, 0);
    call_game(ctx, func_007212A8, {e + 8});    /* talk command 10's */
}

const char kTitle[] = "Statue";
const char kAsk[] = "Return to your class's starting stats?\nThe souls of every level come back,\n"
                    "and every spell must be remembered again.";
const char kCapped[] = "Not now: with the souls back,\nyou would hold more than 999,999,999.";
const char kKeep[] = "Keep this appearance?\nThe Nexus reloads to show it.";

/* idle; menu, the statue's open; confirm, its question; notice, the cap's; maiden, her menu open with our row;
 * maiden_ask, our row answered, her menu closing; maiden_confirm and maiden_notice, the question and the cap's
 * notice while her script waits; maiden_again, the dialog closing before her menu comes back. look_title, the statue
 * menu's title bar going; look_load, the title's menus loading; look_edit, the editor open; look_close, the title's
 * menus ending; look_settle, their pages going; look_keep, the question. */
enum class Phase {
    idle, menu, confirm, notice, maiden, maiden_ask, maiden_confirm, maiden_notice, maiden_again,
    look_title, look_load, look_edit, look_close, look_settle, look_keep,
};
Phase s_phase = Phase::idle;
bool s_here = false;              /* the player was at the statue last frame (for the log) */
unsigned s_cross_prev = 0x4000u;  /* Cross as last seen, held at the start: a press must begin near the statue */
int s_chosen = 0;                /* the row the menu answered, run once the menu has closed */

/* The menu's title with no talk partner: the game's missing-name text ("?NpcName?", 0x016EBB98, ten UTF-16 units
 * with its end, which five pointers in the game's tables give the name lookups), ours while the menu is open. */
constexpr uint32_t kNoName = 0x016EBB98u, kNoNameUnits = 10;
uint16_t s_no_name[kNoNameUnits];
bool s_title = false;

void title_on()
{
    static_assert(sizeof kTitle <= kNoNameUnits, "the title must fit the missing-name text");
    if (s_title || vm_read16(DES_AT(kNoName)) != (uint16_t)'?') return;
    for (uint32_t i = 0; i < kNoNameUnits; i++) s_no_name[i] = vm_read16(DES_AT(kNoName) + 2 * i);
    for (uint32_t i = 0; i < sizeof kTitle; i++) vm_write16(DES_AT(kNoName) + 2 * i, (uint16_t)(uint8_t)kTitle[i]);
    s_title = true;
}

void title_off()
{
    if (!s_title) return;
    for (uint32_t i = 0; i < kNoNameUnits; i++) vm_write16(DES_AT(kNoName) + 2 * i, s_no_name[i]);
    s_title = false;
}

/* No menu or dialog open (talk command 10's test, 0x00720F80, masked as it masks it). */
bool menus_clear(ppu_context* ctx, uint32_t m)
{
    if (slot(m, kSlotTalk) || slot(m, kSlotDialog) || slot(m, kSlotLevelUp)) return false;
    return (call_game(ctx, func_00720F80, {}) & 0xDEFFFF7Fu) == 0;
}

/* The player stands in front of the statue, in the Nexus, facing it, alive, with no menu or dialog open. */
bool at_statue(ppu_context* ctx, uint32_t m)
{
    if (s_map.load(std::memory_order_relaxed) != kNexus) return false;
    const uint32_t chr = player_chr();
    if (!chr || (int32_t)vm_read32(chr + 0x3C4) <= 0) return false;
    uint32_t xf = vm_read32(chr + 0x20);
    if (!guest_ok(xf) || !guest_ok(xf = vm_read32(xf + 0x10))) return false;
    const float x = readf(xf + 0x10), y = readf(xf + 0x14), z = readf(xf + 0x18);
    const float dx = kStatueX - x, dz = kStatueZ - z;
    if (std::fabs(y - kStatueY) > 1.5f || dx * dx + dz * dz > kReach * kReach || z < kStatueZ) return false;
    /* The facing: the yaw at +4, the bearing + pi (des_options.cpp's roll turn), so it faces (-sin, -cos). */
    const float yaw = readf(xf + 4), fx = -std::sin(yaw), fz = -std::cos(yaw);
    if (fx * dx + fz * dz < 0.5f * std::sqrt(dx * dx + dz * dz)) return false;   /* within 60 degrees */
    return menus_clear(ctx, m);
}

void open_menu(ppu_context* ctx)
{
    if (!talk_entry(ctx)) return;
    call_game(ctx, func_00723450, {});
    for (int i = 0, own = 0; i < kRowCount; i++) {
        if (kRows[i].shown && !kRows[i].shown()) continue;
        const int32_t id = kRows[i].own ? own_text_id(ctx, own++, own_text(ctx, kRows[i].own)) : kRows[i].game;
        if (id >= 0) add_row(ctx, id, i + 1);
    }
    title_on();
    call_game(ctx, func_00721490, {-1});             /* no talk partner: the title is the missing name's */
    show_menu(ctx);
    s_phase = Phase::menu;
}

/* ---- The Maiden's menu ---- */

int32_t s_her[kMaxEntries][2];   /* her menu's entries, as her script added them */
int s_her_count = 0;

/* Her script has just asked for her menu (slot 9 at 1, the list holding Seek Soul Power and not our row): her
 * entries go to s_her. The menus' update has not read the list yet: her script adds it all in one of its updates,
 * and this tick comes before the menus' next update. */
bool maiden_menu_asked(uint32_t m)
{
    if (slot(m, kSlotTalk) != 1) return false;
    const uint32_t v = vm_read32(m + kTalkList + 4);
    if (!guest_ok(v)) return false;
    const uint32_t b = vm_read32(v + 4), e = vm_read32(v + 8);
    if (!guest_ok(b) || e < b || e - b > 8u * kMaxEntries || (e - b) % 8) return false;
    bool seek = false;
    for (uint32_t a = b; a < e; a += 8) {
        if ((int32_t)vm_read32(a + 4) == kMaidenRespecRow) return false;
        seek = seek || (int32_t)vm_read32(a) == kTextSeekSoulPower;
    }
    if (!seek) return false;
    s_her_count = (int)((e - b) / 8);
    for (int i = 0; i < s_her_count; i++) {
        s_her[i][0] = (int32_t)vm_read32(b + 8 * i);
        s_her[i][1] = (int32_t)vm_read32(b + 8 * i + 4);
    }
    return true;
}

/* Her entries again, ours after Seek Soul Power. */
void maiden_list(ppu_context* ctx)
{
    const int32_t own = own_text_id(ctx, 0, own_text(ctx, kRespecText));
    call_game(ctx, func_00723450, {});
    for (int i = 0; i < s_her_count; i++) {
        add_row(ctx, s_her[i][0], s_her[i][1]);
        if (s_her[i][0] == kTextSeekSoulPower && own >= 0) add_row(ctx, own, kMaidenRespecRow);
    }
}

/* Her menu again, as her script showed it (the talk partner is still hers). */
void maiden_menu_again(ppu_context* ctx)
{
    maiden_list(ctx);
    show_menu(ctx);
    s_phase = Phase::maiden;
    if (logging()) fprintf(stderr, "[des] respec: the Maiden's menu again\n");
}

void dialog(ppu_context* ctx, int32_t kind, const char* text, Phase next)
{
    static uint32_t s_ask = 0, s_capped = 0, s_keep = 0, s_ok = 0;
    uint32_t& t = text == kAsk ? s_ask : text == kKeep ? s_keep : s_capped;
    if (!t) t = guest_text(ctx, text);
    const uint32_t fmg = call_game(ctx, func_005C2810, {kFmgTalk});
    if (!t || !guest_ok(fmg)) return;
    uint32_t yes = call_game(ctx, func_005C2848, {fmg, kTextYes});
    const uint32_t no = call_game(ctx, func_005C2848, {fmg, kTextNo});
    if (kind == kDialogOk && (s_ok || (s_ok = guest_text(ctx, "OK")))) yes = s_ok;   /* the talk texts have no OK */
    call_game(ctx, func_007216D0, {-1, -1});
    call_game(ctx, func_00720F50, {(int64_t)kSlotDialog, kind});
    call_game(ctx, func_00720F50, {0x54, 0});
    call_game(ctx, func_007216F0, {t, yes, no});    /* talk command 17's */
    call_game(ctx, func_00720F50, {0x4E, -1});
    call_game(ctx, func_00720F50, {0x4F, -1});
    s_phase = next;
    if (logging()) fprintf(stderr, "[des] respec: dialog %d\n", kind);
}

void ask_respec(ppu_context* ctx) { dialog(ctx, kDialogYesNo, kAsk, Phase::confirm); }

/* ---- The respec ---- */

/* The respec's numbers: the class's stat sum and the souls the levels above it cost. false if the game's state is
 * not there. */
bool plan(ppu_context* ctx, uint32_t p, uint32_t chr, int32_t* floor_sum, int64_t* back)
{
    const uint32_t row = p ? class_row(ctx, p) : 0;
    if (!row || !chr || vm_read32(chr + 0x49C) != p) return false;
    *floor_sum = 0;
    for (uint32_t i = 0; i < 8; i++) *floor_sum += (int32_t)vm_read32(row + 4 * i);
    const int32_t sum = stat_sum(p);
    *back = sum > *floor_sum ? refund(ctx, *floor_sum, sum) : 0;
    return true;
}

/* The souls held plus those the respec gives back would pass the cap. */
bool capped_now(ppu_context* ctx)
{
    const uint32_t p = pgd();
    int32_t floor_sum;
    int64_t back;
    if (!plan(ctx, p, player_chr(), &floor_sum, &back)) return false;
    const int32_t souls = (int32_t)vm_read32(p + kPgdSouls);
    if (souls >= 0 && souls + back <= kSoulsCap) return false;
    fprintf(stderr, "[des] respec: refused, %d souls and %lld back would pass the cap\n", souls, (long long)back);
    return true;
}

/* false (and nothing changed) if the souls would pass the cap or the game's state is not there. */
bool respec(ppu_context* ctx, bool* capped)
{
    *capped = false;
    const uint32_t p = pgd();
    const uint32_t chr = player_chr();
    int32_t floor_sum;
    int64_t back;
    if (!plan(ctx, p, chr, &floor_sum, &back)) return false;
    const int32_t sum = stat_sum(p), souls = (int32_t)vm_read32(p + kPgdSouls), levels = (int32_t)vm_read32(p + kPgdLevels);
    if (souls < 0 || souls + back > kSoulsCap) {
        fprintf(stderr, "[des] respec: refused, %d souls and %lld back would pass the cap\n", souls, (long long)back);
        *capped = true;
        return false;
    }
    if (sum <= floor_sum) return true;   /* nothing to give back: the screen alone, the spells kept */
    double share[3];
    for (int b = 0; b < 3; b++) {
        const int32_t cur = (int32_t)vm_read32(chr + kChrBars[b]), max = (int32_t)vm_read32(chr + kChrBars[b] + 4);
        share[b] = max > 0 ? std::clamp((double)cur / max, 0.0, 1.0) : 1.0;
    }
    call_game(ctx, func_0090FCD0, {0, p});
    vm_write32(p + kPgdSouls, (uint32_t)(souls + back));
    /* 0x007E4248, made for raises, moves each bar by the change from PGD's old max to the character's new one, after
     * the character's own update (0x002802E0, 0x002737A0, 0x00273620) has already cut its current value to a lower
     * max: after a respec the cut would count twice, and from the reset's bare base values a soul form's HP could
     * reach 0. So the character's update runs first, and PGD's old values are set so that each bar ends at its
     * old share of the new max (at least 1). */
    call_game(ctx, func_002802E0, {chr});
    call_game(ctx, func_002737A0, {chr});
    call_game(ctx, func_00273620, {chr});
    for (int b = 0; b < 3; b++) {
        const int32_t cur = (int32_t)vm_read32(chr + kChrBars[b]), max = (int32_t)vm_read32(chr + kChrBars[b] + 4);
        const int32_t want = std::clamp((int32_t)(share[b] * max + 0.5), std::min(1, max), max);
        vm_write32(p + kPgdBars[b][0], (uint32_t)cur);
        vm_write32(p + kPgdBars[b][1], (uint32_t)(max - want + cur));
    }
    call_game(ctx, func_007E4248, {});
    fprintf(stderr, "[des] respec: stat sum %d to %d (class %d), %d levels, souls %d + %lld = %d\n", sum, stat_sum(p),
            floor_sum, levels, souls, (long long)back, (int32_t)vm_read32(p + kPgdSouls));
    return true;
}

/* ---- Change Appearance ---- */

/* What the editor writes in PGD: the hair lists at +0x198 and, from +0x3D0, the origin, the colours and the face's
 * 100 bytes. */
struct Range { uint32_t at, len; };
constexpr Range kLook[] = {{0x198u, 0x48u}, {0x3D0u, 0x98u}};
constexpr uint32_t kLookSize = 0x48u + 0x98u;
constexpr uint32_t kLookCounter = 0x2C8u;   /* a word the editor moves as well; no part of how the character looks */

uint8_t s_look[kLookSize];
uint32_t s_look_counter = 0;
uint32_t s_menu_task = 0, s_menu_step = 0, s_face_task = 0;

void look_read(uint32_t p, uint8_t* out)
{
    for (const Range& r : kLook)
        for (uint32_t i = 0; i < r.len; i++) *out++ = vm_read8(p + r.at + i);
}

bool look_changed(uint32_t p)
{
    uint8_t now[kLookSize];
    look_read(p, now);
    return memcmp(now, s_look, sizeof now) != 0;
}

void look_restore(uint32_t p)
{
    const uint8_t* in = s_look;
    for (const Range& r : kLook)
        for (uint32_t i = 0; i < r.len; i++) vm_write8(p + r.at + i, *in++);
    vm_write32(p + kLookCounter, s_look_counter);
}

/* A new step of the game: size bytes made by ctor and started in the manager's phase, under id. Its task, held once
 * more so it stays readable after it ends (release_task), or 0. */
uint32_t start_step(ppu_context* ctx, uint32_t size, void (*ctor)(ppu_context*), int32_t phase, int32_t id, bool face, uint32_t* out)
{
    const uint32_t man = vm_read32(DES_AT(kStepMan)), heap = vm_read32(DES_AT(kStepHeap));
    if (!guest_ok(man) || !guest_ok(heap)) return 0;
    const uint32_t step = call_game(ctx, func_00A941F8, {size, heap});
    if (!guest_ok(step)) return 0;
    call_game(ctx, ctor, {step, 0});
    if (face) vm_write32(step + kFaceJob, 0);
    const uint32_t task = call_game(ctx, func_001381E8, {man, phase, step, id});
    if (!guest_ok(task)) return 0;
    vm_write32(task + kTaskRefs, vm_read32(task + kTaskRefs) + 1);
    if (out) *out = step;
    return task;
}

/* As the game lets go of a task: the last reference destroys it and frees it on the heap it came from. */
void release_task(ppu_context* ctx, uint32_t task)
{
    if (!task) return;
    const uint32_t refs = vm_read32(task + kTaskRefs) - 1;
    vm_write32(task + kTaskRefs, refs);
    if (refs) return;
    const uint32_t heap = call_game(ctx, func_00A36220, {task});
    call_virtual(ctx, task, 4, {task});
    call_virtual(ctx, heap, 0x38, {heap, task});
}

/* The statue's row. The character stays still until the editor has gone: slot 5 stops the buttons and the Start menu,
 * and the scripts' EnableLogic stops the stick, which the slot does not (the editor picks rows with it). The title's
 * menus wait for the statue menu's title bar to go (look_title). */
void open_editor(ppu_context* ctx)
{
    const uint32_t p = pgd();
    if (!p) return;
    look_read(p, s_look);
    s_look_counter = vm_read32(p + kLookCounter);
    call_game(ctx, func_00720F50, {kSlotNoControl, 1});
    call_game(ctx, func_00443838, {0, kPlayerEntity, 0});
    s_phase = Phase::look_title;
}

/* The title's menus, since their bundle holds the editor's page. */
void start_menus(ppu_context* ctx)
{
    s_menu_task = start_step(ctx, 0x58, func_00196158, 3, 8, false, &s_menu_step);
    if (s_menu_task) {
        s_phase = Phase::look_load;
    } else {
        s_phase = Phase::idle;
        call_game(ctx, func_00720F50, {kSlotNoControl, 0});
        call_game(ctx, func_00443838, {0, kPlayerEntity, 1});
        fprintf(stderr, "[des] appearance: the title's menus did not start\n");
    }
    if (logging()) fprintf(stderr, "[des] appearance: loading the menus\n");
}

/* The editor has gone. Both tasks are let go, the controls come back, and the question follows once the pages have
 * gone (look_settle). */
void look_end(ppu_context* ctx)
{
    release_task(ctx, s_face_task);
    release_task(ctx, s_menu_task);
    s_face_task = s_menu_task = s_menu_step = 0;
    call_game(ctx, func_00720F50, {kSlotNoControl, 0});
    call_game(ctx, func_00443838, {0, kPlayerEntity, 1});
    s_phase = Phase::look_settle;
    if (logging()) fprintf(stderr, "[des] appearance: the menus ended\n");
}

} // namespace

/* From des_options.cpp's file-open hook (the file thread): a level's script archive, /script/mNN.luabnd..., names
 * the map that is loading. */
extern "C" void des_respec_on_open(const char* path)
{
    const char* s = strstr(path, "/script/m");
    if (!s || !strstr(s, ".luabnd") || s[9] < '0' || s[9] > '9' || s[10] < '0' || s[10] > '9') return;
    s_map.store((s[9] - '0') * 10 + (s[10] - '0'), std::memory_order_relaxed);
}

/* From des_menu_tick (des_menu_sfx.cpp), on the main thread just before the menus' update (0x00724AD0). */
extern "C" void des_respec_tick(ppu_context* ctx)
{
    const uint32_t m = menus();
    if (!m) return;
    const unsigned cross = g_pad0_buttons & 0x4000u, pressed = cross & ~s_cross_prev;
    s_cross_prev = cross;
    /* The title's menus clear the slot as they start, so it is set again until the editor has gone. */
    const bool held = s_phase == Phase::look_title || s_phase == Phase::look_load || s_phase == Phase::look_edit ||
                      s_phase == Phase::look_close;
    if (held && slot(m, kSlotNoControl) != 1) call_game(ctx, func_00720F50, {kSlotNoControl, 1});
    switch (s_phase) {
    case Phase::idle: {
        if (on() && maiden_menu_asked(m)) {
            maiden_list(ctx);
            s_phase = Phase::maiden;
            if (logging()) fprintf(stderr, "[des] respec: the Maiden's menu, with Respecialization\n");
            break;
        }
        const bool here = (on() || look_on()) && at_statue(ctx, m);
        if (here != s_here && logging()) fprintf(stderr, "[des] respec: %s the statue\n", here ? "at" : "away from");
        s_here = here;
        if (here && pressed) {
            open_menu(ctx);
            if (logging()) fprintf(stderr, "[des] respec: menu\n");
        }
        break;
    }
    case Phase::menu: {   /* the answer (slot 9 at 4, the row at +0xABC), then the menu closes (slot 9 to 0) */
        const int32_t talk = slot(m, kSlotTalk);
        if (talk == kTalkAnswered) s_chosen = 1;
        if (talk || (call_game(ctx, func_00720F80, {}) & 0xDEFFFF7Fu)) break;
        const int row = s_chosen ? (int32_t)vm_read32(m + kMenuRow) : 0;
        call_game(ctx, func_00723450, {});   /* as the scripts do once the menu answers */
        title_off();
        s_phase = Phase::idle;
        s_chosen = 0;
        if (logging()) fprintf(stderr, "[des] respec: menu row %d\n", row);
        if (row >= 1 && row <= kRowCount && kRows[row - 1].choose) kRows[row - 1].choose(ctx);
        break;
    }
    case Phase::confirm: {
        if (slot(m, kSlotDialog)) break;
        s_phase = Phase::idle;
        bool capped = false;
        const int32_t answer = (int32_t)vm_read32(m + kDialogAnswer);
        if (logging()) fprintf(stderr, "[des] respec: answer %d\n", answer);
        if (answer != 1) break;
        if (respec(ctx, &capped)) call_game(ctx, func_007DC7A0, {});   /* talk command 31: the level-up screen */
        else if (capped) dialog(ctx, kDialogOk, kCapped, Phase::notice);
        break;
    }
    case Phase::notice:
        if (!slot(m, kSlotDialog)) s_phase = Phase::idle;
        break;
    case Phase::maiden: {   /* her menu with our row: hers are her script's to act on */
        if (maiden_menu_asked(m)) {   /* her script asked for it again, without our row */
            maiden_list(ctx);
            break;
        }
        const int32_t talk = slot(m, kSlotTalk), row = (int32_t)vm_read32(m + kMenuRow);
        if (row == kMaidenRespecRow) {
            if (!talk) s_phase = Phase::maiden_ask;   /* closed on ours: her script waits, as for no row of hers */
        } else if (!talk || (talk == kTalkAnswered && row >= 0)) {
            s_phase = Phase::idle;
            if (logging()) fprintf(stderr, "[des] respec: the Maiden's menu row %d, hers\n", row);
        }
        break;
    }
    case Phase::maiden_ask:   /* as the statue's question, once her menu's page is gone */
        if (!menus_clear(ctx, m)) break;
        if (logging()) fprintf(stderr, "[des] respec: the Maiden's Respecialization\n");
        if (capped_now(ctx)) dialog(ctx, kDialogOk, kCapped, Phase::maiden_notice);
        else dialog(ctx, kDialogYesNo, kAsk, Phase::maiden_confirm);
        break;
    case Phase::maiden_confirm: {
        if (slot(m, kSlotDialog)) break;
        bool capped = false;
        const int32_t answer = (int32_t)vm_read32(m + kDialogAnswer);
        if (logging()) fprintf(stderr, "[des] respec: answer %d (the Maiden's)\n", answer);
        if (answer == 1 && respec(ctx, &capped)) {
            /* Her Seek Soul Power: her script opens the level-up screen (talk command 31), whose page the menus' next
             * update opens, after this respec. */
            vm_write32(m + kMenuRow, (uint32_t)kMaidenSeekRow);
            s_phase = Phase::idle;
        } else {
            s_phase = Phase::maiden_again;
        }
        break;
    }
    case Phase::maiden_notice:
        if (!slot(m, kSlotDialog)) s_phase = Phase::maiden_again;
        break;
    case Phase::maiden_again:   /* NO, Circle or the cap: her menu again, once the dialog's page is gone */
        if (menus_clear(ctx, m)) maiden_menu_again(ctx);
        break;
    case Phase::look_title:   /* the menus' own close of it stops while the title's menus run */
        if (!des_hud_menu_title_shown()) start_menus(ctx);
        break;
    case Phase::look_load:
        if (vm_read32(s_menu_task + kTaskState) == kTaskEnded) { look_end(ctx); break; }
        if (vm_read32(s_menu_step + kStepState) != kMenuRunning) break;
        s_face_task = start_step(ctx, 0x34, func_0017B258, 2, 0, true, nullptr);
        if (s_face_task) {
            s_phase = Phase::look_edit;
            if (logging()) fprintf(stderr, "[des] appearance: the editor\n");
        } else {
            vm_write8(s_menu_step + kMenuClose, 1);
            s_phase = Phase::look_close;
            fprintf(stderr, "[des] appearance: the editor did not start\n");
        }
        break;
    case Phase::look_edit:   /* Back: its slot reaches 10 and the step ends */
        if (vm_read32(s_face_task + kTaskState) != kTaskEnded) break;
        call_game(ctx, func_00720F50, {kSlotFace, 0});
        call_game(ctx, func_00720F50, {kSlotFaceFrame, 0});
        call_game(ctx, func_00720F50, {kSlotHelp, -1});
        call_game(ctx, func_00720F50, {kSlotLegend, -1});
        vm_write8(s_menu_step + kMenuClose, 1);
        s_phase = Phase::look_close;
        if (logging()) fprintf(stderr, "[des] appearance: closing\n");
        break;
    case Phase::look_close:
        if (vm_read32(s_menu_task + kTaskState) == kTaskEnded) look_end(ctx);
        break;
    case Phase::look_settle: {
        if (!menus_clear(ctx, m)) break;
        const uint32_t p = pgd();
        const bool changed = p && look_changed(p);
        if (logging()) fprintf(stderr, "[des] appearance: closed, %s\n", changed ? "changed" : "no change");
        s_phase = Phase::idle;
        if (changed) dialog(ctx, kDialogYesNo, kKeep, Phase::look_keep);
        break;
    }
    case Phase::look_keep: {
        if (slot(m, kSlotDialog)) break;
        s_phase = Phase::idle;
        const uint32_t p = pgd();
        const int32_t answer = (int32_t)vm_read32(m + kDialogAnswer);
        if (logging()) fprintf(stderr, "[des] appearance: answer %d\n", answer);
        if (answer == 1) call_game(ctx, func_00451430, {0, 1, 0, 0, 0, -1});   /* the warp the scripts use: this map again */
        else if (p) look_restore(p);
        break;
    }
    }
}
