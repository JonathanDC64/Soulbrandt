/* Demon's Souls: the game's own debug menus (docs/180). The retail EBOOT still has them; only the menu's root and
 * the body of its per-frame call were cut.
 *
 * DES_DBGMENU=1 — the runtime debug menu: Select cycles it hidden -> shown with the pad (the game ignores the
 *   buttons) -> shown while the game has the pad -> hidden. D-pad up and down move, Cross opens a folder or runs an
 *   item, Circle goes back.
 * DES_DBGMENU_TITLE=1 — the title's debug menu (the developers' step list: model viewer, Walk Through, SFX test,
 *   FaceGen test and others) in place of the title menu.
 * DES_FREECAM=1 — the developers' debug camera (docs/181; also Settings > Controls > Camera > Free camera, off by
 *   default). Hold Cross and press L3 to cycle its modes: free camera with
 *   time stopped (Square held lets frames through) -> free camera while time runs -> the view stays where the camera
 *   is while you play -> off. Left stick moves, right stick turns, R2 and L2 go up and down.
 * All are read once, at start (des_dbgmenu_boot); setting them later does nothing.
 *
 * None can be used to cheat. With any set, the session is offline (below). With the menus, also:
 *   - the session is offline: the network knobs are cleared and the runtime is locked offline before anything
 *     connects (des_ui_net_off), so no online play, no co-op and no Pantheon records. The Network tab says why;
 *   - the game saves to a copy of the saves made at start, <save root>-debug, replaced at each start. The saves the
 *     game normally uses are only read, so nothing the menu changes (items, flags, tendency) can be carried into a
 *     later online session. If the copy can't be made, the menus stay off.
 *
 * The hooks (port/insn_hooks.txt, des_dbgmenu_hook):
 *   0x00020F44  create_debug_menu (0x00015A58) has returned the menu: 0x14 bytes, vtable, +4 the root, +8 the
 *               DbgMenuPad, +0xC the DbgMenuHgDraw, +0x10 the state, all 0. The root is made here with the game's
 *               own label function: the game asks for it right after (0x00020F4C) and adds its items to it, as the
 *               other places that add items (0x00015328's callers) do later.
 *   0x0016F42C  cmpwi cr7, r3, 0 on the title step's new memory: the debug title step's constructor 0x0015C878 runs
 *               on it, and cr7 EQ takes the beq past the title step's own constructor (0x0018B7D0, same arguments).
 *   0x0001BB38  after the frame function's call of 0x00015A50, the retail game's emptied menu frame (a blr): the
 *               pad's keys, Select's toggle, the root's input (vtable +8) while the menu has the pad, the window's
 *               update and timer, and while shown the window and the tree (vtable +0xC). The window's timer steps by
 *               the frame's time, which the frame function passes in f1.
 *   0x0014422C  the frame step 0x00144160 (r31) has asked for pad device 0 (r3): the free camera (free_camera). */
#include "ps3emu/ppu_lift.h"
#include "des_region.h"         /* DES_AT: the running disc's addresses */
#include "ps3emu/ps3_knob.h"    /* ps3_setenv */
#include "ui/include/des_ui.h"  /* des_ui_net_off */
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <system_error>

#ifndef PS3_GAME_SPLIT   /* DES_SPLIT: the game DLL's (ps3emu/game_split.h) */
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
#endif
extern "C" void ps3_indirect_call(ppu_context* ctx);   /* ppu_loader.cpp: a call to the guest address in CTR */

