// Settings' rows (settings_catalog.hpp). The help is RML; the window adds "(KEY)" and the notes about pinned and
// restart-only settings itself.
#include "settings_catalog.hpp"

#include "looks.hpp"
#include "settings.hpp"
#include "settings_hooks.hpp"
#include "settings_values.hpp"
#include "version.hpp"
#if DES_DEV
#include "../../des_dev_marker.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string_view>

namespace ui::catalog {
namespace {

using hooks::escape;
namespace s = ui::settings;

// A choice list from a table of values kept elsewhere (settings_values.hpp's kFpsValues): the labels, in its order.
struct Label {
    const char* label;
    bool original = false;
};
template <size_t N>
std::vector<Choice> with_values(const char* const (&values)[N], std::initializer_list<Label> labels) {
    if (labels.size() != N) {
        std::fprintf(stderr, "settings_catalog: %zu labels for %zu values\n", labels.size(), N);
        std::abort();
    }
    std::vector<Choice> out;
    size_t i = 0;
    for (const Label& l : labels) out.push_back({values[i++], l.label, l.original});
    return out;
}

RowDesc toggle(std::string label, std::string key, Text help, bool inverted = false) {
    return {.label = std::move(label), .key = std::move(key), .kind = ToggleRow{inverted}, .help = std::move(help)};
}

RowDesc choice(std::string label, std::string key, std::vector<Choice> choices, Text help) {
    return {.label = std::move(label), .key = std::move(key), .kind = ChoiceRow{.choices = std::move(choices)},
        .help = std::move(help)};
}

RowDesc info(std::string label, Text value, Text help) {
    return {.label = std::move(label), .kind = InfoRow{std::move(value)}, .help = std::move(help)};
}

RowDesc action(std::string label, ActionRow::Opens opens, Text help) {
    return {.label = std::move(label), .kind = ActionRow{opens}, .help = std::move(help)};
}

// Shown only where `when` holds.
RowDesc only(std::function<bool()> when, RowDesc row) {
    row.when = std::move(when);
    return row;
}

// A setting of the program's own (Interface's, Check for updates), not a run option: its help has no "(KEY)".
RowDesc own_setting(RowDesc row) {
    row.flags |= kHideKey;
    return row;
}

// A setting that changes the picture at once: Preview sets the menu aside so it shows on the game.
RowDesc preview(RowDesc row) {
    row.flags |= kPreview;
    return row;
}

// ---- Network ----

std::vector<Choice> server_choices() {
    std::vector<Choice> out;
    for (const s::NetServer& n : s::kNetServers) out.push_back({n.key, n.name});
    return out;
}

std::string server_help() {
    std::string help = "The game server to connect to. <br/>";
    for (const s::NetServer& n : s::kNetServers) {
        const bool custom = n.key == std::string_view(s::kNetCustomKey);
        help += "<br/><b>" + escape(n.name) + "</b>" +
                (*n.address ? " (" + escape(n.address) + ")" : custom ? "" : " (not available yet)") + ": " +
                escape(n.note);
    }
    return help;
}

// ---- About ----

// The software this program is made of (mirrors port/ui/NOTICE.md).
struct Credit {
    const char* name;
    const char* use;
    const char* licence;
    const char* url;  // empty: no page to open
};
constexpr Credit kCredits[] = {
    {"ps3recomp", "The recompiler and runtime the port is built on, by Ned Heller (sp00nznet) and its contributors.", "MIT",
     "https://github.com/sp00nznet/ps3recomp"},
    {"RmlUi", "The menus.", "MIT", "https://github.com/mikke89/RmlUi"},
    {"FreeType", "Text in the menus. Portions of this software are copyright © 2025 The FreeType Project "
                  "(www.freetype.org). All rights reserved.",
     "FreeType Licence (FTL)", "https://freetype.org"},
    {"Dusklight", "The start screen, menu bar and settings windows are adapted from its interface code.", "CC0",
     "https://github.com/TwilitRealm/dusklight"},
    {"Cinzel and Alegreya Sans", "The fonts of the menus.", "SIL Open Font Licence 1.1", "https://fonts.google.com"},
    {"Old Standard TT", "The DeS UI look's text.", "SIL Open Font Licence 1.1",
     "https://fonts.google.com/specimen/Old+Standard+TT"},
    {"Material Symbols", "The icons of the menus.", "Apache-2.0", "https://fonts.google.com/icons"},
    {"Mbed TLS", "The secure connection to the online server.", "Apache-2.0",
     "https://www.trustedfirmware.org/projects/mbed-tls/"},
    {"SMAA", "The SMAA anti-aliasing setting. By Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro "
             "and Diego Gutierrez.",
     "MIT", "https://www.iryoku.com/smaa/"},
    {"SDL", "The controllers, and on Linux the window, keyboard, mouse and sound.", "zlib", "https://www.libsdl.org"},
    {"libusb", "The Switch 2 controllers over USB (Windows).", "LGPL 2.1 or later", "https://libusb.info"},
    {"FFmpeg", "The game's movies.", "LGPL 2.1 or later", "https://ffmpeg.org"},
    {"vkd3d", "The D3D12 renderer on Vulkan (Linux).", "LGPL 2.1 or later", "https://gitlab.winehq.org/wine/vkd3d"},
    {"glslang, SPIRV-Tools and SPIRV-Headers", "Compiling shaders for the Vulkan renderer.",
     "BSD-3-Clause, Apache-2.0 and MIT", "https://github.com/KhronosGroup/glslang"},
    {"volk and Vulkan-Headers", "Loading and calling Vulkan.", "MIT and Apache-2.0", "https://github.com/zeux/volk"},
    {"RPCS3", "A behavioural reference for the PS3 system libraries. No RPCS3 code is copied.", "", ""},
};

// Its licence as its value (a reference without one: none of its code is in this program).
RowDesc credit_row(const Credit& c) {
    RowDesc row = info(c.name, c.licence[0] ? c.licence : "Reference", c.use);
    if (c.licence[0]) row.footnote = "Licence: " + escape(c.licence);
    if (c.url[0])
        row.buttons = [url = c.url] {
            return std::vector<Button>{{.text = "Open the website", .press = [url] { hooks::open_url(url); }}};
        };
    return row;
}

std::string dump_help() {
    const std::string sha = kDumpSha256;
    std::string grouped;  // (in fours of 16, so the help pane can wrap it)
    for (size_t i = 0; i < sha.size(); i += 16) grouped += (i ? " " : "") + sha.substr(i, 16);
    return "The SHA-256 of the game file this build was made from (EBOOT.BIN, decrypted).<br/><br/>" +
           (sha.empty() ? std::string("Not known: the build did not find work/game/EBOOT.ELF.") : grouped);
}

std::vector<TabDesc> make_tabs() {
    using Opens = ActionRow::Opens;
    std::vector<TabDesc> tabs;

    tabs.push_back({"Display", {
        {"Window", {
#ifdef _WIN32
            choice("Display mode", "DES_FULLSCREEN",
                {{"0", "Window"}, {"1", "Borderless fullscreen"}, {"2", "Exclusive fullscreen"}},
                "Borderless fills the display the window is on. Exclusive gives the game the display to itself, "
                "which can make switching to other programs slower. Alt+Enter also switches to and from a window."),
#else
            choice("Display mode", "DES_FULLSCREEN", {{"0", "Window"}, {"1", "Borderless fullscreen"}},
                "Borderless fills the display the window is on. Alt+Enter also switches between the two."),
#endif
            toggle("Start maximized", "DES_MAXIMIZED",
                "The window opens maximized, and comes back maximized from Fullscreen."),
#if DES_RSX_VULKAN  // (with a runtime that has the Vulkan renderer: port/CMakeLists.txt)
            choice("Renderer", "RSX_BACKEND", {{"d3d12", "D3D12"}, {"vulkan", "Vulkan"}},
                "Vulkan or D3D12. On GeForce GTX 10 series and older cards, Vulkan is the default. Restart to apply."),
#endif
            preview(choice("Aspect ratio", "DES_ASPECT",
                {{"", "16:9", true}, {"16:10", "16:10 (Steam Deck)"}, {"2560:1080", "21:9 (2560 × 1080)"},
                    {"3440:1440", "21:9 (3440 × 1440)"}, {"3840:1600", "21:9 (3840 × 1600)"}, {"32:9", "32:9"},
                    {"auto", "Match the display"}},
                "The shape of the picture. Wider shows more at the sides, 16:10 more above and below. Raise Render "
                "resolution to keep a wide picture sharp.")),
        }},
        {"HDR", {
#ifdef _WIN32
            preview(choice("HDR", "RSX_HDR", {{"0", "Off", true}, {"1", "On"}},
                "Brighter lights and sky on an HDR display. Turn on HDR in Windows first. Turn off RTX HDR and Auto "
                "HDR for the game when this is on.")),
            preview(choice("HDR brightness", "RSX_HDR_WHITE",
                {{"0", "Windows' setting"}, {"100", "100 nits"}, {"150", "150 nits"}, {"200", "200 nits"},
                    {"250", "250 nits"}, {"300", "300 nits"}, {"400", "400 nits"}},
                "How bright white is with HDR on. Lower leaves more room for the highlights.")),
            preview(choice("HDR peak", "RSX_HDR_PEAK",
                {{"0", "The display's"}, {"400", "400 nits"}, {"600", "600 nits"}, {"1000", "1000 nits"},
                    {"1500", "1500 nits"}},
                "The brightest the highlights get with HDR on. The display's own figure suits most.")),
#else  // (the window's own HDR: a Wayland desktop's, or gamescope's on a Steam Deck; docs/104 section 8)
            preview(choice("HDR", "RSX_HDR", {{"0", "Off", true}, {"1", "On"}},
                "Brighter lights and sky on an HDR display. Turn on HDR for the display in your desktop's settings "
                "first, or play in Game Mode on a Steam Deck OLED. On a desktop, restart the game after turning "
                "this on.")),
            preview(choice("HDR brightness", "RSX_HDR_WHITE",
                {{"0", "Automatic"}, {"100", "100 nits"}, {"150", "150 nits"}, {"200", "200 nits"},
                    {"250", "250 nits"}, {"300", "300 nits"}, {"400", "400 nits"}},
                "How bright white is with HDR on. Automatic is the desktop's white, or 200 nits in Game Mode. "
                "Lower leaves more room for the highlights.")),
            preview(choice("HDR peak", "RSX_HDR_PEAK",
                {{"0", "Automatic"}, {"400", "400 nits"}, {"600", "600 nits"}, {"1000", "1000 nits"},
                    {"1500", "1500 nits"}},
                "The brightest the highlights get with HDR on. Automatic is 1000 nits; pick your display's own "
                "figure if you know it.")),
#endif
            preview(choice("HDR shadows", "RSX_HDR_GAMMA", {{"0", "Gamma 2.2 (darker)"}, {"1", "sRGB (lighter)"}},
                "How dark areas look with HDR on. Gamma 2.2 matches a TV.")),
        }},
        {"Frame rate", {
            {.label = "Frame rate", .key = "DES_FPS",
                .kind = ChoiceRow{
                    .choices = with_values(s::kFpsValues,
                        {{"30 fps", true}, {"30 fps, real frame time"}, {"60 fps"}, {"120 fps"}, {"144 fps"},
                            {"165 fps"}, {"240 fps"}, {"Unlocked"}}),
                    .custom = ChoiceRow::Custom{"Custom", [] { return std::to_string(s::fps_custom()); }}},
                .help = "The game's frame rate. 30 fps (original) is the PS3's. \"Real frame time\" is also 30 but "
                        "copes better when a frame runs late. Unlocked has no cap. Above 60 fps is less tested."},
            {.label = "Custom frame rate", .key = "DES_FPS",
                .kind = NumberRow{.min = s::kFpsMin, .max = s::kFpsMax, .suffix = " fps", .get = s::fps_custom,
                    .set = s::set_fps_custom, .modified = s::fps_is_custom},
                .help = "Your own frame rate, 30 to 500. Setting it picks Custom in Frame rate."},
            choice("Cutscene frame rate", "DES_CUTFPS", {{"0", "30 fps", true}, {"1", "Same as Frame rate"}},
                "By default cutscenes play at your frame rate, with their camera smoothed between its 30 fps steps, "
                "so they don't judder. 30 fps plays them as in the original."),
            choice("V-sync", "DES_VSYNC",
                {{"1", "On"}, {"even", "Even frames"}, {"vrr", "G-Sync / FreeSync"}, {"0", "Off"}},
                "On waits for the display's refresh. Even frames keeps every frame on screen equally long, for smooth "
                "motion, and moves the frame rate to one your display fits (60 becomes 72 at 144 Hz). G-Sync / "
                "FreeSync needs a display with it, turned on in your graphics driver too. Off can tear, but shows more "
                "fps than the refresh rate."),
            // (drawn by this UI over the game: perf_overlay.hpp)
            own_setting(choice("Frame rate counter", "fps_counter",
                {{"0", "Off"}, {"1", "Frame rate"}, {"2", "Frame rate and frame times"}},
                "Shows the game's frame rate at the top of the screen, to see how a mod or a setting runs. Frame times "
                "adds a graph of the last 120 frames, with a line at the frame time Frame rate allows (with Unlocked, "
                "a typical frame's); a frame that came late shows in amber. The 1% low is the frame rate of the "
                "slowest 1% of the frames of the last 10 seconds, so a load shows in it for a while.")),
            own_setting(choice("Counter position", "fps_counter_at",
                {{"", "Top right"}, {"centre", "Top centre"}, {"left", "Top left"}},
                "Where the frame rate counter sits. Top right is above the area name; Top left covers the HUD's bars "
                "unless you moved them. If it covers a part of the HUD, try Top centre.")),
        }},
    }});

    tabs.push_back({"Picture", {
        {"Sharpness", {
            preview(choice("Render resolution", "RSX_SCALE",
                {{"50", "50% (640 × 360)"}, {"60", "60% (768 × 432)"}, {"70", "70% (896 × 504)"},
                    {"80", "80% (1024 × 576)"}, {"90", "90% (1152 × 648)"}, {"100", "100% (1280 × 720)", true},
                    {"150", "150% (1920 × 1080)"}, {"200", "200% (2560 × 1440)"}, {"300", "300% (3840 × 2160)"}},
                "Above 100% draws the game sharper and costs more GPU time. Below 100% costs less, for handhelds and "
                "slower graphics cards, but the picture and the HUD are softer.")),
            preview(choice("Scaling filter", "RSX_OUTFILTER", {{"0", "Bilinear"}, {"1", "Sharp"}},
                "How the picture is fitted to the window. Sharp is cleaner, most of all with a high Render "
                "resolution.")),
            preview(choice("Anti-aliasing", "RSX_AA", {{"0", "Off", true}, {"1", "FXAA"}, {"2", "SMAA"}},
                "Smooths jagged edges and keeps the HUD sharp. FXAA softens textures a little; SMAA keeps them sharper "
                "and costs a little more.")),
        }},
        {"Textures", {
            preview(choice("Texture filtering", "RSX_TEXFILTER",
                {{"0", "Original"}, {"1", "Nearest"}, {"2", "Bilinear"}, {"3", "Trilinear"}},
                "How textures are smoothed. Original uses the game's choice. Nearest looks pixelated; Trilinear is the "
                "smoothest.")),
            preview(choice("Anisotropic filtering", "RSX_ANISO",
                {{"0", "Original (4x)"}, {"1", "Off"}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}, {"16", "16x"}},
                "Keeps floors and walls sharp into the distance. Original is the game's own, 4x on most surfaces.")),
            preview(choice("Mipmaps", "RSX_TEX_MIPS", {{"1", "On", true}, {"0", "Off"}},
                "Smaller copies of each texture for distant surfaces, as on the PS3. Off draws them at full size, "
                "so they shimmer.")),
        }},
        {"Effects", {
            preview(toggle("Depth of field", "DES_DOF", "The game's blur on distant scenery. Off keeps it sharp.")),
            preview(toggle("Motion blur", "DES_MOTION_BLUR", "The game's blur when the camera moves.")),
        }},
    }});

