// storage_check's stand-in for the runtime: sparse, big-endian guest memory,
// registers and a trampoline. No game files, generated lift or network.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>

#define PPU_THREAD_LOCAL thread_local
extern "C" void (*g_trampoline_fn)(void*);
struct ppu_context {
    uint64_t gpr[32] = {};
    double fpr[32] = {};
    uint8_t vr[32][16] = {};
    uint64_t lr = 0, ctr = 0;
    uint32_t cr = 0, xer = 0;
};
inline uint32_t ppu_cr_pack(ppu_context* ctx) { return ctx->cr; }
inline void ppu_cr_unpack(ppu_context* ctx, uint32_t value) { ctx->cr = value; }
inline std::map<uint32_t, uint8_t>& storage_mem() {
    static std::map<uint32_t, uint8_t> memory;
    return memory;
}
inline uint8_t vm_read8(uint64_t at) {
    const auto found = storage_mem().find((uint32_t)at);
    return found == storage_mem().end() ? 0 : found->second;
}
inline uint16_t vm_read16(uint64_t at) { return (uint16_t)(vm_read8(at) << 8 | vm_read8(at + 1)); }
inline uint32_t vm_read32(uint64_t at) { return (uint32_t)vm_read16(at) << 16 | vm_read16(at + 2); }
inline void vm_write8(uint64_t at, uint8_t value) { storage_mem()[(uint32_t)at] = value; }
inline void vm_write16(uint64_t at, uint16_t value) {
    vm_write8(at, (uint8_t)(value >> 8)); vm_write8(at + 1, (uint8_t)value);
}
inline void vm_write32(uint64_t at, uint32_t value) {
    vm_write16(at, (uint16_t)(value >> 16)); vm_write16(at + 2, (uint16_t)value);
}