namespace {

constexpr uint32_t kToc         = 0x019B5040u;
constexpr uint32_t kTitleStep   = 0x0015C878u;   /* the debug title step's constructor (step, 0, 1) */
/* Pad device 0's sources (0, source): 0x001C67A8 raises the source's count at +0x20 and 0x001C6760 lowers it; while
 * the count is above 0 the game ignores that source. The menu's own pad is source 2, so 0, 1 and 3 go off while it
 * has the pad. */
constexpr uint32_t kPadOff      = 0x001C67A8u;
constexpr uint32_t kPadOn       = 0x001C6760u;
constexpr uint32_t kPadKeys     = 0x00400190u;   /* (DbgMenuPad): this frame's keys */
constexpr uint32_t kDrawTimer   = 0x004008A0u;   /* (DbgMenuHgDraw, f1 = seconds) */
constexpr uint32_t kDrawUpdate  = 0x00400F30u;   /* (DbgMenuHgDraw) */
constexpr uint32_t kDrawBox     = 0x00401C60u;   /* (DbgMenuHgDraw): the window */
constexpr uint32_t kNewLabel    = 0x00406F08u;   /* (UTF-16 text) -> a label node, the root's kind */
constexpr uint32_t kRootText    = 0x01705B38u;   /* L"ROOT", the game's own */
constexpr const char* kSaveRoot = "gamedata/dev_hdd0/home/00000001/savedata";   /* cellSaveData.c's default */
constexpr uint32_t kDbgCamControls = 0x0012B2F8u;   /* (camera, pad device, 0, 0): the debug camera's moves */

std::atomic<bool> s_menu{false}, s_title{false}, s_cam{false};
std::atomic<uint32_t> s_obj{0};   /* the menu object, once its root is made */
/* The free camera's (the game's main thread only): the cameras object, and whether we stopped time. */
uint32_t s_cam_cams = 0;
bool s_cam_stopped = false;

bool knob_on(const char* name)
{
    const char* v = std::getenv(name);
    return v && *v && std::strcmp(v, "0") != 0;
}

bool guest_ok(uint32_t a) { return ((a >= 0x00010000u && a < 0x50000000u) || (a >= 0xD0000000u && a < 0xE0000000u)) && !(a & 3u); }
uint32_t rd(uint32_t a) { return guest_ok(a) ? vm_read32(a) : 0; }

/* Calls a game function with up to six arguments (r3-r8) and returns r3, on the thread's own registers, which are
 * put back after (des_respec.cpp's call_at). */
uint32_t call_at(ppu_context* ctx, uint32_t toc, uint32_t code, std::initializer_list<uint32_t> a)
{
    uint64_t g[32]; double f[32]; uint8_t v[32][16];
    memcpy(g, ctx->gpr, sizeof g); memcpy(f, ctx->fpr, sizeof f); memcpy(v, ctx->vr, sizeof v);
    const uint64_t lr = ctx->lr, ctr = ctx->ctr;
    const uint32_t cr = ppu_cr_pack(ctx), xer = ctx->xer;
    ctx->gpr[1] -= 0x1000; ctx->gpr[2] = toc;
    ctx->ctr = code;
    int i = 0;
    for (uint32_t x : a) if (i < 6) ctx->gpr[3 + i++] = x;
    ctx->lr = DES_AT(0x00724E40u);
    ps3_indirect_call(ctx);
    while (g_trampoline_fn) {
        void (*tf)(void*) = g_trampoline_fn;
        g_trampoline_fn = nullptr;
        tf(ctx);
    }
    const uint32_t r3 = (uint32_t)ctx->gpr[3];
    memcpy(ctx->gpr, g, sizeof g); memcpy(ctx->fpr, f, sizeof f); memcpy(ctx->vr, v, sizeof v);
    ctx->lr = lr; ctx->ctr = ctr; ppu_cr_unpack(ctx, cr); ctx->xer = xer;
    return r3;
}

uint32_t call_game(ppu_context* ctx, uint32_t code, std::initializer_list<uint32_t> a)
{
    return call_at(ctx, DES_AT(kToc), code, a);
}

/* The virtual function at off in obj's table: the entry is a descriptor {code, TOC}, called as the game calls it. */
void call_virtual(ppu_context* ctx, uint32_t obj, uint32_t off, std::initializer_list<uint32_t> a)
{
    const uint32_t d = rd(rd(obj) + off);
    if (rd(d)) call_at(ctx, rd(d + 4), rd(d), a);
}

std::filesystem::path upath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

/* The session's saves go to <root>-debug, a copy of the save root made now (the root as cellSaveData.c takes it:
 * PS3_SAVEDATA_ROOT, else its default, relative to the working folder). The last session's copy is replaced; the root
 * itself is only read. False, with why, when the copy can't be made. */
bool use_save_copy(std::string* why)
{
    namespace fs = std::filesystem;
    const char* given = std::getenv("PS3_SAVEDATA_ROOT");
    std::string from = given && *given ? given : kSaveRoot;
    while (from.size() > 1 && (from.back() == '/' || from.back() == '\\')) from.pop_back();
    const std::string to = from + "-debug";
    std::error_code ec;
    fs::remove_all(upath(to), ec);
    if (ec) { *why = "the last copy at " + to + " could not be removed (" + ec.message() + ")"; return false; }
    if (fs::is_directory(upath(from), ec)) {
        fs::copy(upath(from), upath(to), fs::copy_options::recursive, ec);
        if (ec) { *why = "the saves could not be copied to " + to + " (" + ec.message() + ")"; return false; }
    } else if (!fs::create_directories(upath(to), ec) && ec) {
        *why = to + " could not be made (" + ec.message() + ")";
        return false;
    }
    ps3_setenv("PS3_SAVEDATA_ROOT", to.c_str());
    *why = to;
    return true;
}

/* The menu's frame (the top: the 0x0001BB38 hook). */
void frame(ppu_context* ctx)
{
    const uint32_t menu = s_obj.load(std::memory_order_relaxed);
    const uint32_t root = rd(menu + 4), pad = rd(menu + 8), draw = rd(menu + 0xC);
    if (!root || !draw) return;   /* (the pad and the window are made by the title step) */
    if (pad) {
        call_game(ctx, DES_AT(kPadKeys), {pad});
        if (vm_read16(pad + 0xC) & 1u) {   /* Select went down this frame */
            const uint32_t was = vm_read32(menu + 0x10), now = (was + 1) % 3;
            vm_write32(menu + 0x10, now);
            vm_write8(draw + 0x60, now == 1 ? 1 : 0);   /* the window's focus */
            if ((was == 1) != (now == 1))
                for (uint32_t source : {0u, 1u, 3u}) call_game(ctx, now == 1 ? DES_AT(kPadOff) : DES_AT(kPadOn), {0, source});
        }
        if (vm_read32(menu + 0x10) == 1) {
            vm_write32(pad + 4, 0xFFFFFFFFu);
            call_virtual(ctx, root, 8, {root, pad, 1});
        }
    }
    call_game(ctx, DES_AT(kDrawUpdate), {draw});
    const double f1 = ctx->fpr[1];
    ctx->fpr[1] = f1 > 0.0 && f1 < 0.25 ? f1 : 1.0 / 30.0;
    call_game(ctx, DES_AT(kDrawTimer), {draw});
    ctx->fpr[1] = f1;
    if (!vm_read32(menu + 0x10)) return;
    call_game(ctx, DES_AT(kDrawBox), {draw});
    vm_write32(draw + 4, 0xFFFFFFFFu);
    call_virtual(ctx, root, 0xC, {root, draw, 1});
}

/* A camera's 4x4 matrix (64 bytes at +0x10) onto another camera. */
void copy_matrix(uint32_t from, uint32_t to)
{
    if (!guest_ok(from) || !guest_ok(to)) return;
    for (uint32_t k = 0; k < 0x40; k += 4) vm_write32(to + 0x10 + k, vm_read32(from + 0x10 + k));
}

/* The free camera (the 0x0014422C hook): the frame step r31 has just asked for pad device 0 (r3). The step's +0x34 is
 * its cameras (made by 0x0021D280 at each area's load): +0x8 the game's camera, +0xC the one drawn from in mode 3,
 * +0x204 the developers' debug camera, and +0x1FC the debug camera's mode. The retail game still makes the debug
 * camera and still draws from it while the mode isn't 0 (the frame step at 0x001446FC, the cameras' update at
 * 0x00219CD8); only the code that set the mode and moved the camera was cut. This puts that back:
 *   - the keys are the game's own pad state (pad +4): +0x120 bit 9 Cross held, +0x11E 0x0A L3 pressed now with no
 *     shoulder button, +0x122 bit 4 Square;
 *   - mode 1 and 2 move the debug camera with the game's DbgCamControls; +0x44 = 1 keeps the frame step on its
 *     debug-camera path, where the character takes no input;
 *   - mode 1 stops time with the frame step's pause flags (+0x58, +0x59), except while Square is held;
 *   - mode 3 draws from where the debug camera is, through +0xC, while the game plays on;
 *   - mode 0 keeps the debug camera on the game's, so it starts from the view. */
void free_camera(ppu_context* ctx)
{
    const uint32_t step = (uint32_t)ctx->gpr[31], pad = (uint32_t)ctx->gpr[3];
    const uint32_t keys = rd(pad + 4), cams = rd(step + 0x34);
    if (!guest_ok(keys) || !guest_ok(cams)) return;
    const uint32_t cam = rd(cams + 0x204);
    if (!guest_ok(cam)) return;
    if (cams != s_cam_cams) {   /* a new area's cameras: their constructor set the mode to 0 */
        s_cam_cams = cams;
        s_cam_stopped = false;
    }
    uint32_t mode = vm_read32(cams + 0x1FC) & 3u;
    if ((vm_read16(keys + 0x120) >> 9 & 1u) && vm_read16(keys + 0x11E) == 0x0A) {   /* Cross held, L3 pressed */
        mode = (mode + 1) % 4;
        vm_write32(cams + 0x1FC, mode);
        static const char* const kModes[] = {"off", "free, time stopped", "free, time runs", "the view stays"};
        std::fprintf(stderr, "[des] free camera: %s\n", kModes[mode]);
    }
    bool stop = false;
    switch (mode) {
    case 0:
        copy_matrix(rd(cams + 8), cam);
        break;
    case 1:
        stop = !(vm_read16(keys + 0x122) >> 4 & 1u);   /* Square held lets the frames through */
        [[fallthrough]];
    case 2:
        vm_write32(step + 0x44, 1);
        call_game(ctx, DES_AT(kDbgCamControls), {cam, pad, 0, 0});
        break;
    default:
        copy_matrix(cam, rd(cams + 0xC));
        break;
    }
    if (stop || s_cam_stopped) {   /* (the flags are left alone unless we stopped time) */
        vm_write8(step + 0x58, stop ? 1 : 0);
        vm_write8(step + 0x59, stop ? 1 : 0);
        s_cam_stopped = stop;
    }
}

} // namespace

