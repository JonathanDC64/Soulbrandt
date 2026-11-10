/* Room for Lua when its heap is full.
 *
 * Each of the game's Lua states takes its memory from a small-block pool
 * (0x00B0FCE8, at L+0x60), and the pool takes its pages and every block over
 * 128 bytes from a heap object. Two heap classes stand behind the game's Lua
 * states, and each hands everything to one fixed arena of its own:
 *   vtable 0x01852D20 (object 0x01AD66B0): the two script systems' states
 *     (des_luagc.cpp), arena of 0x260000 bytes (2.375 MB) made by 0x00038FB8;
 *   vtable 0x01852EC0 (object 0x01AD68D0): a third, small state, arena of
 *     0x900000 bytes (9 MB) made by 0x0003C5B8.
 * The two classes are one template over a different arena: the same methods
 * at the same vtable offsets, forwarding to the same functions.
 *
 * When an arena can't give a block, the pool returns 0, luaM_realloc
 * (0x00CBA290) throws LUA_ERRMEM, nothing catches it on a character's script,
 * and Lua calls exit(1): the window closes. Above 30 fps the scripts' garbage
 * piles up faster than the game's timed collections clear it, and
 * des_luagc.cpp collects early, which keeps them far from the limit. This is
 * the net under that.
 *
 * Every call to these classes goes through their vtables (nothing calls the
 * methods directly), so each method is wrapped (ppu_register_function). An
 * allocation the arena can't give comes from the port's own guest heap
 * (des_heap.cpp) instead, and is remembered. Free, realloc, a block's size and
 * "is it yours" send those blocks back there, and pass every other block to the
 * game's own method unchanged. Until an arena is full nothing here changes
 * what the game does. The pool finds a block's page by its address range, not
 * by its alignment, so pages from the port's heap work the same way. Nothing
 * here collects: Lua 5.0 can't collect safely inside its allocator.
 *
 * The methods (vtable offset; US address in 0x01852D20, in 0x01852EC0):
 *   +0x28, +0x40  alloc(size)                  0x00039210 0x00039A10, 0x0003D548 0x0003D478
 *   +0x2C, +0x44  alloc(size, align)           0x0003A0D8 0x00039D50, 0x0003CEE8 0x0003CA98
 *   +0x30, +0x48  realloc(block, size)         0x00039BB0 0x00039790, 0x0003D208 0x0003D618
 *   +0x34, +0x4C  realloc(block, size, align)  0x00039F20 0x00039E30, 0x0003C9A8 0x0003CDF8
 *   +0x38, +0x50  free(block)                  0x00039940 0x00039AE0, 0x0003C758 0x0003D2E8
 *   +0x24         size of a block              0x00039540,            0x0003CB78
 *   +0x58         the block is the arena's     0x00039870,            0x0003CC48
 * The game's realloc keeps the old block when the new one can't be had.
 *
 * Which heaps: the game's lua_open (0x00CC0558) takes the heap, builds the
 * state's pool over it, and its threads share that pool. Only the state's
 * constructor (0x00A6E5A8) calls it, from six places. Four (the script
 * systems) read one global heap pointer, which holds 0x01AD66B0. The other
 * two are "new state on this heap" methods, each reached only through its
 * class's vtable: 0x00A7CC60, and 0x01610188 in the code with the second TOC
 * (it calls the constructor through a TOC stub). Both are wrapped too: each
 * says in the log when a state lands on a heap these classes don't cover (a
 * full heap there would still end the game), and with DES_LUAGC_LOG it names
 * every heap. The script systems' calls are direct and can't be wrapped, so
 * des_luagc.cpp checks each new state of theirs the same way
 * (des_luaheap_check).
 *
 * DES_LUAHEAP=0        off: the game's methods only (a full arena ends the game again)
 * DES_LUAHEAP_TEST=n   TEST: every n-th allocation comes from the port's heap even with
 *                      room in the arena, so both kinds of block are in use together.
 */
#include "ps3emu/ppu_lift.h"
#include "des_region.h"       /* DES_REGISTER_FUNCTION: the running disc's addresses */
#include "des_game_calls.h"   /* the classes' own methods (the game DLL's with DES_SPLIT) */
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

