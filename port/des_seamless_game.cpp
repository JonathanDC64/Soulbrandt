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
//     after a trip or a death). It stays in the session. Once it is back in the world it tells the others (BACK),
//     and each makes its character again, as after a load: the copy that died stays in its death animation.
//   - The host's death: its respawn is a load like any other, so the guests follow it.
// The flags: a guest sees the host's world, and keeps its own world apart.
//   - A phantom's game plays with the host's flags (the join brings them; each summoned reload puts them back from
//     a copy, +0x20CC). The host sends its flags with each READY (FLGS), so that copy is the host's world as it is
//     now, not as it was at the join.
//   - A load into another world keeps the old world's own flags in its bank and takes the new world's. On a guest,
//     the old world's are the host's, and the new world's are the guest's own. So at the join the guest's own banks
//     are kept apart, put back after each load in the session, and the host's own flags of its world take the place
//     of the guest's.
//   - The trip home puts back the guest's own flags (+0x187C) after its load; its save holds what that copy and its
//     banks hold.
// Progress for everyone (the lists are des_seamless_scripts.cpp's: boss kills, doors, levers, elevators, shortcuts,
// bridges, fog walls):
//   - A progress flag set on a guest in a session also goes into its own world: the home copy, or the bank of the
//     world it was set in. The host tells its guests each progress flag set in its world (FLAG), so a guest gets the
//     ones its own game didn't set (a lever pulled far away). Flags only move forward: a flag cleared is never sent
//     or kept.
//   - A boss's soul taken by a guest: kept for its own world too, so its own world doesn't offer it again.
//   - The Archstones' seal (the Monumental's tale heard): its handlers run in the hearer's game alone, so a guest that
//     opens it tells the host (FLAG), whose world takes it and passes it on as its own progress.
// The Nexus in a session (shared, as the host has it):
//   - The NPCs a visitor's game would set up as phantoms (drawn as such) stay as the host has them, and a guest can
//     talk to them: the talk scripts' phantom check sees it as alive.
//   - Each Nexus cutscene, and the Monumental's tale, plays for each player once: a guest's flag in play for one is
//     the host's and its own, and the guest keeps the flag in its own world once it has played.
#include "des_seamless.h"

#include "ps3emu/ppu_lift.h"
#include "ps3emu/psnr_host.h"
#include "des_region.h"      // DES_AT: the running disc's addresses

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <initializer_list>
#include <mutex>
#include <thread>
#include <vector>

#ifndef PS3_GAME_SPLIT   // DES_SPLIT: the game DLL's (ps3emu/game_split.h)
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
#endif
extern "C" void ds_malloc(ppu_context* ctx);                     // des_heap.cpp: the guest heap
bool des_seamless_script(ppu_context* ctx, uint32_t buf, uint32_t size, uint32_t* out, uint32_t* out_size);  // _scripts.cpp
int des_seamless_flag_kind(uint32_t world, uint32_t flag);       // _scripts.cpp: 1 progress, 3 a boss kill's, 2 a soul pickup, 4 a Nexus cutscene, 5 the Archstones' seal
extern "C" void ps3_indirect_call(ppu_context* ctx);             // ppu_loader.cpp: a call to the guest address in CTR
extern "C" int des_player_xyz(float* x, float* y, float* z);     // des_options.cpp: 0 with no player
extern "C" int np_psnr_connected(void);                         // ps3recomp's np_psnr.c: connected to the room server

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
constexpr uint32_t kGmSaveAsked = 0xA97;   // u8: a save asked for (SaveRequest_Profile's 0x0020FBA8; SaveRequest too)
constexpr uint32_t kGmSaveAsked2 = 0xA98;  // u8: SaveRequest's second request
constexpr uint32_t kGmSaveTask = 0xAA4;    // u32: the save task, 0 idle (1 and 3 while it saves: 0x00211A50, 0x0020F738)
constexpr uint32_t kGmNextPoint = 0x0C;                      // the next load's warp point (-1: a place)
constexpr uint32_t kGmSummonedPlace = 0x1C;                  // a byte: the next load puts the player at +0x20
constexpr uint32_t kGmPlace[4] = {0x20, 0xA40, 0xAE0, 0xB20}; // where a summoned reload puts the player (x, y, z)
// The dead-character records ("\0CHR", one bit per character of the area; always 76 bytes, 0x00294B10): the host's
// (slot 0, bytes at +0xB34), which a summoned reload's load applies, and this game's own (slot 1, +0xB3C), which its
// own loads apply. A load's pass (0x002953F0) kills the characters marked, with no souls paid, and empties the slot.
constexpr uint32_t kGmHostRecord = 0xB34;
constexpr uint32_t kGmOwnRecord = 0xB3C;
constexpr uint32_t kRecordSize = 0x4C;
// The event flags, *(*(*(TOC - 0x7B44) - 0x8000)): flag n is bit n & 7 of byte n >> 3, 17,024 of them (0x850 bytes;
// the setter 0x005086F8). The structure also holds:
//   - from a guest's join (0x00508C00): its own flags (+0x187C), which the trip home puts back (0x00508BA8), and the
//     host's (+0x20CC), which each summoned reload puts in place (0x00508B70);
//   - the current area (+0x3AD0);
//   - each world's own flags (the world: an area's top byte - 1, 0-7): flags 0-3199 (bytes 0-0x18F) in a bank at
//     +0x850 + w * 0x190, and 8192-12287 (bytes 0x400-0x5FF) at +0x291C + w * 0x200. A load into another world
//     (0x005098D8) keeps the old world's there, clears flags 0-4095 and 8192-12287 and takes the new world's.
constexpr uint32_t kFlagCount = 0x4280, kFlagBytes = 0x850;
constexpr uint32_t kFlagsHome = 0x187C, kFlagsReload = 0x20CC, kFlagsArea = 0x3AD0;
constexpr uint32_t kBank1 = 0x850, kBank1Size = 0x190, kBank2 = 0x291C, kBank2Size = 0x200, kWorlds = 8;
constexpr uint32_t kLocal2 = 0x400;   // the byte of flag 8192
constexpr uint32_t kBanksSize = kWorlds * (kBank1Size + kBank2Size);
constexpr uint32_t kFlagChunk = 1000;   // FLGS: the flags in three messages (a message is at most 1024 bytes)
constexpr uint32_t kFlagReloadGuard = 4047;   // global_event.lua: a reload event under way (a trip home), no other

