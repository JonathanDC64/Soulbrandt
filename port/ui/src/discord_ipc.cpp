// Written for Demon's Souls (docs/137). Discord's local connection: see discord_ipc.hpp.
#include "discord_ipc.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace ui::discord {

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char hex[8];
                std::snprintf(hex, sizeof hex, "\\u%04x", c);
                out += hex;
            } else {
                out += ch;
            }
        }
    }
    return out;
}

namespace {

void put_le32(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; i++) out += static_cast<char>((v >> (8 * i)) & 0xFF);
}

uint32_t get_le32(const std::string& s, size_t at) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(static_cast<unsigned char>(s[at + i])) << (8 * i);
    return v;
}

// "key":"value", with a comma first when the object has fields already.
void field(std::string& out, bool& first, const char* key, std::string_view value) {
    if (value.empty()) return;
    if (!first) out += ',';
    first = false;
    out += '"';
    out += key;
    out += "\":\"";
    out += json_escape(value);
    out += '"';
}

}  // namespace

std::string encode_frame(uint32_t op, std::string_view json) {
    std::string out;
    out.reserve(8 + json.size());
    put_le32(out, op);
    put_le32(out, static_cast<uint32_t>(json.size()));
    out.append(json.data(), json.size());
    return out;
}

bool decode_frame(std::string& buf, uint32_t& op, std::string& json) {
    if (buf.size() < 8) return false;
    const uint32_t len = get_le32(buf, 4);
    if (len > kMaxFrame) {
        op = kClose;
        json.clear();
        buf.clear();
        return true;
    }
    if (buf.size() < 8u + len) return false;
    op = get_le32(buf, 0);
    json.assign(buf, 8, len);
    buf.erase(0, 8u + len);
    return true;
}

std::string handshake_json(std::string_view client_id) {
    return "{\"v\":1,\"client_id\":\"" + json_escape(client_id) + "\"}";
}

std::string activity_json(const Activity* a, int64_t pid, uint64_t nonce) {
    std::string out = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid);
    std::string act;
    if (a) {
        bool first = true;
        field(act, first, "details", a->details);
        field(act, first, "state", a->state);
        if (a->start > 0) {
            if (!first) act += ',';
            first = false;
            act += "\"timestamps\":{\"start\":" + std::to_string(a->start) + "}";
        }
        if (!a->large_image.empty() || !a->large_text.empty()) {
            if (!first) act += ',';
            first = false;
            act += "\"assets\":{";
            bool f2 = true;
            field(act, f2, "large_image", a->large_image);
            field(act, f2, "large_text", a->large_text);
            act += '}';
        }
        if (!a->buttons.empty()) {
            if (!first) act += ',';
            first = false;
            act += "\"buttons\":[";
            for (size_t i = 0; i < a->buttons.size() && i < 2; i++) {
                if (i) act += ',';
                bool f2 = true;
                act += '{';
                field(act, f2, "label", a->buttons[i].first);
                field(act, f2, "url", a->buttons[i].second);
                act += '}';
            }
            act += ']';
        }
    }
    if (!act.empty()) out += ",\"activity\":{" + act + "}";
    out += "},\"nonce\":\"" + std::to_string(nonce) + "\"}";
    return out;
}

std::vector<std::string> ipc_paths() {
    std::vector<std::string> out;
#ifdef _WIN32
    for (int i = 0; i < 10; i++) out.push_back("\\\\.\\pipe\\discord-ipc-" + std::to_string(i));
#else
    std::vector<std::string> bases;
    for (const char* var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        const char* v = std::getenv(var);
        if (v && *v) bases.emplace_back(v);
    }
    bases.emplace_back("/tmp");
    // Discord's own build, then its Flatpak and Snap builds, which keep theirs in a folder of their own.
    static const char* const kSubdirs[] = {"", "app/com.discordapp.Discord/", "snap.discord/"};
    for (int i = 0; i < 10; i++) {
        for (const std::string& b : bases) {
            for (const char* sub : kSubdirs) {
                std::string p = b;
                if (!p.empty() && p.back() != '/') p += '/';
                p += sub;
                p += "discord-ipc-" + std::to_string(i);
                bool seen = false;
                for (const std::string& q : out) seen = seen || q == p;
                if (!seen) out.push_back(p);
            }
        }
    }
#endif
    return out;
}

bool Connection::open(const std::string& path) {
    close();
#ifdef _WIN32
    std::wstring w(path.begin(), path.end());  // (the pipe names are ASCII)
    HANDLE h = CreateFileW(w.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    mPipe = h;
#else
    sockaddr_un addr{};
    if (path.size() >= sizeof addr.sun_path) return false;
    const int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return false;
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    if (::connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0) {
        ::close(s);
        return false;
    }
    mSock = s;
#endif
    mOpen = true;
    mPath = path;
    mBuf.clear();
    return true;
}

bool Connection::open_any() {
    for (const std::string& p : ipc_paths())
        if (open(p)) return true;
    return false;
}

bool Connection::send(uint32_t op, std::string_view json) {
    if (!mOpen) return false;
    const std::string f = encode_frame(op, json);
    size_t done = 0;
    while (done < f.size()) {
#ifdef _WIN32
        DWORD n = 0;
        if (!WriteFile(static_cast<HANDLE>(mPipe), f.data() + done, static_cast<DWORD>(f.size() - done), &n, nullptr) ||
            n == 0) {
            close();
            return false;
        }
#else
#ifdef MSG_NOSIGNAL
        const ssize_t n = ::send(mSock, f.data() + done, f.size() - done, MSG_NOSIGNAL);
#else
        const ssize_t n = ::send(mSock, f.data() + done, f.size() - done, 0);
#endif
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            close();
            return false;
        }
#endif
        done += static_cast<size_t>(n);
    }
    return true;
}

int Connection::poll(uint32_t& op, std::string& json) {
    if (!mOpen) return -1;
    if (decode_frame(mBuf, op, json)) return 1;
    char tmp[4096];
    for (;;) {
#ifdef _WIN32
        DWORD avail = 0;
        if (!PeekNamedPipe(static_cast<HANDLE>(mPipe), nullptr, 0, nullptr, &avail, nullptr)) {
            close();
            return -1;
        }
        if (avail == 0) break;
        DWORD n = 0;
        if (!ReadFile(static_cast<HANDLE>(mPipe), tmp, avail < sizeof tmp ? avail : DWORD(sizeof tmp), &n, nullptr) ||
            n == 0) {
            close();
            return -1;
        }
#else
        const ssize_t n = ::recv(mSock, tmp, sizeof tmp, MSG_DONTWAIT);
        if (n == 0) {
            close();
            return -1;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            close();
            return -1;
        }
#endif
        mBuf.append(tmp, static_cast<size_t>(n));
        if (mBuf.size() > 4 * kMaxFrame) break;  // (decode below; a frame too long closes the stream)
    }
    return decode_frame(mBuf, op, json) ? 1 : 0;
}

void Connection::close() {
#ifdef _WIN32
    if (mPipe) CloseHandle(static_cast<HANDLE>(mPipe));
    mPipe = nullptr;
#else
    if (mSock >= 0) ::close(mSock);
    mSock = -1;
#endif
    mOpen = false;
    mBuf.clear();
}

}  // namespace ui::discord
