// Written for Demon's Souls. Seamless co-op's game side (docs/146); des_seamless.cpp is the network side.
// Nothing here runs unless co-op started (des::seamless::active()). It runs on the game's main thread (des_menu_tick,
// through des_seamless_tick) and in instruction hooks (port/insn_hooks.txt, des_coop_hook).
//
// Joining with no sign:
//   - The host opens its room with the room step a sign's touch uses (0x00540D90), once it stands in a loaded area
//     out of a session. It then builds the room's data the way the sign code does for a summon, and sends it to its
//     guests through the room server (psnr_app_send), again to any guest that asks.
//   - A guest that stands in a loaded area out of a session joins with that data (0x0053CFE8, as with a sign's
//     answer). Its join is a summoned reload, which loads the host's area wherever the guest was, beside the host
//     (the host says where it is every 2 s: READY). The host's game places it, as after a summon.
// Following the host with one load:
//   - When the host's own load starts (a warp, a death), it tells its guests (LEAD). Each guest starts a summoned
//     reload (SummonedMapReload, the guest's side of a summon), which keeps its session.
//   - The guest's load step waits, on the game's own loading screen, until the host says it has arrived (READY: its
//     area and place). The map it loads then becomes the host's area, and its spawn point the host's place.
//   - After a load, each game puts the other players back on its list of players to place (sm+0x128), which the game
//     fills only when a player joins its room. So the host places the guest, and the guest the host.
// The host keeps its session through its own loads: the game's leaves are skipped on the host (its session ends when
// co-op does, with the game).
// Deaths (the scripts' side is des_seamless_scripts.cpp):
//   - A guest's death: a summoned reload instead of the trip home, to where the host last arrived (the area's start
//     after a trip or a death). It stays in the session.
//   - The host's death: its respawn is a load like any other, so the guests follow it.
#include "des_seamless.h"

#include "ps3emu/ppu_lift.h"
#include "ps3emu/psnr_host.h"
#include "des_region.h"      // DES_AT: the running disc's addresses

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <mutex>
#include <thread>
#include <vector>

#ifndef PS3_GAME_SPLIT   // DES_SPLIT: the game DLL's (ps3emu/game_split.h)
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
#endif
extern "C" void ds_malloc(ppu_context* ctx);                     // des_heap.cpp: the guest heap
bool des_seamless_script(ppu_context* ctx, uint32_t buf, uint32_t size, uint32_t* out, uint32_t* out_size);  // _scripts.cpp
extern "C" void ps3_indirect_call(ppu_context* ctx);             // ppu_loader.cpp: a call to the guest address in CTR
extern "C" int des_player_xyz(float* x, float* y, float* z);     // des_options.cpp: 0 with no player

namespace co = des::seamless;