/* Called once from ps3_title_options_init, after settings.ini and before the network modules start: the knobs, the
 * offline rule and the save copy (the top). */
extern "C" void des_dbgmenu_boot(void)
{
    const bool menu = knob_on("DES_DBGMENU"), title = knob_on("DES_DBGMENU_TITLE"), cam = knob_on("DES_FREECAM");
    if (!menu && !title && !cam) return;
    des_ui_net_off(menu ? "Off: the debug menu is on (DES_DBGMENU)"
                   : title ? "Off: the debug menu is on (DES_DBGMENU_TITLE)"
                           : "Off: the free camera is on");
    if (cam) {
        s_cam = true;
        std::fprintf(stderr, "[des] free camera on (DES_FREECAM): this session is offline\n");
    }
    if (!menu && !title) return;
    std::string where;
    if (!use_save_copy(&where)) {
        std::fprintf(stderr, "[des] debug menu: stays off: %s. This session is offline all the same\n", where.c_str());
        return;
    }
    s_menu = menu;
    s_title = title;
    std::fprintf(stderr, "[des] debug menu on (%s%s%s): this session is offline, and the game saves to %s, a copy of "
                 "the saves made now\n", menu ? "DES_DBGMENU" : "", menu && title ? ", " : "",
                 title ? "DES_DBGMENU_TITLE" : "", where.c_str());
    if (const char* t = std::getenv("PS3_TITLE"); t && *t) {   /* the window's caption says so too */
        const std::string caption = std::string(t) + " (debug menu)";
        ps3_setenv("PS3_TITLE", caption.c_str());
    }
}