bool world_flag(uint32_t flag) { return flag < 3200 || (flag >= 8192 && flag < 12288); }
uint32_t world_of(uint32_t area) { return (area >> 24) - 1; }   // 0xFFFFFFFF with no area

constexpr uint32_t kNoArea = 0xFFFFFFFFu;

// Messages between the players' games (psnr_app_send): a 4-letter tag, then little-endian fields.
constexpr char kMsgRoom[4] = {'R', 'O', 'O', 'M'};   // the host's room data
constexpr char kMsgAsk[4] = {'R', 'O', 'M', '?'};    // a guest asks for it
constexpr char kMsgLead[4] = {'L', 'E', 'A', 'D'};   // u32 trip: the host's load started
// u32 trip, u32 area, f32 x, y, z where it arrived, f32 x, y, z where it is, then its area's dead-character record
// (76 bytes, or none): the host has arrived (after each load), every 2 s while it hosts, and when a guest asks
constexpr char kMsgReady[4] = {'R', 'E', 'D', 'Y'};
constexpr char kMsgWhere[4] = {'H', 'E', 'R', '?'};  // a guest asks for a READY now (its respawn)
constexpr char kMsgFlag[4] = {'F', 'L', 'A', 'G'};   // u32 flag, u32 area: a progress flag set in the host's world (from a guest: the seal)
// u32 area, u32 offset, then up to kFlagChunk bytes of the host's flags from that offset (with each READY)
constexpr char kMsgFlags[4] = {'F', 'L', 'G', 'S'};
constexpr char kMsgBack[4] = {'B', 'A', 'C', 'K'};   // u32 player id: a guest back in the world after its respawn
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
uint32_t flag_base() { return rd(rd(rd(DES_AT(kGameSlot)) - 0x8000u)); }


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
// DES_GAME_CALLS: those must exist on every disc.)
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
struct HostFlag { uint32_t flag, area; };
std::deque<HostFlag> sHostFlags;     // FLAGs not yet taken: a guest's from the host, the host's from its guests
std::deque<uint32_t> sBack;          // BACKs not yet taken: the player ids
struct HostFlags {
    uint32_t area = kNoArea;
    std::vector<uint8_t> bytes = std::vector<uint8_t>(kFlagBytes);
    uint32_t parts = 0;              // the chunks received for this area since the last full set (a bit each)
    bool fresh = false;              // a full set not yet put in place
};
HostFlags sHostFlagSet;              // the host's flags (a guest)

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
    } else if (!std::memcmp(d, kMsgFlag, 4) && n >= 12 && get32(d + 4) < kFlagCount && sHostFlags.size() < 4096) {
        sHostFlags.push_back({get32(d + 4), get32(d + 8)});
    } else if (!std::memcmp(d, kMsgBack, 4) && n >= 8 && sBack.size() < 16) {
        sBack.push_back(get32(d + 4));
    } else if (!std::memcmp(d, kMsgFlags, 4) && n > 12) {
        HostFlags& f = sHostFlagSet;
        const uint32_t area = get32(d + 4), off = get32(d + 8), len = n - 12;
        if (off % kFlagChunk || off >= kFlagBytes || len > kFlagBytes - off || len > kFlagChunk) return;
        if (area != f.area) {
            f.area = area;
            f.parts = 0;
        }
        std::memcpy(f.bytes.data() + off, d + 12, len);
        f.parts |= 1u << (off / kFlagChunk);
        const uint32_t all = (1u << ((kFlagBytes + kFlagChunk - 1) / kFlagChunk)) - 1;
        if ((f.parts & all) == all) {
            f.parts = 0;
            f.fresh = true;
        }
    }
}

// This game's own state, on the main thread.
struct Game {
    uint32_t scratch = 0;     // guest memory for the room calls
    int calm = 0;             // frames in a loaded area out of a session
    int asked_at = 0;         // a guest: frames since it last asked for the room data
    int tries = 0;            // room openings or joins tried since the last session
    bool connected = false;   // a guest: connected to the room server (the host's game) at least once
    bool host_gone = false;   // a guest: that connection lost since; said once
    bool home_wait = false;   // a guest: its game is taking it home (flag 4047): no join until it's there; said once
    bool room_sent = false;   // the host: this room's data made and sent
    int room_frames = 0;
    bool was_world = false;   // the host: in the world last frame
    Place arrival;            // the host: where it arrived after its latest load
    int ready_frames = 0;     // the host: frames since it last said where it is
    std::atomic<bool> arrived{false};  // a guest: in the host's world in this session (its load step reads it)
    bool in_load = false;     // a load seen since the last arrival
    uint32_t trip = 0;        // the host: its loads in this session; a guest: the host's load it follows
    std::atomic<uint32_t> follow{0};   // a guest: the trip its summoned reload follows (0: none)
    std::atomic<bool> respawn{false};  // a guest: its summoned reload is a respawn (BACK once it has arrived)
    bool loading = false;     // a guest: a load asked for (gm's next area) and the world not back since (Quit Game
                              // to the title asks for none)
    uint32_t record_buf = 0;  // a guest: guest memory for the host's dead-character record (its load step)
};
Game sGame;

// A guest's own world while it plays in the host's: its banks from the join (both kinds, world by world, its home
// world's from the flags in play) with the progress since, its home world (the world of its own flags' copy), and
// where it stood. Read and written by hooks on the game's threads and by the main thread.
struct Own {
    std::mutex lock;
    bool kept = false;
    uint32_t home = kNoArea;
    uint32_t area = kNoArea;    // its area at the join, and where it stood
    float x = 0, y = 0, z = 0;
    bool stood = false;
    bool helped = false;        // a boss was killed while it was in the session: it goes home revived
    bool body = false;          // it was in body form before the join: it goes home in body form
    bool sent = false;          // its trip home was pointed at area (going_home)
    bool came_home = false;     // a trip home after a session came in this run (after_world_swap), at home_at
    std::filesystem::file_time_type home_at{};
    std::vector<uint8_t> flags = std::vector<uint8_t>(kFlagBytes);   // its own flags (the trip home's copy, +0x187C)
    std::vector<uint8_t> banks = std::vector<uint8_t>(kBanksSize);
    std::vector<uint32_t> taken;   // the boss souls it took in the session (its view keeps them taken)
};
Own sOwn;
std::atomic<bool> sJoinedInBody{false};   // set by guest_tick before a join from body form; taken by keep_own_world
uint32_t sJoinedDeaths = 0;   // the game thread's: the player's death count when it took soul form for the join

