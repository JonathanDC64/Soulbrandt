/* More memory for mods (docs/143 section 4, docs/59): video memory and the file loader's heap.
 *
 * Video memory. The game keeps its textures, vertex and index buffers and render targets in one heap over the RSX's
 * local memory (LOCAL), and its size is a constant: 248 MB, 8 MB less than a PS3's 256 MB (func_0002F270 is
 * `lis r3,0x0F80`, read once at the start). Big texture mods fill it (issue #635: black shapes at the 1-1 boss with
 * six mods on). The runtime can give local memory more room (PS3_LOCAL_MB, ps3recomp's vm.h): it ends where the PPU
 * stacks start, so a bigger one starts lower. The game learns the base from cellGcmGetConfiguration, and
 * des_vram_heap gives its heap the new size less the same 8 MB. The cost is RAM: the GPU write-back maps all of
 * local memory, so each 256 MB more is about 256 MB more.
 *
 * The file loader's heap. CORERES (12.25 MB, func_0003B3B8) holds each file while it loads, the whole file at once
 * (docs/59 1.5). A mod's texture file four times the size of the disc's doesn't fit, and the loader waits for room
 * forever: the loading screen never ends. des_coreres_size gives its three sizes (what it takes from the game's
 * memory window, its arena, the range the memory manager records) one bigger size. It must hold the largest file
 * unpacked, plus a little (measured: a 35.3 MB file loads with 40 MB, not with 32), so with mods it is 64 MB, or
 * the mods' largest file (mods.cpp) plus 8 MB when that is more. It comes from the game's memory window (ps3recomp's
 * sys_memory, 256 MB), where the game's other blocks take 152.75 MB: 96 MB is the most it can be.
 *
 * When: with any mod on (PS3_VFS_MODS, mods.cpp), both bigger. Without mods, a PS3's sizes, so the original layout
 * stays everyone else's.
 *
 * Knobs:
 *   DES_VRAM_MB=<256-1024>      video memory of that size, with or without mods (256 keeps a PS3's, for comparisons)
 *   DES_CORERES_MB=<12-96>      the file loader's heap of that size, with or without mods (12 keeps the game's)
 *   PS3_LOCAL_MB                the runtime's own knob; when set, it is kept as it is
 */
#include "ps3emu/ppu_lift.h"   /* ppu_context */

#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" uint32_t vm_rsx_local_size(void);   /* ps3recomp runtime/memory/vm.h */
extern "C" void ps3_setenv(const char* name, const char* value);   /* boot_main.cpp */
extern "C" void des_ui_mods_early(void);   /* des_ui.h: the mods in use, found now (PS3_VFS_MODS) */
extern "C" unsigned long long des_ui_mods_largest_load(char* which, unsigned n);   /* des_ui.h (mods.cpp) */

namespace {

constexpr unsigned kModsVramMB = 512;
constexpr unsigned kModsCoreresMB = 64;   /* the file loader's heap with mods: at least this */
constexpr unsigned kCoreresSpareMB = 8;   /* beside the mods' largest file */
constexpr unsigned kCoreresMostMB = 96;   /* what the game's memory window has room for */
constexpr uint32_t kHeapLess = 8u << 20;   /* LOCAL is local memory less 8 MB, as on a PS3 */
constexpr uint32_t kCoreresGame = 0xC4u << 16;   /* 12.25 MB */

uint32_t s_coreres = 0;   /* the file loader's heap in bytes; 0: the game's own */

/* DES_CORERES_MB's size in bytes, or 0 for none or a bad one (said once). */
uint32_t coreres_knob(const char* v)
{
    char* end = nullptr;
    const unsigned long mb = std::strtoul(v, &end, 10);
    if (end == v || *end || mb < 12 || mb > kCoreresMostMB) {
        std::fprintf(stderr, "[des] DES_CORERES_MB=%s is not a size from 12 to %u (MB): the game's 12.25 MB\n", v,
                     kCoreresMostMB);
        return 0;
    }
    return mb == 12 ? 0 : uint32_t(mb) << 20;
}

/* The file loader's heap for the mods in use: 64 MB, or their largest file plus 8 MB when that is more, 96 MB at
 * most (said when a file needs more). */
uint32_t coreres_for_mods()
{
    char which[512];
    const unsigned long long need = des_ui_mods_largest_load(which, sizeof which);
    const unsigned long long want = ((need + (1u << 20) - 1) >> 20) + kCoreresSpareMB;
    unsigned mb = kModsCoreresMB;
    if (want > mb) mb = want > kCoreresMostMB ? kCoreresMostMB : unsigned(want);
    if (need)
        std::fprintf(stderr, "[des] the file loader's heap: %u MB, for the mods (their largest file: %.1f MB, %s; "
                     "DES_CORERES_MB=12 keeps the game's)\n", mb, need / 1048576.0, which);
    else
        std::fprintf(stderr, "[des] the file loader's heap: %u MB, for the mods (DES_CORERES_MB=12 keeps the game's)\n",
                     mb);
    if (want > kCoreresMostMB)
        std::fprintf(stderr, "[des] %s takes %.1f MB to load, more than the file loader's heap can be (%u MB): the "
                     "loading screen may never end\n", which, need / 1048576.0, kCoreresMostMB);
    return uint32_t(mb) << 20;
}

}  // namespace

