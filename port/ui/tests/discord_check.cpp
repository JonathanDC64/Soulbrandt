// discord_ipc.cpp on its own (docs/137): the frames and the JSON, then a whole exchange with a stand-in for Discord on
// a pipe (Windows) or a Unix socket (elsewhere) of this test's own, never Discord's: the handshake, the READY answer,
// a status, a ping, and Discord closing the connection. Prints each check; exits 1 if one fails.
//   cmake --build <build> --target discord_check, then discord_check
#include "discord_ipc.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

using namespace ui::discord;

namespace {

int sFailed = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) sFailed++;
}

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// The stand-in's side: blocking reads and writes of whole frames.
struct Server {
#ifdef _WIN32
    HANDLE h = INVALID_HANDLE_VALUE;
    std::string name;
    bool listen(const std::string& n) {
        name = n;
        std::wstring w(n.begin(), n.end());
        h = CreateNamedPipeW(w.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536,
                             65536, 0, nullptr);
        return h != INVALID_HANDLE_VALUE;
    }
    bool accept() { return ConnectNamedPipe(h, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED; }
    bool read_all(char* p, size_t n) {
        while (n) {
            DWORD got = 0;
            if (!ReadFile(h, p, static_cast<DWORD>(n), &got, nullptr) || !got) return false;
            p += got;
            n -= got;
        }
        return true;
    }
    bool write_all(const std::string& s) {
        DWORD put = 0;
        return WriteFile(h, s.data(), static_cast<DWORD>(s.size()), &put, nullptr) && put == s.size();
    }
    void close() {
        if (h != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(h);
            DisconnectNamedPipe(h);
            CloseHandle(h);
        }
        h = INVALID_HANDLE_VALUE;
    }
#else
    int ls = -1, cs = -1;
    std::string name;
    bool listen(const std::string& n) {
        name = n;
        ::unlink(n.c_str());
        ls = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un a{};
        a.sun_family = AF_UNIX;
        std::snprintf(a.sun_path, sizeof a.sun_path, "%s", n.c_str());
        return ls >= 0 && ::bind(ls, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && ::listen(ls, 1) == 0;
    }
    bool accept() { return (cs = ::accept(ls, nullptr, nullptr)) >= 0; }
    bool read_all(char* p, size_t n) {
        while (n) {
            const ssize_t got = ::recv(cs, p, n, 0);
            if (got <= 0) return false;
            p += got;
            n -= static_cast<size_t>(got);
        }
        return true;
    }
    bool write_all(const std::string& s) { return ::send(cs, s.data(), s.size(), 0) == static_cast<ssize_t>(s.size()); }
    void close() {
        if (cs >= 0) ::close(cs);
        if (ls >= 0) ::close(ls);
        ::unlink(name.c_str());
        cs = ls = -1;
    }
#endif
    bool read_frame(uint32_t& op, std::string& json) {
        char hdr[8];
        if (!read_all(hdr, 8)) return false;
        std::string b(hdr, 8);
        uint32_t len = 0;
        for (int i = 0; i < 4; i++) len |= static_cast<uint32_t>(static_cast<unsigned char>(b[4 + i])) << (8 * i);
        if (len > kMaxFrame) return false;
        std::string body(len, '\0');
        if (len && !read_all(body.data(), len)) return false;
        b += body;
        return decode_frame(b, op, json);
    }
};

// The client's side: poll until a frame or the deadline.
int wait_frame(Connection& c, uint32_t& op, std::string& json, int ms = 3000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    for (;;) {
        const int r = c.poll(op, json);
        if (r != 0 || std::chrono::steady_clock::now() > end) return r;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

}  // namespace

int main() {
    // ---- frames and JSON ----
    {
        const std::string f = encode_frame(kFrame, "{}");
        check(f.size() == 10 && f[0] == 1 && f[1] == 0 && f[4] == 2 && f[5] == 0 && f.substr(8) == "{}",
              "a frame: opcode and length little-endian, then the JSON");
        std::string buf = f.substr(0, 9);
        uint32_t op = 99;
        std::string json;
        check(!decode_frame(buf, op, json) && buf.size() == 9, "half a frame waits for the rest");
        buf += f.substr(9) + encode_frame(kPing, "[1]");
        check(decode_frame(buf, op, json) && op == kFrame && json == "{}", "a whole frame comes off the front");
        check(decode_frame(buf, op, json) && op == kPing && json == "[1]" && buf.empty(), "and the next one after it");
        std::string big = encode_frame(kFrame, "");
        big[6] = 0x10;  // a length of 1 MB
        check(decode_frame(big, op, json) && op == kClose && big.empty(), "a length past the limit closes");
    }
    check(json_escape("a\"b\\c\nd\x01") == "a\\\"b\\\\c\\nd\\u0001", "quotes, backslashes, control characters");
    check(json_escape("\xC3\x9Cn\xC3\xAF \xE2\x9C\x93") == "\xC3\x9Cn\xC3\xAF \xE2\x9C\x93", "UTF-8 passes through");
    check(handshake_json("123") == "{\"v\":1,\"client_id\":\"123\"}", "the handshake");
    {
        Activity a;
        a.details = "Boletarian Palace";
        a.state = "Online";
        a.start = 1700000000;
        a.buttons.emplace_back("On GitHub", "https://example.org/x");
        const std::string j = activity_json(&a, 42, 7);
        check(j == "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":42,\"activity\":{\"details\":\"Boletarian Palace\","
                   "\"state\":\"Online\",\"timestamps\":{\"start\":1700000000},\"buttons\":[{\"label\":\"On GitHub\","
                   "\"url\":\"https://example.org/x\"}]}},\"nonce\":\"7\"}",
              "a status: details, state, start time, a button");
        a.large_image = "logo";
        a.large_text = "Demon's Souls";
        check(has(activity_json(&a, 42, 8), "\"assets\":{\"large_image\":\"logo\",\"large_text\":\"Demon's Souls\"}"),
              "an image and its text");
        check(activity_json(nullptr, 42, 9) == "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":42},\"nonce\":\"9\"}",
              "no status: cleared");
    }
    check(!ipc_paths().empty() && has(ipc_paths()[0], "discord-ipc-0"), "Discord's places start at discord-ipc-0");

    // ---- an exchange with a stand-in ----
#ifdef _WIN32
    const std::string name = "\\\\.\\pipe\\des-discord-check-" + std::to_string(GetCurrentProcessId());
#else
    const char* tmp = std::getenv("TMPDIR");
    const std::string name = std::string(tmp && *tmp ? tmp : "/tmp") + "/des-discord-check-" + std::to_string(getpid());
#endif
    Server s;
    check(s.listen(name), "the stand-in listens");
    std::atomic<int> served{0};
    std::string gotHandshake, gotStatus, gotPong;
    std::thread server([&] {
        uint32_t op = 0;
        if (!s.accept()) return;
        if (s.read_frame(op, gotHandshake) && op == kHandshake) served = 1;
        s.write_all(encode_frame(kFrame, "{\"cmd\":\"DISPATCH\",\"evt\":\"READY\",\"data\":{\"v\":1}}"));
        if (s.read_frame(op, gotStatus) && op == kFrame) served = 2;
        s.write_all(encode_frame(kPing, "{\"n\":5}"));
        if (s.read_frame(op, gotPong) && op == kPong) served = 3;
        s.write_all(encode_frame(kClose, "{\"code\":4000,\"message\":\"Invalid Client ID\"}"));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        s.close();
    });
    Connection c;
    bool opened = false;
    for (int i = 0; i < 100 && !(opened = c.open(name)); i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(opened, "the client connects");
    check(c.send(kHandshake, handshake_json("123")), "the handshake goes out");
    uint32_t op = 0;
    std::string json;
    check(wait_frame(c, op, json) == 1 && op == kFrame && has(json, "\"evt\":\"READY\""), "READY comes back");
    Activity a;
    a.details = "The Nexus";
    check(c.send(kFrame, activity_json(&a, 1, 1)), "a status goes out");
    check(wait_frame(c, op, json) == 1 && op == kPing, "a ping comes in");
    check(c.send(kPong, json), "the pong goes back with the ping's JSON");
    check(wait_frame(c, op, json) == 1 && op == kClose && has(json, "Invalid Client ID"), "Discord's close, with why");
    server.join();
    check(served == 3, "the stand-in saw the handshake, the status and the pong");
    check(has(gotHandshake, "\"client_id\":\"123\""), "the handshake held the application id");
    check(has(gotStatus, "\"details\":\"The Nexus\""), "the status held the area");
    check(gotPong == "{\"n\":5}", "the pong held the ping's JSON");
    check(wait_frame(c, op, json, 2000) == -1 && !c.is_open(), "a closed connection reads as gone");
    std::printf("%s\n", sFailed ? "discord_check: FAILED" : "discord_check: all passed");
    return sFailed ? 1 : 0;
}