namespace {

constexpr uint32_t kToc = 0x019B5040u;
constexpr uint32_t kChrMan = 0x01B3D6E0u;        // *(*this + 0x1BC): the player's character
constexpr uint32_t kSessionSlot = 0x019AD7BCu;   // TOC - 0x7884: the session manager = *(*(*this - 0x7F08))
constexpr uint32_t kGameSlot = 0x019AD4FCu;      // TOC - 0x7B44: GameMan = *(*(*this - 0x7FFC))
constexpr uint32_t kSignSlot = 0x019AD694u;      // TOC - 0x79AC: the sign code's base (r30 at 0x003A1160)
constexpr uint32_t kWorldSlot = 0x019AD7B4u;     // TOC - 0x788C: the world's characters = *(*(*this - 0x7FFC))

// The session manager.
constexpr uint32_t kSmState = 0x9C;      // 0 none, 1-2 opening a room, 3 host, 4 joining, 6 client, 7 leaving
constexpr uint32_t kSmWaiting = 0x128;   // a list: the members waiting to be placed (count at +0x10)
constexpr uint32_t kSmMembers = 0x13C;   // a list: the members, each node's slot at +0x38
constexpr uint32_t kSmArrival = 0x1DC;   // 4: in the world
constexpr uint32_t kSmArea = 0x2A8;      // the area this game is in, FFFFFFFF while it loads
constexpr uint32_t kStateHost = 3, kStateClient = 6;
// GameMan.
constexpr uint32_t kGmNextArea = 0x08;                       // FFFFFFFF unless a load was asked for
constexpr uint32_t kGmPlace[4] = {0x20, 0xA40, 0xAE0, 0xB20}; // where a summoned reload puts the player (x, y, z)
// The dead-character records ("\0CHR", one bit per character of the area; always 76 bytes, 0x00294B10): the host's
// (slot 0, bytes at +0xB34), which a summoned reload's load applies, and this game's own (slot 1, +0xB3C), which its
// own loads apply. A load's pass (0x002953F0) kills the characters marked, with no souls paid, and empties the slot.
constexpr uint32_t kGmHostRecord = 0xB34;
constexpr uint32_t kGmOwnRecord = 0xB3C;
constexpr uint32_t kRecordSize = 0x4C;

constexpr uint32_t kNoArea = 0xFFFFFFFFu;

// Messages between the players' games (psnr_app_send): a 4-letter tag, then little-endian fields.
constexpr char kMsgRoom[4] = {'R', 'O', 'O', 'M'};   // the host's room data
constexpr char kMsgAsk[4] = {'R', 'O', 'M', '?'};    // a guest asks for it
constexpr char kMsgLead[4] = {'L', 'E', 'A', 'D'};   // u32 trip: the host's load started
// u32 trip, u32 area, f32 x, y, z where it arrived, f32 x, y, z where it is, then its area's dead-character record
// (76 bytes, or none): the host has arrived (after each load), every 2 s while it hosts, and when a guest asks
constexpr char kMsgReady[4] = {'R', 'E', 'D', 'Y'};
constexpr char kMsgWhere[4] = {'H', 'E', 'R', '?'};  // a guest asks for a READY now (its respawn)
constexpr uint32_t kRoomMax = 0x200;

bool guest_ok(uint32_t a) { return a >= 0x00010000u && a < 0xF0000000u; }
uint32_t rd(uint32_t a) { return guest_ok(a) ? vm_read32(a) : 0; }

float rdf(uint32_t a)
{
    const uint32_t w = vm_read32(a);
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}

void wrf(uint32_t a, float f)
{
    uint32_t w;
    std::memcpy(&w, &f, 4);
    vm_write32(a, w);
}

uint32_t session_manager() { return rd(rd(rd(DES_AT(kSessionSlot)) - 0x7F08u)); }
uint32_t game_manager() { return rd(rd(rd(DES_AT(kGameSlot)) - 0x7FFCu)); }

void drain(ppu_context* ctx)
{
    while (g_trampoline_fn) {
        void (*tf)(void*) = g_trampoline_fn;
        g_trampoline_fn = nullptr;
        tf(ctx);
    }
}

// A game function with up to eight arguments (r3-r10), returning r3, on the thread's own registers, which are put
// back after (des_respec.cpp's).
uint32_t call_at(ppu_context* ctx, void (*fn)(ppu_context*), uint32_t toc, uint32_t code, std::initializer_list<int64_t> a)
{
    uint64_t g[32]; double f[32]; uint8_t v[32][16];
    std::memcpy(g, ctx->gpr, sizeof g); std::memcpy(f, ctx->fpr, sizeof f); std::memcpy(v, ctx->vr, sizeof v);
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
    std::memcpy(ctx->gpr, g, sizeof g); std::memcpy(ctx->fpr, f, sizeof f); std::memcpy(ctx->vr, v, sizeof v);
    ctx->lr = lr; ctx->ctr = ctr; ppu_cr_unpack(ctx, cr); ctx->xer = xer;
    return r3;
}

// A game function by its address (the running disc's, DES_AT), through the runtime's indirect call. (Not
// DES_GAME_CALLS: those must exist on every disc, and co-op refuses the Japanese and Asian ones.)
uint32_t call_addr(ppu_context* ctx, uint32_t code, std::initializer_list<int64_t> a)
{
    return call_at(ctx, ps3_indirect_call, DES_AT(kToc), code, a);
}

// The virtual function at off in obj's table: the entry is a descriptor {code, TOC}, called as the game calls it.
uint32_t call_virtual(ppu_context* ctx, uint32_t obj, uint32_t off, std::initializer_list<int64_t> a)
{
    const uint32_t d = vm_read32(vm_read32(obj) + off);
    return call_at(ctx, ps3_indirect_call, vm_read32(d + 4), vm_read32(d), a);
}

// size bytes of the guest heap, for good; 0 if none.
uint32_t guest_alloc(ppu_context* ctx, uint32_t size)
{
    const uint64_t r3 = ctx->gpr[3];
    ctx->gpr[3] = size;
    ds_malloc(ctx);
    const uint32_t a = (uint32_t)ctx->gpr[3];
    ctx->gpr[3] = r3;
    return a;
}

void put32(std::vector<uint8_t>& b, uint32_t v)
{
    for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i)));
}

uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

void send(const char tag[4], const std::vector<uint8_t>& body = {})
{
    std::vector<uint8_t> m(tag, tag + 4);
    m.insert(m.end(), body.begin(), body.end());
    psnr_app_send(m.data(), (uint32_t)m.size());
}

// What the other games said. The room data and the requests wait for the main thread; the host's arrival is read
// by the guest's load step, on another thread, while the main thread draws the loading screen.
std::mutex sMsgLock;
std::vector<uint8_t> sRoom;          // the host's room data (a guest)
bool sAsked = false;                 // a guest asked for the room data (the host)
uint32_t sLeadTrip = 0;              // the host's latest load (a guest)
struct Place { float x = 0, y = 0, z = 0; };
struct Ready {
    uint32_t trip = 0, area = kNoArea;
    Place arrived, now;
    std::vector<uint8_t> record;     // the host's area's dead characters (empty: not known)
    uint32_t count = 0;              // READYs received
};
Ready sReady;                        // where the host is (a guest)
bool sAskedWhere = false;            // a guest asked for a READY now (the host)

float getf(const uint8_t* p)
{
    const uint32_t w = get32(p);
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}

void putf(std::vector<uint8_t>& b, float f)
{
    uint32_t w;
    std::memcpy(&w, &f, 4);
    put32(b, w);
}

void on_message(uint32_t from, const uint8_t* d, uint32_t n)
{
    (void)from;
    if (n < 4) return;
    std::lock_guard<std::mutex> g(sMsgLock);
    if (!std::memcmp(d, kMsgRoom, 4) && n > 4 && n - 4 <= kRoomMax) {
        sRoom.assign(d + 4, d + n);
    } else if (!std::memcmp(d, kMsgAsk, 4)) {
        sAsked = true;
    } else if (!std::memcmp(d, kMsgLead, 4) && n >= 8) {
        sLeadTrip = get32(d + 4);
    } else if (!std::memcmp(d, kMsgReady, 4) && n >= 36) {
        Ready r;
        r.trip = get32(d + 4);
        r.area = get32(d + 8);
        r.arrived = {getf(d + 12), getf(d + 16), getf(d + 20)};
        r.now = {getf(d + 24), getf(d + 28), getf(d + 32)};
        if (n == 36 + kRecordSize) r.record.assign(d + 36, d + n);
        r.count = sReady.count + 1;
        sReady = std::move(r);
    } else if (!std::memcmp(d, kMsgWhere, 4)) {
        sAskedWhere = true;
    }
}

// This game's own state, on the main thread.
struct Game {
    uint32_t scratch = 0;     // guest memory for the room calls
    int calm = 0;             // frames in a loaded area out of a session
    int asked_at = 0;         // a guest: frames since it last asked for the room data
    int tries = 0;            // room openings or joins tried since the last session
    bool room_sent = false;   // the host: this room's data made and sent
    int room_frames = 0;
    bool was_world = false;   // the host: in the world last frame
    Place arrival;            // the host: where it arrived after its latest load
    int ready_frames = 0;     // the host: frames since it last said where it is
    std::atomic<bool> arrived{false};  // a guest: in the host's world in this session (its load step reads it)
    bool in_load = false;     // a load seen since the last arrival
    uint32_t trip = 0;        // the host: its loads in this session; a guest: the host's load it follows
    std::atomic<uint32_t> follow{0};   // a guest: the trip its summoned reload follows (0: none)
    uint32_t record_buf = 0;  // a guest: guest memory for the host's dead-character record (its load step)
};
Game sGame;