    const auto region_is = [](int r) { return [r] { return hooks::region() == r; }; };
    tabs.push_back({"Game", {
        {"Start", {
            choice("Boot logos", "DES_SKIP_INTRO", {{"0", "Show", true}, {"1", "Skip"}, {"2", "Last logo only"}},
                "The company logos at start. Skip goes straight to the title screen."),
            toggle("Skip opening movie", "DES_SKIP_INTRO_MOVIE",
                "Skips the movie after the logos. Other movies still play."),
            toggle("Continue automatically", "DES_AUTO_CONTINUE", "After Play, goes straight into your last save."),
        }},
        {"Language", {
            only(region_is(3), choice("Language", "DES_LANGUAGE",
                {{"", "PC's language"}, {"en", "English"}, {"zh-tw", "Chinese (Traditional)"}, {"ko", "Korean"}},
                "The game's text. PC's language uses your PC's when the disc has it, else English. Restart to "
                "apply.")),
            // (the US disc has English and Japanese)
            only(region_is(0), choice("Language", "DES_LANGUAGE",
                {{"", "PC's language"}, {"en", "English"}, {"ja", "Japanese"}},
                "The game's text. Trophy names stay English. PC's language uses your PC's when the disc has it, "
                "else English. Restart to apply.")),
            only(region_is(1), choice("Language", "DES_LANGUAGE",
                {{"", "PC's language"}, {"en", "English"}, {"fr", "Français"}, {"de", "Deutsch"},
                    {"es", "Español"}, {"it", "Italiano"}, {"ja", "Japanese"}, {"zh-tw", "Chinese (Traditional)"},
                    {"ko", "Korean"}},
                "The game's text and trophy names. Japanese, Chinese and Korean change the text only. PC's language "
                "uses your PC's when the disc has it, else English. Restart to apply.")),
        }, [] { return hooks::region() != 2; }},  // (the Japanese disc has only Japanese)
        {"Sound", {
            toggle("Sound", "AUDIO_MUTE", "The game's sound and music.", true),
        }},
        {"Items", {
            toggle("Use several souls at once", "DES_SOULS_AT_ONCE",
                "Use on a soul item, such as an Unknown Soldier's Soul, asks how many to use, like Drop does. Boss "
                "souls are not affected."),
            toggle("Stat respec in the Nexus", "DES_RESPEC",
                "Pick Respecialization in the Maiden in Black's menu, or press Cross facing the statue in front of "
                "where you wake in the Nexus (no prompt shows there). It can put your stats back to your class's and "
                "give back the souls your levels cost, to spend again. Spells must be remembered again."),
            toggle("Change appearance in the Nexus", "DES_APPEARANCE",
                "Press Cross facing the statue in front of where you wake in the Nexus (no prompt shows there) and "
                "pick Change Appearance for the character creation screen. Keeping the changes reloads the Nexus."),
            toggle("Change name and body in the Nexus", "DES_IDENTITY",
                "Needs Change appearance in the Nexus. The statue also offers Change Name and Change Body. A new "
                "body takes off worn armour made only for the other body (it stays in the inventory), then opens "
                "Edit Appearance."),
            toggle("Change tendency in the Nexus", "DES_TENDENCY",
                "Press Cross facing the statue in front of where you wake in the Nexus and pick Change Tendency to "
                "set a world's tendency or your character's, from Pure White to Pure Black. Some events only happen "
                "at a tendency, and what has already happened stays."),
            choice("Hold for the first spell or item", "DES_SLOT_HOLD",
                {{"0", "Off", true}, {"300", "0.3 seconds"}, {"500", "0.5 seconds"}, {"800", "0.8 seconds"},
                    {"1000", "1 second"}},
                "Hold Up for that long to go back to your first spell, or Down for your first item. The press "
                "still steps once first, as in the original game."),
            toggle("Escaped crystal lizards come back", "DES_LIZARD_RETURN",
                "A crystal lizard that gets away is back the next time its area loads, without using up a return. "
                "Only a kill uses one. Off, as in the original game: getting away uses a return as a kill does. "
                "Each boss killed in a lizard's world gives it one return.<br/><br/>It counts from the next area load "
                "after you turn it on: a lizard that got away before then has used up a return."),
        }},
        {"HUD", {
            action("Edit HUD", Opens::HudEditor, "Move, resize, hide or show each part of the HUD on the game screen."),
            {.label = "HUD size", .key = "DES_HUD_SIZE",
                .kind = NumberRow{.min = s::kHudSizeMin, .max = s::kHudSizeMax, .step = 5, .suffix = "%",
                    .get = s::hud_size_percent,
                    .set = [](int v) { s::set("DES_HUD_SIZE", std::to_string(v)); }},
                .help = "The size of the bars, area name, equipment and souls. Each grows or shrinks from its "
                        "corner of the screen."},
            choice("HUD on wide screens", "DES_HUD", {{"", "Corners"}, {"centre", "Centred 16:9"}, {"stretch", "Stretched"}},
                "Where the HUD goes when Aspect ratio is wider than 16:9. Corners puts each part in its corner of the "
                "screen. Centred keeps the HUD in a 16:9 area in the middle. Stretched spreads it across."),
            choice("Button icons", "DES_GLYPHS",
                {{"auto", "Automatic"}, {"playstation", "PlayStation"}, {"xbox", "Xbox"}, {"nintendo", "Nintendo"},
                    {"steamdeck", "Steam Deck"}, {"keyboard", "Keyboard and mouse"}},
                "The buttons in the game's prompts. Automatic shows the controller or keyboard you used last and "
                "changes when you switch. A Steam Deck's own controls show the Deck's buttons. Keyboard and mouse "
                "shows the keys you set in Controls. PlayStation is the game's own."),
            toggle("Health, magic, stamina", "DES_HUD_BARS", "The bars at the top left."),
            toggle("Area name", "DES_HUD_AREA", "The name of the area at the top right."),
            toggle("Equipment", "DES_HUD_EQUIP", "Your weapons, item and spell at the bottom left."),
            toggle("Souls", "DES_HUD_SOULS", "Your souls at the bottom right."),
        }},
    }});