extern "C" uint8_t* vm_base;                     /* the flat guest address space */
extern "C" void ds_memalign(ppu_context* ctx);   /* des_heap.cpp: (align, size) -> block */
extern "C" void ds_free(ppu_context* ctx);       /* des_heap.cpp: (block) */

namespace {

/* 0x01852D20, the script systems' heap */
constexpr uint32_t kAlloc = 0x00039210u, kAlloc2 = 0x00039A10u;
constexpr uint32_t kAllocAligned = 0x0003A0D8u, kAllocAligned2 = 0x00039D50u;
constexpr uint32_t kRealloc = 0x00039BB0u, kRealloc2 = 0x00039790u;
constexpr uint32_t kReallocAligned = 0x00039F20u, kReallocAligned2 = 0x00039E30u;
constexpr uint32_t kFree = 0x00039940u, kFree2 = 0x00039AE0u;
constexpr uint32_t kSizeOf = 0x00039540u, kOwns = 0x00039870u;
/* 0x01852EC0, the third state's heap */
constexpr uint32_t kSmallAlloc = 0x0003D548u, kSmallAlloc2 = 0x0003D478u;
constexpr uint32_t kSmallAllocAligned = 0x0003CEE8u, kSmallAllocAligned2 = 0x0003CA98u;
constexpr uint32_t kSmallRealloc = 0x0003D208u, kSmallRealloc2 = 0x0003D618u;
constexpr uint32_t kSmallReallocAligned = 0x0003C9A8u, kSmallReallocAligned2 = 0x0003CDF8u;
constexpr uint32_t kSmallFree = 0x0003C758u, kSmallFree2 = 0x0003D2E8u;
constexpr uint32_t kSmallSizeOf = 0x0003CB78u, kSmallOwns = 0x0003CC48u;
/* The two new_state(this, heap) methods, and where a heap class keeps alloc(size) */
constexpr uint32_t kNewState = 0x00A7CC60u, kNewState2 = 0x01610188u;
constexpr uint32_t kAllocSlot = 0x28;
constexpr uint32_t kStatePool = 0x60, kPoolHeap = 0x04;   /* lua_State -> its pool -> the pool's heap */
constexpr uint32_t kMinAlign = 16;

/* A class's own methods for the arena's side of a move: alloc, alloc aligned, a block's size, free. */
struct Game {
    void (*alloc)(ppu_context*);
    void (*alloc_aligned)(ppu_context*);
    void (*size)(ppu_context*);
    void (*free)(ppu_context*);
};
constexpr Game kScripts{func_00039210, func_0003A0D8, func_00039540, func_00039940};
constexpr Game kSmall{func_0003D548, func_0003CEE8, func_0003CB78, func_0003C758};

std::mutex s_mtx;
std::unordered_map<uint32_t, uint32_t> s_ours;   /* block -> size, for blocks from the port's heap */
std::atomic<uint32_t> s_count{0};                /* s_ours.size(), read without the lock */
uint64_t s_bytes = 0, s_peak = 0, s_total = 0;   /* under s_mtx */
uint32_t s_test = 0;                             /* DES_LUAHEAP_TEST */
std::atomic<uint32_t> s_test_n{0};
bool s_on = false;                               /* the net is in (not DES_LUAHEAP=0) */
bool s_log = false;                              /* DES_LUAGC_LOG: name every heap a new state gets */
std::atomic<uint32_t> s_uncovered{0};            /* states made on a heap no class here covers */

uint32_t reg(ppu_context* ctx, int r) { return (uint32_t)ctx->gpr[r]; }
bool ok(uint32_t a) { return a >= 0x00010000u && a < 0xD0000000u && !(a & 3u); }

uint32_t s_fills = 0, s_empties = 0;             /* under s_mtx: times the port's heap took the first block, gave the last */

/* The log for the n-th time: the 1st, 2nd, 4th, 8th... so a heap that sits at its limit can't flood it. */
bool log_time(uint32_t n) { return (n & (n - 1)) == 0; }

/* A block from the port's heap, remembered. 0 if even that is out (it is 1.5 GB), and for sizes no real block has
 * (ds_memalign adds the alignment to the size). */
uint32_t ours_alloc(uint32_t self, uint32_t size, uint32_t align)
{
    if (size > 0x7FFFFFFFu || align > 0x10000u) return 0;
    ppu_context t{};
    t.gpr[3] = std::max(align, kMinAlign);
    t.gpr[4] = size;
    ds_memalign(&t);
    const uint32_t b = (uint32_t)t.gpr[3];
    if (!b) return 0;
    uint32_t fill = 0;
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        s_ours[b] = size;
        if (s_ours.size() == 1) fill = ++s_fills;
        s_count.store((uint32_t)s_ours.size(), std::memory_order_release);
        s_bytes += size;
        s_peak = std::max(s_peak, s_bytes);
        s_total += size;
    }
    if (fill && !s_test && log_time(fill))
        std::fprintf(stderr, "[des] Lua's heap is full (0x%08X): %u bytes from the port's heap instead (time %u)\n",
                     self, size, fill);
    return b;
}

