/* Lua's garbage above 30 fps.
 *
 * The game's scripts run on Lua 5.0 with the automatic collector off: the
 * threshold in the global state is 0, and the game's copy of the check skips
 * a threshold of 0. Two script systems run the collector themselves from a
 * countdown in game time, once a frame: 0x0043EB48 every 1.0 s (timer at
 * +0x70, script manager at +0x04) and 0x00676F78 every 0.5 s (timer at +0x10,
 * script manager at +0x00).
 *
 * Scripts make their garbage per frame, so at 200 fps there is about seven
 * times as much between two collections as the 30 fps game made, and the two
 * states share a small heap. At the 1-2 bridge they hold about 1.4 MB, and
 * the game dies at about 1.9 MB. When an allocation fails, luaM_realloc
 * (0x00CBA290) calls luaD_throw (0x00CAE730) with LUA_ERRMEM. Nothing catches
 * it on a character's script, so Lua calls exit(1), and the window closes.
 *
 * So once a frame, above 30 fps, the countdown is expired early when its state
 * has made kGarbage bytes since its last collection. The game then runs its
 * own collection at its own place in the frame. The count is of the guest's
 * bytes, so it doesn't depend on the PC, and at the rates of a quiet scene the
 * game's own countdown comes first and this does nothing. At 30 fps and below
 * it never acts.
 *
 * DES_LUAGC=0        the game's countdowns only
 * DES_LUAGC_LOG=1    each second: each state's bytes in use, the peak, early collections
 * DES_LUAGC_HOLD=1   TEST: the game's countdowns never reach zero. With DES_LUAGC=0 this
 *                    is the crash on demand (about 30 s at the 1-2 bridge at 200 fps);
 *                    without it, every collection is one of these.
 */
#include "ps3emu/ppu_lift.h"
#include "des_region.h"      /* DES_AT: the running disc's addresses */
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr uint32_t kLuaSystemA = 0x01B42A80u;   // *this + 4: the 1.0 s system
constexpr uint32_t kLuaSystemB = 0x01B49BD8u;   // **this: the 0.5 s system
constexpr uint32_t kTimerA = 0x70, kManagerA = 0x04;
constexpr uint32_t kTimerB = 0x10, kManagerB = 0x00;
constexpr uint32_t kManagerLua = 0x18;          // script manager -> lua_State
constexpr uint32_t kLuaGlobal = 0x10;           // lua_State -> global_State
constexpr uint32_t kGlobalInUse = 0x24;         // nblocks: the state's bytes in use

// About what a busy 30 fps second makes, and a small part of the room there is.
constexpr uint32_t kGarbage = 64 * 1024;
constexpr double kAbove30 = 1.0 / 31.0;         // a frame shorter than this (seconds)
constexpr uint32_t kShortFrames = 8;            // that many in a row
constexpr uint32_t kNever = 0x44FA0000u;        // 2000.0f, for DES_LUAGC_HOLD

bool ok(uint32_t a) { return a >= 0x00010000u && a < 0xD0000000u && !(a & 3u); }

struct State {
    uint32_t lua = 0;
    uint32_t base = 0;       // bytes in use after the last collection
    uint32_t peak = 0;       // for the log: the second's highest
    uint32_t now = 0;
    uint32_t early = 0;      // for the log: collections asked for
    bool pending = false;    // the countdown was expired, its collection is still to come
};

int g_mode = -1;
bool g_log = false, g_hold = false;

void step(uint32_t system, uint32_t timer, uint32_t manager_at, State& s, bool above_30)
{
    if (!ok(system)) { s = State{}; return; }
    const uint32_t manager = vm_read32(system + manager_at);
    const uint32_t lua = ok(manager) ? vm_read32(manager + kManagerLua) : 0;
    const uint32_t global = ok(lua) ? vm_read32(lua + kLuaGlobal) : 0;
    if (!ok(global)) { s = State{}; return; }

    const uint32_t ea = system + timer;
    const uint32_t n = vm_read32(global + kGlobalInUse);
    if (lua != s.lua) {   // a new state, after a load
        s = State{};
        s.lua = lua;
        s.base = n;
    }
    s.now = n;
    if (n > s.peak) s.peak = n;
    if (s.pending) {
        if (vm_read32(ea) == 0) return;   // the game has not got to it yet
        s.pending = false;
        s.base = n;   // what the collection left, even if the live set grew
    }
    if (n < s.base) s.base = n;   // the game's own collection
    if (g_mode != 0 && above_30 && n - s.base > kGarbage) {
        vm_write32(ea, 0);
        s.pending = true;
        s.early++;
        return;
    }
    if (g_hold) vm_write32(ea, kNever);
}

} // namespace

extern "C" void des_lua_gc_tick(double dt)
{
    if (g_mode < 0) {
        const char* v = std::getenv("DES_LUAGC");
        g_mode = v && *v ? std::atoi(v) : 1;
        g_log = std::getenv("DES_LUAGC_LOG") != nullptr;
        g_hold = std::getenv("DES_LUAGC_HOLD") != nullptr;
        if (g_mode == 0) std::fprintf(stderr, "[des] DES_LUAGC=0: Lua is collected on the game's countdowns only\n");
        if (g_hold) std::fprintf(stderr, "[des] DES_LUAGC_HOLD: the game's Lua countdowns never reach zero (test)\n");
    }
    if (g_mode == 0 && !g_log && !g_hold) return;

    // Several in a row: a 30 fps game's frame times wobble, and one short frame is not a high frame rate.
    static uint32_t short_frames = 0;
    short_frames = dt > 0.0 && dt < kAbove30 ? short_frames + 1 : 0;
    const bool above_30 = short_frames >= kShortFrames;
    static State a, b;
    const uint32_t ga = vm_read32(DES_AT(kLuaSystemA));
    step(ok(ga) ? vm_read32(ga + 4) : 0, kTimerA, kManagerA, a, above_30);
    const uint32_t gb = vm_read32(DES_AT(kLuaSystemB));
    step(ok(gb) ? vm_read32(gb) : 0, kTimerB, kManagerB, b, above_30);
    if (!g_log) return;

    static uint32_t frames = 0, seconds = 0;
    static auto next = std::chrono::steady_clock::now();
    ++frames;
    const auto t = std::chrono::steady_clock::now();
    if (t < next) return;
    next = t + std::chrono::seconds(1);
    std::fprintf(stderr, "[des] lua: %us, %u frames, dt %.4f | A %u now, %u peak, %u early | B %u now, %u peak, %u early\n",
                 ++seconds, frames, dt, a.now, a.peak, a.early, b.now, b.peak, b.early);
    frames = 0;
    a.peak = b.peak = 0;
}
