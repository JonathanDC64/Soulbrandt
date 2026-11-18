// shader_cache.cpp on its own: a stored shader comes back only for exactly the input it was made from, and only whole.
// Every way a file can be wrong (another input, cut short, a byte changed in any part, bytes added, another format,
// the wrong kind of bytecode, a half-written copy left behind) must give the shader to the compiler again, never to
// the device. In a folder of its own under the system's temporary folder. Exit 0 when every case holds.
#include "shader_cache.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace sc = ui::shader_cache;
namespace fs = std::filesystem;

namespace {

int g_bad = 0, g_cases = 0;

void check(bool ok, const char* what) {
    g_cases++;
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        g_bad++;
    }
}

const char* name_of(sc::Miss m) {
    switch (m) {
    case sc::Miss::Used: return "used";
    case sc::Miss::Missing: return "missing";
    case sc::Miss::Outdated: return "outdated";
    case sc::Miss::Damaged: return "damaged";
    }
    return "?";
}

void expect(sc::Miss got, sc::Miss want, const std::vector<uint8_t>& out, const char* what) {
    g_cases++;
    // (anything but Used must hand out nothing)
    if (got != want || (got != sc::Miss::Used && !out.empty())) {
        std::printf("FAIL: %s: got %s (%zu bytes), want %s\n", what, name_of(got), out.size(), name_of(want));
        g_bad++;
    }
}

// A whole DXBC container of n bytes (its size field at 24) and a SPIR-V module of n words, with made-up contents.
std::vector<uint8_t> dxbc(size_t n, uint8_t fill) {
    std::vector<uint8_t> b(n, fill);
    std::memcpy(b.data(), "DXBC", 4);
    for (int i = 0; i < 4; i++) b[24 + i] = uint8_t(n >> (8 * i));
    return b;
}
std::vector<uint8_t> spirv(size_t words, uint8_t fill) {
    std::vector<uint8_t> b(words * 4, fill);
    const uint32_t magic = 0x07230203u;
    for (int i = 0; i < 4; i++) b[i] = uint8_t(magic >> (8 * i));
    return b;
}

std::vector<uint8_t> read_all(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void write_all(const fs::path& p, const std::vector<uint8_t>& b) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

size_t count_files(const fs::path& dir) {
    size_t n = 0;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) n++;
    return n;
}

fs::path program_path() {
#ifdef _WIN32
    std::wstring p(32768, L'\0');
    p.resize(GetModuleFileNameW(nullptr, p.data(), DWORD(p.size())));
    return fs::path(p);
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec);
#endif
}

