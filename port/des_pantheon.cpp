/* Pantheon records.
 *
 * Beside each ranking score the game uploads its character as a record of 0x1160 bytes, and in the Nexus it shows
 * other players' records as statues and in a dialog. A record comes from another player's machine through a server,
 * so nothing in one is trusted: the runtime hands each whole record to the hook here (np_score_set_game_data_hook)
 * before it is sent and before the game can read it.
 *
 * - Going up, the character's name and the one code address in the record are blanked. The game writes the player's
 *   online name over the name when it reads a record, and the address differs per disc.
 * - Coming down, and going up too, every field must pass the table below. The server has the same limits, and a
 *   record that fails here counts as a row with no record, so the worst a bad one can do is leave a statue empty.
 * - Equipment ids must also be rows of the game's own param tables: the statue and the dialog load by them.
 *
 * Limits on what a value means come from the game's code and from real records. Change one here and in the server's
 * table together.
 *
 * The param tables are read from guest memory: the hook has no guest context to call the game's getters with. */

#include "ps3emu/ppu_lift.h"
#include "des_region.h"      /* DES_AT: the running disc's addresses */

#include <atomic>
#include <cstdio>
#include <cstring>

extern "C" {
typedef int (*np_score_game_data_hook_fn)(int upload, uint32_t board_id, uint8_t* data, uint32_t size);
void np_score_set_game_data_hook(np_score_game_data_hook_fn fn);   /* ps3recomp libs/network/sceNp.h */
}

