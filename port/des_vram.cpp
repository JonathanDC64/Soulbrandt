/* More memory for mods (docs/143 section 4, docs/59): video memory, the game's memory window and its heaps.
 *
 * Video memory. The game keeps its textures, vertex and index buffers and render targets in one heap over the RSX's
 * local memory (LOCAL), and its size is a constant: 248 MB, 8 MB less than a PS3's 256 MB (func_0002F270 is
 * `lis r3,0x0F80`, read once at the start). Big texture mods fill it (issue #635: black shapes at the 1-1 boss with
 * six mods on). The runtime can give local memory more room (PS3_LOCAL_MB, ps3recomp's vm.h): it ends where the PPU
 * stacks start, so a bigger one starts lower. The game learns the base from cellGcmGetConfiguration, and
 * des_vram_heap gives its heap the new size less the same 8 MB. With mods it is the most there is, 1 GB: the runtime
 * commits only the first 256 MB up front (the part the GPU write-back maps), and the rest as the game fills it, so
 * the PC pays only for what the mods use.
 *
 * The game's memory window. The game asks the OS for 15 blocks at boot, its heaps among them, from ps3recomp's
 * sys_memory window: 256 MB from 0x40000000, as on a PS3. With mods it is 511 MB from 0x30100000 (PS3_SYSMEM_MB,
 * where RPCS3 puts these blocks too), so the heaps can be bigger.
 *
 * The heaps. Every MemMan heap's size is a constant in its creator, three `lis` (docs/59 1.1): the size it takes
 * from the window (r3), its arena's (r5), and the range the memory manager records (r5). des_heap_size gives the
 * eight that hold game data a bigger size with mods on (kHeaps): MAIN, the graphics system's, Havok's, effects,
 * sound, Lua, menus and TEMP. They measured 15-84% full with the 2x every-world mod, so this is room for heavier
 * models, areas, effects and sounds, not a fix for one seen today.
 *
 * The file loader's heap. CORERES (12.25 MB, func_0003B3B8) holds each file while it loads, the whole file at once
 * (docs/59 1.5). A mod's texture file four times the size of the disc's doesn't fit, and the loader waits for room
 * forever: the loading screen never ends. des_coreres_size gives its three sizes one bigger size. It must hold the
 * largest file unpacked, plus a little (measured: a 35.3 MB file loads with 40 MB, not with 32), so with mods it is
 * 64 MB, or the mods' largest file (mods.cpp) plus 8 MB when that is more. It takes what the window has left after
 * the other blocks.
 *
 * When: with any mod on (PS3_VFS_MODS, mods.cpp), all of it. Without mods, a PS3's sizes, so the original layout
 * stays everyone else's.
 *
 * Knobs:
 *   DES_VRAM_MB=<256-1024>      video memory of that size, with or without mods (256 keeps a PS3's, for comparisons)
 *   DES_SYSMEM_MB=<256-511>     the game's memory window of that size, with or without mods (256 keeps a PS3's)
 *   DES_MODS_HEAPS=0            the game's own heap sizes with mods on (the window and CORERES still grow)
 *   DES_CORERES_MB=<12-...>     the file loader's heap of that size, with or without mods (12 keeps the game's)
 *   PS3_LOCAL_MB, PS3_SYSMEM_MB the runtime's own knobs; when set, they are kept as they are
 */
#include "ps3emu/ppu_lift.h"   /* ppu_context */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" uint32_t vm_rsx_local_size(void);   /* ps3recomp runtime/memory/vm.h */
extern "C" uint32_t vm_sysmem_size(void);      /* ps3recomp runtime/memory/vm.h: the sys_memory window */
extern "C" void ps3_setenv(const char* name, const char* value);   /* boot_main.cpp */
extern "C" void des_ui_mods_early(void);   /* des_ui.h: the mods in use, found now (PS3_VFS_MODS) */
extern "C" unsigned long long des_ui_mods_largest_load(char* which, unsigned n);   /* des_ui.h (mods.cpp) */

