// Written for Demon's Souls. The menus' shaders kept compiled between starts: see shader_cache.hpp.
//
// A file: 32 bytes of header, the input, the bytecode.
//   0   "DESUISC1" (the format, 1)
//   8   the input's size, 12 the bytecode's (little-endian u32 each)
//   16  FNV-1a 64 of the input, 24 of the bytecode (little-endian u64 each)
// Its size is exactly the header's and the two together.
#include "shader_cache.hpp"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#pragma comment(lib, "version.lib")
#else
#include <unistd.h>
#if defined(__linux__)
#include <elf.h>
#include <link.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#endif
#endif

namespace ui::shader_cache {
namespace {

constexpr char kMagic[8] = {'D', 'E', 'S', 'U', 'I', 'S', 'C', '1'};
constexpr size_t kHeader = 32;
constexpr uintmax_t kMaxFile = uintmax_t(64) << 20;  // (the largest of the menu's is under 100 KB)

uint64_t fnv(const void* data, size_t n) {
    const auto* p = static_cast<const uint8_t*>(data);
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t rd64(const uint8_t* p) { return uint64_t(rd32(p)) | uint64_t(rd32(p + 4)) << 32; }
void wr32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i));
}
void wr64(uint8_t* p, uint64_t v) {
    wr32(p, uint32_t(v));
    wr32(p + 4, uint32_t(v >> 32));
}

// A path as text, the same at every start (UTF-8 on Windows).
std::string text_of(const std::filesystem::path& p) {
#ifdef _WIN32
    const std::wstring& w = p.native();
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
#else
    return p.native();
#endif
}

// A file's path, size and time; empty if any of them cannot be read.
std::string identity_of(const std::filesystem::path& p) {
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(p, ec);
    if (ec) return {};
    const auto time = std::filesystem::last_write_time(p, ec);
    if (ec) return {};
    const std::string path = text_of(p);
    if (path.empty()) return {};
    return path + " size " + std::to_string(size) + " time " +
           std::to_string(static_cast<long long>(time.time_since_epoch().count()));
}

unsigned long this_process() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}

}  // namespace

bool enabled() {
    static const bool on = [] {
        const char* v = std::getenv("DES_UI_SHADER_CACHE");
        return !v || std::strcmp(v, "0") != 0;
    }();
    return on;
}

std::filesystem::path file(const std::string& dir, const std::string& name) {
    if (!enabled() || dir.empty() || name.empty()) return {};
    std::string n;
    for (const char c : name) n += std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' ? c : '_';
    // (dir as the compile service has it: the system's code page on Windows, as its own file calls take it)
    return std::filesystem::path(dir) / "ui" / (n + ".bin");
}

bool whole(Code code, const uint8_t* data, size_t size) {
    if (!data) return false;
    if (code == Code::Dxbc)
        return size >= 32 && std::memcmp(data, "DXBC", 4) == 0 && rd32(data + 24) == size;
    return size >= 20 && size % 4 == 0 && rd32(data) == 0x07230203u;
}

Miss load(const std::filesystem::path& file, const std::string& input, Code code, std::vector<uint8_t>& out) {
    out.clear();
    if (file.empty()) return Miss::Missing;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return Miss::Missing;
    const uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec) return Miss::Missing;
    if (size < kHeader || size > kMaxFile) return Miss::Damaged;
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    {
        std::ifstream f(file, std::ios::binary);
        if (!f) return Miss::Missing;
        f.read(reinterpret_cast<char*>(buf.data()), std::streamsize(buf.size()));
        // (a file that grew since its size was read: not the one whose size was checked)
        if (size_t(f.gcount()) != buf.size() || f.peek() != std::ifstream::traits_type::eof()) return Miss::Damaged;
    }
    const uint8_t* h = buf.data();
    if (std::memcmp(h, kMagic, 7) != 0) return Miss::Damaged;
    if (h[7] != uint8_t(kMagic[7])) return Miss::Outdated;  // (another format of these files)
    const uint64_t in = rd32(h + 8), cn = rd32(h + 12);
    if (kHeader + in + cn != size || cn == 0) return Miss::Damaged;
    const uint8_t* stored = h + kHeader;
    const uint8_t* bytes = stored + in;
    if (fnv(stored, size_t(in)) != rd64(h + 16)) return Miss::Damaged;
    if (in != input.size() || std::memcmp(stored, input.data(), size_t(in)) != 0) return Miss::Outdated;
    if (fnv(bytes, size_t(cn)) != rd64(h + 24) || !whole(code, bytes, size_t(cn))) return Miss::Damaged;
    out.assign(bytes, bytes + cn);
    return Miss::Used;
}