namespace {

constexpr uint32_t kRecordSize = 0x1160;
constexpr uint32_t kBoards = 5;
constexpr uint32_t kNameAt = 0x088, kNameSize = 0x22;
constexpr uint32_t kCodeAddressAt = 0x168;
constexpr uint32_t kEquipAt = 0x178;
constexpr uint32_t kEquipVt = 0x0185A8B8u;   /* the equip set object's vtable, the record's code address */
constexpr uint32_t kToc = 0x019B5040u;

uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint64_t be64(const uint8_t* p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

enum Kind : uint8_t { kU8, kU16, kI32, kU32, kI64, kF32, kZero, kAny };

/* n values in a row, each from lo to hi. kZero and kAny count bytes. */
struct Field {
    const char* name;
    uint16_t at, n;
    Kind kind;
    int64_t lo, hi;
};

const Field kFields[] = {
    {"magic", 0x000, 1, kU32, 0x10000002, 0x10000002},
    {"hp", 0x004, 3, kI32, 0, 9999},
    {"mp", 0x010, 3, kI32, 0, 9999},
    {"word after mp", 0x01C, 4, kZero, 0, 0},
    {"stamina", 0x020, 3, kI32, 0, 9999},
    {"word after stamina", 0x02C, 4, kZero, 0, 0},
    {"stats", 0x030, 16, kI32, 0, 150},
    {"counter 0x70", 0x070, 1, kI32, 0, 999999999},
    {"counter 0x74", 0x074, 1, kU32, 0, 999999999},
    {"value 0x78", 0x078, 1, kI64, 0, 999999999},
    {"counters 0x80", 0x080, 2, kI32, 0, 999999999},
    {"character name", kNameAt, kNameSize, kZero, 0, 0},
    {"body type", 0x0AA, 1, kU8, 0, 1},
    {"byte 0xAB", 0x0AB, 1, kZero, 0, 0},
    {"value 0xAC", 0x0AC, 1, kU16, 100, 100},
    {"padding 0xAE", 0x0AE, 0x22, kZero, 0, 0},
    {"flags", 0x0D0, 1, kU32, 0xC0, 0xC0},
    {"words 0xD4", 0x0D4, 3, kU32, 0xFFFF0000, 0xFFFF0000},
    {"values 0xE0", 0x0E0, 3, kU16, 0, 1000},
    {"value 0xE6", 0x0E6, 2, kZero, 0, 0},
    {"padding 0xE8", 0x0E8, 0x38, kZero, 0, 0},
    {"derived values", 0x120, 6, kI32, 0, 999999},
    {"value 0x138", 0x138, 1, kI32, 0, 999999},
    {"floats 0x13C", 0x13C, 3, kF32, 0, 100000},
    {"value 0x148", 0x148, 1, kI32, 0, 999999},
    {"padding 0x14C", 0x14C, 0xC, kZero, 0, 0},
    {"values 0x158", 0x158, 4, kI32, 0, 1000},
    {"code address", kCodeAddressAt, 4, kZero, 0, 0},
    {"equip setting 0x16C", 0x16C, 1, kI32, 0, 3},
    {"equip settings 0x170", 0x170, 2, kI32, 0, 1},
    {"equipment ids", kEquipAt, 18, kI32, -1, 999999},
    {"colours", 0x1C0, 4, kF32, 0, 1},
    {"face shape", 0x1D0, 0x64, kAny, 0, 0},
    {"padding 0x234", 0x234, 0xC, kZero, 0, 0},
    {"pairs 0x240", 0x240, 38, kI32, -1, 999999},
    {"padding 0x2D8", 0x2D8, 0x34, kZero, 0, 0},
    {"list A count", 0x30C, 1, kU32, 0, 128},
    {"list B count", 0xF10, 1, kU32, 0, 64},
    {"padding 0xF14", 0xF14, 0xC, kZero, 0, 0},
    {"totals", 0xF20, 3, kI64, 0, 999999999},
    {"play time and counters", 0xF40, 3, kU32, 0, 2147483647},
    {"values 0xF4C", 0xF4C, 2, kI32, 0, 1000000},
    {"padding 0xF54", 0xF54, 0x20C, kZero, 0, 0},
};

constexpr uint32_t kEntrySize = 16;

/* Each value at p of the field, in order: false, with why filled in, at the first one out of its limits. */
bool field_ok(const Field& f, const uint8_t* r, char* why, size_t cap)
{
    if (f.kind == kAny) return true;
    if (f.kind == kZero) {
        for (uint32_t i = 0; i < f.n; i++)
            if (r[f.at + i]) {
                snprintf(why, cap, "%s is not blank at +0x%X", f.name, f.at + i);
                return false;
            }
        return true;
    }
    static const uint8_t kSize[] = {1, 2, 4, 4, 8, 4};
    for (uint32_t i = 0; i < f.n; i++) {
        const uint32_t at = f.at + i * kSize[f.kind];
        const uint8_t* p = r + at;
        if (f.kind == kF32) {
            const uint32_t bits = be32(p);
            float x;
            memcpy(&x, &bits, 4);
            if (!(x >= (float)f.lo && x <= (float)f.hi)) {   /* a NaN fails this, as an infinity does */
                snprintf(why, cap, "%s at +0x%X is out of range", f.name, at);
                return false;
            }
            continue;
        }
        const int64_t v = f.kind == kU8    ? (int64_t)*p
                          : f.kind == kU16 ? (int64_t)be16(p)
                          : f.kind == kI32 ? (int64_t)(int32_t)be32(p)
                          : f.kind == kU32 ? (int64_t)be32(p)
                                           : (int64_t)be64(p);
        if (v < f.lo || v > f.hi) {
            snprintf(why, cap, "%s at +0x%X is %lld, outside %lld..%lld", f.name, at, (long long)v, (long long)f.lo,
                     (long long)f.hi);
            return false;
        }
    }
    return true;
}

/* Used entries {1, id, count, 0} first, then blank ones {0, -1, -1, 0}, and the count says how many are used. */
bool list_ok(const char* name, const uint8_t* r, uint32_t at, uint32_t n, uint32_t count_at, char* why, size_t cap)
{
    uint32_t used = 0;
    bool blanks = false;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* e = r + at + i * kEntrySize;
        const int32_t kind = (int32_t)be32(e), id = (int32_t)be32(e + 4), qty = (int32_t)be32(e + 8), last = (int32_t)be32(e + 12);
        if (kind == 0 && id == -1 && qty == -1 && last == 0) {
            blanks = true;
            continue;
        }
        if (blanks || kind != 1 || id < 1 || id > 999999 || qty < 1 || qty > 9999 || last != 0) {
            snprintf(why, cap, "%s: entry %u is not a valid one", name, i);
            return false;
        }
        used++;
    }
    if (be32(r + count_at) != used) {
        snprintf(why, cap, "%s: the count is %u, and %u entries are used", name, be32(r + count_at), used);
        return false;
    }
    return true;
}

/* +0xF38: a packed date (u16 year, month, day, hour; what the latest-clear board carries) or the clock's raw ticks. */
bool time_ok(const uint8_t* r, char* why, size_t cap)
{
    const uint8_t* p = r + 0xF38;
    const uint64_t v = be64(p);
    if (v >> 56 == 0) return true;   /* ticks: a packed date has its year in the top bytes */
    const uint32_t year = be16(p), month = be16(p + 2), day = be16(p + 4), hour = be16(p + 6);
    if (year >= 2000 && year <= 2100 && month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour <= 23) return true;
    snprintf(why, cap, "time at +0xF38 is neither a date nor ticks");
    return false;
}

bool guest_ok(uint32_t a) { return a >= 0x00010000u && a < 0xD0000000u && !(a & 3u); }

/* The game's param file of a type name, as loaded, 0 if it isn't there. The tables are 0x005A5920's: 36 bytes each,
 * from *(*(*(TOC - 0x7624) - 0x8000)), the count at +8 and the first holder at +0xC, whose +0x20 is the file. A file
 * has its type name at +0x0C, its row count at +0x0A and its rows {id, data offset, name offset} from +0x30. */
uint32_t param_file(const char* type)
{
    const uint32_t slot = vm_read32(DES_AT(kToc) - 0x7624u);
    const uint32_t holder_of = guest_ok(slot) ? vm_read32(slot - 0x8000u) : 0;
    const uint32_t root = guest_ok(holder_of) ? vm_read32(holder_of) : 0;
    if (!guest_ok(root)) return 0;
    const size_t len = strlen(type);
    for (uint32_t t = 0; t < 0x1E; t++) {
        if ((int32_t)vm_read32(root + 36 * t + 8) <= 0) continue;
        const uint32_t holder = vm_read32(root + 36 * t + 0x0C);
        const uint32_t file = guest_ok(holder) ? vm_read32(holder + 0x20) : 0;
        if (!guest_ok(file)) continue;
        size_t i = 0;
        while (i < len && vm_read8(file + 0x0C + (uint32_t)i) == (uint8_t)type[i]) i++;
        if (i == len && vm_read8(file + 0x0C + (uint32_t)len) == 0) return file;
    }
    return 0;
}

bool param_has(uint32_t file, int32_t id)
{
    const uint32_t n = vm_read16(file + 0x0A);
    for (uint32_t k = 0; k < n; k++)
        if ((int32_t)vm_read32(file + 0x30 + 12 * k) == id) return true;
    return false;
}

const char* const kWeapon = "EQUIP_PARAM_WEAPON_ST";
const char* const kProtector = "EQUIP_PARAM_PROTECTOR_ST";
const char* const kAccessory = "EQUIP_PARAM_ACCESSORY_ST";
const char* const kGoods = "EQUIP_PARAM_GOODS_ST";

/* Each of the 18 equipment slots' table: hands and ammunition, armour, rings, quick items. */
const char* const kSlotTable[18] = {
    kWeapon, kWeapon, kWeapon, kWeapon, kWeapon, kWeapon,
    kProtector, kProtector, kProtector, kProtector, kProtector,
    kAccessory, kAccessory,
    kGoods, kGoods, kGoods, kGoods, kGoods,
};

bool equipment_ok(const uint8_t* r, char* why, size_t cap)
{
    for (uint32_t slot = 0; slot < 18; slot++) {
        const int32_t id = (int32_t)be32(r + kEquipAt + 4 * slot);
        if (id == -1) continue;
        const uint32_t file = param_file(kSlotTable[slot]);
        if (!file || !param_has(file, id)) {
            snprintf(why, cap, "equipment slot %u holds %d, not in the game's table", slot, id);
            return false;
        }
    }
    return true;
}

bool record_ok(uint32_t board, const uint8_t* r, uint32_t size, char* why, size_t cap)
{
    if (size != kRecordSize) {
        snprintf(why, cap, "size is 0x%X", size);
        return false;
    }
    if (board < 1 || board > kBoards) {
        snprintf(why, cap, "board %u is not one of the game's", board);
        return false;
    }
    for (const Field& f : kFields)
        if (!field_ok(f, r, why, cap)) return false;
    return list_ok("list A", r, 0x310, 128, 0x30C, why, cap) && list_ok("list B", r, 0xB10, 64, 0xF10, why, cap) &&
           time_ok(r, why, cap) && equipment_ok(r, why, cap);
}

int record_hook(int upload, uint32_t board, uint8_t* r, uint32_t size)
{
    if (upload && size == kRecordSize) {
        memset(r + kNameAt, 0, kNameSize);
        memset(r + kCodeAddressAt, 0, 4);
    }
    char why[96];
    if (!record_ok(board, r, size, why, sizeof why)) {
        static std::atomic<int> s_refused{0};
        const int n = s_refused.fetch_add(1, std::memory_order_relaxed);
        if (n < 8) fprintf(stderr, "[des] Pantheon: %s record refused: %s\n", upload ? "our" : "a received", why);
        return 0;
    }
    if (!upload) {
        const uint32_t vt = DES_AT(kEquipVt);
        r[kCodeAddressAt] = (uint8_t)(vt >> 24);
        r[kCodeAddressAt + 1] = (uint8_t)(vt >> 16);
        r[kCodeAddressAt + 2] = (uint8_t)(vt >> 8);
        r[kCodeAddressAt + 3] = (uint8_t)vt;
    }
    return 1;
}

} // namespace

/* From ps3_title_options_init, before the guest runs. */
extern "C" void des_pantheon_init(void)
{
    np_score_set_game_data_hook(record_hook);
}
