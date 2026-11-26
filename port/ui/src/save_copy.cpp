// Written for Demon's Souls. See save_copy.hpp.
#include "save_copy.hpp"

#include "path_utf8.hpp"
#include "sha256.hpp"

#include <system_error>
#include <utility>
#include <vector>

namespace ui::saves {
namespace {

namespace fs = std::filesystem;

// The parts of a gamedata folder that are the player's: the users' folders (saves, co-op's saves) and the trophies.
constexpr const char* kParts[] = {"dev_hdd0/home", "trophies"};

// Every regular file under gamedata's parts, as paths relative to gamedata. False, with why, if one cannot be listed.
bool list_files(const fs::path& gamedata, std::vector<fs::path>& out, std::string& why) {
    for (const char* part : kParts) {
        const fs::path dir = gamedata / fs::path(part);
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        fs::recursive_directory_iterator it(dir, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code ec2;
            if (it->is_regular_file(ec2)) out.push_back(it->path().lexically_relative(gamedata));
        }
        if (ec) {
            why = "cannot list " + path_utf8(dir) + ": " + ec.message();
            return false;
        }
    }
    return true;
}

}  // namespace

CopyResult copy_saves(const fs::path& from, const fs::path& to) {
    CopyResult r;
    const fs::path source = from / "gamedata", target = to / "gamedata", staging = to / kCopyFolder;
    if (std::error_code same; from.empty() || to.empty() || fs::equivalent(from, to, same)) {
        r.why = "no other folder to copy from";
        return r;
    }
    std::vector<fs::path> files;
    if (!list_files(source, files, r.why)) return r;
    if (files.empty()) {
        r.why = "there are no saves in " + path_utf8(source);
        return r;
    }
    // 1. Nothing in this install is replaced.
    std::error_code ec;
    for (const fs::path& rel : files) {
        if (fs::exists(target / rel, ec) || ec) {
            r.why = path_utf8(target / rel) + " is there already";
            return r;
        }
    }
    // 2. The copies, beside gamedata, each checked against its original.
    fs::remove_all(staging, ec);  // (an earlier try's copies: this folder holds only those)
    if (fs::exists(staging, ec)) {
        r.why = "cannot empty " + path_utf8(staging);
        return r;
    }
    const auto drop_copies = [&] {
        std::error_code e;
        fs::remove_all(staging, e);
    };
    for (const fs::path& rel : files) {
        const fs::path src = source / rel, copy = staging / rel;
        fs::create_directories(copy.parent_path(), ec);
        if (ec || !fs::copy_file(src, copy, fs::copy_options::none, ec) || ec) {
            r.why = "cannot copy " + path_utf8(src) + (ec ? ": " + ec.message() : std::string());
            drop_copies();
            return r;
        }
        const uintmax_t size = fs::file_size(src, ec);
        const uintmax_t copied = ec ? 0 : fs::file_size(copy, ec);
        const std::string sum = sha256_file(src);
        if (ec || size != copied || sum.empty() || sum != sha256_file(copy)) {
            r.why = "the copy of " + path_utf8(src) + " is not the same as the file";
            drop_copies();
            return r;
        }
        r.bytes += size;
    }
    // 3. Into place. A copy that cannot be moved puts the ones moved before it back.
    std::vector<fs::path> moved;
    for (const fs::path& rel : files) {
        const fs::path dest = target / rel;
        fs::create_directories(dest.parent_path(), ec);
        if (!ec && fs::exists(dest, ec)) ec = std::make_error_code(std::errc::file_exists);  // (made meanwhile)
        if (!ec) fs::rename(staging / rel, dest, ec);
        if (ec) {
            r.why = "cannot put the copy of " + path_utf8(rel) + " in " + path_utf8(target) + ": " + ec.message();
            for (const fs::path& back : moved) {
                std::error_code e;
                fs::rename(target / back, staging / back, e);
            }
            drop_copies();
            return r;
        }
        moved.push_back(rel);
    }
    drop_copies();  // (only its emptied folders are left in it)
    r.ok = true;
    r.files = files.size();
    return r;
}

}  // namespace ui::saves
