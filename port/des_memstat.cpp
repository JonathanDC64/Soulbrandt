/* How full the game's own heaps get: DES_MEMSTAT=1 (docs/59 section 1.4, T1).
 *
 * At boot the game makes 13 heaps of fixed sizes (MemMan, docs/59 section 1.1)
 * and never asks for more. A heap that runs out ends in the game's error
 * report ("Transaction failed", des_game_error in des_options.cpp). Mods with
 * bigger textures, models or sounds need more of some heaps; this shows which,
 * and by how much.
 *
 * Each heap class keeps its arena in a global of its own, which the class's
 * methods fill the first time they run (0x00039610 for the Lua heap). The game
 * has two kinds of arena:
 *   - ten heaps: the free bytes at +0x3C (0x00AC7A98) and the two ends of the
 *     block list at +0x04 and +0x18, so the end minus the start is the size
 *     (0x00AC75F8);
 *   - HAVOK, MO and TEMP: the same fields 8 bytes lower, the free bytes at
 *     +0x34 and the arena's two ends at +0x28 and +0x2C (read from a running
 *     game: the free bytes drop by each block taken, header included).
 * A 14th, LOCAL, is the game's heap over the RSX's local (video) memory,
 * 0xC0000000 up to 248 MB: textures, vertex buffers and render targets. Its
 * arena is the first kind, in a static object rather than one a global points
 * to: its heap object (0x01AD6364, the generic class 0x01852AA8) points at a
 * base (0x01AD6358), and the arena is that base + 0x18, as the class's methods
 * take it. The base is the address the game's code loads, so the region tables
 * can be checked for it on every disc.
 * The game's own heap report (0x000303C0) asks for both through the classes'
 * methods. A thread here reads every arena's free bytes each
 * millisecond and keeps each heap's lowest. It only reads: it takes no lock
 * and calls nothing in the game, so the game runs as it would without it.
 *
 * The log gets:
 *   - each heap's size, the first time its arena exists;
 *   - a line each time a heap's peak use passes another tenth of its size,
 *     from half full up;
 *   - a table every DES_MEMSTAT_EVERY seconds and when the game reports an
 *     error: each heap's size, use now, peak use and that as a share of the
 *     size, then the port's own guest heap (des_heap.cpp).
 * A block that lives less than a millisecond can be missed, so a peak here can
 * be a little under the true one.
 *
 * DES_MEMSTAT=1        on. Off by default: no thread and nothing read, until a
 *                      game error, which still gets one table (each heap's use
 *                      at that moment), so a report shows which heap was full
 * DES_MEMSTAT_EVERY=s  the table every s seconds (default 60; 0: only on game errors)
 */
#include "ps3emu/ppu_lift.h"
#include "des_region.h"   /* DES_AT: the running disc's addresses */
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

extern "C" void ds_heap_usage(uint64_t* cur, uint64_t* peak);   /* des_heap.cpp */

