// The values Settings' rows show and store where they are not the setting's text as it stands: Mouse sensitivity in
// percent, a custom frame rate or glow colour, the online name's checks, the gamepads to choose from. Used by the rows
// in settings_catalog.cpp; no RmlUi.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ui::settings {

// The value in use: the environment's when the setting is pinned (set when the game started), else settings.ini's.
std::string in_use(std::string_view key);

std::string trim_spaces(const std::string& s);

// A developer build's session: des_dev_boot set a cheat at start and cleared the network knobs (docs/55 section 6),
// or made it online. Release builds never set DES_DEV_SESSION.
bool cheat_session();
bool developer_session_online();

// Frame rate's fixed choices (DES_FPS). Any other value (a number from 30 to 500, des_options.cpp's fps_parse) is its
// Custom choice, whose number Custom frame rate sets; fps_custom keeps that number while a fixed choice is on.
inline constexpr const char* kFpsValues[] = {"", "30", "60", "120", "144", "165", "240", "unlocked"};
inline constexpr int kFpsMin = 30;
inline constexpr int kFpsMax = 500;
bool fps_is_custom();
int fps_custom();             // DES_FPS while it is custom, else the last one set
void set_fps_custom(int fps);  // and picks Custom

// Your glow colour's named choices (DES_SEAMLESS_GLOW_COLOR, "RRGGBB"; "" the game's blue). Any other colour is its
// Custom choice, which Custom glow colour sets from a hue; glow_hue_custom keeps that hue.
inline constexpr const char* kGlowColours[] = {"", "F0F0FF", "FFC040", "FF7010", "FF2010", "FF60C0", "A040FF",
    "30FF50", "20E0D0"};
bool glow_is_custom();
int glow_hue();
void set_glow_hue(int hue);  // and picks Custom
std::string hue_hex(int hue);  // the hue (degrees) at full saturation and brightness, as "RRGGBB"

// Mouse sensitivity as a percentage of its default (DES_MOUSE_SENS, degrees per count; des_mouse_look in
// des_options.cpp).
inline constexpr int kMouseSensMin = 5;
inline constexpr int kMouseSensMax = 500;
int mouse_sens_percent();
void set_mouse_sens_percent(int percent);

// HUD size in percent (DES_HUD_SIZE; des_hud.cpp takes 25 to 200, the row offers 50 to 150).
inline constexpr int kHudSizeMin = 50;
inline constexpr int kHudSizeMax = 150;
int hud_size_percent();

// A stick's deadzone in percent (PAD_DEADZONE_LEFT, _RIGHT), 0 to 70. Its default sits at the number the deadzone
// comes to with the connected controller, so stepping from it starts where the stick already starts; it stores "" and
// leaves the runtime's own deadzone in place.
inline constexpr int kStickZoneMax = 70;
int stick_zone(std::string_view key);
void set_stick_zone(std::string_view key, int percent);

// Online name (DES_NET_NAME): never stores an empty or invalid name, since online play needs one and a player who
// cleared it would stay offline without knowing why. It keeps the last good name (with none, a suggested one), and
// net_name_note says so.
void set_net_name(const std::string& typed);
const std::string& net_name_note();

int net_p2p_port();  // DES_NET_P2P_PORT, the default when out of range

// Custom server (developer builds for now: settings.hpp's kNetServers). choose_custom_and_say: its address, chosen on
// this PC (settings::choose_custom_here), says so, since the row looks the same as before. custom_held: Custom is
// picked but its address came in a copied settings file, so the game is offline.
void choose_custom_and_say();
bool custom_held();
void set_custom_server(const std::string& typed);

// The Controller row's choices (PAD_CONTROLLER): Automatic, the pads connected now by what they are (a second pad of
// the same kind gets a number), and the saved choice while its pad isn't connected. Read again at most twice a second:
// the row's value is drawn every frame, and naming an XInput pad asks its driver.
struct PadChoice {
    std::string value, label;
};
const std::vector<PadChoice>& pad_choices(const std::string& current);

}  // namespace ui::settings