// The player stands in a loaded area: no load asked for or under way, and a character.
bool in_world(uint32_t sm, uint32_t gm)
{
    const uint32_t man = rd(DES_AT(kChrMan));
    return vm_read32(gm + kGmNextArea) == kNoArea && vm_read32(sm + kSmArea) != kNoArea && guest_ok(man) &&
           guest_ok(rd(man + 0x1BC));
}

// The host's room step (0x00540D90), as a sign's touch calls it (0x003A1160): the manager *(*(base - 0x7F98)) and
// f1 = the float at base - 0x7F90, base = *(TOC - 0x79AC). Asks for a room; the game's callback makes it the host.
void open_room(ppu_context* ctx, uint32_t sm)
{
    const uint32_t base = rd(DES_AT(kSignSlot));
    const uint32_t mgr = rd(rd(base - 0x7F98u));
    if (!guest_ok(base) || mgr != sm) {
        std::fprintf(stderr, "[coop] can't open a room: the session manager isn't the sign code's (%08X, %08X)\n", mgr, sm);
        return;
    }
    const double f1 = ctx->fpr[1];
    ctx->fpr[1] = rdf(base - 0x7F90u);
    const uint32_t r = call_addr(ctx, DES_AT(0x00540D90u), {sm});
    ctx->fpr[1] = f1;
    std::fprintf(stderr, "[coop] room asked for (%u), session state %u\n", r, vm_read32(sm + kSmState));
}

// The room's data, as the sign code builds it for a summon (0x003A1E30, 0x003A2724..0x003A277C): the session's room
// object (0x0052C2D8), copied (0x00E00D10) and checked (0x00DFE110); a writer over a buffer (0x00A8BEA8); the join
// type -1 (0x00A8BCE0); the object encoded (0x00DFE2F0); its length (0x00A8B9D0); the copy destroyed (0x00DFE768).
std::vector<uint8_t> room_data(ppu_context* ctx, uint32_t sm)
{
    const uint32_t s = sGame.scratch;
    const uint32_t obj = s, wr = s + 0x200, tmp = s + 0x300, buf = s + 0x400;
    const uint32_t room = call_addr(ctx, DES_AT(0x0052C2D8u), {sm});
    call_addr(ctx, DES_AT(0x00E00D10u), {obj, room});
    const uint32_t valid = call_addr(ctx, DES_AT(0x00DFE110u), {obj}) & 0xFFu;
    call_addr(ctx, DES_AT(0x00A8BEA8u), {wr, buf, kRoomMax});
    vm_write32(tmp, 0xFFFFFFFFu);
    call_addr(ctx, DES_AT(0x00A8BCE0u), {wr, tmp, 4});
    call_addr(ctx, DES_AT(0x00DFE2F0u), {obj, wr});
    const uint32_t len = call_addr(ctx, DES_AT(0x00A8B9D0u), {wr});
    call_addr(ctx, DES_AT(0x00DFE768u), {obj});
    std::vector<uint8_t> out;
    if (valid && len > 4 && len <= kRoomMax)
        for (uint32_t i = 0; i < len; i++) out.push_back(vm_read8(buf + i));
    std::fprintf(stderr, "[coop] room data: %zu bytes%s\n", out.size(), valid ? "" : " (no room)");
    return out;
}