namespace {

constexpr unsigned kModsVramMB = 1024;   /* the most cellGcm's offset table covers; RAM only as it is used */
constexpr unsigned kModsSysmemMB = 511;  /* from 0x30100000, right above libsre (ps3recomp vm.h) */
constexpr unsigned kModsCoreresMB = 64;  /* the file loader's heap with mods: at least this */
constexpr unsigned kCoreresSpareMB = 8;  /* beside the mods' largest file */
constexpr uint32_t kHeapLess = 8u << 20;   /* LOCAL is local memory less 8 MB, as on a PS3 */
constexpr uint32_t kCoreresGame = 0xC4u << 16;   /* 12.25 MB */
/* The game's 15 blocks other than CORERES, at the game's sizes (docs/59 1.1: 163.9 MB less CORERES's 12.25, less
 * the libc block the port skips), and the slack the window's 1 MB alignment and the page allocator's reserve take. */
constexpr uint32_t kOtherBlocks = 0x98C0000u;   /* 152.75 MB */
constexpr uint32_t kWindowSlack = 4u << 20;

/* A MemMan heap the mods may need more of: the game's size, and the size with mods. Its creator's three `lis` are
 * des_heap_size's cases. */
struct Heap {
    const char* name;
    uint32_t game, mods;
};
constexpr uint32_t MB(double mb) { return uint32_t(mb * 1048576.0); }
const Heap kHeaps[] = {
    {"MAIN", 0x5600000u, MB(160)},
    {"HGSYS", 0x0D00000u, MB(32)},
    {"HAVOK", 0x1400000u, MB(40)},
    {"FFX", 0x0900000u, MB(20)},
    {"MO", 0x0400000u, MB(12)},
    {"LUA", 0x0260000u, MB(6)},   /* 64 KB pages */
    {"MENU", 0x0200000u, MB(6)},
    {"TEMP", 0x0100000u, MB(3)},
};

bool s_heaps = false;     /* the heaps at kHeaps' mods sizes */
uint32_t s_coreres = 0;   /* the file loader's heap in bytes; 0: the game's own */

/* What the heaps grow by with mods on. */
uint32_t heaps_growth()
{
    uint32_t g = 0;
    for (const Heap& h : kHeaps) g += h.mods - h.game;
    return g;
}

/* The most the file loader's heap can be: what the window has left after the game's other blocks. */
uint32_t coreres_most()
{
    const uint32_t others = kOtherBlocks + (s_heaps ? heaps_growth() : 0) + kWindowSlack;
    const uint32_t window = vm_sysmem_size();
    const uint32_t room = window > others ? window - others : 0;
    return room > kCoreresGame ? room & ~0xFFFFFu : kCoreresGame;
}

/* A size in whole MB from a knob, from lo to hi, or 0 for a bad one (said once). */
unsigned mb_knob(const char* name, const char* v, unsigned lo, unsigned hi)
{
    char* end = nullptr;
    const unsigned long mb = std::strtoul(v, &end, 10);
    if (end == v || *end || mb < lo || mb > hi) {
        std::fprintf(stderr, "[des] %s=%s is not a size from %u to %u (MB): left as it is\n", name, v, lo, hi);
        return 0;
    }
    return unsigned(mb);
}

/* The file loader's heap for the mods in use: 64 MB, or their largest file plus 8 MB when that is more, what the
 * window has left at most (said when a file needs more). */
uint32_t coreres_for_mods()
{
    char which[512];
    const unsigned long long need = des_ui_mods_largest_load(which, sizeof which);
    const unsigned long long want = ((need + (1u << 20) - 1) >> 20) + kCoreresSpareMB;
    const unsigned most = coreres_most() >> 20;
    unsigned mb = kModsCoreresMB < most ? kModsCoreresMB : most;
    if (want > mb) mb = want > most ? most : unsigned(want);
    if (need)
        std::fprintf(stderr, "[des] the file loader's heap: %u MB, for the mods (their largest file: %.1f MB, %s; "
                     "DES_CORERES_MB=12 keeps the game's)\n", mb, need / 1048576.0, which);
    else
        std::fprintf(stderr, "[des] the file loader's heap: %u MB, for the mods (DES_CORERES_MB=12 keeps the game's)\n",
                     mb);
    if (want > most)
        std::fprintf(stderr, "[des] %s takes %.1f MB to load, more than the file loader's heap can be (%u MB): the "
                     "loading screen may never end\n", which, need / 1048576.0, most);
    return uint32_t(mb) << 20;
}

/* name=value for the runtime, unless that knob is set already. */
void set_unless_given(const char* name, unsigned mb, const char* what)
{
    if (const char* given = std::getenv(name); given && *given) {
        std::fprintf(stderr, "[des] %s: %s MB (%s)\n", what, given, name);
        return;
    }
    char v[16];
    std::snprintf(v, sizeof v, "%u", mb);
    ps3_setenv(name, v);
}

}  // namespace

