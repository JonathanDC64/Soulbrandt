/* pantheon_check's stand-in for ps3recomp's ppu_lift.h: des_pantheon.cpp reads guest memory only through vm_read8, 16
 * and 32, so here they read a sparse, big-endian memory the test fills in (pantheon_mem). Nothing else of the runtime
 * is there: the test runs no game. */
#pragma once
#include <cstdint>
#include <cstring>
#include <map>

inline std::map<uint32_t, uint8_t>& pantheon_mem()
{
    static std::map<uint32_t, uint8_t> m;
    return m;
}
inline uint8_t vm_read8(uint64_t a)
{
    const auto it = pantheon_mem().find((uint32_t)a);
    return it == pantheon_mem().end() ? 0 : it->second;
}
inline uint16_t vm_read16(uint64_t a) { return (uint16_t)(vm_read8(a) << 8 | vm_read8(a + 1)); }
inline uint32_t vm_read32(uint64_t a) { return (uint32_t)vm_read16(a) << 16 | vm_read16(a + 2); }
inline void pantheon_put8(uint32_t a, uint8_t v) { pantheon_mem()[a] = v; }
inline void pantheon_put16(uint32_t a, uint16_t v) { pantheon_put8(a, (uint8_t)(v >> 8)); pantheon_put8(a + 1, (uint8_t)v); }
inline void pantheon_put32(uint32_t a, uint32_t v) { pantheon_put16(a, (uint16_t)(v >> 16)); pantheon_put16(a + 2, (uint16_t)v); }