/* The size of one of ours, or false if the block is the arena's. */
bool ours_size(uint32_t b, uint32_t* size)
{
    if (!b || s_count.load(std::memory_order_acquire) == 0) return false;
    std::lock_guard<std::mutex> lk(s_mtx);
    const auto it = s_ours.find(b);
    if (it == s_ours.end()) return false;
    if (size) *size = it->second;
    return true;
}

/* Gives one of ours back to the port's heap; false if the block is the arena's. */
bool ours_free(uint32_t b)
{
    if (!b || s_count.load(std::memory_order_acquire) == 0) return false;
    uint32_t empty = 0;
    uint64_t peak = 0, total = 0;
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        const auto it = s_ours.find(b);
        if (it == s_ours.end()) return false;
        s_bytes -= it->second;
        s_ours.erase(it);
        s_count.store((uint32_t)s_ours.size(), std::memory_order_release);
        if (s_ours.empty()) {
            empty = ++s_empties;
            peak = s_peak;
            total = s_total;
            s_peak = s_total = 0;
        }
    }
    ppu_context t{};
    t.gpr[3] = b;
    ds_free(&t);
    if (empty && !s_test && log_time(empty))
        std::fprintf(stderr, "[des] Lua's heap has room again: all blocks back (at most %llu bytes at once, %llu in all; time %u)\n",
                     (unsigned long long)peak, (unsigned long long)total, empty);
    return true;
}

/* DES_LUAHEAP_TEST: this allocation comes from the port's heap even if the arena has room. */
bool test_ours()
{
    return s_test && s_test_n.fetch_add(1, std::memory_order_relaxed) % s_test == 0;
}

/* The game's own method, with these arguments after `this`; r3 back. */
uint32_t game(ppu_context* ctx, void (*fn)(ppu_context*), uint32_t self, uint32_t a4, uint32_t a5 = 0, uint32_t a6 = 0)
{
    ctx->gpr[3] = self;
    ctx->gpr[4] = a4;
    ctx->gpr[5] = a5;
    ctx->gpr[6] = a6;
    fn(ctx);
    return reg(ctx, 3);
}

void copy(uint32_t to, uint32_t from, uint32_t n) { std::memmove(vm_base + to, vm_base + from, n); }

/* alloc(this, size) and alloc(this, size, align). */
template <void (*Fn)(ppu_context*), bool Aligned>
void heap_alloc(ppu_context* ctx)
{
    const uint32_t self = reg(ctx, 3), size = reg(ctx, 4), align = Aligned ? reg(ctx, 5) : 0;
    if (size && test_ours()) {
        ctx->gpr[3] = ours_alloc(self, size, align);
        if (reg(ctx, 3)) return;
        ctx->gpr[3] = self;
        ctx->gpr[4] = size;
        ctx->gpr[5] = align;
    }
    Fn(ctx);
    if (!reg(ctx, 3) && size) ctx->gpr[3] = ours_alloc(self, size, align);
}

/* realloc(this, block, size) and realloc(this, block, size, align), as the game's: no block is an alloc, no size
 * a free, and otherwise the bytes move to a new block and the old one goes; if no new block can be had, 0 and the
 * old block stays. */