// A guest's join with the host's room data, as the sign code joins with a sign's answer (0x003A2510..0x003A2D18):
// the data copied (0x00011720), a reader over it (0x00A8B770), its join type read (0x00A8B580) and 0 written in its
// place (0x002107B8: -1, a sign's, makes a later arrival go wrong, E11), the room object built (0x00DFE9F8) and
// decoded (0x00DFF0D0), the join step (0x0053CFE8), the object destroyed (0x00DFE768).
void join(ppu_context* ctx, uint32_t sm, const std::vector<uint8_t>& data)
{
    const uint32_t s = sGame.scratch;
    const uint32_t obj = s, rdr = s + 0x200, tmp = s + 0x300, str = s + 0x400, dec = s + 0x700;
    const uint32_t n = (uint32_t)data.size();
    for (uint32_t i = 0; i < n; i++) vm_write8(str + i, data[i]);
    vm_write8(str + n, 0);
    call_addr(ctx, DES_AT(0x00011720u), {dec, str, n});
    call_addr(ctx, DES_AT(0x00A8B770u), {rdr, dec, n});
    vm_write32(tmp, 0xFFFFFFFFu);
    call_addr(ctx, DES_AT(0x00A8B580u), {rdr, tmp, 4});
    call_addr(ctx, DES_AT(0x002107B8u), {0});
    call_addr(ctx, DES_AT(0x00DFE9F8u), {obj});
    const uint32_t ok = call_addr(ctx, DES_AT(0x00DFF0D0u), {obj, rdr}) & 0xFFu;
    const uint32_t r = ok ? call_addr(ctx, DES_AT(0x0053CFE8u), {sm, obj}) : 0;
    call_addr(ctx, DES_AT(0x00DFE768u), {obj});
    std::fprintf(stderr, "[coop] joining the host's room: data %s, join %u, session state %u\n", ok ? "read" : "unreadable",
                 r, vm_read32(sm + kSmState));
}

// Each member on the member list (sm+0x13C) with a slot goes back on the waiting list (sm+0x128), as a room join's
// AddMember (0x0053D288, 0x0053D480..0x0053D534) puts it there: a 0x38-byte node from the list's allocator (its
// table's +0x2C), the member copied to node+8 (0x00E2B0B8), linked at the tail. 0x0052E050 then hands it to the
// placement step 0x001A05B8, once this game is in the world and the member's slot is ready.
void requeue(ppu_context* ctx, uint32_t sm)
{
    const uint32_t wl = sm + kSmWaiting, ml = sm + kSmMembers;
    const uint32_t wsent = rd(wl + 0xC), msent = rd(ml + 0xC), alloc = rd(wl);
    if (!guest_ok(wsent) || !guest_ok(msent) || !guest_ok(alloc) || vm_read32(wl + 0x10) != 0) return;
    int n = 0;
    for (uint32_t m = rd(msent); m != msent && guest_ok(m) && n < 8; m = rd(m)) {
        if ((int32_t)vm_read32(m + 0x38) == -1) continue;
        const uint32_t node = call_virtual(ctx, alloc, 0x2C, {alloc, 0x38, 8});
        if (!guest_ok(node)) break;
        const uint32_t tail = rd(wsent + 4);
        vm_write32(node + 0, wsent);
        vm_write32(node + 4, tail);
        call_addr(ctx, DES_AT(0x00E2B0B8u), {node + 8, m + 8});
        vm_write32(wl + 0x10, vm_read32(wl + 0x10) + 1);
        vm_write32(wsent + 4, node);
        vm_write32(tail + 0, node);
        n++;
    }
    if (n) std::fprintf(stderr, "[coop] %d player(s) to place again after the load\n", n);
}

Place player_place()
{
    Place p;
    des_player_xyz(&p.x, &p.y, &p.z);
    return p;
}

// This game's dead-character record of its area, built as a guest's join builds its own (0x004174A8): the world's
// characters *(*(*(TOC - 0x788C) - 0x7FFC)) written to a buffer by 0x002977C0(world, buf, 76). Empty if there is no
// world.
std::vector<uint8_t> dead_record(ppu_context* ctx)
{
    const uint32_t world = rd(rd(rd(DES_AT(kWorldSlot)) - 0x7FFCu));
    const uint32_t buf = sGame.scratch + 0xC00;
    std::vector<uint8_t> out;
    if (!guest_ok(world)) return out;
    call_addr(ctx, DES_AT(0x002977C0u), {world, buf, kRecordSize});
    for (uint32_t i = 0; i < kRecordSize; i++) out.push_back(vm_read8(buf + i));
    return out;
}

