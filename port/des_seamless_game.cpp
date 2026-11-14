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
//     area and place). The map it loads then becomes the host's area, and its spawn point the host's place, facing
//     the way the host faces there (a join's too).
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
//     the host's and its own, and the guest keeps the flag in its own world once it has played. So does an area's
//     opening (1-1's dragon): the one its start plays.
// Items, each player's own (a phantom's game makes none: the item lists' adds return early for any type but body or
// soul form, read from the type the next load is for):
//   - The area's items (TreasureMan, at each load) and a dead enemy's drop (CorpseMan, rolled by each game) are made
//     for a guest too. Nothing about them goes over the network: each game has its own list, so one player's pickup
//     doesn't take the item from the others.
//   - Whether an area item was taken is read from the guest's own world, not from the host's flags in play: a guest
//     sees the items it hasn't taken, wherever it took them. One it takes is kept in its own world, as a boss's soul is.
#include "des_seamless.h"

#include "ps3emu/ppu_lift.h"
#include "ps3emu/psnr_host.h"
#include "des_region.h"      // DES_AT: the running disc's addresses

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <set>
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
int des_seamless_flag_kind(uint32_t world, uint32_t flag);       // _scripts.cpp: 1 progress, 3 a boss kill's, 2 a soul, key or item pickup, 4 a cutscene seen, 5 the Archstones' seal
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
// u32: the player's type when its session started (0x004193F0): 0 body form, 8 soul form. On a guest, the form it
// goes home in: SetChrTypeDataGreyNext gives soul form for 8 and body form for 0 (a blue phantom's type then).
constexpr uint32_t kGmForm = 0xA90;
constexpr uint32_t kGmNextPoint = 0x0C;                      // the next load's warp point (-1: a place)
constexpr uint32_t kGmSummonedPlace = 0x1C;                  // a byte: the next load puts the player at +0x20
constexpr uint32_t kGmPlace[4] = {0x20, 0xA40, 0xAE0, 0xB20}; // where a summoned reload puts the player (x, y, z)
// The rotation beside the first three places (x, y, z, w; y = the yaw, as in the player's transform). Zero unless
// written, so a summoned reload faced one fixed way, whichever way the host looked.
constexpr uint32_t kGmFacing[3] = {0x30, 0xA50, 0xAF0};
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
constexpr char kMsgFull[4] = {'F', 'U', 'L', 'L'};   // u32 most: the host's answer instead while its session is full
constexpr char kMsgLead[4] = {'L', 'E', 'A', 'D'};   // u32 trip: the host's load started
// u32 trip, u32 area, f32 x, y, z, yaw where it arrived, f32 x, y, z, yaw where it is, then its area's dead-character
// record (76 bytes, or none): the host has arrived (after each load), every 2 s while it hosts, and when a guest asks
constexpr char kMsgReady[4] = {'R', 'E', 'D', 'Y'};
constexpr char kMsgWhere[4] = {'H', 'E', 'R', '?'};  // a guest asks for a READY now (its respawn)
constexpr char kMsgFlag[4] = {'F', 'L', 'A', 'G'};   // u32 flag, u32 area: a progress flag set in the host's world (from a guest: the seal)
// u32 area, u32 offset, then up to kFlagChunk bytes of the host's flags from that offset (with each READY)
constexpr char kMsgFlags[4] = {'F', 'L', 'G', 'S'};
constexpr char kMsgBack[4] = {'B', 'A', 'C', 'K'};   // u32 player id: a guest back in the world after its respawn
// u32 player id, u32 1 or 0: whether that player shows its phantom glow, u32 its colour (0x01RRGGBB; 0 the game's)
constexpr char kMsgGlow[4] = {'G', 'L', 'O', 'W'};
// 14 x u32: the host's world tendency in use, two floats for each of worlds 1-7 (before each READY), as the game's own
// message to a phantom (P2P 0x17) gives them
constexpr char kMsgQwc[4] = {'Q', 'W', 'C', ' '};
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
// The next load's record, *(*(*(*(TOC - 0x7884) - 0x8000)) + 8): +0x8C is the player's type at the load's request
// (WarpNextStageKick copies it), and a summoned load makes a blue phantom (1) of soul form (8) only.
uint32_t load_record() { return rd(rd(rd(rd(DES_AT(kSessionSlot)) - 0x8000u)) + 0x08u); }
constexpr uint32_t kLoadType = 0x8C;
uint32_t player_chr() { return rd(rd(DES_AT(kChrMan)) + 0x1BC); }
constexpr uint32_t kChrHp = 0x3C4;   // the character's HP (GetHp's read)
// The scripts' effect ids are behaviours (BehaviorParam, whose +0x14 names the effect: 0x00595B80). 101 is soul form's
// effect (SpEffect 8: the most HP x0.5), which the game's revivals erase (global_event.lua's BlockClear2_3).
constexpr uint32_t kSoulFx = 101;
constexpr uint32_t kSoulEffect = 8;   // soul form's effect itself (SpEffect 8)


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
bool sFull = false;                  // the host's last answer was FULL: no join until its room data comes (a guest)
bool sAsked = false;                 // a guest asked for the room data (the host)
uint32_t sLeadTrip = 0;              // the host's latest load (a guest)
struct Place { float x = 0, y = 0, z = 0, yaw = NAN; };   // yaw: the facing (player_transform), NaN if unknown
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
std::atomic<bool> sDied{false};      // this game's player died in the session since its last BACK
std::set<uint32_t> sDeadCopies;      // the others' characters seen dead (their game data's HP at 0), tick thread only
struct HostFlags {
    uint32_t area = kNoArea;
    std::vector<uint8_t> bytes = std::vector<uint8_t>(kFlagBytes);
    uint32_t parts = 0;              // the chunks received for this area since the last full set (a bit each)
    bool fresh = false;              // a full set not yet put in place
};
HostFlags sHostFlagSet;              // the host's flags (a guest)
struct Glow { uint32_t id; bool on; uint32_t colour; };
std::vector<Glow> sGlow;             // each other player's own glow and its colour (GLOW), by player id
constexpr uint32_t kQwcInUse = 0x10, kQwcWords = 14;   // the QWC object's tendency in use: worlds 1-7, two floats each
constexpr uint32_t kQwcOwn = 0x48;                      // and the player's own (the save keeps them), the same layout
struct Qwc { bool have = false; uint32_t w[kQwcWords] = {}; };
Qwc sHostQwc;                        // the host's tendency in use (QWC, a guest)
Qwc sOwnQwc;                         // this guest's own in use, kept while the host's is in place
uint32_t sOwnSaved[kQwcWords];       // and its own values (+0x48) at that time

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
        sFull = false;
    } else if (!std::memcmp(d, kMsgFull, 4)) {   // (a guest in the session ignores it: guest_tick reads it only out)
        sRoom.clear();
        sFull = true;
    } else if (!std::memcmp(d, kMsgAsk, 4)) {
        sAsked = true;
    } else if (!std::memcmp(d, kMsgLead, 4) && n >= 8) {
        sLeadTrip = get32(d + 4);
    } else if (!std::memcmp(d, kMsgReady, 4) && n >= 44) {
        Ready r;
        r.trip = get32(d + 4);
        r.area = get32(d + 8);
        r.arrived = {getf(d + 12), getf(d + 16), getf(d + 20), getf(d + 24)};
        r.now = {getf(d + 28), getf(d + 32), getf(d + 36), getf(d + 40)};
        if (n == 44 + kRecordSize) r.record.assign(d + 44, d + n);
        r.count = sReady.count + 1;
        sReady = std::move(r);
    } else if (!std::memcmp(d, kMsgWhere, 4)) {
        sAskedWhere = true;
    } else if (!std::memcmp(d, kMsgFlag, 4) && n >= 12 && get32(d + 4) < kFlagCount && sHostFlags.size() < 4096) {
        sHostFlags.push_back({get32(d + 4), get32(d + 8)});
    } else if (!std::memcmp(d, kMsgBack, 4) && n >= 8 && sBack.size() < 16) {
        sBack.push_back(get32(d + 4));
    } else if (!std::memcmp(d, kMsgGlow, 4) && n >= 12) {
        const uint32_t id = get32(d + 4);
        const bool on = get32(d + 8) != 0;
        const uint32_t colour = n >= 16 && (get32(d + 12) >> 24) == 1 ? get32(d + 12) : 0;   // 1 << 24 | RRGGBB
        auto say_colour = [&] {
            if (colour) std::fprintf(stderr, "[coop] player %u: its glow colour %06X\n", id, colour & 0xFFFFFFu);
            else std::fprintf(stderr, "[coop] player %u: its glow colour the game's\n", id);
        };
        bool known = false;
        for (Glow& gl : sGlow)
            if (gl.id == id) {
                if (gl.on != on) std::fprintf(stderr, "[coop] player %u: its glow %s\n", id, on ? "on" : "off");
                if (gl.colour != colour) say_colour();
                gl.on = on;
                gl.colour = colour;
                known = true;
            }
        if (!known && sGlow.size() < 16) {
            sGlow.push_back({id, on, colour});
            if (!on) std::fprintf(stderr, "[coop] player %u: its glow off\n", id);
            if (colour) say_colour();
        }
    } else if (!std::memcmp(d, kMsgQwc, 4) && n >= 4 + 4 * kQwcWords) {
        Qwc q;
        q.have = true;
        for (uint32_t i = 0; i < kQwcWords; i++) {
            q.w[i] = get32(d + 4 + 4 * i);
            float f;
            std::memcpy(&f, &q.w[i], 4);
            if (!std::isfinite(f)) return;   // not a tendency
        }
        sHostQwc = q;
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
    bool told_full = false;   // the host: its last answer to a guest's ask was FULL (logged once in a row)
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
    bool sent = false;         // its trip home was pointed at area (going_home)
    bool came_home = false;     // a trip home after a session came in this run (after_world_swap), at home_at
    std::filesystem::file_time_type home_at{};
    std::vector<uint8_t> flags = std::vector<uint8_t>(kFlagBytes);   // its own flags (the trip home's copy, +0x187C)
    std::vector<uint8_t> banks = std::vector<uint8_t>(kBanksSize);
    std::vector<uint32_t> taken;   // the boss souls it took in the session (its view keeps them taken)
};
Own sOwn;

