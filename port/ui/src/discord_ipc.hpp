// Written for Demon's Souls (docs/137).
// Discord's local connection, the one its desktop app offers programs on the same PC for Rich Presence: a named pipe
// on Windows (\\.\pipe\discord-ipc-0 to 9), a Unix socket elsewhere (discord-ipc-0 to 9 in the runtime folder, also
// where the Flatpak and Snap builds of Discord put theirs). Each message is a frame: an opcode and a length (both
// 32-bit little-endian) and that many bytes of JSON. No library: this file and discord_ipc.cpp are the whole client,
// and tests/discord_check.cpp checks them against a stand-in for Discord.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ui::discord {

// The frames' opcodes.
enum Op : uint32_t { kHandshake = 0, kFrame = 1, kClose = 2, kPing = 3, kPong = 4 };

// What the profile shows. Empty strings are left out of the message; no fields at all clears the status.
struct Activity {
    std::string details;      // the first line: what the player is doing ("Boletarian Palace")
    std::string state;        // the second line ("Online")
    int64_t start = 0;        // Unix seconds the session started (Discord counts up from it); 0 = no time
    std::string large_image;  // an image key uploaded to the application, or an https URL; empty = none
    std::string large_text;   // the image's hover text
    std::vector<std::pair<std::string, std::string>> buttons;  // up to 2: label (1-32 characters), https URL
};

// A JSON string's contents for s: quotes, backslashes and control characters escaped; UTF-8 passes through.
std::string json_escape(std::string_view s);
// One frame: opcode and length, then the JSON.
std::string encode_frame(uint32_t op, std::string_view json);
// Takes the first whole frame off the front of buf (bytes as read), into op and json. False while buf holds less than
// a frame; true with op = kClose and an empty json for a length past kMaxFrame (a broken stream: close it).
bool decode_frame(std::string& buf, uint32_t& op, std::string& json);
constexpr uint32_t kMaxFrame = 64 * 1024;

// The handshake's JSON: protocol version 1 and the application's id.
std::string handshake_json(std::string_view client_id);
// SET_ACTIVITY for this process: with a == nullptr, or an empty one, the status is cleared.
std::string activity_json(const Activity* a, int64_t pid, uint64_t nonce);

// The places Discord's connection can be, in the order to try them.
std::vector<std::string> ipc_paths();

// One connection to Discord. Not thread-safe: one thread owns it.
class Connection {
public:
    Connection() = default;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ~Connection() { close(); }

    bool open(const std::string& path);    // one pipe or socket path
    bool open_any();                       // the first of ipc_paths() that answers
    bool is_open() const { return mOpen; }
    const std::string& path() const { return mPath; }
    bool send(uint32_t op, std::string_view json);
    // 1: a frame in op and json; 0: no whole frame yet (never waits); -1: the connection is gone (closed here).
    int poll(uint32_t& op, std::string& json);
    void close();

private:
    bool mOpen = false;
    std::string mPath;
    std::string mBuf;  // bytes read and not yet a whole frame
#ifdef _WIN32
    void* mPipe = nullptr;
#else
    int mSock = -1;
#endif
};

}  // namespace ui::discord