// The player's death count (0x0090BF60 reads what the scripts' AddDeathCount adds to at each death).
uint32_t death_count(ppu_context* ctx)
{
    return call_addr(ctx, DES_AT(0x0090BF60u), {});
}

// A friend that took soul form for a join that never came to a load (its Leave, or the host's game gone): its body
// form back where it stands. ChangeGreyGhost changes the form only, and the load after it makes the soul form's own
// changes, so it is given back as global_event.lua's OnDemonsSoulRevive gives it (EraseEventSpecialEffect(10000,
// 101), RevivePlayer), with no animation. Its game may have saved it in soul form while it waited, so it saves
// (SaveRequest_Profile). False if none is owed (no soul form taken, or a death since: guest_tick), or not yet: with
// no player (a load), it stays owed (sJoinedInBody) for a later call.
bool body_form_back(ppu_context* ctx)
{
    if (!sJoinedInBody.load()) return false;
    const int32_t form = (int32_t)call_addr(ctx, DES_AT(0x0043E660u), {0});   // GetLocalPlayerChrType: -1 no player
    if (form < 0 || !sJoinedInBody.exchange(false) || form == 0) return false;
    call_addr(ctx, DES_AT(0x00446C68u), {0, 10000, 101});   // EraseEventSpecialEffect(10000, 101)
    call_addr(ctx, DES_AT(0x0043EE10u), {0});               // RevivePlayer
    call_addr(ctx, DES_AT(0x004418B8u), {0});               // SaveRequest_Profile
    return true;
}

bool own_kept()
{
    std::lock_guard<std::mutex> l(sOwn.lock);
    return sOwn.kept;
}

// A boss was killed with this guest in the session: as the game revives a blue phantom after a boss kill, it goes
// home in body form.
void helped_kill()
{
    std::lock_guard<std::mutex> l(sOwn.lock);
    if (sOwn.kept && !sOwn.helped) {
        sOwn.helped = true;
        std::fprintf(stderr, "[coop] a boss killed with this player in the session: it goes home revived\n");
    }
}

// The load that takes a guest home (WarpNextStageKick, out of the session) asks for the area it was in, where it
// stood (the game's trip home reloads the area in play: a phantom of the game's own stands in its own area). Into the
// game manager's next load: the area (+0x08) and the summoned place (+0x20 and its copies; SetSummonedPos set +0x1C).
void going_home(uint32_t gm)
{
    std::lock_guard<std::mutex> l(sOwn.lock);
    if (!sOwn.kept || sOwn.area == kNoArea || !guest_ok(gm)) return;
    const uint32_t was = vm_read32(gm + kGmNextArea);
    vm_write32(gm + kGmNextArea, sOwn.area);
    vm_write32(gm + kGmNextPoint, 0xFFFFFFFFu);
    sOwn.sent = true;
    if (sOwn.stood) {
        vm_write8(gm + kGmSummonedPlace, 1);
        for (uint32_t off : kGmPlace) {
            wrf(gm + off, sOwn.x);
            wrf(gm + off + 4, sOwn.y);
            wrf(gm + off + 8, sOwn.z);
        }
    }
    std::fprintf(stderr, "[coop] going home: area %08X (the game asked for %08X), at %.2f %.2f %.2f\n", sOwn.area, was,
                 sOwn.x, sOwn.y, sOwn.z);
}

// The end of SetChrTypeDataGreyNext on a guest going home (its form after the next load: soul form, since it joined
// in soul form). In body form instead:
// - after a boss kill, revived as global_event.lua's BlockClear2_3 revives a blue phantom (SetAliveMotion,
//   EraseEventSpecialEffect(10000, 101), RequestFullRecover, RevivePlayerNext);
// - if it was in body form before the join (it took soul form for it), its body form back as a revival gives it,
//   with full health (EraseEventSpecialEffect(10000, 101), RequestFullRecover, RevivePlayerNext), but no revival
//   animation.
void going_home_form(ppu_context* ctx)
{
    bool helped, body;
    {
        std::lock_guard<std::mutex> l(sOwn.lock);
        if (!sOwn.kept) return;
        helped = sOwn.helped;
        body = sOwn.body;
    }
    if (!helped && !body) return;
    if (helped) call_addr(ctx, DES_AT(0x004457A8u), {0, 1});   // SetAliveMotion(true)
    call_addr(ctx, DES_AT(0x00446C68u), {0, 10000, 101});      // EraseEventSpecialEffect(10000, 101)
    call_addr(ctx, DES_AT(0x00439358u), {0});                  // RequestFullRecover
    call_addr(ctx, DES_AT(0x00439288u), {0});                  // RevivePlayerNext
    std::fprintf(stderr, "[coop] going home: in body form (%s)\n",
                 helped ? "revived: a boss was killed in the session" : "as before the join");
}

// Where flag is in Own::banks for world w, or -1 if it isn't a world's own flag.
long own_bank_at(uint32_t w, uint32_t flag)
{
    if (w >= kWorlds || !world_flag(flag)) return -1;
    const uint32_t at = flag >> 3;
    return flag < 3200 ? (long)(w * kBank1Size + at) : (long)(kWorlds * kBank1Size + w * kBank2Size + (at - kLocal2));
}

// A flag into this guest's own world (world: the one it was set in); false if it was there already. A world's own
// flag goes into that world's kept bank, and for the home world into the home copy too; any other into the home copy.
bool keep_flag(uint32_t fb, uint32_t world, uint32_t flag)
{
    const uint8_t bit = (uint8_t)(1u << (flag & 7));
    bool changed = false;
    std::lock_guard<std::mutex> l(sOwn.lock);
    if (!sOwn.kept) return false;
    if (!world_flag(flag) || world == sOwn.home) {
        if (!(sOwn.flags[flag >> 3] & bit)) {
            sOwn.flags[flag >> 3] |= bit;
            changed = true;
        }
        const uint32_t a = fb + kFlagsHome + (flag >> 3);
        vm_write8(a, (uint8_t)(vm_read8(a) | bit));
    }
    const long b = own_bank_at(world, flag);
    if (b >= 0 && !(sOwn.banks[b] & bit)) {
        sOwn.banks[b] |= bit;
        changed = true;
    }
    return changed;
}

