/* A game that ends itself with an error is a crash to the player.
 *
 * The game's fatal paths end in sys_process_exit with a non-zero code: abort()
 * does, and so does Lua's exit(1) when an error finds no protected call (at
 * 0x00CAE79C; an out-of-memory comes through luaM_realloc). None of that is a
 * fault, so the crash report's exception filter and abort handler never run,
 * and the window just closes. This reports it.
 *
 * The host's own ways out (closing the window, Quit, EXIT_AFTER_MS,
 * DES_ROUTE_QUIT) are a TerminateProcess with code 0 and never come here. Nor
 * does a code-0 exit, or going back to the title, which ends nothing.
 *
 * exit() runs the game's static destructors under threads that are still
 * playing, so several threads can get here within a millisecond of each other:
 * in the crashes this was written for, the character and sound threads both
 * aborted on objects the teardown had freed. Each thread records what it
 * knows. The first waits a moment for the others and writes the one report.
 * The others stay here, since their sys_process_exit would end the process
 * under the report.
 */
#include "ps3emu/ppu_lift.h"
#include "des_region.h"      /* DES_AT: the running disc's addresses */
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

extern "C" void (*g_ps3_guest_exit_hook)(uint32_t code, uint32_t guest_lr, uint32_t guest_sp, const char* host_bt);
extern "C" int des_ui_crash_reports_on(void);                                  /* crash_report.cpp */
extern "C" void des_ui_report_guest_exit(const char* what, const char* notes); /* crash_report.cpp */

namespace {

constexpr uint32_t kLuaMemoryThrow = 0x00CBA408u;   // luaM_realloc: where luaD_throw(L, LUA_ERRMEM) returns to
constexpr uint32_t kStackScan = 0x4000;
constexpr int kThreads = 8;

struct Arrival {
    uint32_t code, lr, sp;
    char bt[1100];
    std::atomic<bool> set;
};
Arrival g_arrival[kThreads];
std::atomic<int> g_arrived{0};
std::atomic<bool> g_lua_memory{false};

// The live frames above sp hold the saved return addresses, whichever thread this is.
bool lua_memory_on_stack(uint32_t sp)
{
    if (sp < 0xD0000000u || sp >= 0xE0000000u) return false;   // the guest's thread stacks
    const uint32_t want = DES_AT(kLuaMemoryThrow);
    const uint32_t from = sp & ~3u;
    for (uint32_t a = from; a < from + kStackScan; a += 4)
        if (vm_read32(a) == want) return true;
    return false;
}

void on_guest_exit(uint32_t code, uint32_t lr, uint32_t sp, const char* host_bt)
{
    if (!des_ui_crash_reports_on()) return;   // a test run: as if this weren't here
    const int n = g_arrived.fetch_add(1, std::memory_order_acq_rel);
    if (lua_memory_on_stack(sp)) g_lua_memory.store(true, std::memory_order_release);
    if (n < kThreads) {
        Arrival& a = g_arrival[n];
        a.code = code;
        a.lr = lr;
        a.sp = sp;
        std::snprintf(a.bt, sizeof a.bt, "%s", host_bt ? host_bt : "");
        a.set.store(true, std::memory_order_release);
    }
    if (n != 0)
        for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));   // the first ends the process

    std::this_thread::sleep_for(std::chrono::milliseconds(200));   // the others, usually a millisecond behind

    char what[160];
    if (g_lua_memory.load(std::memory_order_acquire))
        std::snprintf(what, sizeof what, "the game's scripts ran out of memory, and the game closed itself");
    else
        std::snprintf(what, sizeof what, "the game closed itself with error code %u", code);

    std::string notes;
    const int seen = g_arrived.load(std::memory_order_acquire);
    for (int i = 0; i < seen && i < kThreads; i++) {
        const Arrival& a = g_arrival[i];
        if (!a.set.load(std::memory_order_acquire)) continue;
        char line[96];
        std::snprintf(line, sizeof line, "guest exit, thread %d: code %u, lr %08X, sp %08X\n", i + 1, a.code, a.lr, a.sp);
        notes += line;
        if (a.bt[0]) {
            notes += a.bt;
            notes += '\n';
        }
    }
    des_ui_report_guest_exit(what, notes.c_str());
}

} // namespace

extern "C" void des_guest_exit_init(void)
{
    g_ps3_guest_exit_hook = on_guest_exit;
}
