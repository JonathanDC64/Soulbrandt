// pantheon_check: des_pantheon.cpp's hook on Pantheon records, in a program of its own that runs no game. The hook
// reads the game's param tables from guest memory; tests/pantheon_stub stands in for ps3recomp's ppu_lift.h with a
// memory this test fills in, so des_pantheon.cpp is compiled here as it is.
//   clang-cl /nologo /EHsc /std:c++17 /I port/tests/pantheon_stub port/tests/pantheon_check.cpp
//   c++ -std=c++17 -I port/tests/pantheon_stub port/tests/pantheon_check.cpp
// What it checks: our own record is sent whatever the table says, with the name and the code address blanked; a
// received one is refused unless it passes, and gets the running disc's code address back.
#include "../des_pantheon.cpp"

#include <cstdio>
#include <vector>

extern "C" {
uint8_t g_des_region = 0;  // the US disc (des_region.cpp)
}

static np_score_game_data_hook_fn s_hook;
extern "C" void np_score_set_game_data_hook(np_score_game_data_hook_fn fn) { s_hook = fn; }

namespace {

int g_failed = 0;

// Always on, unlike assert(): a build with NDEBUG still runs every call inside it and still fails.
#define CHECK(x)                                                                    \
    do {                                                                            \
        if (!(x)) {                                                                 \
            fprintf(stderr, "pantheon_check: line %d failed: %s\n", __LINE__, #x); \
            ++g_failed;                                                             \
        }                                                                           \
    } while (0)

void put(std::vector<uint8_t>& r, uint32_t at, uint32_t v)
{
    r[at] = (uint8_t)(v >> 24), r[at + 1] = (uint8_t)(v >> 16), r[at + 2] = (uint8_t)(v >> 8), r[at + 3] = (uint8_t)v;
}
void put16(std::vector<uint8_t>& r, uint32_t at, uint16_t v) { r[at] = (uint8_t)(v >> 8), r[at + 1] = (uint8_t)v; }
void put64(std::vector<uint8_t>& r, uint32_t at, uint64_t v) { put(r, at, (uint32_t)(v >> 32)), put(r, at + 4, (uint32_t)v); }
void putf(std::vector<uint8_t>& r, uint32_t at, float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    put(r, at, b);
}

// A record as the game builds one: every field inside the table's limits, with a name and the equip set's address,
// which an upload must blank. Field by field as the server's desRecord (desrecord_test.go).
std::vector<uint8_t> game_record()
{
    std::vector<uint8_t> r(kRecordSize, 0);
    put(r, 0x000, 0x10000002);
    for (uint32_t at : {0x004u, 0x008u, 0x00Cu, 0x010u, 0x014u, 0x018u, 0x020u, 0x024u, 0x028u}) put(r, at, 300);
    for (uint32_t at = 0x030; at <= 0x06C; at += 4) put(r, at, 20);
    put(r, 0x070, 5), put(r, 0x074, 9), put64(r, 0x078, 7), put(r, 0x080, 1), put(r, 0x084, 2);
    for (uint32_t i = 0; i < 6; i++) put16(r, kNameAt + 2 * i, (uint16_t)"Alice"[i % 5]);  // a name: the upload blanks it
    r[0x0AA] = 1;
    put16(r, 0x0AC, 100);
    put(r, 0x0D0, 0xC0);
    for (uint32_t at : {0x0D4u, 0x0D8u, 0x0DCu}) put(r, at, 0xFFFF0000u);
    for (uint32_t at : {0x0E0u, 0x0E2u, 0x0E4u}) put16(r, at, 40);
    for (uint32_t at = 0x120; at <= 0x134; at += 4) put(r, at, 1000);
    put(r, 0x138, 999999);
    for (uint32_t at : {0x13Cu, 0x140u, 0x144u}) putf(r, at, 98.0f);
    put(r, 0x148, 999999);
    for (uint32_t at = 0x158; at <= 0x164; at += 4) put(r, at, 40);
    put(r, kCodeAddressAt, DES_AT(kEquipVt));  // the equip set's vtable: the upload blanks it
    put(r, 0x16C, 1), put(r, 0x170, 0), put(r, 0x174, 1);
    for (uint32_t at = kEquipAt; at <= 0x1BC; at += 4) put(r, at, 0xFFFFFFFFu);
    put(r, kEquipAt, 1500);           // slot 0, a weapon
    put(r, kEquipAt + 4 * 6, 2000);   // slot 6, armour
    put(r, kEquipAt + 4 * 11, 100);   // slot 11, a ring
    put(r, kEquipAt + 4 * 13, 1);     // slot 13, a quick item
    for (uint32_t at : {0x1C0u, 0x1C4u, 0x1C8u, 0x1CCu}) putf(r, at, 0.5f);
    for (uint32_t at = 0x1D0; at < 0x234; at++) r[at] = 0x33;
    for (uint32_t at = 0x240; at < 0x2D8; at += 4) put(r, at, 0xFFFFFFFFu);
    for (uint32_t i = 0; i < 128 + 64; i++) {
        const uint32_t at = (i < 128 ? 0x310 : 0xB10) + 16 * (i < 128 ? i : i - 128);
        put(r, at, 0), put(r, at + 4, 0xFFFFFFFFu), put(r, at + 8, 0xFFFFFFFFu), put(r, at + 12, 0);
    }
    put(r, 0x30C, 2);
    put(r, 0x310, 1), put(r, 0x314, 100), put(r, 0x318, 3);
    put(r, 0x320, 1), put(r, 0x324, 2000), put(r, 0x328, 1);
    for (uint32_t at : {0xF20u, 0xF28u, 0xF30u}) put64(r, at, 3);
    put64(r, 0xF38, 2026ull << 48 | 11ull << 32 | 9ull << 16 | 12);  // a packed date
    for (uint32_t at : {0xF40u, 0xF44u, 0xF48u}) put(r, at, 100);
    put(r, 0xF4C, 5), put(r, 0xF50, 6);
    return r;
}

// The game's param tables as param_file finds them: *(*(*(TOC - 0x7624) - 0x8000)) is the list, 36 bytes a table,
// the count at +8 and the holder at +0xC, whose +0x20 is the file: type name at +0x0C, row count at +0x0A, rows of
// 12 bytes from +0x30 with the id first. A table of another type comes first, as in the game.
void put_tables()
{
    struct Table { const char* type; std::vector<int32_t> ids; };
    const Table tables[] = {
        {"SKELETON_PARAM_ST", {1500, 2000, 100, 1}},  // not an equipment table: its ids must not count
        {kWeapon, {1, 1500, 1501}},
        {kProtector, {2000, 2001}},
        {kAccessory, {100}},
        {kGoods, {1, 2}},
    };
    const uint32_t slot = 0x20008000u, holder_of = 0x20000100u, list = 0x20001000u;
    pantheon_put32(DES_AT(kToc) - 0x7624u, slot);
    pantheon_put32(slot - 0x8000u, holder_of);
    pantheon_put32(holder_of, list);
    uint32_t next = 0x20010000u;
    for (uint32_t t = 0; t < sizeof tables / sizeof tables[0]; t++) {
        const uint32_t holder = next, file = next + 0x100;
        next = file + 0x1000;
        pantheon_put32(list + 36 * t + 8, 1);
        pantheon_put32(list + 36 * t + 0xC, holder);
        pantheon_put32(holder + 0x20, file);
        pantheon_put16(file + 0x0A, (uint16_t)tables[t].ids.size());
        for (uint32_t i = 0; tables[t].type[i]; i++) pantheon_put8(file + 0x0C + i, (uint8_t)tables[t].type[i]);
        for (uint32_t k = 0; k < tables[t].ids.size(); k++) pantheon_put32(file + 0x30 + 12 * k, (uint32_t)tables[t].ids[k]);
    }
}

bool blank(const std::vector<uint8_t>& r, uint32_t at, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (r[at + i]) return false;
    return true;
}

int up(std::vector<uint8_t>& r, uint32_t board = 1) { return s_hook(1, board, r.data(), (uint32_t)r.size()); }
int down(std::vector<uint8_t>& r, uint32_t board = 1) { return s_hook(0, board, r.data(), (uint32_t)r.size()); }

}  // namespace

int main()
{
    des_pantheon_init();
    CHECK(s_hook);
    put_tables();

    // A good record: sent with the name and the address blanked, and taken back with the address of this disc.
    std::vector<uint8_t> r = game_record();
    char why[96];
    CHECK(!record_ok(1, r.data(), kRecordSize, why, sizeof why));  // as the game built it: name and address set
    CHECK(up(r) == 1);
    CHECK(blank(r, kNameAt, kNameSize) && blank(r, kCodeAddressAt, 4));
    CHECK(record_ok(1, r.data(), kRecordSize, why, sizeof why));
    CHECK(down(r) == 1);
    CHECK(be32(r.data() + kCodeAddressAt) == DES_AT(kEquipVt));
    for (uint32_t board = 1; board <= kBoards; board++) {
        std::vector<uint8_t> b = game_record();
        CHECK(up(b, board) == 1 && down(b, board) == 1);
    }

    // The shapes of real characters past the first area, which v0.1.2's limits refused (the server's
    // desrecord_test.go has the same cases): another player's record with them is taken.
    {
        std::vector<uint8_t> p = game_record();
        put16(p, 0x0AC, 0);
        put16(p, 0x0AE, 8);
        const int32_t counts[8] = {119, 28, 2, 1, 3, 1, 0, 0};
        for (uint32_t i = 0; i < 8; i++) put(p, 0x0B0 + 4 * i, (uint32_t)counts[i]);
        put(p, 0x0D8, 0x139C021Au);
        const float colours[4] = {1.333f, 1.6f, 1.875f, 1.004f};
        for (uint32_t i = 0; i < 4; i++) putf(p, 0x1C0 + 4 * i, colours[i]);
        put(p, 0x310, 2), put(p, 0x320, 3);   // list A's kinds
        // list B: a stack word in each last word, two entries with a count of 1, and an entry after a blank one
        const uint32_t b[4][3] = {{1, 16, 15}, {1, 17, 16}, {0, 0, 0}, {1, 18, 1}};
        for (uint32_t i = 0; i < 4; i++) {
            if (!b[i][0]) continue;
            put(p, 0xB10 + 16 * i, b[i][0]), put(p, 0xB14 + 16 * i, b[i][1]), put(p, 0xB18 + 16 * i, b[i][2]);
            put(p, 0xB1C + 16 * i, 0xD0112E60u);
        }
        put(p, 0xF10, 1);
        CHECK(up(p) == 1);
        CHECK(record_ok(1, p.data(), kRecordSize, why, sizeof why));
        CHECK(down(p) == 1);
    }
    // ... and just past each widened limit, it is refused.
    {
        struct Bad { uint32_t at; uint32_t v; int size; };
        const Bad bad[] = {
            {0x0AC, 101, 2}, {0x0AE, 10000, 2}, {0x0B0, 0xFFFFFFFFu, 4}, {0x0CC, 1000000000u, 4},
            {0x0D4, 0xFFFEFFFFu, 4}, {0x0DC, 0x80000000u, 4}, {0x310, 4, 4}, {0xB10, 4, 4},
        };
        for (const Bad& x : bad) {
            std::vector<uint8_t> p = game_record();
            up(p);
            if (x.size == 2) put16(p, x.at, (uint16_t)x.v);
            else put(p, x.at, x.v);
            CHECK(down(p) == 0);
        }
        std::vector<uint8_t> p = game_record();
        up(p);
        putf(p, 0x1C0, 4.5f);
        CHECK(down(p) == 0);
    }

    // Our own record that breaks a limit goes up all the same, blanked: the server judges it.
    std::vector<uint8_t> hp = game_record();
    put(hp, 0x004, 12000);
    CHECK(up(hp) == 1);
    CHECK(blank(hp, kNameAt, kNameSize) && blank(hp, kCodeAddressAt, 4));
    CHECK(!record_ok(1, hp.data(), kRecordSize, why, sizeof why) && strstr(why, "hp at +0x4 is 12000"));
    // The same record from another player is refused, and keeps no code address.
    CHECK(down(hp) == 0);
    CHECK(blank(hp, kCodeAddressAt, 4));

    // An equipment id that is in range but in no table of its slot: ours is sent, another player's refused. An id
    // only another table has (the skeleton table, or the weapons' 1501 in a ring slot) is no better.
    for (uint32_t slot_id : {6u * 1000000u + 123456u, 11u * 1000000u + 1501u, 13u * 1000000u + 1500u}) {
        std::vector<uint8_t> e = game_record();
        put(e, kEquipAt + 4 * (slot_id / 1000000u), slot_id % 1000000u);
        CHECK(up(e) == 1);
        std::vector<uint8_t> d = e;
        CHECK(down(d) == 0);
        CHECK(!record_ok(1, e.data(), kRecordSize, why, sizeof why) && strstr(why, "not in the game's table"));
    }

    // Other boards and sizes: a received one is refused.
    std::vector<uint8_t> b6 = game_record();
    CHECK(up(b6) == 1 && down(b6, 6) == 0 && down(b6, 0) == 0);
    std::vector<uint8_t> small(0x1000, 0);
    CHECK(down(small) == 0);

    if (g_failed) {
        fprintf(stderr, "pantheon_check: %d failed\n", g_failed);
        return 1;
    }
    puts("pantheon checks passed");
    return 0;
}