// A guest's join is about to fill the flags' copies (0x00508C00, before its first load into the host's world, with
// its own flags still in play): its own flags and banks, kept apart for the session. A join again before the trip
// home (the session ended with the guest still in the host's world, which then plays with the host's flags) keeps
// the first join's: they are still the guest's own.
void keep_own_world(uint32_t fb)
{
    std::lock_guard<std::mutex> l(sOwn.lock);
    if (sOwn.kept) {
        std::fprintf(stderr, "[coop] flags: a join again before going home; the own world stays as kept\n");
        return;
    }
    for (uint32_t i = 0; i < kFlagBytes; i++) sOwn.flags[i] = vm_read8(fb + i);
    for (uint32_t i = 0; i < kWorlds * kBank1Size; i++) sOwn.banks[i] = vm_read8(fb + kBank1 + i);
    for (uint32_t i = 0; i < kWorlds * kBank2Size; i++) sOwn.banks[kWorlds * kBank1Size + i] = vm_read8(fb + kBank2 + i);
    sOwn.area = vm_read32(fb + kFlagsArea);
    sOwn.home = world_of(sOwn.area);
    if (sOwn.home < kWorlds) {   // the world it stands in: its flags in play are newer than its bank
        for (uint32_t i = 0; i < kBank1Size; i++) sOwn.banks[sOwn.home * kBank1Size + i] = sOwn.flags[i];
        for (uint32_t i = 0; i < kBank2Size; i++)
            sOwn.banks[kWorlds * kBank1Size + sOwn.home * kBank2Size + i] = sOwn.flags[kLocal2 + i];
    }
    sOwn.stood = des_player_xyz(&sOwn.x, &sOwn.y, &sOwn.z) != 0;
    sOwn.helped = false;
    sOwn.body = sJoinedInBody.exchange(false);
    sOwn.sent = false;
    sOwn.taken.clear();
    sOwn.kept = true;
    std::fprintf(stderr, "[coop] flags: this player's own world is kept apart for the session (home world %u, area %08X%s)\n",
                 sOwn.home, sOwn.area, sOwn.stood ? "" : ", no place");
}

// After a load's world swap (0x005098D8) on a guest: its own banks back in place. In a session, the host's flags
// (the copy +0x20CC: the join's, then each READY's) in place of the ones the swap took from this guest's banks, and
// the boss souls this guest took still taken. Out of a session this is the trip home (its own flags already back,
// 0x00508BA8, its home world's in play): the swap put those in the bank of the world it left, and took the new
// world's from its bank, which for the world it left is now those; so the new world's come from the kept banks. The
// trip goes to the area it had at the join (going_home), or else reloads the area in play (+0x3AD0, not yet the new
// one here). The own world is the game's own again.
void after_world_swap(uint32_t fb, bool in_session)
{
    std::lock_guard<std::mutex> l(sOwn.lock);
    if (!sOwn.kept) return;
    for (uint32_t i = 0; i < kWorlds * kBank1Size; i++) vm_write8(fb + kBank1 + i, sOwn.banks[i]);
    for (uint32_t i = 0; i < kWorlds * kBank2Size; i++) vm_write8(fb + kBank2 + i, sOwn.banks[kWorlds * kBank1Size + i]);
    if (!in_session) {
        const uint32_t to = sOwn.sent ? sOwn.area : vm_read32(fb + kFlagsArea);
        const uint32_t w = world_of(to);
        if (w < kWorlds) {
            for (uint32_t i = 0; i < kBank1Size; i++) vm_write8(fb + i, sOwn.banks[w * kBank1Size + i]);
            for (uint32_t i = 0; i < kBank2Size; i++)
                vm_write8(fb + kLocal2 + i, sOwn.banks[kWorlds * kBank1Size + w * kBank2Size + i]);
        }
        sOwn.kept = false;
        sOwn.came_home = true;
        sOwn.home_at = std::filesystem::file_time_type::clock::now();
        std::fprintf(stderr, "[coop] flags: back in this player's own world (%08X), with the session's progress\n", to);
        return;
    }
    for (uint32_t i = 0; i < kFlagBytes; i++) vm_write8(fb + kFlagsHome + i, sOwn.flags[i]);   // a join again refills it
    for (uint32_t i = 0; i < kFlagBytes; i++) vm_write8(fb + i, vm_read8(fb + kFlagsReload + i));
    for (uint32_t f : sOwn.taken) vm_write8(fb + (f >> 3), (uint8_t)(vm_read8(fb + (f >> 3)) | (1u << (f & 7))));
}

// Leaving (a guest: the Co-op window's Leave). The game's own session leave leaves a phantom standing in the host's
// world, so the guest does what its game does when the host's room is gone (global_event.lua's OnRoomDisappeared,
// then OnLeave_Limit): it leaves the session (LeaveSession's native) and asks for its own flags back at the next load
// (0x00508820: SetFlagInitState(2)); once out of the session it goes back to where it stood before the join
// (SetSummonedPos, SetDefaultMapUid(-1)), in soul form (SetChrTypeDataGreyNext), with a load (WarpNextStageKick).
// Its game saves once home: the leave is done when the save folder has a file written since the arrival and the
// game's save is over (save_idle).
enum { kLeaveNone, kLeaveAsked, kLeaveOut, kLeaveGoing, kLeaveHome, kLeaveDone };
std::atomic<int> sLeave{kLeaveNone};
struct LeaveState {   // the game thread's only
    bool seen = false;
    std::chrono::steady_clock::time_point asked{};
    std::filesystem::file_time_type home_at{};
};
LeaveState sLeaveState;

// A file under the save folder (PS3_SAVEDATA_ROOT, co-op's own) written after t.
bool saved_since(std::filesystem::file_time_type t)
{
    const char* root = std::getenv("PS3_SAVEDATA_ROOT");
    std::error_code ec;
    if (!root || !*root) return false;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && it->last_write_time(e2) > t && !e2) return true;
    }
    return false;
}

// The game's saving is over: no save asked for and its save task idle. A file written since a moment is only the start
// of a save (PARAM.SFO goes first, the data after), and the restart after a leave ends the process at once.
bool save_idle()
{
    const uint32_t gm = game_manager();
    return guest_ok(gm) && !vm_read8(gm + kGmSaveAsked) && !vm_read8(gm + kGmSaveAsked2) && !vm_read32(gm + kGmSaveTask);
}

// The latest trip home after a session in this run (the game's own, or a leave's), at *at, if its game hasn't saved
// since it arrived (it saves once home).
bool home_unsaved(std::filesystem::file_time_type* at)
{
    {
        std::lock_guard<std::mutex> l(sOwn.lock);
        if (!sOwn.came_home) return false;
        *at = sOwn.home_at;
    }
    return !(saved_since(*at) && save_idle());
}