    const auto stick_row = [](std::string label, std::string key, int stick, std::string help) {
        return RowDesc{.label = std::move(label), .key = key,
            .kind = NumberRow{.min = 0, .max = s::kStickZoneMax, .suffix = "%",
                .get = [key] { return s::stick_zone(key); },
                .set = [key](int v) { s::set_stick_zone(key, v); },
                .format = [key](int v) {
                    return s::in_use(key).empty() ? "Default (" + std::to_string(v) + "%)" : std::to_string(v) + "%";
                },
                .stick = stick},
            .help = std::move(help)};
    };
    tabs.push_back({"Controls", {
        {"This menu", {
            info("Open the menu", [] { return "F1 / " + hooks::menu_chord_label(); },
                [] {
                    return "F1, or " + escape(hooks::menu_chord_label()) +
                           " on a gamepad (change it in Interface). Esc is the game's own menu.";
                }),
            info("In the menus", "Arrows, Enter, Esc",
                "Keyboard: arrows, Enter, Esc; Page Up and Page Down change tabs. The mouse works too.<br/><br/>"
                "Gamepad: D-pad, Cross, Circle; L1 and R1 change tabs.<br/><br/>"
                "With Same buttons as the game on, these are the buttons you set in Change Buttons, and the keys "
                "for Cross and Circle (E and Space at first) confirm and go back too."),
            own_setting(toggle("Same buttons as the game", "menu_follow_buttons",
                "These menus use the buttons and keys you set in Change Buttons, as the game's own menus do. Off: "
                "Cross confirms and Circle goes back, whatever you set. Enter and Esc always work.")),
        }},
        {"The game", {
            action("Change Buttons", Opens::Controls, "Change which buttons and keys do what."),
            info("Gamepads", [] { return hooks::pad_support().value; }, [] { return hooks::pad_support().help; }),
            {.label = "Controller", .key = "PAD_CONTROLLER",
                .kind = ChoiceRow{
                    .live = [](const std::string& current) {
                        std::vector<Choice> out;
                        for (const s::PadChoice& c : s::pad_choices(current)) out.push_back({c.value, c.label});
                        return out;
                    },
                    .blank = "auto"},
                .help = "The gamepad the game and these menus read, when more than one is connected. Automatic: "
                        "the one you used last plays, and the button icons follow it."},
            toggle("Vibration", "PAD_RUMBLE", "Lets the game make your gamepad vibrate."),
            stick_row("Left stick deadzone", "PAD_DEADZONE_LEFT", 0,
                "How far you tilt the left stick before it counts, the same on every controller. Default is how the "
                "game has always played with this controller. Very low values can let a worn stick drift."),
            stick_row("Right stick deadzone", "PAD_DEADZONE_RIGHT", 1,
                "How far you tilt the right stick before it counts, the same on every controller. Default is how the "
                "game has always played with this controller. Very low values can let a worn stick drift."),
            choice("Left stick axial deadzone", "PAD_AXIAL_LEFT",
                {{"0", "Off", true}, {"3", "3%"}, {"5", "5%"}, {"8", "8%"}, {"12", "12%"}},
                "For a stick that wavers a little sideways when you push it straight, so your character trembles "
                "while running. A push that's nearly straight ahead, back, left or right counts as straight: a "
                "sideways waver smaller than this is ignored. Try 3% or 5%. Higher values also turn slight diagonals "
                "straight. Off is how the game has always played."),
            choice("Left stick smoothing", "PAD_SMOOTH_LEFT",
                {{"0", "Off", true}, {"1", "Light"}, {"2", "Medium"}, {"3", "Strong"}},
                "Smooths out a noisy stick's small jitter. Quick pushes stay quick. Slow, small movements follow the "
                "stick a little later: about 35 ms with Light, 55 ms with Medium and 105 ms with Strong. Off is how "
                "the game has always played."),
            toggle("Roll toward the stick", "DES_ROLLDIR",
                "Locked on, a roll goes where the stick points. Off, it goes only forward, back, left or right, as in "
                "the original."),
            choice("Forward attack", "DES_KICK", {{"0", "Forward + attack", true}, {"1", "Own key"}},
                "The forward attack is the kick, or with a weapon held in both hands that weapon's forward attack."
                "<br/><br/>Forward + attack: push the left stick forward as you attack, as in the original.<br/><br/>"
                "Own key: attacking never does it. "
                "The Kick (own key) row in Change Buttons, Keyboard and mouse, does it instead, and on a gamepad the "
                "Kick row in Change Buttons, Gamepad: the button you choose there kicks and does nothing else. A "
                "back button or paddle works too, when the pad reports it. With no key or button set, there's no "
                "forward attack. Choosing a Kick key or button there sets this to Own key.<br/><br/>Only your own "
                "moves change; other players see them as usual."),
            info("Keyboard and mouse", "WASD, mouse, Space",
                "As in the Souls games on PC:<br/>"
                "<b>W A S D</b> move &nbsp; <b>Left Alt</b> (held) walk &nbsp; <b>Mouse</b> camera (Mouse look, "
                "below)<br/>"
                "<b>Left button</b> attack (R1) &nbsp; <b>Shift + left button</b> strong attack (R2)<br/>"
                "<b>Right button</b> guard (L1) &nbsp; <b>Left Ctrl</b> or <b>Shift + right button</b> parry (L2)<br/>"
                "<b>Space</b> roll, backstep, dash (Circle) &nbsp; <b>C</b> left stick press (L3)<br/>"
                "<b>Q</b> or <b>middle button</b> lock on (R3)<br/>"
                "<b>E</b> action (Cross) &nbsp; <b>R</b> use item (Square) &nbsp; <b>F</b> two-hand (Triangle)<br/>"
                "<b>Arrows</b> D-pad &nbsp; <b>Wheel up / down</b> spell / item &nbsp; <b>Shift + wheel up / down</b> "
                "right / left hand<br/>"
                "<b>Esc</b> menu (Start) &nbsp; <b>G</b> or <b>Tab</b> Select<br/><br/>"
                "The mouse buttons and wheel need Mouse look on. Change Buttons changes any of these."),
        }},
        {"Camera", {
            toggle("Camera follows your movement", "DES_CAM_FOLLOW",
                "On, as in the original: running sideways swings the camera round behind you. Off: the camera keeps "
                "its direction until you turn it with the right stick or the mouse. Locked on, it follows the "
                "target either way."),
            toggle("Walls turn the camera", "DES_CAM_WALL",
                "On, as in the original: running along a wall or round a corner turns the camera away from it. "
                "Off: walls don't turn it. It still moves in close, so you never see through a wall."),
            toggle("Lock on with no target turns the camera", "DES_CAM_RESET",
                "On, as in the original: lock on (R3) with nothing to lock on to turns the camera behind you. Off: "
                "the camera stays where it is."),
            toggle("Free camera (offline)", "DES_FREECAM",
                "The developers' camera. Hold Cross and press L3: the camera flies with time stopped (hold Square to "
                "step time), then with time running, then stays where it is while you play, then is off. Left stick "
                "moves, right stick turns, R2 goes up. Online play and co-op are off while this is on. Restart to "
                "apply."),
        }},
        {"Mouse", {
            toggle("Mouse look", "DES_MOUSE_LOOK",
                "Turn the camera with the mouse, and use the mouse buttons and wheel in the game. Smoothest at 60 fps "
                "or more."),
            toggle("Mouse in game menus (experimental)", "DES_MENU_MOUSE",
                "Point and click in the game's own menus: left click picks, right click goes back, the wheel scrolls. "
                "A key or gamepad button hides the pointer. Experimental: some screens may not take the mouse."),
            {.label = "Mouse sensitivity", .key = "DES_MOUSE_SENS",
                .kind = NumberRow{.min = s::kMouseSensMin, .max = s::kMouseSensMax, .step = 5, .suffix = "%",
                    .get = s::mouse_sens_percent, .set = s::set_mouse_sens_percent},
                .help = "How far the camera turns when you move the mouse."},
            toggle("Invert vertical", "DES_MOUSE_INVERT_Y", "On: moving the mouse forward looks down."),
            toggle("Invert horizontal", "DES_MOUSE_INVERT_X", "On: moving the mouse right turns the camera left."),
            choice("Switch targets", "DES_MOUSE_FLICK",
                {{"0", "Off"}, {"8", "Short move"}, {"15", "Medium move"}, {"30", "Long move"}},
                "While locked on, moving the mouse sideways switches targets. This sets how far you have to move it."),
        }},
    }});

