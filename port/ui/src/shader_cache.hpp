// Written for Demon's Souls. The menus' own shaders kept compiled between starts (render_gpu.cpp): compiling all 17
// at every start took ~0.4 s before the start screen could draw, the smoke's alone ~0.25 s.
//
// A file holds the compiler's output for one shader and a copy of everything that output was made from: the exact
// text the compiler got, the target, profile and flags, the compiler (its file, or its version and options), the
// layout, this program's build and version. It is used only when that copy is today's input byte for byte, its sizes
// add up and its bytes hash as they did when written. Anything else (no file, an update, another compiler, a changed
// option, a damaged file) means the shader is compiled again, as at every start before, and the file rewritten. The
// game's shaders and their cache (ps3recomp's compile service) are not touched: these files are in a folder of their
// own (<shader cache>/ui), which nothing else reads.
//
// DES_UI_SHADER_CACHE=0 compiles them at every start, as before.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ui::shader_cache {

enum class Code { Dxbc, Spirv };

// Why a file was not used: it was (Used), there is none, it was made from another input (an update, another compiler,
// another option), or it is damaged (cut short, changed, not that kind of bytecode).
enum class Miss { Used, Missing, Outdated, Damaged };

// False with DES_UI_SHADER_CACHE=0.
bool enabled();

// <dir>/ui/<name>.bin: empty when dir is (no cache), or when the cache is not enabled.
std::filesystem::path file(const std::string& dir, const std::string& name);

// The bytecode stored in file for exactly this input, in out (emptied unless Used).
Miss load(const std::filesystem::path& file, const std::string& input, Code code, std::vector<uint8_t>& out);

// Stores it: the whole file or none, written beside it and then put in its place. False if it could not be.
bool store(const std::filesystem::path& file, const std::string& input, const void* data, size_t size);

// Removes a file whose bytecode the device would not take.
void forget(const std::filesystem::path& file);

// Whether data is a whole program of that kind: DXBC's container (its magic, and its size field the size), or SPIR-V's
// magic and whole words.
bool whole(Code code, const uint8_t* data, size_t size);

// This program's build, which holds this code, the compile service's and glslang: on Windows the linker's stamp and
// debug id in its header, on Linux its build id (else its file's size and time). A copy of the program keeps it, a new
// build changes it. Empty if it cannot be read: then no cache.
std::string program_identity();

// A loaded library's file (Windows: d3dcompiler_47.dll): its path, size, time and version. Empty if it is not loaded
// or cannot be read, and on other systems: then no cache for what it compiles.
std::string library_identity(const char* name);

}  // namespace ui::shader_cache