bool store(const std::filesystem::path& file, const std::string& input, const void* data, size_t size) {
    if (file.empty() || !data || !size || input.size() > 0xFFFFFFFFu || size > 0xFFFFFFFFu ||
        kHeader + input.size() + size > kMaxFile)
        return false;
    std::error_code ec;
    const std::filesystem::path dir = file.parent_path();
    std::filesystem::create_directories(dir, ec);
    // A copy of this file left half written (a game that ended while it wrote): gone. (One another game is writing just
    // now: then that game's copy is not stored, which only means it compiles again next time.)
    const std::string base = text_of(file.filename()) + ".";
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = text_of(it->path().filename());
        if (n.size() > base.size() + 4 && n.compare(0, base.size(), base) == 0 && n.compare(n.size() - 4, 4, ".tmp") == 0) {
            std::error_code rm;
            std::filesystem::remove(it->path(), rm);
        }
    }
    static std::atomic<unsigned> counter{0};
    std::filesystem::path tmp = file;
    tmp += "." + std::to_string(this_process()) + "." + std::to_string(counter.fetch_add(1)) + ".tmp";
    uint8_t h[kHeader];
    std::memcpy(h, kMagic, sizeof kMagic);
    wr32(h + 8, uint32_t(input.size()));
    wr32(h + 12, uint32_t(size));
    wr64(h + 16, fnv(input.data(), input.size()));
    wr64(h + 24, fnv(data, size));
    bool ok;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(h), sizeof h);
        f.write(input.data(), std::streamsize(input.size()));
        f.write(static_cast<const char*>(data), std::streamsize(size));
        f.close();
        ok = !f.fail();
    }
    if (ok) {
        std::filesystem::rename(tmp, file, ec);  // (in its place at once: a reader sees the old file or this one)
        ok = !ec;
    }
    if (!ok) std::filesystem::remove(tmp, ec);
    return ok;
}

void forget(const std::filesystem::path& file) {
    if (file.empty()) return;
    std::error_code ec;
    std::filesystem::remove(file, ec);
}

std::string program_identity() {
#ifdef _WIN32
    // The module this code is in (the program), as loaded: the linker's stamp, the image's size and checksum, and the
    // debug id (CodeView's GUID and age), which a new link changes, the same contents or not.
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&program_identity), &m) ||
        !m)
        return {};
    const auto* base = reinterpret_cast<const uint8_t*>(m);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return {};
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return {};
    char buf[96];
    std::snprintf(buf, sizeof buf, "pe stamp %08lx size %08lx sum %08lx", (unsigned long)nt->FileHeader.TimeDateStamp,
                  (unsigned long)nt->OptionalHeader.SizeOfImage, (unsigned long)nt->OptionalHeader.CheckSum);
    std::string id = buf;
    bool debug_id = false;
    const IMAGE_DATA_DIRECTORY& dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (dd.VirtualAddress && dd.Size >= sizeof(IMAGE_DEBUG_DIRECTORY)) {
        const auto* dbg = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + dd.VirtualAddress);
        for (size_t i = 0; i < dd.Size / sizeof *dbg && !debug_id; i++) {
            if (dbg[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW || !dbg[i].AddressOfRawData || dbg[i].SizeOfData < 24) continue;
            const uint8_t* cv = base + dbg[i].AddressOfRawData;
            if (std::memcmp(cv, "RSDS", 4) != 0) continue;
            id += " debug ";
            for (int k = 4; k < 24; k++) {  // (the GUID, then the age)
                std::snprintf(buf, sizeof buf, "%02x", cv[k]);
                id += buf;
            }
            debug_id = true;
        }
    }
    if (!debug_id && nt->FileHeader.TimeDateStamp == 0) return {};  // (nothing that tells one build from another)
    return id;