template <const Game& G, void (*Fn)(ppu_context*), bool Aligned>
void heap_realloc(ppu_context* ctx)
{
    const uint32_t self = reg(ctx, 3), block = reg(ctx, 4), size = reg(ctx, 5), align = Aligned ? reg(ctx, 6) : 0;
    uint32_t old = 0;
    if (ours_size(block, &old)) {
        if (!size) {
            ours_free(block);
            ctx->gpr[3] = 0;
            return;
        }
        uint32_t nb = test_ours() ? ours_alloc(self, size, align) : 0;
        if (!nb) nb = Aligned ? game(ctx, G.alloc_aligned, self, size, align) : game(ctx, G.alloc, self, size);
        if (!nb) nb = ours_alloc(self, size, align);
        if (nb) {
            copy(nb, block, std::min(old, size));
            ours_free(block);
        }
        ctx->gpr[3] = nb;
        return;
    }
    if (block && size && test_ours()) {
        const uint32_t nb = ours_alloc(self, size, align);
        if (nb) {
            copy(nb, block, std::min(game(ctx, G.size, self, block), size));
            game(ctx, G.free, self, block);
            ctx->gpr[3] = nb;
            return;
        }
        ctx->gpr[3] = self;
        ctx->gpr[4] = block;
        ctx->gpr[5] = size;
        ctx->gpr[6] = align;
    }
    Fn(ctx);
    if (reg(ctx, 3) || !size) return;
    /* The arena is full: a block of ours, and the old one (if any) back to the arena. */
    const uint32_t nb = ours_alloc(self, size, align);
    if (nb && block) {
        copy(nb, block, std::min(game(ctx, G.size, self, block), size));
        game(ctx, G.free, self, block);
    }
    ctx->gpr[3] = nb;
}

/* free(this, block) */
template <void (*Fn)(ppu_context*)>
void heap_free(ppu_context* ctx)
{
    if (ours_free(reg(ctx, 4))) return;
    Fn(ctx);
}

/* size of a block (this, block) -> bytes */
template <void (*Fn)(ppu_context*)>
void heap_size(ppu_context* ctx)
{
    uint32_t n = 0;
    if (ours_size(reg(ctx, 4), &n)) {
        ctx->gpr[3] = n;
        return;
    }
    Fn(ctx);
}

/* the block is the heap's (this, block) -> bool */
template <void (*Fn)(ppu_context*)>
void heap_owns(ppu_context* ctx)
{
    if (ours_size(reg(ctx, 4), nullptr)) {
        ctx->gpr[3] = 1;
        return;
    }
    Fn(ctx);
}

/* The heap's class is one of the two wrapped here: its alloc slot holds one of their allocs. *fn gets the heap's
 * alloc for the log (0 if it can't be read). */
bool covered(uint32_t heap, uint32_t* fn)
{
    const uint32_t vt = ok(heap) ? vm_read32(heap) : 0;
    const uint32_t opd = ok(vt) ? vm_read32(vt + kAllocSlot) : 0;
    *fn = ok(opd) ? vm_read32(opd) : 0;
    return *fn == DES_AT(kAlloc) || *fn == DES_AT(kSmallAlloc);
}

/* A word in the log about a new Lua state's heap: always when the net doesn't cover it, with DES_LUAGC_LOG when
 * it does. */
void tell(const char* what, uint32_t heap)
{
    uint32_t fn = 0;
    if (covered(heap, &fn)) {
        if (s_log) std::fprintf(stderr, "[des] %s on heap 0x%08X, which des_luaheap covers\n", what, heap);
        return;
    }
    const uint32_t n = s_uncovered.fetch_add(1, std::memory_order_relaxed) + 1;
    if (s_log || log_time(n))
        std::fprintf(stderr, "[des] %s on heap 0x%08X (alloc 0x%08X), which des_luaheap doesn't cover: "
                     "if that heap fills, the game closes (time %u)\n", what, heap, fn, n);
}

/* new_state(this, heap): the game's, then the word about the heap. */
template <void (*Fn)(ppu_context*)>
void new_state(ppu_context* ctx)
{
    const uint32_t heap = reg(ctx, 4);
    Fn(ctx);
    tell("a new Lua state", heap);
}

} // namespace