    // Online play (docs/46 A5; settings.cpp's apply_network turns the rows into the runtime's knobs at start).
    const auto warning = [](std::string text) { return Note{Note::Kind::Warning, std::move(text)}; };
    tabs.push_back({"Network", {
        {"Online play", {
            {.label = "Online", .key = "DES_ONLINE", .kind = ToggleRow{},
                .help = "Plays online on the server below: messages, bloodstains, ghosts, co-op and invasions. Off "
                        "plays offline.",
                .notes = [warning] {
                    std::vector<Note> out;
                    if (s::cheat_session())
                        out.push_back(warning("Off this session: a developer cheat is on."));
                    else if (s::net_mods_held())
                        out.push_back(warning("Off this session: gameplay mods are on (see Mods, beside Settings in the "
                                              "menu)."));
                    return out;
                },
                .locked = s::cheat_session,
                .lockedValue = "Off: cheats used"},
#if DES_DEV  // (Custom and its address: developer builds only for now, settings.hpp's kNetServers)
            {.label = "Server", .key = "DES_NET_SERVER",
                .kind = ChoiceRow{.choices = server_choices(),
                    .liveNote = ChoiceRow::LiveNote{warning("Custom's address came in a copied settings file, so you "
                                                            "stay offline. Press Custom above to play on it."),
                        s::custom_held},
                    // (pressing Custom, even when it is already picked, chooses its address on this PC)
                    .changed = [] {
                        if (s::get("DES_NET_SERVER") == s::kNetCustomKey) s::choose_custom_and_say();
                    }},
                .help = server_help()},
            {.label = "Custom server", .key = "DES_NET_CUSTOM", .kind = TextRow{253, s::set_custom_server},
                .help = "A server not in the list: an IP address or a host name. Typing one picks Custom.",
                .notes = [warning] {
                    std::vector<Note> out;
                    const std::string v = s::trim_spaces(s::in_use("DES_NET_CUSTOM"));
                    if (s::in_use("DES_NET_SERVER") == s::kNetCustomKey && !s::net_address_ok(v))
                        out.push_back(warning(std::string(v.empty() ? "No address yet" : "Not a valid address") +
                                              ", so you stay offline."));
                    else if (s::custom_held())
                        out.push_back(warning("This address came in a copied settings file, so you stay offline. To "
                                              "play on it, press Custom in Server again."));
                    return out;
                }},
#else
            {.label = "Server", .key = "DES_NET_SERVER", .kind = ChoiceRow{.choices = server_choices()},
                .help = server_help()},
#endif
            {.label = "Online name", .key = "DES_NET_NAME", .kind = TextRow{16, s::set_net_name},
                .help = "Your name online: 3 to 16 letters, digits, - or _. Pick one nobody else uses. A new name "
                        "makes you a new player to the server: your old messages stay, but their ratings no longer "
                        "reach you.",
                .notes = [warning] {
                    std::vector<Note> out;
                    const std::string v = s::in_use("DES_NET_NAME");
                    if (!s::net_name_note().empty() && !s::pinned("DES_NET_NAME"))
                        out.push_back({Note::Kind::Info, s::net_name_note()});
                    else if (v.empty())
                        out.push_back({Note::Kind::Info, "No name yet, so you stay offline."});
                    else if (!s::net_name_ok(v))
                        out.push_back(warning("Not valid, so you stay offline."));
                    return out;
                }},
            {.label = "Matchmaking password", .key = "DES_NET_PASSWORD",
                .kind = TextRow{32, [](const std::string& v) { s::set("DES_NET_PASSWORD", s::trim_spaces(v)); }},
                .help = "Summon signs and invasions only between players with the same password. Messages, "
                        "bloodstains and ghosts stay shared. Empty plays with everyone."},
            choice("World tendency", "DES_NET_TENDENCY", {{"server", "With the server's"}, {"own", "Yours only"}},
                "Online, the game mixes other players' world tendency into yours. Yours only changes it by what you "
                "do alone. Applies from the next load."),
            choice("Message ratings", "DES_NET_RATING_POLL", {{"", "Every 5 minutes"}, {"30", "Every 30 seconds"}},
                "How often the game checks whether someone rated your messages. The PS3 checked every 5 minutes."),
            {.label = "P2P port", .key = "DES_NET_P2P_PORT",
                .kind = NumberRow{.min = s::kNetP2pPortMin, .max = s::kNetP2pPortMax, .get = s::net_p2p_port,
                    .set = [](int v) { s::set("DES_NET_P2P_PORT", std::to_string(v)); }},
                .help = "The port for co-op and invasions. Change it only if your router needs another one."},
            choice("Your movement updates", "DES_NET_SYNC",
                {{"", "Original (every 6th frame)"}, {"15", "Smoother (at least 15 a second)"}},
                "How often your position goes to other players. Smoother makes you look less laggy on their screens."),
        }},
        // Seamless co-op's rows in two: the host's game (only the host's setting counts there, for everyone in it),
        // and a friend's own (each player's, in a host's world). coop_tabs() shows both on the co-op screen.
        {std::string(kCoopHeading) + kCoopHost, {
            {.label = "Players when you host", .key = "DES_SEAMLESS_PLAYERS",
                .kind = ChoiceRow{
                    .choices = {{"2", "2: you and a friend"}, {"3", "3: you and two friends"},
                        {"4", "4: you and three friends"}},
                    .changed = hooks::party_size_changed},
                .help = "The most players in a co-op game you host (Seamless co-op, in the menu), you included. One more "
                        "waits, and joins as soon as a place frees up. A change counts at once; players already here "
                        "stay.<br/><br/>The original game plays three at most in co-op."},
            choice("Enemy difficulty", "DES_SEAMLESS_DIFFICULTY",
                {{"", "Vanilla", true}, {"bosses", "Scaled like bosses"}, {"1.5", "x1.5"}, {"2", "x2"},
                    {"2.5", "x2.5"}, {"3", "x3"}, {"3.5", "x3.5"}, {"4", "x4"}},
                "How much health enemies have in a co-op game you host. Vanilla: only a boss gets more, at its fog "
                "gate, when blue phantoms are there. Scaled like bosses: every enemy gets more with each player, x1.5 "
                "with two players, x2 with three and x2.5 with four. x1.5 to x4: that much, however many play. "
                "Bosses get the same, or their own if it is more."
                "<br/><br/>The host's choice counts for everyone in its game. A change counts at once."),
            toggle("Scale enemy attack power", "DES_SEAMLESS_DIFFICULTY_ATTACK",
                "Enemies' hits on players do as much more as Enemy difficulty gives them health: at x2, twice the "
                "damage. Off with Vanilla.<br/><br/>The host's choice counts for everyone in its game. A change "
                "counts at once."),
            toggle("Scale enemy defenses", "DES_SEAMLESS_DIFFICULTY_DEFENSE",
                "Players' hits on enemies do as much less as Enemy difficulty gives them health: at x2, half the "
                "damage. Off with Vanilla.<br/><br/>The host's choice counts for everyone in its game. A change "
                "counts at once."),
            choice("When you die as host", "DES_SEAMLESS_HOST_DEATH",
                {{"", "You respawn alone"}, {"together", "Everyone goes back"}},
                "You respawn alone: you go back to the area's start by yourself, your friends play on, and dead "
                "enemies stay dead. In a boss fight you wait until the boss is dead. Only when no friend is left "
                "standing does everyone go back with you, and the enemies come back.<br/><br/>Everyone goes back: "
                "each of your deaths takes everyone back to the area's start, and the enemies come back. A change "
                "counts at once."),
        }},
        {std::string(kCoopHeading) + kCoopJoin, {
            toggle("Your glow", "DES_SEAMLESS_GLOW",
                "Whether the other players see your blue phantom glow while you are in a host's world. Off: they see "
                "you as you look in your own world. A change counts at once."),
            {.label = "Your glow colour", .key = "DES_SEAMLESS_GLOW_COLOR",
                .kind = ChoiceRow{
                    .choices = with_values(s::kGlowColours,
                        {{"Blue", true}, {"White"}, {"Gold"}, {"Orange"}, {"Red"}, {"Pink"}, {"Purple"}, {"Green"},
                            {"Teal"}}),
                    .custom = ChoiceRow::Custom{"Custom", [] { return s::hue_hex(s::glow_hue()); }}},
                .help = "The colour of your phantom glow while you are in a host's world. The other players see it, "
                        "and so do you. Each player picks its own. A change counts at once.",
                .swatch = s::glow_swatch},
            {.label = "Custom glow colour", .key = "DES_SEAMLESS_GLOW_COLOR",
                .kind = NumberRow{.min = 0, .max = 350, .step = 10, .get = s::glow_hue, .set = s::set_glow_hue,
                    .format = [](int v) { return "Hue " + std::to_string(v); }, .modified = s::glow_is_custom},
                .help = "Your own glow colour, by hue: 0 red, 60 yellow, 120 green, 180 cyan, 240 blue, 300 magenta. "
                        "Setting it picks Custom in Your glow colour.",
                .swatch = [](const std::string&) { return s::hue_hex(s::glow_hue()); }},
        }},
        {std::string(kCoopHeading) + kCoopScreen, {
            own_setting(toggle("Co-op notices", "coop_notices",
                "Short notes while you play: a player joined, left, died or is back, how your join "
                "went, and who waits to join your game. Off hides them, except a note that co-op didn't start. A "
                "change counts at once.")),
        }},
        {"In your world", {
            toggle("Messages", "DES_NET_MESSAGES", "Messages from other players. Off hides all of them, yours too."),
            toggle("Bloodstains", "DES_NET_BLOODSTAINS",
                "Other players' bloodstains. Your own bloodstain with your souls always shows."),
            toggle("Ghosts", "DES_NET_GHOSTS", "Other players' ghosts walking through your world."),
        }},
        {"Discord", {
            toggle("Discord status", "DES_DISCORD",
                "Shows on your Discord profile that you are playing, the area you are in and whether you are online, "
                "with a link to this project's page. Needs the Discord app open on this PC."),
        }},
        {"Players", {
            action("Players you met", Opens::Players,
                "The players you met online lately: who helped you, who you helped, and who invaded. Block one so you "
                "never meet again, or report one to the server's admins."),
        }},
        {"Your data", {
            action("Privacy note", Opens::Privacy,
                "What this project's online server keeps about you, and for how long. The game asks you to accept it "
                "before it first goes online with that server."),
            action("Delete my data", Opens::DeleteData,
                "Deletes your name and what this project's online server keeps about you, at once. Online play then "
                "turns off. Works while you are signed in to that server."),
        }},
        {"This session", {
            info("Network", [] { return s::cheat_session() ? std::string("Off: cheats used") : s::net_status(); },
                [] {
                    return s::cheat_session() ? std::string("A developer cheat is on, so this session is offline.")
                                              : "This session: " + escape(s::net_status()) + ".";
                }),
            info("About online play", "Read first",
                "Online play is new and less tested than offline.<br/><br/>"
                "You play with everyone on the same server. Players on other servers, on RPCS3 or on a PS3 are not in "
                "your world.<br/><br/>"
                "Co-op and invasions connect you directly to the other player, so they can see your IP address, as "
                "on a PS3."),
        }},
    }});