bool own_kept()
{
    std::lock_guard<std::mutex> l(sOwn.lock);
    return sOwn.kept;
}

// Forms (docs/146). In the host's world a friend is a blue phantom, in either form; its own form is the one the game
// keeps for its trip home (kGmForm: 0 body form, 8 soul form).
bool body_form()
{
    const uint32_t gm = game_manager();
    return guest_ok(gm) && vm_read32(gm + kGmForm) == 0;
}

void set_form(bool body)
{
    const uint32_t gm = game_manager();
    if (guest_ok(gm)) vm_write32(gm + kGmForm, body ? 0u : 8u);
}

// A boss was killed with this guest in the session (a flag of the kill's handlers; a kill sets several): a friend in
// soul form is revived, as the game revives the host. forms_tick does it.
std::atomic<bool> sBossRevive{false};
void helped_kill()
{
    if (own_kept() && !body_form() && !sBossRevive.exchange(true))
        std::fprintf(stderr, "[coop] a boss killed with this player in the session, in soul form: to be revived\n");
}

// This game showed "The Demon Was Destroyed" in a session (des_coop_hook at SetTextEffect's native). A blue phantom's
// boss clear (global_event.lua's BlockClear2) shows only that; a soul-form host's shows it and then the revival's.
std::atomic<bool> sKillText{false};
std::atomic<bool> sKillShown{false};   // a friend's "The Demon Was Destroyed" was shown since the last load
constexpr uint32_t kTextKillDemon = 1, kTextRevival = 3;   // TEXT_TYPE_KillDemon, TEXT_TYPE_Revival (event_define.lua)

// Whether the character has effect fx: its effect list (chr+0x304, +4: the first; each +0x18 its effect, +0x20 the
// next), as the most HP's walk reads it (0x00378940).
bool has_effect(uint32_t chr, uint32_t fx)
{
    const uint32_t list = rd(chr + 0x304);
    uint32_t n = guest_ok(list) ? rd(list + 4) : 0;
    for (int k = 0; k < 64 && guest_ok(n); ++k, n = rd(n + 0x20))
        if (rd(n + 0x18) == fx) return true;
    return false;
}

// Body form's health on a blue phantom: the summoned load makes it in soul form, and soul form's effect (half the most
// HP) comes a little after its arrival. It goes as a revival takes it (EraseEventSpecialEffect(10000, 101)), then a
// full recover, as the game gives at a load.
void body_health(ppu_context* ctx)
{
    call_addr(ctx, DES_AT(0x00446C68u), {0, 10000, kSoulFx});   // EraseEventSpecialEffect(10000, 101)
    call_addr(ctx, DES_AT(0x00439358u), {0});                   // RequestFullRecover
}