void leave_tick(ppu_context* ctx, uint32_t st, bool world)
{
    const int lv = sLeave.load();
    if (lv == kLeaveNone || lv == kLeaveDone) return;
    LeaveState& s = sLeaveState;
    const auto now = std::chrono::steady_clock::now();
    if (!s.seen) {
        s.seen = true;
        s.asked = now;
    }
    if (now - s.asked > std::chrono::seconds(120)) {
        std::fprintf(stderr, "[coop] leave: not home and saved after 2 minutes; leaving all the same\n");
        sLeave = kLeaveDone;
        return;
    }
    if (lv == kLeaveAsked) {
        if (st == kStateClient && sGame.arrived.load() && world) {
            call_addr(ctx, DES_AT(0x00447258u), {0});                // LeaveSession
            call_addr(ctx, DES_AT(0x00508820u), {flag_base(), 2});   // SetFlagInitState(2)
            sLeave = kLeaveOut;
            std::fprintf(stderr, "[coop] leave: out of the host's session; going home\n");
        } else if (st == 0 && own_kept() && world) {   // out of the session but still in the host's world
            call_addr(ctx, DES_AT(0x00508820u), {flag_base(), 2});
            sLeave = kLeaveOut;
            std::fprintf(stderr, "[coop] leave: going home\n");
        } else if (st == 0 && !own_kept()) {   // not in a session: nothing to take home
            // During a load (the game's own trip home after a session, say) it waits for the player in the world.
            // Back from a session with no save since, it waits for that save, as its own trip home does. Its body
            // form back if it took soul form for a join that never came (body_form_back), and it waits for the save
            // that asks for. Else (the title screen, say) it's done.
            std::filesystem::file_time_type home{};
            const bool unsaved = home_unsaved(&home);
            if (!world && (sGame.loading || unsaved)) {
                // again next frame
            } else if (world && body_form_back(ctx)) {
                s.home_at = std::filesystem::file_time_type::clock::now();
                sLeave = kLeaveHome;
                std::fprintf(stderr, "[coop] leave: not in a session; body form again (the join never came), "
                                     "waiting for the game's save\n");
            } else if (unsaved) {
                s.home_at = home;
                sLeave = kLeaveHome;
                std::fprintf(stderr, "[coop] leave: back home from the session; waiting for the game's save\n");
            } else {
                sLeave = kLeaveDone;
                std::fprintf(stderr, "[coop] leave: not in a session\n");
            }
        }
    } else if (lv == kLeaveOut) {
        if (st == 0) {
            call_addr(ctx, DES_AT(0x0043EDE0u), {0});       // SetSummonedPos
            call_addr(ctx, DES_AT(0x0044A368u), {0, -1});   // SetDefaultMapUid(-1)
            call_addr(ctx, DES_AT(0x0044C6F0u), {0});       // WarpNextStageKick
            call_addr(ctx, DES_AT(0x00447EA0u), {0});       // SetChrTypeDataGreyNext
            sLeave = kLeaveGoing;
        }
    } else if (lv == kLeaveGoing) {
        if (!own_kept() && st == 0 && world) {   // the trip home's load is done (after_world_swap)
            s.home_at = std::filesystem::file_time_type::clock::now();
            sLeave = kLeaveHome;
            std::fprintf(stderr, "[coop] leave: home; waiting for the game's save\n");
        }
    } else if (lv == kLeaveHome) {
        if (saved_since(s.home_at) && save_idle()) {   // a save since the arrival, and finished
            sLeave = kLeaveDone;
            std::fprintf(stderr, "[coop] leave: saved at home; done\n");
        }
    }
}

// The host's latest flags (FLGS) into the copy each summoned reload puts in place, if a full set came since.
void take_host_flags(uint32_t fb)
{
    std::lock_guard<std::mutex> l(sMsgLock);
    HostFlags& f = sHostFlagSet;
    if (!f.fresh || !guest_ok(fb)) return;
    f.fresh = false;
    for (uint32_t i = 0; i < kFlagBytes; i++) vm_write8(fb + kFlagsReload + i, f.bytes[i]);
}

// The host's flags to its guests, in chunks (before each READY, so a guest's load step has them once READY comes).
void send_flags(uint32_t sm)
{
    const uint32_t fb = flag_base();
    if (!guest_ok(fb)) return;
    for (uint32_t off = 0; off < kFlagBytes; off += kFlagChunk) {
        std::vector<uint8_t> b;
        put32(b, vm_read32(sm + kSmArea));
        put32(b, off);
        for (uint32_t i = off; i < kFlagBytes && i < off + kFlagChunk; i++) b.push_back(vm_read8(fb + i));
        send(kMsgFlags, b);
    }
}

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

// A member of the member list (sm+0x13C) back on the waiting list (sm+0x128), as a room join's AddMember (0x0053D288,
// 0x0053D480..0x0053D534) puts it there: a 0x38-byte node from the list's allocator (its table's +0x2C), the member
// copied to node+8 (0x00E2B0B8), linked at the tail. 0x0052E050 then hands it to the placement step 0x001A05B8, once
// this game is in the world and the member's slot is ready.
bool wait_again(ppu_context* ctx, uint32_t sm, uint32_t m)
{
    const uint32_t wl = sm + kSmWaiting;
    const uint32_t wsent = rd(wl + 0xC), alloc = rd(wl);
    if (!guest_ok(wsent) || !guest_ok(alloc)) return false;
    const uint32_t node = call_virtual(ctx, alloc, 0x2C, {alloc, 0x38, 8});
    if (!guest_ok(node)) return false;
    const uint32_t tail = rd(wsent + 4);
    vm_write32(node + 0, wsent);
    vm_write32(node + 4, tail);
    call_addr(ctx, DES_AT(0x00E2B0B8u), {node + 8, m + 8});
    vm_write32(wl + 0x10, vm_read32(wl + 0x10) + 1);
    vm_write32(wsent + 4, node);
    vm_write32(tail + 0, node);
    return true;
}

// After a load, each member with a slot (+0x38) goes back on the waiting list.
void requeue(ppu_context* ctx, uint32_t sm)
{
    const uint32_t wl = sm + kSmWaiting, ml = sm + kSmMembers;
    const uint32_t wsent = rd(wl + 0xC), msent = rd(ml + 0xC), alloc = rd(wl);
    if (!guest_ok(wsent) || !guest_ok(msent) || !guest_ok(alloc) || vm_read32(wl + 0x10) != 0) return;
    int n = 0;
    for (uint32_t m = rd(msent); m != msent && guest_ok(m) && n < 8; m = rd(m)) {
        if ((int32_t)vm_read32(m + 0x38) == -1) continue;
        if (!wait_again(ctx, sm, m)) break;
        n++;
    }
    if (n) std::fprintf(stderr, "[coop] %d player(s) to place again after the load\n", n);
}