    std::vector<Choice> swords;
    for (const hooks::Sword& sword : hooks::swords()) swords.push_back({sword.name, sword.label});
    std::vector<Choice> looks;
    for (const looks::Look& look : looks::kLooks)
        if (looks::offered(look)) looks.push_back({std::string(look.id), std::string(look.label)});
    tabs.push_back({"Interface", {
        {"This menu", {
            own_setting({.label = "UI scale", .key = "ui_scale",
                .kind = ChoiceRow{
                    .choices = {{"0", "Auto"}, {"25", "25%"}, {"50", "50%"}, {"75", "75%"}, {"100", "100%"},
                                {"125", "125%"}, {"150", "150%"}},
                    .changed = hooks::apply_scale},
                .help = "The size of these menus. Auto fits them to the window."}),
            // (shown once this build offers more than one look)
            only([] { return looks::offered_count() > 1; },
                own_setting({.label = "Menu look", .key = "menu_look",
                    .kind = ChoiceRow{.choices = std::move(looks), .changed = hooks::apply_look},
                    .help = "How these menus look. Every look has the same menus and rows."})),
            // (shown in a look whose frame it sizes)
            only(hooks::menu_height_used, own_setting({.label = "Menu height", .key = "ui_menu_height",
                .kind = NumberRow{.min = 0, .max = 100, .step = 10,
                    .get = [] { return std::clamp(s::get_int("ui_menu_height"), 0, 100); },
                    .set = [](int v) { s::set("ui_menu_height", std::to_string(v)); },
                    .format = [](int v) {
                        return v == 0 ? std::string("Compact")
                             : v == 100 ? std::string("Whole window")
                                        : std::to_string(v) + "%";
                    }},
                .help = "How tall Settings is in a large window: from Compact, its height in a 720p window, to the "
                        "window's whole height. Taller shows more rows before they scroll."})),
            own_setting(choice("Gamepad menu buttons", "pad_menu_chord", {{"0", "L3 + R3"}, {"1", "Select + Start"}},
                "The gamepad buttons that open this menu, pressed together. Select + Start also opens the game's "
                "menu.")),
            own_setting(toggle("Menu hint", "menu_hint", "After Play, a note says how to open this menu.")),
            own_setting(toggle("Crystal lizard", "DES_UI_LIZARD",
                "A crystal lizard dances in the corner of these menus.")),
        }},
        {"Start screen", {
            own_setting(toggle("Start screen", "show_start_screen",
                "Shows Play, Settings and Quit when the game starts. Off goes straight to the game.")),
            // (the look that draws them: render_gpu.cpp, the menus' renderer on D3D12 and Vulkan, Linux's included)
            only(hooks::start_effects_used, own_setting(toggle("Start screen smoke", "start_smoke",
                "Smoke rises behind the start screen. Off saves a little graphics work, most on integrated "
                "graphics."))),
            only(hooks::start_effects_used, own_setting(toggle("Soul flame", "start_flame",
                "A pale flame rises off the logo on the start screen. Off saves a little graphics work."))),
        }},
        {"Cursor", {
            own_setting(toggle("Sword cursor", "DES_CURSOR3D",
                "The cursor in these menus is a sword from the game. Off uses the usual arrow.")),
            own_setting(choice("Cursor sword", "DES_CURSOR_SWORD", std::move(swords),
                "Which of the game's swords the cursor is.")),
            own_setting(choice("Cursor size", "DES_CURSOR_SIZE",
                {{"48", "Small"}, {"64", "Normal"}, {"96", "Large"}, {"128", "Very large"}, {"192", "Huge"}},
                "How long the sword is.")),
        }},
        {"Trophies", {
            own_setting(toggle("Trophy pop-ups", "trophy_popups",
                "Shows a note when you earn a trophy. Off only hides the note.")),
            own_setting(toggle("Trophy sound", "trophy_sound",
                "Plays a short tune from the game when you earn a trophy.")),
        }},
    }});

#if DES_DEV
    // Developer builds only (cmake -DDES_DEV=ON; docs/55, des_dev.cpp). The rows are live and never saved. des_dev_boot
    // sets the session at start (cheats, online or offline); des_dev.cpp makes it online for good when PS3_NET_ONLINE
    // or PSNR_SERVER is set later. The tab is built again each time it is shown.
    const auto offline = [] { return !s::developer_session_online(); };
    tabs.push_back({"Developer", {{"", {
        only(s::developer_session_online, info("Cheats", "Off: online session",
            "Cheats are off because this session is online. Turn Online off and restart to use them.")),
        only(offline, info("Developer build", "Cheats compiled in",
            "Cheats for testing (" DES_DEV_MARKER "). Any cheat keeps the session offline. They reset each start.")),
        only(offline, toggle("God mode", "DES_GOD", "Your HP, MP and stamina never drop. Instant deaths still kill.")),
        only(offline, choice("Attack power", "DES_ATK",
            {{"1", "Normal"}, {"2", "2x"}, {"5", "5x"}, {"10", "10x"}, {"100", "100x"}, {"0", "No damage"}},
            "Multiplies your damage.")),
        only(offline, choice("Movement speed", "DES_SPEED",
            {{"1", "Normal"}, {"1.5", "1.5x"}, {"2", "2x"}, {"3", "3x"}, {"0.5", "Half"}},
            "Multiplies how fast you move. High values can pass through thin walls.")),
        only(offline, info("Warp (scripts)", "DES_WARP=x,y,z",
            "For scripts: DES_WARP=x,y,z moves you there, and DES_WARP_CLEAR=m moves enemies within m metres away. "
            "Counts as a cheat.")),
        only(offline, info("Test switches (scripts)", "DES_KILLSELF, DES_KILLALL, DES_AREA",
            "For scripts. DES_KILLSELF=1 kills you. DES_KILLALL=all kills every enemy in the area, or a number kills "
            "those within that many metres. DES_AREA=2,1 loads m02_01. Each new value acts once. Cheats.<br/><br/>"
            "DES_FLAGWATCH=a,b logs those event flags when they change. Not a cheat.")),
    }}}});
#endif