// The Stone of Ephemeral Eyes (goods 1021) in a session. Its row (EQUIP_PARAM_GOODS_ST, the param table 3:
// 0x005A5920(3, 0) -> +0x20 the param's data, rows {id, offset, name} from +0x30) allows it in soul form only
// (enable_gray, +0x19) and never in multiplayer (enable_multi, +0x1C: 0). In a session every player in soul form may
// use it: multiplayer is allowed, and a friend in soul form (a blue phantom, enable_white +0x1A) may too. Out of a
// session the row is as the disc has it. The game reads the row when the item is used, so it is set each frame.
constexpr uint32_t kStone = 1021;
void stone_tick(ppu_context* ctx, bool session, bool friend_soul)
{
    static uint32_t row = 0;
    static uint8_t disc_white = 0, disc_multi = 0;
    if (!row) {
        const uint32_t p = call_addr(ctx, DES_AT(0x005A5920u), {3, 0});
        const uint32_t data = guest_ok(p) ? rd(p + 0x20) : 0;
        static const char kName[] = "EQUIP_PARAM_GOODS_ST";
        bool named = guest_ok(data);
        for (int k = 0; named && kName[k]; ++k) named = vm_read8(data + 0xC + k) == (uint8_t)kName[k];
        if (!named) return;
        const uint32_t count = vm_read16(data + 0xA);
        for (uint32_t r = 0; r < count && r < 4096; ++r)
            if (rd(data + 0x30 + 12 * r) == kStone) row = data + rd(data + 0x30 + 12 * r + 4);
        if (!guest_ok(row)) {
            row = 1;   // not on this disc: never looked for again
            return;
        }
        disc_white = vm_read8(row + 0x1A);
        disc_multi = vm_read8(row + 0x1C);
    }
    if (row == 1) return;
    const uint8_t white = friend_soul ? 1 : disc_white, multi = session ? 1 : disc_multi;
    if (vm_read8(row + 0x1A) != white) vm_write8(row + 0x1A, white);
    if (vm_read8(row + 0x1C) != multi) vm_write8(row + 0x1C, multi);
}

// The player's own bloodstain in a session. At each load the global script's ParamInitialize (0x0044A660) shows it
// only for a player in body form of its own world (type 0); a soul-form player with others in its world, and any
// phantom, has it hidden (InvalidMyBloodMarkInfo: the game data's "shown" byte, +0x18, cleared). In a session that
// hides souls the player can still take back: a friend's, dropped in the host's world, and the host's after a death
// with friends in. So it is shown again as ParamInitialize shows it for type 0: 0x00419670(bloodstain manager, the
// player, 0) sets the byte and shows the stain if it lies in this area. A stain already touched holds no souls (-1)
// and stays hidden. Only its owner sees a stain, so the others' games are unchanged.
constexpr uint32_t kLuaSystemA = 0x01B42A80u;   // *this: the bloodstain manager (+0x10 the own stain's object)
constexpr uint32_t kPgdRoot = 0x01B4EF9Cu;      // *this: the game data (+0x18 own stain shown, +0x1C its record)
constexpr uint32_t kStainShown = 0x18, kStainRecord = 0x1C, kStainSouls = 0x1C;
void stain_tick(ppu_context* ctx, bool session)
{
    if (!session) return;
    const uint32_t gd = rd(DES_AT(kPgdRoot));
    if (!guest_ok(gd) || vm_read8(gd + kStainShown) != 0) return;
    const uint32_t rec = rd(gd + kStainRecord);
    if (!guest_ok(rec)) return;
    const int32_t souls = (int32_t)vm_read32(rec + kStainSouls);
    const uint32_t mgr = rd(DES_AT(kLuaSystemA)), pc = player_chr();
    if (souls < 0 || !guest_ok(mgr) || !guest_ok(pc)) return;
    call_addr(ctx, DES_AT(0x00419670u), {mgr, pc, 0});
    std::fprintf(stderr, "[coop] own bloodstain: shown again after the load hid it (%d souls in it)\n", souls);
}

// Each player's own phantom glow (DES_SEAMLESS_GLOW, Settings > Network; on unless "0"). A friend is a blue phantom
// (type 1) in the host's world, and every game draws it with the game's phantom material. Each game tells the others
// its own choice with its player id (GLOW, every 2 s in a session), and draws a blue phantom whose player turned its
// glow off, itself included, as an alive player is drawn (the hook at 0x002DDAA8). Only the material changes: the
// type, and all the game reads from it, stay a blue phantom's.
//
// Each player's glow colour (DES_SEAMLESS_GLOW_COLOR, "RRGGBB"; "" the game's blue), sent the same way. The colours
// are the phantom material's (kind 2, 0x0007FD38): an edge colour at +0x2A8 and a body colour at +0x2B8, which the
// ghost colour upload (0x00080E88) reads from the ghost manager's table of materials by kind (+0x68 + kind * 4) for
// each draw of a phantom, and sends to the shader when they differ from the last ones sent. A player with its own
// colour gets its own copy of that material with its colours in it, and for the length of that upload, only for its
// own draws, its copy takes kind 2's place in the table (the hooks at 0x00080E88 and 0x0006C860). A draw is a
// player's when the drawn object's handle (+0x8) is its character's draw object's (*(chr + 0x6A4) + 0x4).
std::atomic<bool> sOwnGlow{true};
std::atomic<uint32_t> sOwnChr{0};
constexpr int kTints = 4;                    // the players on one screen, this one included
constexpr uint32_t kTintSize = 0x400;        // a material copy: all of the material, and its colours at +0x2A8/+0x2B8
std::atomic<uint64_t> sTint[kTints];         // handle << 32 | its material copy; 0 = none
std::atomic<uint32_t> sWhiteMat{0};          // kind 2's own material, as the upload last read it
uint32_t sTintBuf = 0;                       // the copies, kTints * kTintSize bytes of the guest heap
thread_local uint32_t tTintMgr = 0, tTintMat = 0;   // the table put back after the upload (this thread's)

bool tint_copy(uint32_t a) { return sTintBuf && a >= sTintBuf && a < sTintBuf + kTints * kTintSize; }

// "RRGGBB" (a leading '#' allowed) as 0x01RRGGBB; 0 for "" or anything else.
uint32_t glow_colour(const char* v)
{
    if (!v) return 0;
    if (*v == '#') ++v;
    uint32_t c = 0;
    for (int i = 0; i < 6; i++) {
        const char h = v[i];
        const int d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
        if (d < 0) return 0;
        c = c << 4 | (uint32_t)d;
    }
    return v[6] ? 0 : (1u << 24) | c;
}