namespace {

/* The globals that hold each heap's arena (US addresses), and the arena's fields */
constexpr uint32_t kArenaMain = 0x01AD6AF0u, kArenaHavok = 0x01AD6960u, kArenaHgsys = 0x01AD69E0u;
constexpr uint32_t kArenaCoreRes = 0x01AD65B0u, kArenaFfx = 0x01AD6858u, kArenaMo = 0x01AD68E0u;
constexpr uint32_t kArenaLua = 0x01AD6638u, kArenaMenu = 0x01AD67D0u, kArenaDebug = 0x01AD6498u;
constexpr uint32_t kArenaIngame = 0x01AD6A68u, kArenaServer = 0x01AD6748u, kArenaNetwork = 0x01AD66C0u;
constexpr uint32_t kArenaTemp = 0x01AD6530u;
constexpr uint32_t kLocalBase = 0x01AD6358u;    /* LOCAL's base: its arena is at +0x18 */
constexpr uint32_t kBaseArena = 0x18;
constexpr uint32_t kAtFirst = 0x04, kAtEnd = 0x18, kAtFree = 0x3C;      /* the ten heaps' arenas */
constexpr uint32_t kAtFirst2 = 0x28, kAtEnd2 = 0x2C, kAtFree2 = 0x34;   /* HAVOK's, MO's and TEMP's */

struct Heap {
    const char* name;
    bool second = false;   /* the second kind of arena (kAtFirst2...) */
    bool direct = false;   /* `global` is a base whose arena is at +kBaseArena (LOCAL) */
    uint32_t global = 0;   /* the running disc's address of the global that holds the arena */
    uint32_t size = 0;     /* the arena's size, once it exists */
    uint32_t low = 0;      /* the fewest free bytes seen */
    uint32_t now = 0;      /* free bytes at the last sample */
    uint32_t told = 0;     /* the tenths of the size that the log has had */
    bool odd = false;      /* an arena that didn't read as one (said once) */
};

/* The names are docs/59's (from the game's own heap report) */
Heap s_heaps[] = {{"MAIN"}, {"HAVOK", true}, {"HGSYS"},  {"CORERES"}, {"FFX"},     {"MO", true},  {"LUA"},
                  {"MENU"}, {"DEBUG"},       {"INGAME"}, {"SERVER"},  {"NETWORK"}, {"TEMP", true},
                  {"LOCAL", false, true}};
std::mutex s_mtx;   /* s_heaps, between the thread and des_memstat_report */
bool s_on = false;
int s_level = 0;    /* DES_MEMSTAT */

bool guest(uint32_t a) { return a >= 0x00010000u && a < 0xD0000000u && !(a & 3u); }
double mb(uint64_t bytes) { return bytes / (1024.0 * 1024.0); }

/* The heap's arena, or 0 while it isn't made: LOCAL's is a static object, empty until the game fills it */
uint32_t arena_of(const Heap& h)
{
    if (!h.direct) return vm_read32(h.global);
    const uint32_t arena = h.global + kBaseArena;
    return vm_read32(arena + kAtEnd) ? arena : 0;
}

/* The running disc's address of each heap's global (or LOCAL's base) */
void set_globals()
{
    const uint32_t at[] = {DES_AT(kArenaMain),  DES_AT(kArenaHavok),   DES_AT(kArenaHgsys),  DES_AT(kArenaCoreRes),
                           DES_AT(kArenaFfx),   DES_AT(kArenaMo),      DES_AT(kArenaLua),    DES_AT(kArenaMenu),
                           DES_AT(kArenaDebug), DES_AT(kArenaIngame),  DES_AT(kArenaServer), DES_AT(kArenaNetwork),
                           DES_AT(kArenaTemp),  DES_AT(kLocalBase)};
    static_assert(sizeof at / sizeof at[0] == sizeof s_heaps / sizeof s_heaps[0], "one global per heap");
    for (size_t i = 0; i < sizeof at / sizeof at[0]; i++) s_heaps[i].global = at[i];
}

/* One read of every arena; under s_mtx. Quiet: no size and tenths lines (the one read on a game error). */
void sample(bool quiet = false)
{
    for (Heap& h : s_heaps) {
        const uint32_t arena = arena_of(h);
        if (!guest(arena)) continue;
        const uint32_t first = vm_read32(arena + (h.second ? kAtFirst2 : kAtFirst));
        const uint32_t end = vm_read32(arena + (h.second ? kAtEnd2 : kAtEnd));
        const uint32_t free = vm_read32(arena + (h.second ? kAtFree2 : kAtFree));
        const uint32_t size = end - first;
        if (!guest(first) || end <= first || size > 0x10000000u || free > size) {
            if (!h.odd)
                fprintf(stderr, "[memstat] %s: the arena at %08X doesn't read as one (start %08X, end %08X, free %u)\n",
                        h.name, arena, first, end, free);
            h.odd = true;
            continue;
        }
        if (!h.size) {
            h.size = h.low = size;
            if (!quiet) fprintf(stderr, "[memstat] %s: %.2f MB, %.2f MB free\n", h.name, mb(size), mb(free));
        }
        h.now = free;
        if (free < h.low) h.low = free;
        const uint64_t peak = h.size - h.low;
        const uint32_t tenths = (uint32_t)(peak * 10 / h.size);
        if (tenths > h.told && tenths >= 5 && !quiet) {
            h.told = tenths;
            fprintf(stderr, "[memstat] %s: %.2f of %.2f MB used at most (%.0f%%)\n", h.name, mb(peak), mb(h.size),
                    100.0 * peak / h.size);
        }
    }
}

void table(const char* why, bool once = false)
{
    if (once)   /* DES_MEMSTAT off: this one read, so "at most" is now */
        fprintf(stderr, "[memstat] the heaps (%s, one read; DES_MEMSTAT=1 follows the peaks): size, in use now, "
                        "in use now, share\n", why);
    else
        fprintf(stderr, "[memstat] the heaps (%s): size, in use now, at most, at most of the size\n", why);
    for (const Heap& h : s_heaps) {
        if (!h.size) {
            fprintf(stderr, "[memstat]   %-8s %s\n", h.name, h.odd ? "(unreadable)" : "(not made yet)");
            continue;
        }
        const uint64_t peak = h.size - h.low;
        fprintf(stderr, "[memstat]   %-8s %7.2f MB %7.2f MB %7.2f MB %4.0f%%\n", h.name, mb(h.size), mb(h.size - h.now),
                mb(peak), 100.0 * peak / h.size);
    }
    if (s_level >= 2) {   /* DES_MEMSTAT=2: each arena's first words, to read a heap class this file doesn't know */
        for (const Heap& h : s_heaps) {
            const uint32_t arena = arena_of(h);
            if (!guest(arena)) continue;
            fprintf(stderr, "[memstat]   %-8s %08X:", h.name, arena);
            for (uint32_t o = 0; o < 0x60; o += 4) fprintf(stderr, " %08X", vm_read32(arena + o));
            fprintf(stderr, "\n");
        }
    }
    uint64_t cur = 0, peak = 0;
    ds_heap_usage(&cur, &peak);
    fprintf(stderr, "[memstat]   the port's guest heap: %.1f MB in use now, %.1f MB at most\n", mb(cur), mb(peak));
}

void run(int every)
{
    using clock = std::chrono::steady_clock;
    auto next = clock::now() + std::chrono::seconds(every);
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(s_mtx);
            sample();
            if (every > 0 && clock::now() >= next) {
                table("timed");
                next += std::chrono::seconds(every);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}  // namespace

/* The table now, for the game's error report (des_options.cpp): a full heap ends in one. With DES_MEMSTAT off it is
 * one read at that moment (no thread ran, so no peaks), so a player's report still shows which heap was full. */
extern "C" void des_memstat_report(const char* why)
{
    std::lock_guard<std::mutex> lk(s_mtx);
    if (!s_on && !s_heaps[0].global) set_globals();
    sample(!s_on);
    table(why, !s_on);
}

extern "C" void des_memstat_init(void)
{
    const char* v = std::getenv("DES_MEMSTAT");
    if (!v || (s_level = std::atoi(v)) <= 0) return;
    set_globals();
    const char* e = std::getenv("DES_MEMSTAT_EVERY");
    const int every = e && *e ? std::atoi(e) : 60;
    s_on = true;
    fprintf(stderr, "[memstat] DES_MEMSTAT: the game's heaps, read each millisecond; a table every %d s\n", every);
    std::thread(run, every).detach();
}