    std::vector<RowDesc> made;
    for (const Credit& c : kCredits) made.push_back(credit_row(c));
    tabs.push_back({"About", {
        {"This program", {
#if DES_DEV
            info("Developer build", "Cheats compiled in",
                "This build has cheats (the Developer tab). Never share it as a release."),
#endif
            {.label = "Version", .kind = InfoRow{kVersion},
                .help = std::string(kProductName) + ": Demon's Souls on PC, a recompilation of the PS3 game.<br/><br/>"
                        "Version " + escape(kVersion) + ".",
                .buttons = [] {
                    return std::vector<Button>{{.text = "Open the project page",
                        .press = [] { hooks::open_url("https://github.com/JonathanDC64/Soulbrandt"); }}};
                }},
            own_setting(toggle("Check for updates", "update_check",
                "Looks for a newer version each time the game starts. It asks before it changes anything.")),
            {.label = "Updates", .kind = InfoRow{hooks::update_status, true},
                .help = [] {
                    if (hooks::update_from_launcher())
                        return escape(hooks::update_status()) +
                               "<br/><br/>RecompLauncher installed this copy, so the game leaves the updates to it. "
                               "It remembers that in setup/recomplauncher.txt in the game's folder. Delete that file, "
                               "and start the game without RecompLauncher, to have it look for updates itself.";
                    return escape(hooks::update_status()) +
                           "<br/><br/>An update keeps your saves and settings. The version before it is kept, so you "
                           "can go back to it.";
                },
                .buttons = [] {
                    std::vector<Button> out{{.text = "Check now", .press = hooks::update_check,
                        .disabled = [] { return !hooks::update_possible() || hooks::update_checking(); }}};
                    if (const std::string v = hooks::update_found_version(); !v.empty())
                        out.push_back({.text = "What's new in " + v, .sound = Sound::Open, .press = hooks::open_update_notes});
                    if (const std::string kept = hooks::update_kept_version(); !kept.empty())
                        out.push_back({.text = "Go back to " + kept, .sound = Sound::None,
                            .press = [kept] { hooks::ask_go_back(kept); }});
                    return out;
                }},
            info("ps3recomp", kPs3recompVersion,
                "The recompiler this build uses.<br/><br/>" + escape(kPs3recompVersion) + "."),
            info("Game dump",
                std::string(kDumpSha256).empty() ? std::string("unknown") : std::string(kDumpSha256).substr(0, 12),
                dump_help()),
            info("Licence", "MIT",
                "The port's code is MIT. You need your own copy of Demon's Souls.<br/><br/>Demon's Souls is a "
                "trademark of Sony Interactive Entertainment. This project is not affiliated with or endorsed by "
                "Sony Interactive Entertainment, FromSoftware or Atlus."),
            info("Settings file", [] { return s::path().empty() ? std::string("(not kept)") : std::string("settings.ini"); },
                [] {
                    return s::path().empty()
                               ? std::string("This run does not keep its settings (a test script started it).")
                               : "Your settings are saved in<br/>" + escape(s::path()) +
                                     (hooks::data_dir_known()
                                             ? std::string("<br/><br/>Logs, crash reports and issue reports are in the "
                                                           "same folder. Your saves are elsewhere.")
                                             : std::string());
                }),
        }},
        {"Made with", std::move(made)},
    }});
    return tabs;
}

}  // namespace

