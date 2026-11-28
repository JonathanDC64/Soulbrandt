// The wrapper UI's interface (ui/include/des_ui.h) for a build without the UI (-DDES_UI=OFF; the default off
// Windows until the UI runs there, docs/76): no start screen, menu, settings.ini, log file or problem reports, and
// mouse look has no mouse. Everything the game itself does is unchanged.
#include "des_ui.h"

#include <cstdio>
#include <initializer_list>

extern "C" void ps3_setenv(const char* name, const char* value);  // boot_main.cpp: through the runtime's getenv cache
extern "C" void ps3_net_lock_offline(void);  // ps3_net.h: the network stays off for the rest of the session

extern "C" {

void (*g_des_ui_sound)(int id) = nullptr;

void des_ui_early_init(void) {}
int des_ui_crash_reports_on(void) { return 0; }
void des_ui_report_guest_exit(const char*, const char*) {}

int des_ui_launch(int*, char***) {
    return -1;  // (no setup screens: the game, as before)
}

void des_ui_setup_only_init(void) {}

void des_ui_privacy_wait(void) {}  // (no settings.ini: nothing goes online through the Network rows)

void des_ui_net_off(const char* why) {
    // (no Network tab: the knobs and the runtime's lock only, as settings.cpp's net_off and des_ui.cpp's do)
    for (const char* k : {"PS3_NET_ONLINE", "PSNR_SERVER", "PS3_NET_HOSTS", "PS3_NET_SIGNIN"})
        ps3_setenv(k, "");  // (an empty value reads as unset)
    ps3_net_lock_offline();
    std::fprintf(stderr, "[ui] network: %s\n", why ? why : "Off");
}

void des_ui_mods_early(void) {}  // (no settings: mods only through a test's PS3_VFS_MODS)

unsigned long long des_ui_mods_largest_load(char* which, unsigned n) {
    if (which && n) which[0] = 0;
    return 0;  // (no Mods window: mods only through a test's PS3_VFS_MODS, unmeasured)
}

void des_mouse_take(int* dx, int* dy) {
    *dx = 0;
    *dy = 0;
}

void des_mouse_refresh(void) {}

int des_ui_start_screen_up(void) {
    return 0;
}

void des_discord_map(int) {}  // (no UI: no Discord status)

int des_ui_keyboard(const unsigned short*, const unsigned short*, unsigned, void (*)(int, const unsigned short*, unsigned)) {
    return 0;  // (no UI: no keyboard, and the statue offers no Change Name)
}

int des_ui_keyboard_ready(void) {
    return 0;
}

}  // extern "C"