// What a program says with --identity (its program_identity), run as it is.
std::string run_for_identity(const fs::path& exe) {
    const std::string cmd = "\"" + exe.string() + "\" --identity";
#ifdef _WIN32
    FILE* p = _popen(("\"" + cmd + "\"").c_str(), "r");  // (cmd.exe takes the outer quotes off)
#else
    FILE* p = popen(cmd.c_str(), "r");
#endif
    if (!p) return {};
    std::string out;
    char buf[512];
    while (std::fgets(buf, sizeof buf, p)) out += buf;
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    // (the copy's run, for "a copy of this program is the same build")
    if (argc == 2 && std::strcmp(argv[1], "--identity") == 0) {
        std::printf("%s\n", sc::program_identity().c_str());
        return 0;
    }
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / ("shader_cache_check_" + std::to_string(getpid()));
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const std::string dir = root.string();

    // Where a file goes: <dir>/ui/<name>.bin, the name kept to letters, digits, _ and -.
    const fs::path f = sc::file(dir, "dxbc_ps_model blend");
    check(f == root / "ui" / "dxbc_ps_model_blend.bin", "the file's name");
    check(sc::file("", "x").empty(), "no folder: no file");
    check(sc::file(dir, "").empty(), "no name: no file");
    check(sc::enabled(), "on unless DES_UI_SHADER_CACHE=0");

    // What makes a whole program.
    {
        const auto d = dxbc(64, 7), s = spirv(8, 9);
        check(sc::whole(sc::Code::Dxbc, d.data(), d.size()), "a whole DXBC container");
        check(!sc::whole(sc::Code::Dxbc, d.data(), d.size() - 1), "DXBC shorter than its size field");
        check(!sc::whole(sc::Code::Spirv, d.data(), d.size()), "DXBC is not SPIR-V");
        check(sc::whole(sc::Code::Spirv, s.data(), s.size()), "a SPIR-V module");
        check(!sc::whole(sc::Code::Spirv, s.data(), s.size() - 2), "SPIR-V of part words");
        check(!sc::whole(sc::Code::Dxbc, s.data(), s.size()), "SPIR-V is not DXBC");
        check(!sc::whole(sc::Code::Dxbc, nullptr, 0), "nothing");
    }

    const std::string input = "the menu's shader, input 1\nprogram x\ncompiler y\nname fog\ntext 11\nfloat4 main";
    const auto code = dxbc(200, 0x5A);
    std::vector<uint8_t> out;

    expect(sc::load(f, input, sc::Code::Dxbc, out), sc::Miss::Missing, out, "no file yet");
    expect(sc::load(fs::path(), input, sc::Code::Dxbc, out), sc::Miss::Missing, out, "no path");

    // Stored, then loaded for the same input: the same bytes.
    check(sc::store(f, input, code.data(), code.size()), "store");
    expect(sc::load(f, input, sc::Code::Dxbc, out), sc::Miss::Used, out, "the same input");
    check(out == code, "the same bytes back");
    check(count_files(f.parent_path()) == 1, "nothing but the file left in the folder");

    // Another input, by one byte anywhere or by its length: compiled again.
    {
        std::string other = input;
        other[other.size() - 1] ^= 1;
        expect(sc::load(f, other, sc::Code::Dxbc, out), sc::Miss::Outdated, out, "the text changed by a byte");
        other = input;
        other[10] ^= 0x20;
        expect(sc::load(f, other, sc::Code::Dxbc, out), sc::Miss::Outdated, out, "the head changed by a byte");
        expect(sc::load(f, input + " ", sc::Code::Dxbc, out), sc::Miss::Outdated, out, "the input longer");
        expect(sc::load(f, input.substr(0, input.size() - 1), sc::Code::Dxbc, out), sc::Miss::Outdated, out,
               "the input shorter");
        expect(sc::load(f, "", sc::Code::Dxbc, out), sc::Miss::Outdated, out, "an empty input");
    }
    // Asked for as the other kind of bytecode: never handed out.
    expect(sc::load(f, input, sc::Code::Spirv, out), sc::Miss::Damaged, out, "DXBC asked for as SPIR-V");

    // Damage: every byte of the file changed in turn, each time loaded back.
    const std::vector<uint8_t> good = read_all(f);
    check(good.size() == 32 + input.size() + code.size(), "the file's size: header, input, bytecode");
    {
        int handed = 0;
        for (size_t i = 0; i < good.size(); i++) {
            std::vector<uint8_t> b = good;
            b[i] ^= 0x01;
            write_all(f, b);
            if (sc::load(f, input, sc::Code::Dxbc, out) == sc::Miss::Used || !out.empty()) {
                std::printf("FAIL: byte %zu of the file changed, and it was still used\n", i);
                handed++;
            }
        }
        g_cases++;
        if (handed) g_bad++;
    }
    // Cut short at every length, and with bytes added.
    {
        int handed = 0;
        for (size_t n = 0; n < good.size(); n++) {
            write_all(f, std::vector<uint8_t>(good.begin(), good.begin() + std::ptrdiff_t(n)));
            if (sc::load(f, input, sc::Code::Dxbc, out) == sc::Miss::Used || !out.empty()) {
                std::printf("FAIL: the file cut to %zu bytes, and it was still used\n", n);
                handed++;
            }
        }
        g_cases++;
        if (handed) g_bad++;
        std::vector<uint8_t> b = good;
        b.push_back(0);
        write_all(f, b);
        expect(sc::load(f, input, sc::Code::Dxbc, out), sc::Miss::Damaged, out, "a byte added");
    }
    // Another format of these files (its number in the magic's last letter): compiled again.
    {
        std::vector<uint8_t> b = good;
        b[7] = '2';
        write_all(f, b);
        expect(sc::load(f, input, sc::Code::Dxbc, out), sc::Miss::Outdated, out, "another format");
    }
    // Bytecode the compiler could not have made (not a whole container), stored as it is: never handed out.
    {
        auto broken = code;
        broken[24] ^= 1;  // (its size field)
        check(sc::store(f, input, broken.data(), broken.size()), "store a broken container");
        expect(sc::load(f, input, sc::Code::Dxbc, out), sc::Miss::Damaged, out, "a broken container");
    }

    // A newer one replaces it; a copy half written by a game that ended is cleared at the next store.
    {
        write_all(fs::path(f.string() + ".4242.0.tmp"), {1, 2, 3});
        const auto newer = dxbc(120, 0x33);
        check(sc::store(f, input, newer.data(), newer.size()), "store over it");
        expect(sc::load(f, input, sc::Code::Dxbc, out), sc::Miss::Used, out, "the newer one");
        check(out == newer, "the newer bytes back");
        check(count_files(f.parent_path()) == 1, "the half-written copy cleared");
    }
    // SPIR-V the same way.
    {
        const fs::path g = sc::file(dir, "spv_vs_main");
        const auto s = spirv(40, 0x11);
        check(sc::store(g, input, s.data(), s.size()), "store SPIR-V");
        expect(sc::load(g, input, sc::Code::Spirv, out), sc::Miss::Used, out, "SPIR-V, the same input");
        check(out == s, "the same SPIR-V back");
        expect(sc::load(g, input, sc::Code::Dxbc, out), sc::Miss::Damaged, out, "SPIR-V asked for as DXBC");
    }
    // forget: gone, so the next start compiles it.
    sc::forget(f);
    expect(sc::load(f, input, sc::Code::Dxbc, out), sc::Miss::Missing, out, "forgotten");
    // Nothing stored without a path, bytes or within the size limit.
    check(!sc::store(fs::path(), input, code.data(), code.size()), "no path: not stored");
    check(!sc::store(f, input, code.data(), 0), "no bytes: not stored");

    // What stands for this program and for a system library: the same at each call, empty when unknown. A copy of this
    // program (another file, another time) is the same build: it says the same.
    const std::string me = sc::program_identity();
    check(!me.empty() && me == sc::program_identity(), "this program's build");
    {
        const fs::path self = program_path();
        const fs::path copy = root / ("copy_" + self.filename().string());
        fs::copy_file(self, copy, fs::copy_options::overwrite_existing, ec);
        check(!ec, "copy this program");
        const std::string said = run_for_identity(copy);
        if (said != me) std::printf("  this program: %s\n  its copy:     %s\n", me.c_str(), said.c_str());
        check(said == me, "a copy of this program is the same build");
    }
    check(sc::library_identity("no_such_library_here.dll").empty(), "a library not loaded");
#ifdef _WIN32
    const std::string k = sc::library_identity("kernel32.dll");
    check(!k.empty() && k.find(" version ") != std::string::npos, "a loaded system library and its version");
#else
    check(sc::library_identity("libc.so.6").empty(), "no library names off Windows");
#endif

    fs::remove_all(root, ec);
    std::printf("shader_cache_check: %s (%d checks)\n", g_bad ? "FAILED" : "all passed", g_cases);
    return g_bad ? 1 : 0;
}
