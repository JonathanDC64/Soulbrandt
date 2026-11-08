/* des_region.h: which disc the game was lifted from, and its addresses.
 *
 * The port supports four programs: BLUS30443 (the US disc), BLES00932 (the
 * European disc, 01.00), and BCJS30022 and BCAS20071 (the Japanese and Asian
 * discs with their 1.04 update). Their EBOOTs are the same program with most
 * functions moved a little, so every guest address in the port is written
 * as the US disc's, and DES_AT gives the running disc's:
 *
 *     vm_read32(DES_AT(kChrMan))
 *     ctx->lr = DES_AT(0x00724E40u);
 *
 * port/regions/<title>.inc has each other disc's address of each. An address
 * missing there is a compile error at its DES_AT, and tools/region/region_table.py
 * (CI) checks that every address in the port's sources is in the tables.
 *
 * What stays a US address:
 * - instruction hook keys (insn_hooks.txt, the handlers' `addr ==` and
 *   `case`): setup lifts another disc's EBOOT with each hook at that disc's
 *   address but called with its US key (ppu_lifter.py's sym@key). A handler
 *   that reads guest memory at its key reads it at des_region_addr(addr);
 * - the lifted functions the port calls by name (func_XXXXXXXX): the split
 *   build resolves them at the disc's address when the game DLL loads
 *   (des_game_host.cpp).
 *
 * The region is set once, when the game DLL loads, before any guest code or
 * port hook runs. A single-exe build (not DES_SPLIT) links the lift's
 * functions by their US names, so it is always the US disc. */
#pragma once
#include <cstdint>

enum class DesRegion : uint8_t { US, EU, JP, ASIA };
inline constexpr int kDesRegions = 4;

extern "C" uint8_t g_des_region;   /* a DesRegion; des_region.cpp */

inline DesRegion des_region() { return static_cast<DesRegion>(g_des_region); }
/* "BLUS30443", "BLES00932", "BCJS30022" or "BCAS20071". */
const char* des_region_title_id(DesRegion r);

/* The Japanese or Asian program (the 1.04 update's): the two choose their
 * language and online tags alike, unlike the US and European programs. */
inline bool des_region_asian() { return des_region() == DesRegion::JP || des_region() == DesRegion::ASIA; }

/* A table's value for what the program does not have (only code that runs on
 * the other discs uses that address). */
inline constexpr uint32_t kDesNotOnDisc = 0xFFFFFFFFu;

namespace des_region_detail {
struct Pair { uint32_t us, at; };
#define DES_REGION_ADDR(us, at) {us, at},
inline constexpr Pair kEu[] = {
#include "regions/BLES00932.inc"
};
inline constexpr Pair kJp[] = {
#include "regions/BCJS30022.inc"
};
inline constexpr Pair kAsia[] = {
#include "regions/BCAS20071.inc"
};
#undef DES_REGION_ADDR

/* A disc's address of a US address in its table; 0 if it isn't there. */
template <uint32_t N>
constexpr uint32_t find(const Pair (&table)[N], uint32_t us)
{
    uint32_t lo = 0, hi = N;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (table[mid].us < us) lo = mid + 1;
        else hi = mid;
    }
    return lo < N && table[lo].us == us ? table[lo].at : 0;
}

constexpr uint32_t eu_of(uint32_t us) { return find(kEu, us); }

/* The address of a US address on the disc r; 0 if it isn't in that disc's table. */
constexpr uint32_t on_disc(DesRegion r, uint32_t us)
{
    return r == DesRegion::US ? us : r == DesRegion::EU ? find(kEu, us) : r == DesRegion::JP ? find(kJp, us) : find(kAsia, us);
}

template <uint32_t N>
constexpr bool sorted(const Pair (&table)[N])
{
    for (uint32_t i = 1; i < N; i++)
        if (!(table[i - 1].us < table[i].us)) return false;
    return true;
}
static_assert(sorted(kEu), "port/regions/BLES00932.inc must be sorted by the US address, one line each");
static_assert(sorted(kJp), "port/regions/BCJS30022.inc must be sorted by the US address, one line each");
static_assert(sorted(kAsia), "port/regions/BCAS20071.inc must be sorted by the US address, one line each");
}

/* The running disc's address for the US address US (a constant). */
template <uint32_t US>
inline uint32_t des_at()
{
    using namespace des_region_detail;
    static constexpr uint32_t at[kDesRegions] = {US, on_disc(DesRegion::EU, US), on_disc(DesRegion::JP, US), on_disc(DesRegion::ASIA, US)};
    static_assert(at[1] != 0 && at[2] != 0 && at[3] != 0,
                  "this US address is not in every table in port/regions/ (tools/region/region_table.py find)");
    return at[g_des_region];
}
#define DES_AT(us) (des_at<(us)>())

/* The running disc's address for a US address known only at run time: an
 * instruction hook's key (the handlers' addr), to read the instruction the
 * hook runs after. 0 if the disc's table doesn't have it (CI checks that every
 * hook is there). */
inline uint32_t des_region_addr(uint32_t us)
{
    return des_region_detail::on_disc(des_region(), us);
}

/* ppu_register_function at the running disc's address of a US address. */
extern "C" void ppu_register_function(uint64_t addr, void (*fn)(struct ppu_context*));
template <uint32_t US>
inline void des_register_function(void (*fn)(struct ppu_context*))
{
    ppu_register_function(des_at<US>(), fn);
}
#define DES_REGISTER_FUNCTION(us, fn) des_register_function<(us)>(fn)