#elif defined(__linux__)
    // The program's build id (its GNU note), as loaded; without one, its file's size and time.
    std::string id;
    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t, void* data) -> int {
            auto* out = static_cast<std::string*>(data);
            for (int i = 0; i < info->dlpi_phnum; i++) {
                const ElfW(Phdr)& ph = info->dlpi_phdr[i];
                if (ph.p_type != PT_NOTE) continue;
                const uint8_t* p = reinterpret_cast<const uint8_t*>(info->dlpi_addr + ph.p_vaddr);
                const uint8_t* const end = p + ph.p_memsz;
                while (p + sizeof(ElfW(Nhdr)) <= end) {
                    const auto* n = reinterpret_cast<const ElfW(Nhdr)*>(p);
                    const uint8_t* name = p + sizeof *n;
                    const uint8_t* desc = name + ((n->n_namesz + 3u) & ~3u);
                    const uint8_t* next = desc + ((n->n_descsz + 3u) & ~3u);
                    if (next > end) break;
                    if (n->n_type == NT_GNU_BUILD_ID && n->n_namesz == 4 && std::memcmp(name, "GNU", 4) == 0 &&
                        n->n_descsz) {
                        *out = "build-id ";
                        char b[3];
                        for (uint32_t k = 0; k < n->n_descsz; k++) {
                            std::snprintf(b, sizeof b, "%02x", desc[k]);
                            *out += b;
                        }
                        return 1;
                    }
                    p = next;
                }
            }
            return 1;  // (the first object is the program: no further)
        },
        &id);
    if (!id.empty()) return id;
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::string() : identity_of(exe);
#elif defined(__APPLE__)
    // The program's UUID (its LC_UUID, which the linker writes new for each build), as loaded: image 0 is the
    // program. Without one, its file's size and time.
    if (const auto* h = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(0));
        h && h->magic == MH_MAGIC_64) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(h + 1);
        for (uint32_t i = 0; i < h->ncmds; i++) {
            const auto* lc = reinterpret_cast<const load_command*>(p);
            if (lc->cmd == LC_UUID) {
                std::string id = "uuid ";
                char b[3];
                for (uint8_t v : reinterpret_cast<const uuid_command*>(p)->uuid) {
                    std::snprintf(b, sizeof b, "%02x", v);
                    id += b;
                }
                return id;
            }
            p += lc->cmdsize;
        }
    }
    char path[4096];
    uint32_t size = sizeof path;
    return _NSGetExecutablePath(path, &size) == 0 ? identity_of(std::filesystem::path(path)) : std::string();
#else
    return {};
#endif
}

std::string library_identity(const char* name) {
#ifdef _WIN32
    const HMODULE m = name ? GetModuleHandleA(name) : nullptr;
    if (!m) return {};
    std::wstring path(32768, L'\0');
    const DWORD n = GetModuleFileNameW(m, path.data(), DWORD(path.size()));
    if (!n || n >= path.size()) return {};
    path.resize(n);
    std::string id = identity_of(std::filesystem::path(path));
    if (id.empty()) return {};
    DWORD handle = 0;
    const DWORD vsize = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (!vsize) return {};
    std::vector<uint8_t> info(vsize);
    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT len = 0;
    if (!GetFileVersionInfoW(path.c_str(), 0, vsize, info.data()) ||
        !VerQueryValueW(info.data(), L"\\", reinterpret_cast<void**>(&ffi), &len) || !ffi || len < sizeof *ffi)
        return {};
    char v[64];
    std::snprintf(v, sizeof v, " version %u.%u.%u.%u", unsigned(HIWORD(ffi->dwFileVersionMS)),
                  unsigned(LOWORD(ffi->dwFileVersionMS)), unsigned(HIWORD(ffi->dwFileVersionLS)),
                  unsigned(LOWORD(ffi->dwFileVersionLS)));
    return id + v;
#else
    (void)name;
    return {};
#endif
}

}  // namespace ui::shader_cache
