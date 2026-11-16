#include "settings_values.hpp"

#include "settings.hpp"
#include "settings_hooks.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iterator>

namespace ui::settings {

std::string in_use(std::string_view key) {
    return pinned(key) ? pinned_value(key) : get(key);
}

std::string trim_spaces(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

bool cheat_session() {
    const char* s = std::getenv("DES_DEV_SESSION");
    return s && std::strcmp(s, "cheats") == 0;
}

bool developer_session_online() {
    const char* s = std::getenv("DES_DEV_SESSION");
    return s && std::strcmp(s, "online") == 0;
}

namespace {
template <size_t N>
bool listed(const char* const (&values)[N], const std::string& v) {
    return std::find(std::begin(values), std::end(values), v) != std::end(values);
}
}  // namespace

bool fps_is_custom() {
    return !listed(kFpsValues, in_use("DES_FPS"));
}

int fps_custom() {
    const std::string v = fps_is_custom() ? in_use("DES_FPS") : get("fps_custom");
    return std::clamp(std::atoi(v.c_str()), kFpsMin, kFpsMax);
}

void set_fps_custom(int fps) {
    if (pinned("DES_FPS")) return;
    set("fps_custom", std::to_string(fps));
    set("DES_FPS", std::to_string(fps));
}

bool glow_is_custom() {
    return !listed(kGlowColours, in_use("DES_SEAMLESS_GLOW_COLOR"));
}

int glow_hue() {
    return std::clamp(std::atoi(get("glow_hue_custom").c_str()) / 10 * 10, 0, 350);
}

void set_glow_hue(int hue) {
    if (pinned("DES_SEAMLESS_GLOW_COLOR")) return;
    set("glow_hue_custom", std::to_string(hue));
    set("DES_SEAMLESS_GLOW_COLOR", hue_hex(hue));
}

std::string hue_hex(int h) {
    const float x = float(h % 60) / 60.0f;
    float r = 0, g = 0, b = 0;
    switch (h / 60 % 6) {
    case 0: r = 1; g = x; break;
    case 1: r = 1 - x; g = 1; break;
    case 2: g = 1; b = x; break;
    case 3: g = 1 - x; b = 1; break;
    case 4: r = x; b = 1; break;
    default: r = 1; b = 1 - x; break;
    }
    char s[8];
    std::snprintf(s, sizeof s, "%02X%02X%02X", int(r * 255 + 0.5f), int(g * 255 + 0.5f), int(b * 255 + 0.5f));
    return s;
}

namespace {
double mouse_sens_default() {
    const Def* d = find("DES_MOUSE_SENS");
    return d ? std::atof(d->def) : 0.09;
}
}  // namespace

int mouse_sens_percent() {
    const double sens = std::atof(in_use("DES_MOUSE_SENS").c_str());
    if (!(sens > 0.0) || !(mouse_sens_default() > 0.0)) return 100;  // (des_mouse_look uses the default then)
    return std::clamp(int(std::lround(sens / mouse_sens_default() * 100.0)), kMouseSensMin, kMouseSensMax);
}

void set_mouse_sens_percent(int percent) {
    if (pinned("DES_MOUSE_SENS")) return;
    set("DES_MOUSE_SENS", std::format("{:g}", mouse_sens_default() * percent / 100.0));
}

int hud_size_percent() {
    const std::string v = in_use("DES_HUD_SIZE");
    const int pct = v.empty() ? 100 : std::atoi(v.c_str());
    return std::clamp(pct > 0 ? pct : 100, kHudSizeMin, kHudSizeMax);
}

int stick_zone(std::string_view key) {
    const std::string v = in_use(key);
    return v.empty() ? catalog::hooks::stick_default_zone() : std::clamp(std::atoi(v.c_str()), 0, kStickZoneMax);
}

void set_stick_zone(std::string_view key, int percent) {
    if (pinned(key)) return;
    set(key, percent == catalog::hooks::stick_default_zone() ? std::string() : std::to_string(percent));
}

namespace {
std::string sNameNote;
}  // namespace

void set_net_name(const std::string& typed) {
    const std::string name = trim_spaces(typed);
    if (net_name_ok(name)) {
        sNameNote.clear();
        set("DES_NET_NAME", name);
        return;
    }
    const std::string old = get("DES_NET_NAME");
    const std::string kept = net_name_ok(old) ? old : suggest_net_name();
    sNameNote = std::string(name.empty() ? "Online play needs a name" : "Names are 3 to 16 letters, digits, - or _") +
                (kept == old ? ", so " + kept + " was kept." : ", so it is " + kept + " for now.");
    set("DES_NET_NAME", kept);
}

const std::string& net_name_note() {
    return sNameNote;
}

int net_p2p_port() {
    const int port = std::atoi(in_use("DES_NET_P2P_PORT").c_str());
    return port < kNetP2pPortMin || port > kNetP2pPortMax ? kNetP2pPortDefault : port;
}

void choose_custom_and_say() {
    if (!choose_custom_here()) return;
    catalog::hooks::toast("Custom server chosen. Restart the game to play on " + trim_spaces(in_use("DES_NET_CUSTOM")) +
                              ".",
        8);
}

bool custom_held() {
    return in_use("DES_NET_SERVER") == kNetCustomKey && net_address_ok(trim_spaces(in_use("DES_NET_CUSTOM"))) &&
           !custom_chosen_here();
}

void set_custom_server(const std::string& typed) {
    if (pinned("DES_NET_CUSTOM")) return;
    const std::string value = trim_spaces(typed);
    set("DES_NET_CUSTOM", value);
    // (as Custom frame rate picks Frame rate's Custom: an address typed here picks Custom)
    if (!value.empty() && !pinned("DES_NET_SERVER")) set("DES_NET_SERVER", kNetCustomKey);
    choose_custom_and_say();
}

namespace {
// "controller2" -> "Controller 2", "playstation1" -> "PlayStation 1": a pad that can't be named.
std::string pad_number_label(const std::string& id) {
    for (const char* kind : {"controller", "playstation"}) {
        const size_t n = std::strlen(kind);
        if (id.size() == n + 1 && id.compare(0, n, kind) == 0)
            return std::string(kind[0] == 'c' ? "Controller " : "PlayStation ") + id.back();
    }
    return id;
}
}  // namespace

const std::vector<PadChoice>& pad_choices(const std::string& current) {
    static std::vector<PadChoice> sChoices;
    static std::string sFor = "\n";
    static std::chrono::steady_clock::time_point sAt{};
    const auto now = std::chrono::steady_clock::now();
    if (current == sFor && now - sAt < std::chrono::milliseconds(500)) return sChoices;
    sFor = current;
    sAt = now;
    sChoices = {{"auto", "Automatic"}};
    for (const catalog::hooks::Pad& pad : catalog::hooks::connected_pads()) {
        const std::string base = !pad.name.empty() ? pad.name : pad_number_label(pad.id);
        std::string label = base;
        for (int n = 2; std::any_of(sChoices.begin(), sChoices.end(), [&](const PadChoice& c) { return c.label == label; });)
            label = base + " " + std::to_string(n++);
        sChoices.push_back({pad.id, label});
    }
    if (!current.empty() && current != "auto" &&
        std::none_of(sChoices.begin(), sChoices.end(), [&](const PadChoice& c) { return c.value == current; }))
        sChoices.push_back({current, pad_number_label(current) + " (not connected)"});
    return sChoices;
}

}  // namespace ui::settings