/* Every hook in port/insn_hooks.txt's debug menu block. */
extern "C" void des_dbgmenu_hook(ppu_context* ctx, uint32_t addr)
{
    switch (addr) {
    case 0x00020F44u: {   /* create_debug_menu has returned the menu in r3 */
        const uint32_t menu = (uint32_t)ctx->gpr[3];
        if (!s_menu.load(std::memory_order_relaxed) || !guest_ok(menu) || rd(menu + 4)) break;
        const uint32_t root = call_game(ctx, DES_AT(kNewLabel), {DES_AT(kRootText)});
        if (!guest_ok(root)) break;
        vm_write32(menu + 4, root);
        s_obj.store(menu, std::memory_order_relaxed);
        std::fprintf(stderr, "[des] debug menu ready: Select shows it\n");
        break;
    }
    case 0x0016F42Cu: {   /* the title step's memory (r3, 0 when out of memory); the beq after this skips 0x0018B7D0 */
        const uint32_t step = (uint32_t)ctx->gpr[3];
        if (!s_title.load(std::memory_order_relaxed) || !guest_ok(step)) break;
        call_game(ctx, DES_AT(kTitleStep), {step, 0, 1});
        ppu_cr_set(ctx, 7, 2u);   /* EQ: past the title step's own constructor; r31 = r3 = the step still */
        std::fprintf(stderr, "[des] the title's debug menu\n");
        break;
    }
    case 0x0001BB38u:
        if (s_obj.load(std::memory_order_relaxed)) frame(ctx);
        break;
    case 0x0014422Cu:   /* the frame step's bl GetPadDeviceForIdx(0) has returned the pad device (r3) */
        if (s_cam.load(std::memory_order_relaxed)) free_camera(ctx);
        break;
    default:
        break;
    }
}