/* From ps3_title_options_init, before the guest runs. */
extern "C" void des_luaheap_init(void)
{
    const char* off = std::getenv("DES_LUAHEAP");
    if (off && *off == '0') {
        std::fprintf(stderr, "[des] DES_LUAHEAP=0: a full Lua heap ends the game, as on the PS3\n");
        return;
    }
    if (const char* t = std::getenv("DES_LUAHEAP_TEST")) {
        s_test = (uint32_t)std::strtoul(t, nullptr, 10);
        if (s_test) std::fprintf(stderr, "[des] DES_LUAHEAP_TEST=%u: every %u-th block of Lua's heap from the port's heap (test)\n", s_test, s_test);
    }
    s_on = true;
    DES_REGISTER_FUNCTION(kAlloc,(heap_alloc<func_00039210, false>));
    DES_REGISTER_FUNCTION(kAlloc2, (heap_alloc<func_00039A10, false>));
    DES_REGISTER_FUNCTION(kAllocAligned, (heap_alloc<func_0003A0D8, true>));
    DES_REGISTER_FUNCTION(kAllocAligned2, (heap_alloc<func_00039D50, true>));
    DES_REGISTER_FUNCTION(kRealloc, (heap_realloc<kScripts, func_00039BB0, false>));
    DES_REGISTER_FUNCTION(kRealloc2, (heap_realloc<kScripts, func_00039790, false>));
    DES_REGISTER_FUNCTION(kReallocAligned, (heap_realloc<kScripts, func_00039F20, true>));
    DES_REGISTER_FUNCTION(kReallocAligned2, (heap_realloc<kScripts, func_00039E30, true>));
    DES_REGISTER_FUNCTION(kFree, heap_free<func_00039940>);
    DES_REGISTER_FUNCTION(kFree2, heap_free<func_00039AE0>);
    DES_REGISTER_FUNCTION(kSizeOf, heap_size<func_00039540>);
    DES_REGISTER_FUNCTION(kOwns, heap_owns<func_00039870>);

    DES_REGISTER_FUNCTION(kSmallAlloc, (heap_alloc<func_0003D548, false>));
    DES_REGISTER_FUNCTION(kSmallAlloc2, (heap_alloc<func_0003D478, false>));
    DES_REGISTER_FUNCTION(kSmallAllocAligned, (heap_alloc<func_0003CEE8, true>));
    DES_REGISTER_FUNCTION(kSmallAllocAligned2, (heap_alloc<func_0003CA98, true>));
    DES_REGISTER_FUNCTION(kSmallRealloc, (heap_realloc<kSmall, func_0003D208, false>));
    DES_REGISTER_FUNCTION(kSmallRealloc2, (heap_realloc<kSmall, func_0003D618, false>));
    DES_REGISTER_FUNCTION(kSmallReallocAligned, (heap_realloc<kSmall, func_0003C9A8, true>));
    DES_REGISTER_FUNCTION(kSmallReallocAligned2, (heap_realloc<kSmall, func_0003CDF8, true>));
    DES_REGISTER_FUNCTION(kSmallFree, heap_free<func_0003C758>);
    DES_REGISTER_FUNCTION(kSmallFree2, heap_free<func_0003D2E8>);
    DES_REGISTER_FUNCTION(kSmallSizeOf, heap_size<func_0003CB78>);
    DES_REGISTER_FUNCTION(kSmallOwns, heap_owns<func_0003CC48>);

    s_log = std::getenv("DES_LUAGC_LOG") != nullptr;
    DES_REGISTER_FUNCTION(kNewState, new_state<func_00A7CC60>);
    DES_REGISTER_FUNCTION(kNewState2, new_state<func_01610188>);
}

/* For des_luagc.cpp, once for each new state of a script system ('A' or 'B'): the word about its pool's heap. */
extern "C" void des_luaheap_check(uint32_t lua, char system)
{
    if (!s_on) return;
    const uint32_t pool = ok(lua) ? vm_read32(lua + kStatePool) : 0;
    const uint32_t heap = ok(pool) ? vm_read32(pool + kPoolHeap) : 0;
    char what[40];
    std::snprintf(what, sizeof what, "script system %c's Lua state", system);
    tell(what, heap);
}

/* For des_luagc.cpp's log: the bytes of Lua's heaps held in the port's heap now. */
extern "C" uint64_t des_luaheap_ours(void)
{
    std::lock_guard<std::mutex> lk(s_mtx);
    return s_bytes;
}