// A guest back after its respawn (BACK). This game's character for it is the one that died, and a character that died
// stays in its death animation: its fade (+0x35C) is held at 0 by the animation's events (0x002AAE10, 0x002AF084), and
// its rolls, attacks and other moves don't play, only where it goes. The game never brings a phantom back from a death
// (it goes home), so that character is taken out as the game takes out a member who left (0x0053BAE0..0x0053BB24:
// 0x0029EC08), and its member goes back on the waiting list: the placement step makes a new one, as after a load.
// The character is the one among the others with that player id (+0x498; the vector at the character manager's +0x19C,
// begin +0x1A0, end +0x1A4, as 0x002945F8 looks one up). Its member is the one whose id is the character's, matched as
// 0x00297410 does: the character's id made (0x002D0EB0), compared (0x00E2A098), freed (0x00E2A9D8).
void make_again(ppu_context* ctx, uint32_t sm, uint32_t id)
{
    const uint32_t man = rd(DES_AT(kChrMan));
    if (!guest_ok(man)) return;
    const uint32_t me = rd(man + 0x1BC), pb = rd(man + 0x1A0), pe = rd(man + 0x1A4);
    if (!guest_ok(pb) || pe < pb || pe - pb > 0x40) return;
    uint32_t chr = 0;
    for (uint32_t q = pb; q < pe; q += 4) {
        const uint32_t p = rd(q);
        if (guest_ok(p) && p != me && vm_read32(p + 0x498) == id) chr = p;
    }
    if (!chr) return;   // none here: the placement makes one
    const uint32_t key = sGame.scratch + 0xB00;
    call_addr(ctx, DES_AT(0x002D0EB0u), {key, chr});
    const uint32_t msent = rd(sm + kSmMembers + 0xC);
    uint32_t member = 0;
    int n = 0;
    for (uint32_t m = guest_ok(msent) ? rd(msent) : msent; m != msent && guest_ok(m) && n < 8; m = rd(m), n++)
        if ((int32_t)vm_read32(m + 0x38) != -1 && (call_addr(ctx, DES_AT(0x00E2A098u), {m + 8, key}) & 0xFFu)) member = m;
    call_addr(ctx, DES_AT(0x00E2A9D8u), {key});
    if (!member || !guest_ok(rd(sm + kSmWaiting + 0xC)) || !guest_ok(rd(sm + kSmWaiting))) {
        std::fprintf(stderr, "[coop] player %u is back, but its member isn't here: its character stays\n", id);
        return;
    }
    call_addr(ctx, DES_AT(0x0029EC08u), {man, chr});
    const bool queued = wait_again(ctx, sm, member);
    std::fprintf(stderr, "[coop] player %u is back after its respawn: its character made again%s\n", id,
                 queued ? "" : " (not placed: no node for the waiting list)");
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
    send_flags(sm);
    send(kMsgReady, b);
}