/* Before the runtime reserves the guest space: from ps3_title_launch, once the mods in use are known. */
extern "C" void des_vram_choose(void)
{
    des_ui_mods_early();   /* (the Mods window's choice: the settings come later, after the guest space is reserved) */
    const char* mods = std::getenv("PS3_VFS_MODS");
    const bool modded = mods && *mods;

    const char* vram = std::getenv("DES_VRAM_MB");
    if (const unsigned mb = vram && *vram ? mb_knob("DES_VRAM_MB", vram, 256, 1024) : 0) {
        set_unless_given("PS3_LOCAL_MB", mb, "video memory");
        std::fprintf(stderr, "[des] video memory: %u MB (DES_VRAM_MB)\n", mb);
    } else if (modded) {
        set_unless_given("PS3_LOCAL_MB", kModsVramMB, "video memory");
        std::fprintf(stderr, "[des] video memory: up to %u MB, for the mods (DES_VRAM_MB=256 keeps a PS3's 256)\n",
                     kModsVramMB);
    }

    const char* sysmem = std::getenv("DES_SYSMEM_MB");
    if (const unsigned mb = sysmem && *sysmem ? mb_knob("DES_SYSMEM_MB", sysmem, 256, 511) : 0) {
        set_unless_given("PS3_SYSMEM_MB", mb, "the game's memory window");
        std::fprintf(stderr, "[des] the game's memory window: %u MB (DES_SYSMEM_MB)\n", mb);
    } else if (modded) {
        set_unless_given("PS3_SYSMEM_MB", kModsSysmemMB, "the game's memory window");
        std::fprintf(stderr, "[des] the game's memory window: %u MB, for the mods (DES_SYSMEM_MB=256 keeps a PS3's)\n",
                     kModsSysmemMB);
    }

    const char* heaps = std::getenv("DES_MODS_HEAPS");
    s_heaps = modded && !(heaps && std::strcmp(heaps, "0") == 0);
    if (s_heaps) {
        /* Only if the window has room for them beside a 64 MB file loader's heap (DES_SYSMEM_MB=256 has not). */
        if (vm_sysmem_size() < kOtherBlocks + heaps_growth() + kWindowSlack + (uint32_t(kModsCoreresMB) << 20)) {
            s_heaps = false;
            std::fprintf(stderr, "[des] the game's heaps: its own sizes (the memory window has no room for more)\n");
        } else {
            std::fprintf(stderr, "[des] the game's heaps, for the mods:");
            for (const Heap& h : kHeaps) std::fprintf(stderr, " %s %u MB", h.name, h.mods >> 20);
            std::fprintf(stderr, " (DES_MODS_HEAPS=0 keeps the game's)\n");
        }
    }

    const char* coreres = std::getenv("DES_CORERES_MB");
    if (coreres && *coreres) {
        const unsigned mb = mb_knob("DES_CORERES_MB", coreres, 12, coreres_most() >> 20);
        s_coreres = mb > 12 ? uint32_t(mb) << 20 : 0;
        if (s_coreres) std::fprintf(stderr, "[des] the file loader's heap: %u MB (DES_CORERES_MB)\n", mb);
    } else if (modded) {
        s_coreres = coreres_for_mods();
    }
}

/* After `lis r3,0x0F80` in func_0002F270 (insn_hooks.txt): the size the game gives its LOCAL heap. */
extern "C" void des_vram_heap(ppu_context* ctx, uint32_t)
{
    const uint32_t local = vm_rsx_local_size();
    if (local <= 0x10000000u) return;   /* a PS3's 256 MB: the game's own 248 */
    if (uint32_t(ctx->gpr[3]) != 0x0F800000u) return;   /* (not the size it expects: left alone) */
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

/* After each of the three `lis` in the creators of kHeaps' heaps (insn_hooks.txt; the US address): r3 at the first
 * (k 0), r5 at the other two, as in CORERES's. */
extern "C" void des_heap_size(ppu_context* ctx, uint32_t addr)
{
    if (!s_heaps) return;
    int h, k;
    switch (addr) {
    case 0x0003FBFCu: h = 0; k = 0; break;   /* MAIN */
    case 0x0003FC44u: h = 0; k = 1; break;   /* MAIN */
    case 0x0003FCB0u: h = 0; k = 2; break;   /* MAIN */
    case 0x00031A14u: h = 2; k = 0; break;   /* HAVOK */
    case 0x00031A5Cu: h = 2; k = 1; break;   /* HAVOK */
    case 0x00031AC8u: h = 2; k = 2; break;   /* HAVOK */
    case 0x0003D7FCu: h = 1; k = 0; break;   /* HGSYS */
    case 0x0003D844u: h = 1; k = 1; break;   /* HGSYS */
    case 0x0003D8B0u: h = 1; k = 2; break;   /* HGSYS */
    case 0x0003C5FCu: h = 3; k = 0; break;   /* FFX */
    case 0x0003C644u: h = 3; k = 1; break;   /* FFX */
    case 0x0003C6B0u: h = 3; k = 2; break;   /* FFX */
    case 0x0003167Cu: h = 4; k = 0; break;   /* MO */
    case 0x000316C4u: h = 4; k = 1; break;   /* MO */
    case 0x00031730u: h = 4; k = 2; break;   /* MO */
    case 0x00038FFCu: h = 5; k = 0; break;   /* LUA */
    case 0x00039044u: h = 5; k = 1; break;   /* LUA */
    case 0x000390B0u: h = 5; k = 2; break;   /* LUA */
    case 0x00041FFCu: h = 6; k = 0; break;   /* MENU */
    case 0x00042044u: h = 6; k = 1; break;   /* MENU */
    case 0x000420B0u: h = 6; k = 2; break;   /* MENU */
    case 0x00031874u: h = 7; k = 0; break;   /* TEMP */
    case 0x000318BCu: h = 7; k = 1; break;   /* TEMP */
    case 0x00031928u: h = 7; k = 2; break;   /* TEMP */
    default: return;
    }
    const int reg = k == 0 ? 3 : 5;
    if (uint32_t(ctx->gpr[reg]) != kHeaps[h].game) return;   /* (not the size it expects: left alone) */
    ctx->gpr[reg] = kHeaps[h].mods;
}