/* Before the runtime reserves the guest space: from ps3_title_launch, once the mods in use are known. */
extern "C" void des_vram_choose(void)
{
    des_ui_mods_early();   /* (the Mods window's choice: the settings come later, after the guest space is reserved) */
    const char* forced = std::getenv("DES_VRAM_MB");
    const char* given = std::getenv("PS3_LOCAL_MB");
    const char* coreres = std::getenv("DES_CORERES_MB");
    const char* mods = std::getenv("PS3_VFS_MODS");
    const bool modded = mods && *mods;
    if (forced && *forced) {
        ps3_setenv("PS3_LOCAL_MB", forced);
        std::fprintf(stderr, "[des] video memory: %s MB (DES_VRAM_MB)\n", forced);
    } else if (given && *given) {
        std::fprintf(stderr, "[des] video memory: %s MB (PS3_LOCAL_MB)\n", given);
    } else if (modded) {
        char mb[16];
        std::snprintf(mb, sizeof mb, "%u", kModsVramMB);
        ps3_setenv("PS3_LOCAL_MB", mb);
        std::fprintf(stderr, "[des] video memory: %u MB, for the mods (DES_VRAM_MB=256 keeps a PS3's 256)\n",
                     kModsVramMB);
    }
    if (coreres && *coreres) {
        s_coreres = coreres_knob(coreres);
        if (s_coreres) std::fprintf(stderr, "[des] the file loader's heap: %u MB (DES_CORERES_MB)\n", s_coreres >> 20);
    } else if (modded) {
        s_coreres = coreres_for_mods();
    }
}

/* After `lis r3,0x0F80` in func_0002F270 (insn_hooks.txt): the size the game gives its LOCAL heap. */
extern "C" void des_vram_heap(ppu_context* ctx, uint32_t)
{
    const uint32_t local = vm_rsx_local_size();
    if (local <= 0x10000000u) return;   /* a PS3's 256 MB: the game's own 248 */
    ctx->gpr[3] = local - kHeapLess;
    static bool said = false;
    if (!said) {
        said = true;
        std::fprintf(stderr, "[des] the game's video memory heap: %u MB of %u MB\n", (local - kHeapLess) >> 20,
                     local >> 20);
    }
}

/* After each `lis rN,0xC4` in CORERES's creator func_0003B3B8 (insn_hooks.txt): r3 at the first (the size it takes
 * from the game's memory window), r5 at the other two (its arena, and the range the memory manager records). */
extern "C" void des_coreres_size(ppu_context* ctx, uint32_t addr)
{
    if (!s_coreres) return;
    const int reg = addr == 0x0003B3FCu ? 3 : 5;
    if (uint32_t(ctx->gpr[reg]) != kCoreresGame) return;   /* (not the size it expects: left alone) */
    ctx->gpr[reg] = s_coreres;
    if (reg == 3) std::fprintf(stderr, "[des] the file loader's heap (CORERES): %u MB in place of 12.25\n", s_coreres >> 20);
}