// The Archstones' seal opened in a guest's game (FLAG from a guest: it heard the Monumental's tale): into the host's
// world too, through the game's own setter, so des_coop_hook sends it on to the other guests as the host's progress.
void take_guest_seal(ppu_context* ctx)
{
    std::deque<HostFlag> flags;
    {
        std::lock_guard<std::mutex> l(sMsgLock);
        flags.swap(sHostFlags);
    }
    const uint32_t fb = flag_base();
    if (flags.empty() || !guest_ok(fb)) return;
    const uint32_t here = world_of(vm_read32(fb + kFlagsArea));
    for (const HostFlag& f : flags) {
        const uint32_t w = world_of(f.area);
        if (f.flag >= kFlagCount || (world_flag(f.flag) && w != here)) continue;   // another world's: not in play here
        if (des_seamless_flag_kind(w, f.flag) != 5 || ((vm_read8(fb + (f.flag >> 3)) >> (f.flag & 7)) & 1)) continue;
        call_addr(ctx, DES_AT(0x005086F8u), {fb, f.flag, 1});
        std::fprintf(stderr, "[coop] progress: flag %u (the Archstones' seal) set in a guest's game; set here too\n",
                     f.flag);
    }
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
    if (world) take_guest_seal(ctx);
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
    // A death after taking soul form for the join (in its own world, before the join came): a player who dies in body
    // form is a soul from then on, so its body form isn't given back.
    if (sJoinedInBody.load() && death_count(ctx) != sJoinedDeaths && sJoinedInBody.exchange(false))
        std::fprintf(stderr, "[coop] this player died after taking soul form for the join: no body form back\n");
    if (world) g.loading = false;
    else if (vm_read32(game_manager() + kGmNextArea) != kNoArea) g.loading = true;
    leave_tick(ctx, st, world);
    // leaving: no join again; and once out of the session (LeaveSession), no follow, requeue or BACK either, while
    // the session state still says client
    const int lv = sLeave.load();
    if (lv != kLeaveNone && (st == 0 || lv != kLeaveAsked)) return;
    if (st == 0) {
        g.arrived = false;
        g.in_load = false;
        g.follow = 0;   // a follow or a respawn whose load never came: not the next session's
        g.respawn = false;
        g.calm = world ? g.calm + 1 : 0;
        std::vector<uint8_t> data;
        uint32_t lead;
        {
            std::lock_guard<std::mutex> l(sMsgLock);
            data = sRoom;
            lead = sLeadTrip;
            // The room server sends the host's FLAGs to every game connected to it, in its session or not: the ones
            // that came out of a session aren't this player's progress (a boss killed while it was away)
            sHostFlags.clear();
        }
        g.trip = lead;   // a session starts: only the host's later loads are followed
        bool host_known;
        {
            std::lock_guard<std::mutex> l(sMsgLock);
            host_known = sReady.area != kNoArea;
        }
        // The room server is the host's game. With the connection to it lost (the host closed its game), the room
        // data and the host's place are stale: a join with them fails, and each failure is two more boxes ("Failed
        // to join the session.", "The session has ended.") after the game's own. Nothing until the connection is
        // back (np_psnr dials again in the background), then the host is asked again.
        if (!np_psnr_connected()) {   // (or not yet connected at the start)
            if (g.connected && !g.host_gone) {
                g.host_gone = true;
                std::lock_guard<std::mutex> l(sMsgLock);
                sRoom.clear();
                sReady = Ready();
                std::fprintf(stderr, "[coop] the host's game is gone: no join until it is back\n");
            }
            // soul form taken for a join that didn't come: the body form back until the host is (the next join
            // takes soul form again); at the first frame with the player in the world
            if (g.host_gone && world && !own_kept() && body_form_back(ctx))
                std::fprintf(stderr, "[coop] body form again until the host's game is back\n");
            g.calm = 0;
            return;
        }
        g.connected = true;
        if (g.host_gone) {
            g.host_gone = false;
            std::fprintf(stderr, "[coop] connected to the host's game again: asking it for its room\n");
        }
        // The game is taking this player home (its session ended: global_event.lua's OnRoomDisappeared, then
        // OnLeave_Limit's load 5 s later), with flag 4047 ("a reload under way: no other") set until then. A join now
        // races that load (seen after a dropped connection that came back at once). It waits for the player at home.
        const uint32_t fb = flag_base();
        if (guest_ok(fb) && ((vm_read8(fb + (kFlagReloadGuard >> 3)) >> (kFlagReloadGuard & 7)) & 1)) {
            if (!g.home_wait) {
                g.home_wait = true;
                std::fprintf(stderr, "[coop] this player is on its way home: no join until it is there\n");
            }
            g.calm = 0;
            return;
        }
        g.home_wait = false;
        if (data.empty() || !host_known) {   // the room data, and where the host is (its join loads the host's area)
            if (++g.asked_at >= 120) {   // every 120 frames until the host answers
                g.asked_at = 0;
                send(kMsgAsk);
            }
            return;
        }
        // standing in the world (150 frames), then every 20 s while not in a session
        if (g.calm == 150 || (g.calm > 150 && (g.calm - 150) % 1200 == 0)) {
            // The game summons only players in soul form: one in body form fails its join (OnFailedJoinSession). It
            // takes soul form for the join, as the game's own ChangeGreyGhost does, and gets its body form back at
            // home (going_home_form), or where it stands if it leaves or the host's game goes before a join came
            // (body_form_back), unless it dies first. The join is 10 frames on.
            if ((int32_t)call_addr(ctx, DES_AT(0x0043E660u), {0}) == 0) {   // GetLocalPlayerChrType: 0 body form
                call_addr(ctx, DES_AT(0x00445E48u), {0});                   // ChangeGreyGhost
                sJoinedDeaths = death_count(ctx);
                sJoinedInBody = true;
                g.calm = 140;
                std::fprintf(stderr, "[coop] this player is in body form: soul form for the join, body form again at home\n");
                return;
            }
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
    // the host's flags for the next reload, and its progress flags into this player's own world (they wait for the
    // join's load to keep the own world apart: keep_own_world, which also starts the session's helped_kill afresh)
    const uint32_t fb = flag_base();
    take_host_flags(fb);
    std::deque<HostFlag> flags;
    if (own_kept()) {
        std::lock_guard<std::mutex> l(sMsgLock);
        flags.swap(sHostFlags);
    }
    for (const HostFlag& f : flags) {
        if (des_seamless_flag_kind(world_of(f.area), f.flag) == 3) helped_kill();
        if (guest_ok(fb) && keep_flag(fb, world_of(f.area), f.flag))
            std::fprintf(stderr, "[coop] progress: flag %u (set in the host's world) kept for this player's world\n", f.flag);
    }
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
        const uint32_t man = rd(DES_AT(kChrMan));
        if (g.respawn.exchange(false) && guest_ok(man) && guest_ok(rd(man + 0x1BC))) {   // and they make this one again
            std::vector<uint8_t> b;
            put32(b, vm_read32(rd(man + 0x1BC) + 0x498));
            send(kMsgBack, b);
            std::fprintf(stderr, "[coop] back after the respawn: the others make this player's character again\n");
        }
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
    if (joined && !trip) sGame.respawn = true;
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
    take_host_flags(flag_base());   // the host's flags where it is now (sent just before that READY), for this load
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

// A guest's connection to the host's game (co::Status::link), kept by des_seamless_tick for the Co-op window.
std::atomic<co::Status::Link> sLink{co::Status::Link::Connecting};

}  // namespace

namespace des::seamless {

Status::Link guest_link()
{
    return sLink.load();
}

void leave()
{
    if (active() != Mode::Join) return;
    int none = kLeaveNone;
    if (sLeave.compare_exchange_strong(none, kLeaveAsked))
        std::fprintf(stderr, "[coop] leave: asked; going home first\n");
}

bool left()
{
    return active() != Mode::Join || sLeave.load() == kLeaveDone;
}

}  // namespace des::seamless

// From des_menu_tick, every frame, on the game's main thread.
extern "C" void des_seamless_tick(ppu_context* ctx)
{
    const co::Mode m = co::active();
    if (m == co::Mode::Off) return;
    if (m == co::Mode::Join) {   // the Co-op window's line: connected, and once it was, lost (the host's game closed)
        if (np_psnr_connected()) sLink = co::Status::Link::Connected;
        else if (sLink.load() == co::Status::Link::Connected) sLink = co::Status::Link::Lost;
    }
    static bool once = false;
    if (!once) {
        once = true;
        psnr_app_on_message(on_message);
    }
    const uint32_t sm = session_manager(), gm = game_manager();
    if (!guest_ok(sm) || !guest_ok(gm)) {
        int asked = kLeaveAsked;   // a leave before the game is in (the title screen): nothing to take home
        if (!own_kept() && sLeave.compare_exchange_strong(asked, kLeaveDone))
            std::fprintf(stderr, "[coop] leave: not in the game yet\n");
        return;
    }
    if (!sGame.scratch) sGame.scratch = guest_alloc(ctx, 0x1000);
    if (!sGame.record_buf) sGame.record_buf = guest_alloc(ctx, 0x100);
    if (!sGame.scratch) return;
    const uint32_t st = vm_read32(sm + kSmState);
    const bool world = in_world(sm, gm);
    std::deque<uint32_t> back;
    {
        std::lock_guard<std::mutex> l(sMsgLock);
        back.swap(sBack);
    }
    // not in the world or the session: this game's next placement (after its load, or the join's) makes them anyway
    if (world && (st == kStateHost || st == kStateClient))
        for (const uint32_t id : back) make_again(ctx, sm, id);
    if (m == co::Mode::Host) host_tick(ctx, sm, gm, st, world);
    else guest_tick(ctx, sm, st, world);
}

// An area script was just read (des_seamless_scripts.cpp), before the area starts. On a guest in a session, each
// Nexus cutscene the host has seen and this player hasn't plays here too: its flag in play becomes the host's and this
// player's own. Once it has played, the flag goes into this player's own world (des_coop_hook's flag set, kind 4).
void des_seamless_area_read(uint32_t world, const std::vector<uint32_t>& scenes)
{
    if (scenes.empty() || co::active() != co::Mode::Join) return;
    const uint32_t sm = session_manager(), fb = flag_base();
    if (!guest_ok(sm) || !guest_ok(fb) || vm_read32(sm + kSmState) != kStateClient) return;
    std::lock_guard<std::mutex> l(sOwn.lock);
    if (!sOwn.kept) return;
    for (const uint32_t flag : scenes) {
        if (flag >= kFlagCount) continue;
        const uint8_t bit = (uint8_t)(1u << (flag & 7));
        bool own;
        if (!world_flag(flag) || world == sOwn.home) {
            own = (sOwn.flags[flag >> 3] & bit) != 0;
        } else {
            const long b = own_bank_at(world, flag);
            own = b >= 0 && (sOwn.banks[b] & bit) != 0;
        }
        const uint32_t a = fb + (flag >> 3);
        const uint8_t v = vm_read8(a);
        if ((v & bit) && !own) {
            vm_write8(a, (uint8_t)(v & ~bit));
            std::fprintf(stderr, "[coop] cutscene %u: seen in the host's world, not yet by this player; it plays here\n",
                         flag);
        }
    }
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
    case 0x005086F8u: { // the event flags' set, after its cmpwi: r3 = the flags, r4 = the flag, r5 = the value
        const uint32_t flag = (uint32_t)ctx->gpr[4];
        if ((uint32_t)ctx->gpr[5] == 0 || flag >= kFlagCount) break;   // flags only move forward
        const uint32_t fb = (uint32_t)ctx->gpr[3];
        if (!guest_ok(fb) || fb != flag_base() || ((vm_read8(fb + (flag >> 3)) >> (flag & 7)) & 1)) break;   // set already
        const uint32_t sm = session_manager();
        const uint32_t st = guest_ok(sm) ? vm_read32(sm + kSmState) : 0;
        const bool host = m == co::Mode::Host && st == kStateHost;
        const bool joined = m == co::Mode::Join && st == kStateClient;
        if (!host && !joined) break;
        const uint32_t area = vm_read32(fb + kFlagsArea);
        const int kind = des_seamless_flag_kind(world_of(area), flag);
        // A Nexus cutscene's script sets its flag as the area starts, before the guest counts as arrived
        const bool guest = joined && (sGame.arrived.load() || kind == 4);
        if (!host && !guest) break;
        if (guest && kind == 3) helped_kill();
        if (host && (kind == 1 || kind == 3 || kind == 5)) {   // the host's world moved forward: its guests keep it too
            std::vector<uint8_t> b;
            put32(b, flag);
            put32(b, area);
            send(kMsgFlag, b);
            std::fprintf(stderr, "[coop] progress: flag %u set; the guests keep it too\n", flag);
        } else if (guest && kind) {
            if (kind == 5) {   // the seal, opened in this game alone (its handlers aren't sent): the host's world too
                std::vector<uint8_t> b;
                put32(b, flag);
                put32(b, area);
                send(kMsgFlag, b);
                std::fprintf(stderr, "[coop] progress: flag %u (the Archstones' seal) set here; the host's world takes it\n",
                             flag);
            }
            if (keep_flag(fb, world_of(area), flag)) {
                if (kind == 2) {
                    std::lock_guard<std::mutex> l(sOwn.lock);
                    sOwn.taken.push_back(flag);
                }
                std::fprintf(stderr, "[coop] %s: flag %u set here; kept for this player's world\n",
                             kind == 2 ? "a boss's soul taken" : kind == 4 ? "a cutscene seen" : "progress", flag);
            }
        }
        break;
    }
    case 0x00299B74u:   // an NPC's setup (0x002999C8), after lbz r0,0xE8(r9) of its param row
    case 0x002C2830u:   // the same test where its controller is set up (0x002C2588, 0x002C3018), after lbz r0,0xE8(r11)
    case 0x002C32C0u:
        // A visitor's game (a phantom) sets up the NPCs whose row has that byte in mode 4: drawn as phantoms, and no
        // talk. In co-op a friend shares the host's world, so the NPCs stay as the host has them (0 skips the 4).
        if (m == co::Mode::Join) ctx->gpr[0] = 0;
        break;
    case 0x004C9638u:   // the talk scripts' check of the player's type (bl 0x0026F378 in 0x004C9600 and 0x004C9C18)
    case 0x004C9C5Cu:
        // The check marks the player as a phantom (the talk scripts' function 19) unless the type is 0 (alive) or 8
        // (soul form), and their scripts then show no talk prompt. A friend is a phantom in the host's world, but in
        // co-op it talks to the NPCs as in its own game (0 is the alive player's type).
        if (m == co::Mode::Join) ctx->gpr[3] = 0;
        break;
    case 0x00508C00u:   // a join fills the flags' copies (r3 = the flags), at its stdu
        if (m == co::Mode::Join && (uint32_t)ctx->gpr[3] == flag_base()) keep_own_world((uint32_t)ctx->gpr[3]);
        break;
    case 0x00419DCCu:   // after a load's world swap of the flags (bl 0x005098D8), at lwz r9,0(r29)
        if (m == co::Mode::Join) {
            const uint32_t sm = session_manager(), gm = game_manager(), fb = flag_base();
            if (guest_ok(fb) && guest_ok(gm))
                after_world_swap(fb, guest_ok(sm) && vm_read32(sm + kSmState) == kStateClient);
        }
        break;
    case 0x0044C780u:   // the end of WarpNextStageKick (ld r0,0x90(r1)): a load asked for
    case 0x00447EF4u: { // the end of SetChrTypeDataGreyNext: the form after the next load
        const uint32_t sm = session_manager();
        if (m != co::Mode::Join || !guest_ok(sm) || vm_read32(sm + kSmState) != 0) break;   // only out of a session
        if (addr == 0x0044C780u) going_home(game_manager());
        else going_home_form(ctx);
        break;
    }
    default:
        break;
    }
}