const std::vector<TabDesc>& tabs() {
    static const std::vector<TabDesc> sTabs = make_tabs();
    return sTabs;
}

const std::vector<TabDesc>& coop_tabs() {
    static const std::vector<TabDesc> sCoop = [] {
        TabDesc coop{"Co-op", {}};
        const std::string head = kCoopHeading;
        for (const TabDesc& tab : tabs())
            for (const SectionDesc& section : tab.sections)
                if (tab.title == "Network" && section.title.rfind(head, 0) == 0) {
                    SectionDesc s = section;
                    s.title = section.title.substr(head.size());   // "when you host" -> "When you host"
                    if (!s.title.empty()) s.title[0] = (char)std::toupper((unsigned char)s.title[0]);
                    coop.sections.push_back(std::move(s));
                }
        return std::vector<TabDesc>{std::move(coop)};
    }();
    return sCoop;
}

std::string choice_label(const Choice& c) {
    return c.original ? c.label + " (original)" : c.label;
}

std::string help_with_key(const RowDesc& row) {
    std::string help = row.help();
    if (!row.key.empty() && !(row.flags & kHideKey)) help += "<br/><br/>(" + row.key + ")";
    return help;
}

namespace {

std::string shown_value(const ChoiceRow& kind, const std::string& value) {
    const std::string v = value.empty() && !kind.blank.empty() ? kind.blank : value;
    for (const Choice& c : kind.live ? kind.live(value) : kind.choices)
        if (c.value == v) return c.label;
    return "Custom (" + value + ")";
}
std::string shown_value(const ToggleRow& kind, const std::string& value) {
    return (value == "1") != kind.inverted ? "On" : "Off";
}
std::string shown_value(const NumberRow& kind, const std::string& value) {
    if (!kind.get) return value;
    const int n = kind.get();
    return kind.format ? kind.format(n) : std::to_string(n) + kind.suffix;
}
std::string shown_value(const auto&, const std::string& value) {  // text, and the rest
    return value;
}

}  // namespace

std::optional<Shown> shown(std::string_view key) {
    const RowDesc* found = nullptr;
    for (const TabDesc& tab : tabs())
        for (const SectionDesc& section : tab.sections)
            for (const RowDesc& row : section.rows)
                if (row.key == key && (!found || (found->when && !found->when()))) found = &row;
    if (!found) return std::nullopt;
    const std::string value = settings::get(key);
    return Shown{found->label, std::visit([&](const auto& kind) { return shown_value(kind, value); }, found->kind)};
}

}  // namespace ui::catalog