// The copy at `at` of kind 2's material `mat`, in colour c: the body colour is c, the edge colour c brightened 40%
// of the way to white (the game's blue is close to that); the alphas stay the material's.
void tint_paint(uint32_t at, uint32_t mat, uint32_t c)
{
    for (uint32_t o = 0; o < kTintSize; o += 4) vm_write32(at + o, vm_read32(mat + o));
    const float rgb[3] = {((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f};
    for (int i = 0; i < 3; i++) {
        const float body = rgb[i], edge = rgb[i] + (1.0f - rgb[i]) * 0.4f;
        uint32_t w;
        std::memcpy(&w, &edge, 4);
        vm_write32(at + 0x2A8 + 4 * i, w);
        std::memcpy(&w, &body, 4);
        vm_write32(at + 0x2B8 + 4 * i, w);
    }
}

void glow_tick(ppu_context* ctx, bool session)
{
    static int frames = 0;
    const char* v = std::getenv("DES_SEAMLESS_GLOW");
    const bool on = !(v && v[0] == '0' && !v[1]);
    const uint32_t colour = glow_colour(std::getenv("DES_SEAMLESS_GLOW_COLOR"));
    const uint32_t pc = player_chr();
    sOwnGlow.store(on);
    sOwnChr.store(pc);
    static bool was = false;   // in a session last frame: the others' choices are forgotten when it ends
    if (!session) {
        for (auto& t : sTint) t.store(0);
        if (was) {
            std::lock_guard<std::mutex> l(sMsgLock);
            sGlow.clear();
        }
        was = false;
        frames = 0;
        return;
    }
    was = true;
    // The players on this screen with a colour of their own, each with its copy of the material in that colour.
    {
        struct Want { uint32_t handle, colour; };
        std::vector<Want> want;
        auto handle_of = [](uint32_t chr) { return rd(rd(chr + 0x6A4) + 0x4); };
        if (colour && guest_ok(pc) && handle_of(pc)) want.push_back({handle_of(pc), colour});
        const uint32_t man = rd(DES_AT(kChrMan));
        const uint32_t pb = rd(man + 0x1A0), pe = rd(man + 0x1A4);
        if (guest_ok(pb) && pe > pb && pe - pb <= 0x40) {
            std::lock_guard<std::mutex> l(sMsgLock);
            for (uint32_t q = pb; q < pe && want.size() < (size_t)kTints; q += 4) {
                const uint32_t c = rd(q);
                if (!guest_ok(c) || c == pc) continue;
                const uint32_t id = vm_read32(c + 0x498);
                for (const Glow& gl : sGlow)
                    if (gl.id == id && gl.colour && handle_of(c)) want.push_back({handle_of(c), gl.colour});
            }
        }
        const uint32_t mat = sWhiteMat.load();
        if (!want.empty() && !sTintBuf && guest_ok(mat)) sTintBuf = guest_alloc(ctx, kTints * kTintSize);
        static uint32_t painted[kTints][2];   // each copy's material and colour, as last painted
        for (int i = 0; i < kTints; i++) {
            if (i >= (int)want.size() || !sTintBuf || !guest_ok(mat)) {
                sTint[i].store(0);
                continue;
            }
            const uint32_t at = sTintBuf + i * kTintSize;
            if (painted[i][0] != mat || painted[i][1] != want[i].colour) {
                sTint[i].store(0);   // not drawn with while it changes
                tint_paint(at, mat, want[i].colour);
                painted[i][0] = mat;
                painted[i][1] = want[i].colour;
            }
            sTint[i].store((uint64_t)want[i].handle << 32 | at);
        }
    }
    if (++frames < 60 || !guest_ok(pc)) return;
    frames = 0;
    std::vector<uint8_t> b;
    put32(b, vm_read32(pc + 0x498));
    put32(b, on ? 1 : 0);
    put32(b, colour);
    send(kMsgGlow, b);
}

bool tints_on()
{
    for (const auto& t : sTint)
        if (t.load(std::memory_order_relaxed)) return true;
    return false;
}

// The ghost model draw (0x000E8C48) records its shader constants (0x0006DA20) before its colour upload (0x0006C850),
// so a model's draws take the colours sent before it: the last model's. Phantoms that share one colour never show
// it; phantoms with their own colours would take each other's on some of their parts. So while any player has its
// own colour, each ghost model's colours are sent first: its kind's material's, or its player's copy, written as the
// upload writes them (the manager's +0x10 constant block: edge +0x11F0+0x8C0, body +0x8D0, dirty bits at +0x11F0+0x10;
// the manager's last ones at +0xC0/+0xD0). Here: after 0x000E8C48's bl 0x0006C908, r26 the ghost owner (the manager at
// +0x4C), r31 the drawn object (its kind at +0xAA).
void tint_pre(ppu_context* ctx)
{
    if (!tints_on()) return;
    const uint32_t obj = (uint32_t)ctx->gpr[31], own = (uint32_t)ctx->gpr[26];
    if (!guest_ok(obj) || !guest_ok(own)) return;
    const uint32_t kind = vm_read16(obj + 0xAA);
    if (!kind || kind > 7) return;
    const uint32_t mgr = vm_read32(own + 0x4C);
    if (!guest_ok(mgr)) return;
    const uint32_t blk = vm_read32(mgr + 0x10);
    uint32_t mat = vm_read32(mgr + 0x68 + kind * 4);
    if (!guest_ok(blk) || !guest_ok(mat) || tint_copy(mat)) return;
    if (kind == 2) {
        const uint32_t handle = vm_read32(obj + 0x8);
        for (const auto& t : sTint) {
            const uint64_t e = t.load(std::memory_order_relaxed);
            if (e && (uint32_t)(e >> 32) == handle) mat = (uint32_t)e;
        }
    }
    const uint32_t cb = blk + 0x11F0;
    for (uint32_t i = 0; i < 16; i += 4) {
        const uint32_t edge = vm_read32(mat + 0x2A8 + i), body = vm_read32(mat + 0x2B8 + i);
        vm_write32(cb + 0x8C0 + i, edge);
        vm_write32(mgr + 0xC0 + i, edge);
        vm_write32(cb + 0x8D0 + i, body);
        vm_write32(mgr + 0xD0 + i, body);
    }
    vm_write32(cb + 0x10, vm_read32(cb + 0x10) | (3u << 22));   // the edge's and the body's dirty bits
}

// The ghost colour upload (0x00080E88) is starting: r3 the ghost manager, r5 and r7 the material kinds it reads
// (r7 0 or a second kind it blends with), r31 the drawn object. A player's own colour takes kind 2's place.
void tint_begin(ppu_context* ctx)
{
    if ((uint32_t)ctx->gpr[5] != 2 && (uint32_t)ctx->gpr[7] != 2) return;
    const uint32_t mgr = (uint32_t)ctx->gpr[3], obj = (uint32_t)ctx->gpr[31];
    if (!guest_ok(mgr) || !guest_ok(obj)) return;
    const uint32_t mat = vm_read32(mgr + 0x70);
    if (!guest_ok(mat) || tint_copy(mat)) return;
    if (sWhiteMat.load(std::memory_order_relaxed) != mat) sWhiteMat.store(mat);
    const uint32_t handle = vm_read32(obj + 0x8);
    for (const auto& t : sTint) {
        const uint64_t e = t.load(std::memory_order_relaxed);
        if (e && (uint32_t)(e >> 32) == handle) {
            tTintMgr = mgr;
            tTintMat = mat;
            vm_write32(mgr + 0x70, (uint32_t)e);
            return;
        }
    }
}

// Whether the character chr (a blue phantom) is drawn with its glow: its player's own choice.
bool glow_shown(uint32_t chr)
{
    if (chr == sOwnChr.load()) return sOwnGlow.load();
    const uint32_t id = vm_read32(chr + 0x498);
    std::lock_guard<std::mutex> l(sMsgLock);
    for (const Glow& gl : sGlow)
        if (gl.id == id) return gl.on;
    return true;
}

// Each frame of a friend's session in the host's world (arrived: the first frame after a load's arrival):
// - in body form, soul form's effect is taken off whenever a load gave it (body form's full health);
// - a death in body form makes it a soul (its HP at 0);
// - a boss kill revives a friend in soul form as BlockClear2 revives a soul-form host: body form, full health, and the
//   revival's text queued after this game's "The Demon Was Destroyed" (no animation). One that is dead at the kill
//   is in body form from its respawn; if a load comes before the text, it is revived there without the text.
void forms_tick(ppu_context* ctx, bool arrived)
{
    static bool was_dead = false, text_seen = false;
    if (sKillText.exchange(false)) text_seen = true;
    const uint32_t pc = player_chr();
    if (!guest_ok(pc)) return;
    const bool dead = (int32_t)vm_read32(pc + kChrHp) <= 0;
    if (dead && !was_dead && body_form()) {
        set_form(false);
        std::fprintf(stderr, "[coop] died in body form: a soul from now on\n");
    }
    was_dead = dead;
    if (sBossRevive.load() && body_form()) sBossRevive = false;
    if (sBossRevive.load()) {
        if (dead) {
            sBossRevive = false;
            set_form(true);
            std::fprintf(stderr, "[coop] revived by the boss kill: body form from its respawn\n");
        } else if (text_seen || arrived) {
            sBossRevive = false;
            set_form(true);
            body_health(ctx);
            if (text_seen) call_addr(ctx, DES_AT(0x0043E4E0u), {0, kTextRevival});   // SetTextEffect(TEXT_TYPE_Revival)
            std::fprintf(stderr, "[coop] revived by the boss kill: body form (%s)\n",
                         text_seen ? "its text after \"The Demon Was Destroyed\"" : "at a load, no text");
        }
    }
    if (arrived) text_seen = false;
    // The Stone of Ephemeral Eyes used in soul form (stone_tick allows it): the game's revival (SpEffect 3024, its
    // stateInfo 17) makes a blue phantom an alive player of its own world (type 0; 8 kept in case). Nothing else turns
    // a friend's blue phantom (type 1) into that during a stay in the host's world, so that change is the stone: a
    // blue phantom again (SetChrType(10000, 1), the type's own setter), then in body form as a boss kill revives it.
    static int32_t last_type = -1;
    if (arrived) last_type = -1;
    const int32_t type = (int32_t)vm_read32(pc + 0x68);
    if (!dead && last_type == 1 && (type == 0 || type == 8)) {
        call_addr(ctx, DES_AT(0x00445F30u), {0, 10000, 1});   // SetChrType(10000, 1)
        set_form(true);
        body_health(ctx);
        std::fprintf(stderr, "[coop] the Stone of Ephemeral Eyes: body form, a blue phantom still (type %d -> %d)\n", type,
                     (int32_t)vm_read32(pc + 0x68));
    }
    last_type = (int32_t)vm_read32(pc + 0x68);
    if (!dead && body_form() && has_effect(pc, kSoulEffect)) {
        body_health(ctx);
        std::fprintf(stderr, "[coop] in body form: soul form's effect taken off, full HP\n");
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

// A flag as this guest's own world has it (world: the one it is read in), where keep_flag keeps it: 1 set, 0 clear,
// -1 when there is no own world (not joined since the start).
int own_flag(uint32_t world, uint32_t flag)
{
    const uint8_t bit = (uint8_t)(1u << (flag & 7));
    std::lock_guard<std::mutex> l(sOwn.lock);
    if (!sOwn.kept || flag >= kFlagCount) return -1;
    if (!world_flag(flag) || world == sOwn.home) return (sOwn.flags[flag >> 3] & bit) ? 1 : 0;
    const long b = own_bank_at(world, flag);
    return b < 0 ? -1 : ((sOwn.banks[b] & bit) ? 1 : 0);
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
    sBossRevive = false;
    sKillText = false;
    sKillShown = false;
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
// (SetSummonedPos, SetDefaultMapUid(-1)), in the form it had before the join (SetChrTypeDataGreyNext), with a load
// (WarpNextStageKick).
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
            // Back from a session with no save since, it waits for that save, as its own trip home does. Else (the
            // title screen, say) it's done.
            std::filesystem::file_time_type home{};
            const bool unsaved = home_unsaved(&home);
            if (!world && (sGame.loading || unsaved)) {
                // again next frame
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

// World tendency (docs/69): the QWC object, *(game data + 0x24), keeps the tendency in use for worlds 1-7 at +0x10, two
// floats each (IsSuccessQWC's 0x009214D0 reads +0x08 + 8 * world), and the player's own at +0x48 (the ones the save
// keeps). A phantom's game takes the host's in-use values from the host's (P2P 0x17, received at 0x005256F0), so the area
// scripts' IsSuccessQWC see the host's world. That message never comes in seamless co-op, so the host sends its own (QWC)
// and each friend puts them in place: at its load step, after the Nexus's blend (0x004117E4), and when they change. The
// friend's own values are never touched, and its own in-use values come back when the session ends.
uint32_t qwc_object()
{
    return rd(rd(DES_AT(kPgdRoot)) + 0x24);
}

void log_qwc(const char* whose, const char* why, const Qwc& t)
{
    std::string s;
    for (uint32_t i = 0; i < kQwcWords; i += 2) {
        float f;
        std::memcpy(&f, &t.w[i], 4);
        char x[24];
        std::snprintf(x, sizeof x, " %u:%.2f", i / 2 + 1, f);
        s += x;
    }
    std::fprintf(stderr, "[coop] world tendency in use: %s (%s; white/black by world%s)\n", whose, why, s.c_str());
}

// The host's tendency in use to its guests (before each READY).
void send_qwc()
{
    const uint32_t q = qwc_object();
    if (!guest_ok(q)) return;
    std::vector<uint8_t> b;
    for (uint32_t i = 0; i < kQwcWords; i++) put32(b, vm_read32(q + kQwcInUse + 4 * i));
    send(kMsgQwc, b);
}

// The host's tendency in use (QWC) in place on this guest's game, its own kept the first time; why for the log.
void take_host_qwc(const char* why)
{
    const uint32_t q = qwc_object();
    if (!guest_ok(q)) return;
    std::lock_guard<std::mutex> l(sMsgLock);
    if (!sHostQwc.have) return;
    if (!sOwnQwc.have) {
        sOwnQwc.have = true;
        for (uint32_t i = 0; i < kQwcWords; i++) {
            sOwnQwc.w[i] = vm_read32(q + kQwcInUse + 4 * i);
            sOwnSaved[i] = vm_read32(q + kQwcOwn + 4 * i);
        }
    }
    bool changed = false;
    for (uint32_t i = 0; i < kQwcWords; i++)
        if (vm_read32(q + kQwcInUse + 4 * i) != sHostQwc.w[i]) {
            vm_write32(q + kQwcInUse + 4 * i, sHostQwc.w[i]);
            changed = true;
        }
    if (changed) log_qwc("the host's", why, sHostQwc);
}

// Out of the session: this guest's own tendency in use again. A value that was its own (saved) one at the join follows
// that own value, so a change to it during the session (a Nexus's blend, say) is in play at once; any other is put back
// as it was at the join.
void own_qwc_back()
{
    std::lock_guard<std::mutex> l(sMsgLock);
    sHostQwc.have = false;
    if (!sOwnQwc.have) return;
    sOwnQwc.have = false;
    const uint32_t q = qwc_object();
    if (!guest_ok(q)) return;
    for (uint32_t i = 0; i < kQwcWords; i++) {
        if (sOwnQwc.w[i] == sOwnSaved[i]) sOwnQwc.w[i] = vm_read32(q + kQwcOwn + 4 * i);
        vm_write32(q + kQwcInUse + 4 * i, sOwnQwc.w[i]);
    }
    log_qwc("this player's own again", "out of the session", sOwnQwc);
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
    if (!chr) {   // none here: the placement makes one
        std::fprintf(stderr, "[coop] player %u is back after its death: no character for it here, the placement makes one\n", id);
        return;
    }
    if (!sDeadCopies.erase(chr)) {   // not one seen dead: a new character since (a load's placement made it)
        std::fprintf(stderr, "[coop] player %u is back: its character is a new one already\n", id);
        return;
    }
    const uint32_t key = sGame.scratch + 0xB00;
    call_addr(ctx, DES_AT(0x002D0EB0u), {key, chr});
    const uint32_t msent = rd(sm + kSmMembers + 0xC);
    uint32_t member = 0;
    int n = 0;
    for (uint32_t m = guest_ok(msent) ? rd(msent) : msent; m != msent && guest_ok(m) && n < 8; m = rd(m), n++)
        if ((int32_t)vm_read32(m + 0x38) != -1 && (call_addr(ctx, DES_AT(0x00E2A098u), {m + 8, key}) & 0xFFu)) member = m;
    // a load's requeue may have put that member on the waiting list already: the placement makes the new one then
    bool waits = false;
    const uint32_t wsent = rd(sm + kSmWaiting + 0xC);
    n = 0;
    for (uint32_t w = guest_ok(wsent) ? rd(wsent) : wsent; w != wsent && guest_ok(w) && n < 8; w = rd(w), n++)
        if (call_addr(ctx, DES_AT(0x00E2A098u), {w + 8, key}) & 0xFFu) waits = true;
    call_addr(ctx, DES_AT(0x00E2A9D8u), {key});
    if (!member || !guest_ok(wsent) || !guest_ok(rd(sm + kSmWaiting))) {
        std::fprintf(stderr, "[coop] player %u is back, but its member isn't here: its character stays\n", id);
        return;
    }
    call_addr(ctx, DES_AT(0x0029EC08u), {man, chr});
    const bool queued = waits || wait_again(ctx, sm, member);
    std::fprintf(stderr, "[coop] player %u is back after its death: its character made again%s\n", id,
                 queued ? "" : " (not placed: no node for the waiting list)");
}

// The others' characters seen dead, each world frame of a session: their game data's (+0x49C) HP (+0xC) at 0. Only
// those are made again on a BACK; one a load's placement made since is left alone.
void dead_copies_tick()
{
    const uint32_t man = rd(DES_AT(kChrMan));
    if (!guest_ok(man)) return;
    const uint32_t me = rd(man + 0x1BC), pb = rd(man + 0x1A0), pe = rd(man + 0x1A4);
    if (!guest_ok(pb) || pe < pb || pe - pb > 0x40) return;
    std::set<uint32_t> here;
    for (uint32_t q = pb; q < pe; q += 4) {
        const uint32_t p = rd(q);
        if (!guest_ok(p) || p == me) continue;
        here.insert(p);
        const uint32_t gd = rd(p + 0x49C);
        if (guest_ok(gd) && (int32_t)vm_read32(gd + 0xC) <= 0) sDeadCopies.insert(p);
    }
    for (auto it = sDeadCopies.begin(); it != sDeadCopies.end();) it = here.count(*it) ? std::next(it) : sDeadCopies.erase(it);
    // and this game's own player: a death in the session, for its BACK at the next arrival
    if (guest_ok(me) && (int32_t)vm_read32(me + kChrHp) <= 0) sDied = true;
}

// This game's player died since its last BACK: the others make its character again (at an arrival after a load).
void send_back_if_died()
{
    const uint32_t me = player_chr();
    if (!sDied.exchange(false) || !guest_ok(me)) return;
    std::vector<uint8_t> b;
    put32(b, vm_read32(me + 0x498));
    send(kMsgBack, b);
    std::fprintf(stderr, "[coop] back after a death: the others make this player's character again\n");
}

// The player's transform: *(*(character + 0x20) + 0x10), the rotation (the yaw at +4) then the position (+0x10), as
// the game's getters read it (0x002E7420, 0x002E73E0). 0 with no player.
uint32_t player_transform()
{
    return rd(rd(rd(rd(DES_AT(kChrMan)) + 0x1BC) + 0x20) + 0x10);
}

Place player_place()
{
    Place p;
    des_player_xyz(&p.x, &p.y, &p.z);
    const uint32_t xf = player_transform();
    if (guest_ok(xf)) p.yaw = rdf(xf + 4);
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
// each with its facing, and which of its area's characters are dead (a respawn's or a follow's load kills them as a
// join's does).
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
        putf(b, p.yaw);
    }
    const std::vector<uint8_t> rec = dead_record(ctx);
    b.insert(b.end(), rec.begin(), rec.end());
    send_flags(sm);
    send_qwc();
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
        // The session has all the players the host takes (Settings' Players when you host): FULL instead, so a guest
        // asking waits (its join would be refused, with the game's failure boxes) and asks again.
        if (co::party_full()) {
            std::vector<uint8_t> b;
            put32(b, (uint32_t)co::party_size());
            send(kMsgFull, b);
            if (!g.told_full) std::fprintf(stderr, "[coop] a player asked to join: the game is full\n");
            g.told_full = true;
        } else {
            std::vector<uint8_t> data;
            {
                std::lock_guard<std::mutex> l(sMsgLock);
                data = sRoom;
            }
            send(kMsgRoom, data);
            g.told_full = false;
        }
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
        send_back_if_died();
    } else if (!g.in_load && world && (where || ++g.ready_frames >= 60)) {
        g.ready_frames = 0;
        send_ready(ctx, sm, g.trip, g.arrival);
    }
}

void guest_tick(ppu_context* ctx, uint32_t sm, uint32_t st, bool world)
{
    Game& g = sGame;
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
            // in either form: the session's check of a joined guest lets a friend in body form stay (0x0053EA2C)
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
    {
        std::lock_guard<std::mutex> l(sMsgLock);
        sFull = false;   // (in a session: the host's FULLs, sent for others asking, aren't for this one)
    }
    if (st != kStateClient) return;
    // the host's flags for the next reload, and its progress flags into this player's own world (they wait for the
    // join's load to keep the own world apart: keep_own_world, which also clears a boss kill's revive left from before)
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
    bool just_arrived = false;
    if (!world) g.in_load = true;
    else if (g.in_load && here) {   // arrived after a load in the session: the host and the others are placed again
        g.in_load = false;
        just_arrived = true;
        sKillShown = false;
        requeue(ctx, sm);
        const uint32_t man = rd(DES_AT(kChrMan));
        // and, after its death (its own respawn, or a load it followed while dead), the others make it again
        if (g.respawn.exchange(false)) sDied = true;
        if (guest_ok(man)) send_back_if_died();
    }
    if (world && !g.in_load) forms_tick(ctx, just_arrived);
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
    // In body form (the type at the load's request 0), as soul form to the load: it makes a blue phantom of soul form
    // only. The form the friend goes home in stays as the session's start recorded it (kGmForm).
    const uint32_t lr = load_record();
    if (guest_ok(lr) && vm_read32(lr + kLoadType) == 0) {
        vm_write32(lr + kLoadType, 8);
        std::fprintf(stderr, "[coop] %s: in body form, it loads as a blue phantom too\n", why);
    }
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
    take_host_qwc(why);             // and its tendency in use, before this load's area scripts read it
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
        if (std::isfinite(at.yaw))   // facing the way the host faces there
            for (uint32_t off : kGmFacing) wrf(gm + off + 4, at.yaw);
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
    std::fprintf(stderr,
                 "[coop] %s: the host is in %s, %s at %.2f %.2f %.2f facing %.2f; dead characters: %s "
                 "(waited %ld ms, %d names)\n",
                 why, want, joined && !trip ? "arrived" : "stands", at.x, at.y, at.z, at.yaw, dead, ms, swapped);
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
        if (np_psnr_connected()) {
            bool full;   // (the host said its game is full: this one waits, out of the session, for a place)
            {
                std::lock_guard<std::mutex> l(sMsgLock);
                full = sFull;
            }
            sLink = full ? co::Status::Link::Full : co::Status::Link::Connected;
        } else if (sLink.load() == co::Status::Link::Connected || sLink.load() == co::Status::Link::Full) {
            sLink = co::Status::Link::Lost;
        }
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
    {   // the Stone of Ephemeral Eyes' row for this frame: a session, and a friend's soul form
        const bool session = (m == co::Mode::Host && st == kStateHost) ||
                             (m == co::Mode::Join && st == kStateClient && own_kept());
        stone_tick(ctx, session, m == co::Mode::Join && session && !body_form());
        stain_tick(ctx, session && world);
        glow_tick(ctx, session);
    }
    const bool session = st == kStateHost || st == kStateClient;
    if (world && session) dead_copies_tick();
    if (!session) sDied = false;
    if (m == co::Mode::Join) {   // the host's tendency in use while in its world (a change: a new QWC); own again after
        if (!session) own_qwc_back();
        else if (world && !sGame.in_load) take_host_qwc("the host's changed");
    }
    if (m == co::Mode::Host) host_tick(ctx, sm, gm, st, world);
    else guest_tick(ctx, sm, st, world);
    // BACKs: once this game is in the world after its own load (the others' dead characters survive a load, and a
    // BACK can come while this game follows the same load); dropped out of a session
    std::deque<uint32_t> back;
    {
        std::lock_guard<std::mutex> l(sMsgLock);
        if (!session && !sBack.empty()) {
            std::fprintf(stderr, "[coop] out of the session: %zu BACK(s) dropped\n", sBack.size());
            sBack.clear();
        }
        else if (world && !sGame.in_load) back.swap(sBack);
    }
    for (const uint32_t id : back) make_again(ctx, sm, id);
}

// Whether this guest's own world has flag (world: the one it is in play for). Under sOwn.lock.
bool own_has(uint32_t world, uint32_t flag)
{
    const uint8_t bit = (uint8_t)(1u << (flag & 7));
    if (!world_flag(flag) || world == sOwn.home) return (sOwn.flags[flag >> 3] & bit) != 0;
    const long b = own_bank_at(world, flag);
    return b >= 0 && (sOwn.banks[b] & bit) != 0;
}

// An area script was just read (des_seamless_scripts.cpp), before the area starts. On a guest in a session:
// - each Nexus cutscene, or area opening, the host has seen and this player hasn't plays here too: its flag in play
//   becomes the host's and this player's own. Once it has played, the flag goes into this player's own world
//   (des_coop_hook's flag set, kind 4);
// - each boss's soul, key and item of the host's item prompts (own: the soul prompts, 3-1's keys, a sword pulled out)
//   is as this player's own world has it, whoever took it in the host's world: its prompt is here if this player
//   hasn't taken it (and it gets it when another player takes it, the scripts' pickups_for_all and gifts_for_all), and
//   not if it has (no second one).
void des_seamless_area_read(uint32_t world, const std::vector<uint32_t>& scenes, const std::vector<uint32_t>& own)
{
    if ((scenes.empty() && own.empty()) || co::active() != co::Mode::Join) return;
    const uint32_t sm = session_manager(), fb = flag_base();
    if (!guest_ok(sm) || !guest_ok(fb) || vm_read32(sm + kSmState) != kStateClient) return;
    std::lock_guard<std::mutex> l(sOwn.lock);
    if (!sOwn.kept) return;
    for (const uint32_t flag : scenes) {
        if (flag >= kFlagCount) continue;
        const uint8_t bit = (uint8_t)(1u << (flag & 7));
        const uint32_t a = fb + (flag >> 3);
        const uint8_t v = vm_read8(a);
        if ((v & bit) && !own_has(world, flag)) {
            vm_write8(a, (uint8_t)(v & ~bit));
            std::fprintf(stderr, "[coop] cutscene %u: seen in the host's world, not yet by this player; it plays here\n",
                         flag);
        }
    }
    for (const uint32_t flag : own) {
        if (flag >= kFlagCount) continue;
        const uint8_t bit = (uint8_t)(1u << (flag & 7));
        const uint32_t a = fb + (flag >> 3);
        const uint8_t v = vm_read8(a);
        const bool mine = own_has(world, flag);
        if (((v & bit) != 0) == mine) continue;
        vm_write8(a, (uint8_t)(mine ? (v | bit) : (v & ~bit)));
        std::fprintf(stderr, "[coop] a boss's soul, a key or an item (flag %u): %s\n", flag,
                     mine ? "this player took it in its own world, so there's none here"
                          : "taken in the host's world, not by this player: its own is here");
    }
}

// Items (see the top): a guest in a session, its own copies.
bool guest_in_session()
{
    const uint32_t sm = session_manager();
    return guest_ok(sm) && vm_read32(sm + kSmState) == kStateClient;
}

// The area items' add reads whether an item was taken (r3, after bl 0x005087C8; r14 = the item's flag): on a guest in
// a session, as the guest's own world has it, not the host's flags in play.
void item_shown(ppu_context* ctx)
{
    const uint32_t flag = (uint32_t)ctx->gpr[14];
    const uint32_t fb = flag_base();
    if (flag >= kFlagCount || !guest_ok(fb) || !guest_in_session()) return;
    const int own = own_flag(world_of(vm_read32(fb + kFlagsArea)), flag);
    if (own < 0) return;
    const int in_play = ((uint32_t)ctx->gpr[3] & 0xFF) ? 1 : 0;
    static std::atomic<int> said{0};
    if (own != in_play && said.fetch_add(1) < 32)
        std::fprintf(stderr, "[coop] an item here (flag %u): %s\n", flag,
                     own ? "taken in this player's world, so not here" : "the host's taken, this player's not");
    ctx->gpr[3] = (uint64_t)own;
}

// An area item taken by a guest in the host's world: kept in its own world, as a boss's soul is.
void item_taken(uint32_t fb, uint32_t flag)
{
    if (!guest_in_session() || !sGame.arrived.load()) return;
    if (keep_flag(fb, world_of(vm_read32(fb + kFlagsArea)), flag))
        std::fprintf(stderr, "[coop] an item taken: flag %u kept for this player's world\n", flag);
}

// Instruction hooks (port/insn_hooks.txt), with the US disc's addresses as keys.
extern "C" void des_coop_hook(ppu_context* ctx, uint32_t addr)
{
    if (addr == 0x0006C860u) {   // 0x0006C850's bl 0x00080E88 returned: kind 2's material back in its place, always
        if (tTintMgr) {
            vm_write32(tTintMgr + 0x70, tTintMat);
            tTintMgr = 0;
        }
        return;
    }
    const co::Mode m = co::active();
    if (m == co::Mode::Off) return;
    switch (addr) {
    case 0x00080E88u:   // the ghost colour upload's first instruction (cmpwi r5,0): a player's own glow colour
        tint_begin(ctx);
        break;
    case 0x000E9650u:   // the ghost model draw 0x000E8C48, after bl 0x0006C908: its own colours before its constants
        tint_pre(ctx);
        break;
    case 0x002DDAA8u:   // a character's draw update (0x002DD920), after bl 0x0026F378: r3 its type, r31 the character
        // The type picks the draw object's (+0x6A4) phantom material, on or off (0x000D7C58: on for types 1-4, 8, 10,
        // 11). A blue phantom whose player turned its glow off is drawn as an alive player (glow_tick).
        if ((uint32_t)ctx->gpr[3] == 1 && !glow_shown((uint32_t)ctx->gpr[31])) ctx->gpr[3] = 0;
        break;
    case 0x0017A4A8u:   // the summoned reload's load step, after addi r9,r26,0x34
        if (m == co::Mode::Join) guest_load_step(ctx);
        break;
    case 0x004117E4u:   // the Nexus script's RecvGlobalQwc (0x004117C0), after bl 0x009204A0: the online blend set the
        // tendency in use from this player's own; a friend in the session plays in the host's
        if (m == co::Mode::Join) take_host_qwc("after the Nexus's blend");
        break;
    case 0x00272A88u:   // an enemy's souls for this game's player (0x00272938), after bl 0x0052A610: r3 1 for a guest
        // Each game gives its own player an enemy's souls when it dies (its NpcParam souls, through 0x002D6C40), halved
        // when the session state is 6 (a guest, 0x0052A610). A friend in seamless co-op gets the full souls, as the
        // host does.
        if (m == co::Mode::Join) ctx->gpr[3] = 0;
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
        if (!guest_ok(fb) || fb != flag_base()) break;
        // An area item taken (the item list's remove sets its flag): a guest keeps it in its own world, where its
        // item list reads it (0x0025CC34). In play it can be set already: the host may have taken its own copy.
        if (m == co::Mode::Join && (uint32_t)ctx->lr - 4 == DES_AT(0x0025C5ECu)) {
            item_taken(fb, flag);
            break;
        }
        if ((vm_read8(fb + (flag >> 3)) >> (flag & 7)) & 1) break;   // set already
        const uint32_t sm = session_manager();
        const uint32_t st = guest_ok(sm) ? vm_read32(sm + kSmState) : 0;
        const bool host = m == co::Mode::Host && st == kStateHost;
        const bool joined = m == co::Mode::Join && st == kStateClient;
        if (!host && !joined) break;
        const uint32_t area = vm_read32(fb + kFlagsArea);
        const int kind = des_seamless_flag_kind(world_of(area), flag);
        // A cutscene's flag (the Nexus's, an area's opening) is set as the area starts, before the guest has arrived
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
                             kind == 2 ? "a soul, key or item taken" : kind == 4 ? "a cutscene seen" : "progress", flag);
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
    case 0x0025CB6Cu:   // the area items' add (0x0025CAE8), after lwz r9,0x8C(r9): the type the area was loaded for
    case 0x002B2984u:   // a corpse's drop add (0x002B2900), the same
        // Only body (0) and soul form (8) get items; a friend is a blue phantom (1) in the host's world
        if (m == co::Mode::Join && (uint32_t)ctx->gpr[9] == 1) ctx->gpr[9] = 0;
        break;
    case 0x0053EA2Cu: { // the session update's check of a joined guest (state 6), after lwz r0,0x8C(r9): its type
        // The game summons only players in soul form: a guest whose type reads body form (0) raises
        // OnFailedJoinSession and leaves the room. A friend joins in either form, and keeps it: the game records it
        // for the trip home (+0xA90), and the summoned load makes it a blue phantom (guest_load_step).
        // Only where the disc has that load (lwz r0,0x8C(r9)), as all four have.
        static const bool known = [] {
            const bool ok = vm_read32(DES_AT(0x0053EA2Cu)) == 0x8009008Cu;
            if (!ok) std::fprintf(stderr, "[coop] the joined guest's check is not where it was looked for: joins stay in soul form\n");
            return ok;
        }();
        if (known && m == co::Mode::Join && (uint32_t)ctx->gpr[0] == 0) {
            static std::atomic<bool> said{false};
            if (!said.exchange(true)) std::fprintf(stderr, "[coop] joining in body form\n");
            ctx->gpr[0] = 1;
        }
        break;
    }
    case 0x0043E4E0u: { // SetTextEffect's native, after its mr r3,r4 (r3: the text's type): "The Demon Was Destroyed"
        // On a friend an area's boss-death handler runs twice (its own copy of the boss, then the host's message: 1-1's
        // OnEvent_4096), and each starts the boss clear, so the text came twice. Once per load: a second is dropped
        // (type 0 queues nothing, 0x00722170).
        static const bool known = vm_read32(DES_AT(0x0043E4E0u)) == 0x7C832378u;   // the instruction on all four discs
        if (known && m == co::Mode::Join && (uint32_t)ctx->gpr[3] == kTextKillDemon && own_kept()) {
            if (sKillShown.exchange(true)) {
                ctx->gpr[3] = 0;
                std::fprintf(stderr, "[coop] a second \"The Demon Was Destroyed\" for this kill: not shown\n");
            } else {
                sKillText = true;
            }
        }
        break;
    }
    case 0x0025CC34u:   // the area items' add, after bl 0x005087C8: the item taken already?
        if (m == co::Mode::Join) item_shown(ctx);
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
    case 0x0044C780u: { // the end of WarpNextStageKick (ld r0,0x90(r1)): a load asked for
        const uint32_t sm = session_manager();
        if (m != co::Mode::Join || !guest_ok(sm) || vm_read32(sm + kSmState) != 0) break;   // only out of a session
        going_home(game_manager());
        break;
    }
    default:
        break;
    }
}