// The host tells its guests where it is: its area, where it arrived (a respawn's place), where it stands (a join's),
// and which of its area's characters are dead (a respawn's or a follow's load kills them as a join's does).
void send_ready(ppu_context* ctx, uint32_t sm, uint32_t trip, const Place& arrived)
{
    const Place now = player_place();
    std::vector<uint8_t> b;
    put32(b, trip);
    put32(b, vm_read32(sm + kSmArea));
    for (const Place& p : {arrived, now}) {
        putf(b, p.x);
        putf(b, p.y);
        putf(b, p.z);
    }
    const std::vector<uint8_t> rec = dead_record(ctx);
    b.insert(b.end(), rec.begin(), rec.end());
    send(kMsgReady, b);
}

void host_tick(ppu_context* ctx, uint32_t sm, uint32_t gm, uint32_t st, bool world)
{
    Game& g = sGame;
    bool asked, where;
    {
        std::lock_guard<std::mutex> l(sMsgLock);
        asked = sAsked;
        where = sAskedWhere;
        sAsked = sAskedWhere = false;
    }
    if (world && !g.was_world) g.arrival = player_place();   // its first frame in the world after a load
    g.was_world = world;
    if (st == 0) {
        g.room_sent = false;
        g.room_frames = 0;
        g.calm = world ? g.calm + 1 : 0;
        // once standing in the world (90 frames), then every 20 s while no room comes
        if (g.calm == 90 || (g.calm > 90 && (g.calm - 90) % 1200 == 0)) {
            g.tries++;
            open_room(ctx, sm);
        }
        return;
    }
    g.calm = 0;
    if (st != kStateHost) return;
    if (!g.room_sent && ++g.room_frames >= 30) {
        const std::vector<uint8_t> data = room_data(ctx, sm);
        if (!data.empty()) {
            g.room_sent = true;
            std::lock_guard<std::mutex> l(sMsgLock);
            sRoom = data;
        }
        asked = !data.empty();
    }
    if (asked && g.room_sent) {
        std::vector<uint8_t> data;
        {
            std::lock_guard<std::mutex> l(sMsgLock);
            data = sRoom;
        }
        send(kMsgRoom, data);
    }
    // The host's own load (a warp, a death): its guests follow, and once it has arrived they are told where.
    if (!g.in_load && vm_read32(gm + kGmNextArea) != kNoArea) {
        g.in_load = true;
        std::vector<uint8_t> b;
        put32(b, ++g.trip);
        send(kMsgLead, b);
        std::fprintf(stderr, "[coop] load %u started: the guests follow\n", g.trip);
    } else if (g.in_load && world && vm_read32(sm + kSmArrival) == 4) {
        g.in_load = false;
        requeue(ctx, sm);
        g.arrival = player_place();
        g.ready_frames = 0;
        send_ready(ctx, sm, g.trip, g.arrival);
        std::fprintf(stderr, "[coop] load %u done: area %08X\n", g.trip, vm_read32(sm + kSmArea));
    } else if (!g.in_load && world && (where || ++g.ready_frames >= 60)) {
        g.ready_frames = 0;
        send_ready(ctx, sm, g.trip, g.arrival);
    }
}

void guest_tick(ppu_context* ctx, uint32_t sm, uint32_t st, bool world)
{
    Game& g = sGame;
    if (st == 0) {
        g.arrived = false;
        g.in_load = false;
        g.calm = world ? g.calm + 1 : 0;
        std::vector<uint8_t> data;
        uint32_t lead;
        {
            std::lock_guard<std::mutex> l(sMsgLock);
            data = sRoom;
            lead = sLeadTrip;
        }
        g.trip = lead;   // a session starts: only the host's later loads are followed
        bool host_known;
        {
            std::lock_guard<std::mutex> l(sMsgLock);
            host_known = sReady.area != kNoArea;
        }
        if (data.empty() || !host_known) {   // the room data, and where the host is (its join loads the host's area)
            if (++g.asked_at >= 120) {   // every 120 frames until the host answers
                g.asked_at = 0;
                send(kMsgAsk);
            }
            return;
        }
        // standing in the world (150 frames), then every 20 s while not in a session
        if (g.calm == 150 || (g.calm > 150 && (g.calm - 150) % 1200 == 0)) {
            g.tries++;
            join(ctx, sm, data);
            if (g.tries % 2 == 0) {   // the host may have opened another room since: ask for its data again
                std::lock_guard<std::mutex> l(sMsgLock);
                sRoom.clear();
                sReady = Ready();
            }
        }
        return;
    }
    g.calm = 0;
    if (st != kStateClient) return;
    const bool here = world && vm_read32(sm + kSmArrival) == 4;
    if (!g.arrived) {
        if (here) {
            g.arrived = true;
            g.tries = 0;
            std::fprintf(stderr, "[coop] in the host's world\n");
        }
        return;
    }
    if (!world) g.in_load = true;
    else if (g.in_load && here) {   // arrived after a load in the session: the host and the others are placed again
        g.in_load = false;
        requeue(ctx, sm);
    }
    uint32_t lead;
    {
        std::lock_guard<std::mutex> l(sMsgLock);
        lead = sLeadTrip;
    }
    if (lead != g.trip && here) {
        g.trip = lead;
        g.follow = lead;
        call_addr(ctx, DES_AT(0x00441AB8u), {});   // SummonedMapReload
        std::fprintf(stderr, "[coop] following the host (its load %u)\n", lead);
    }
}

