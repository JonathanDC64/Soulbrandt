// Written for Demon's Souls (docs/137). The Discord status: see discord_presence.hpp.
#include "discord_presence.hpp"

#include "des_ui.h"
#include "discord_ipc.hpp"
#include "settings.hpp"
#include "version.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ui::discord {
namespace {

// The application on Discord's developer portal whose name the profile shows ("Playing <name>"). Not a secret: every
// program that shows a status sends its id in the clear. DES_DISCORD_APP replaces it (tests, another application).
constexpr const char* kAppId = "1568951566017233079";
constexpr const char* kProjectUrl = "https://github.com/JonathanDC64/Soulbrandt";

// Discord takes 5 status changes per 20 s; a new area waits for the next free one.
constexpr auto kMinGap = std::chrono::seconds(4);
// Discord not running: look again this often. After it refused the application (a wrong id), much less often.
constexpr auto kRetry = std::chrono::seconds(15);
constexpr auto kRetryRefused = std::chrono::minutes(5);
constexpr auto kReadyWait = std::chrono::seconds(10);

std::atomic<int> sMap{-1};          // des_discord_map: the map whose script archive opened last
std::atomic<bool> sOn{false};       // the row (poll)
std::atomic<bool> sOnline{false};   // DES_ONLINE (poll)
std::atomic<bool> sStarted{false};
std::mutex sWakeLock;
std::condition_variable sWake;
bool sWoken = false;
std::string sAppId;
int64_t sSessionStart = 0;

void wake() {
    {
        std::lock_guard<std::mutex> lock(sWakeLock);
        sWoken = true;
    }
    sWake.notify_one();
}

std::string value_of(const char* key) {
    return settings::pinned(key) ? settings::pinned_value(key) : settings::get(key);
}

bool on_value(const std::string& v) { return !v.empty() && v != "0"; }

int64_t process_id() {
#ifdef _WIN32
    return static_cast<int64_t>(GetCurrentProcessId());
#else
    return static_cast<int64_t>(getpid());
#endif
}

Activity current() {
    Activity a;
    const int map = sMap.load(std::memory_order_relaxed);
    const char* area = area_name(map);
    a.details = area ? area : (map < 0 ? "At the title screen" : "Playing");
    a.state = sOnline.load(std::memory_order_relaxed) ? "Online" : "Offline";
    a.start = sSessionStart;
    if (const char* img = std::getenv("DES_DISCORD_IMAGE"); img && *img) {
        a.large_image = img;
        a.large_text = "Demon's Souls";
    }
    std::string label = std::string(kProductName) + " on GitHub";
    if (label.size() > 32) label = "On GitHub";
    a.buttons.emplace_back(label, kProjectUrl);
    return a;
}

// What Discord said, for the log: its "message" field if it has one.
std::string message_of(const std::string& json) {
    const size_t k = json.find("\"message\":\"");
    if (k == std::string::npos) return json.substr(0, 200);
    const size_t a = k + 11, b = json.find('"', a);
    return json.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

void run() {
    using clock = std::chrono::steady_clock;
    Connection c;
    const int64_t pid = process_id();
    uint64_t nonce = 1;
    bool ready = false, toldOff = false, toldError = false;
    std::string sent;  // the status last sent, without its nonce
    clock::time_point lastSend{}, nextTry = clock::now(), handshakeAt{};
    const char* forced = std::getenv("DES_DISCORD_IPC");  // a test's stand-in for Discord
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(sWakeLock);
            sWake.wait_for(lock, std::chrono::seconds(1), [] { return sWoken; });
            sWoken = false;
        }
        const clock::time_point now = clock::now();
        if (!sOn.load(std::memory_order_relaxed)) {
            if (c.is_open()) {
                c.send(kFrame, activity_json(nullptr, pid, nonce++));
                c.close();
            }
            if (!toldOff) std::fprintf(stderr, "[discord] status off (Settings > Network > Discord status)\n");
            toldOff = true;
            ready = false;
            sent.clear();
            continue;
        }
        toldOff = false;
        if (!c.is_open()) {
            if (now < nextTry) continue;
            nextTry = now + kRetry;
            if (!(forced && *forced ? c.open(forced) : c.open_any())) continue;
            if (!c.send(kHandshake, handshake_json(sAppId))) continue;
            ready = false;
            sent.clear();
            handshakeAt = now;
        }
        uint32_t op = 0;
        std::string json;
        int r;
        while ((r = c.poll(op, json)) == 1) {
            if (op == kPing) {
                c.send(kPong, json);
            } else if (op == kClose) {
                std::fprintf(stderr, "[discord] Discord closed the connection: %s\n", message_of(json).c_str());
                c.close();
                nextTry = now + kRetryRefused;
                ready = false;
                break;
            } else if (op == kFrame && json.find("\"evt\":\"READY\"") != std::string::npos) {
                if (!ready) std::fprintf(stderr, "[discord] connected (%s)\n", c.path().c_str());
                ready = true;
            } else if (op == kFrame && json.find("\"evt\":\"ERROR\"") != std::string::npos && !toldError) {
                toldError = true;  // (once a session: a status Discord will not take is sent again only when it changes)
                std::fprintf(stderr, "[discord] the status was refused: %s\n", message_of(json).c_str());
            }
        }
        if (r < 0 && !c.is_open() && ready) {
            std::fprintf(stderr, "[discord] the connection ended; trying again later\n");
            ready = false;
        }
        if (!c.is_open()) continue;
        if (!ready) {
            if (now - handshakeAt > kReadyWait) c.close();  // (no answer to the handshake: try again later)
            continue;
        }
        const Activity a = current();
        std::string body = activity_json(&a, pid, 0);
        if (body != sent && now - lastSend >= kMinGap) {
            if (c.send(kFrame, activity_json(&a, pid, nonce++))) {
                sent = std::move(body);
                lastSend = now;
            }
        }
    }
}

}  // namespace

const char* area_name(int map) {
    switch (map) {
    case 1: return "The Nexus";
    case 2: return "Boletarian Palace";
    case 3: return "Shrine of Storms";
    case 4: return "Tower of Latria";
    case 5: return "Valley of Defilement";
    case 6: return "Stonefang Tunnel";
    case 7: return "Northern Limit";
    case 8: return "Tutorial";
    default: return nullptr;
    }
}

void start() {
    if (sStarted.exchange(true)) return;
    if (settings::harness_run() && !settings::pinned("DES_DISCORD")) {
        std::fprintf(stderr, "[discord] off in a test run unless DES_DISCORD=1 is given\n");
        return;
    }
    const char* id = std::getenv("DES_DISCORD_APP");
    sAppId = id && *id ? id : kAppId;
    if (sAppId.empty()) {
        std::fprintf(stderr, "[discord] no Discord application id in this build: no status\n");
        return;
    }
    sSessionStart = static_cast<int64_t>(std::time(nullptr));
    sOn.store(on_value(value_of("DES_DISCORD")));
    sOnline.store(on_value(value_of("DES_ONLINE")));
    std::thread(run).detach();
}

void poll() {
    if (!sStarted.load(std::memory_order_relaxed)) return;
    static unsigned frames = 0;
    if (frames++ % 60) return;
    const bool on = on_value(value_of("DES_DISCORD")), online = on_value(value_of("DES_ONLINE"));
    if (on != sOn.load(std::memory_order_relaxed) || online != sOnline.load(std::memory_order_relaxed)) {
        sOn.store(on);
        sOnline.store(online);
        wake();
    }
}

}  // namespace ui::discord

extern "C" void des_discord_map(int map) {
    if (ui::discord::sMap.exchange(map) != map) ui::discord::wake();
}