// A guest's summoned reload, at its load step (0x0017A358, this = r26, after its addi r9,r26,0x34; only a summoned
// reload gets there): its join, a follow, or its respawn after a death (the edited scripts' PartyGhostDeath_2). The
// map it loads is named by the string at this+0xB8 (size at +0xC8, capacity at +0xCC; 2-byte characters), not by
// gm+08. It names the host's area (same length, in place) and puts the player there, 1 m to the side:
//   - a join: beside the host;
//   - a follow: waits for the host's arrival, then beside it;
//   - a respawn: asks the host where it is, then goes where the host last arrived (the area's start after a trip or
//     a death).
// Which of the area's characters are dead: a join's load applies the host's record, which the game's own join brought
// (0x005286F4). A follow or a respawn brings none, so the host's latest one (READY) goes in its place (0x0020E788,
// slot 0); without it the area loads with every character alive, and the host's game then kills on this one the
// characters dead in its world, which pays this player their souls. A follow also drops this game's own record (slot
// 1), kept from before the join for its trip home: it is of another area by now.
void guest_load_step(ppu_context* ctx)
{
    const uint32_t trip = sGame.follow.exchange(0);
    const bool joined = sGame.arrived.load();
    const char* why = trip ? "follow" : joined ? "respawn" : "join";
    const uint32_t th = (uint32_t)ctx->gpr[26];
    const uint32_t size = rd(th + 0xC8), cap = rd(th + 0xCC);
    const uint32_t buf = cap > 7 ? rd(th + 0xB8) : th + 0xB8;
    if (!guest_ok(th) || !guest_ok(buf) || size < 12 || size > 120 || vm_read16(buf) != 'm') {
        std::fprintf(stderr, "[coop] %s: the load step's map name is not readable\n", why);
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    uint32_t seen = 0;
    if (joined && !trip) {   // a respawn: a fresh READY, with the host's latest dead characters
        std::lock_guard<std::mutex> l(sMsgLock);
        seen = sReady.count;
        send(kMsgWhere);
    }
    Ready r;
    for (;;) {
        {
            std::lock_guard<std::mutex> l(sMsgLock);
            r = sReady;
        }
        const auto waited = std::chrono::steady_clock::now() - t0;
        if (r.trip >= trip && r.area != kNoArea && (r.count != seen || waited > std::chrono::milliseconds(1500))) break;
        if (waited > std::chrono::seconds(90)) {
            std::fprintf(stderr, "[coop] %s: no word from the host; the load goes on as it was\n", why);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const Place at = trip || !joined ? r.now : r.arrived;
    char want[16];
    std::snprintf(want, sizeof want, "m%02u_%02u_%02u_%02u", r.area >> 24, (r.area >> 16) & 0xFFu, (r.area >> 8) & 0xFFu,
                  r.area & 0xFFu);
    int swapped = 0;
    for (uint32_t i = 0; i + 12 <= size; i++) {
        auto at = [&](uint32_t k) { return (char)vm_read16(buf + 2 * (i + k)); };
        if (at(0) != 'm' || at(3) != '_' || at(6) != '_' || at(9) != '_') continue;
        for (uint32_t k = 0; k < 12; k++) vm_write16(buf + 2 * (i + k), (uint16_t)want[k]);
        swapped++;
        i += 11;
    }
    const uint32_t gm = game_manager();
    const char* dead = "the join's";
    if (guest_ok(gm)) {
        for (uint32_t off : kGmPlace) {
            wrf(gm + off, at.x + 1.0f);   // 1 m to the side
            wrf(gm + off + 4, at.y);
            wrf(gm + off + 8, at.z);
        }
        if (joined || trip) {
            const uint32_t rb = sGame.record_buf;
            if (r.record.size() == kRecordSize && guest_ok(rb)) {
                for (uint32_t i = 0; i < kRecordSize; i++) vm_write8(rb + i, r.record[i]);
                call_addr(ctx, DES_AT(0x0020E788u), {kRecordSize, rb, 0});
                dead = "the host's";
            } else {
                vm_write32(gm + kGmHostRecord, 0);
                dead = "none";
            }
        }
        if (trip) vm_write32(gm + kGmOwnRecord, 0);
    }
    const long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "[coop] %s: the host is in %s, %s at %.2f %.2f %.2f; dead characters: %s (waited %ld ms, %d names)\n",
                 why, want, joined && !trip ? "arrived" : "stands", at.x, at.y, at.z, dead, ms, swapped);
}

}  // namespace

// From des_menu_tick, every frame, on the game's main thread.
extern "C" void des_seamless_tick(ppu_context* ctx)
{
    const co::Mode m = co::active();
    if (m == co::Mode::Off) return;
    static bool once = false;
    if (!once) {
        once = true;
        psnr_app_on_message(on_message);
    }
    const uint32_t sm = session_manager(), gm = game_manager();
    if (!guest_ok(sm) || !guest_ok(gm)) return;
    if (!sGame.scratch) sGame.scratch = guest_alloc(ctx, 0x1000);
    if (!sGame.record_buf) sGame.record_buf = guest_alloc(ctx, 0x100);
    if (!sGame.scratch) return;
    const uint32_t st = vm_read32(sm + kSmState);
    const bool world = in_world(sm, gm);
    if (m == co::Mode::Host) host_tick(ctx, sm, gm, st, world);
    else guest_tick(ctx, sm, st, world);
}

// Instruction hooks (port/insn_hooks.txt), with the US disc's addresses as keys.
extern "C" void des_coop_hook(ppu_context* ctx, uint32_t addr)
{
    const co::Mode m = co::active();
    if (m == co::Mode::Off) return;
    switch (addr) {
    case 0x0017A4A8u:   // the summoned reload's load step, after addi r9,r26,0x34
        if (m == co::Mode::Join) guest_load_step(ctx);
        break;
    case 0x0053B31Cu:   // the session's leave (0x0053B300), after lwz r9,0x9C(r3): 0 takes its "no session" return
        if (m == co::Mode::Host && (uint32_t)ctx->gpr[9] == kStateHost) {
            static uint32_t last = 0;
            const uint32_t from = (uint32_t)ctx->lr - 4;
            if (from != last) std::fprintf(stderr, "[coop] the host stays in its session (a leave from %08X)\n", from);
            last = from;
            ctx->gpr[9] = 0;
        }
        break;
    case 0x00A6B2F8u:   // the game's Lua reader, after lwz r11,8(r4): ud = {buf, size, pos}; pos 0 is the first read
    case 0x00CA15A8u: { // Lua's string reader (getS), after lwz r0,4(r4): ud = {s, size}; size 0 after the first read
        const uint32_t ud = (uint32_t)ctx->gpr[4];
        if (!guest_ok(ud)) break;
        const bool game_reader = addr == 0x00A6B2F8u;
        if (game_reader && vm_read32(ud + 8) != 0) break;
        uint32_t to = 0, size = 0;
        if (!des_seamless_script(ctx, vm_read32(ud), vm_read32(ud + 4), &to, &size)) break;
        vm_write32(ud, to);
        vm_write32(ud + 4, size);
        if (!game_reader) ctx->gpr[0] = size;   // the size it just read
        break;
    }
    default:
        break;
    }
}
