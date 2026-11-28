// What the UI's library takes from the game's side (des_boot: boot_main.cpp, ppu_hle.cpp, ppu_loader.cpp, the lift,
// des_hud.cpp, des_menu_mouse.cpp, des_prework.cpp, des_seamless.cpp, des_seamless_roster.cpp), for ui_render_compare, which runs no game:
// inert, as the runtime's host_stubs.c is for what its modules take from the PPU loader.
#include "../../des_seamless.h"
#include "../../des_seamless_roster.h"
#include "../../des_seamless_notice.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

extern "C" {

uint8_t* vm_base = nullptr;  // guest memory: none
// The lift's functions: none. (extern first: a const's linkage is the file's own otherwise.)
extern const uint64_t function_table[2];
extern const uint64_t function_table_count;
const uint64_t function_table[2] = {0, 0};
const uint64_t function_table_count = 0;
uint32_t g_last_hle_nid = 0;
uint8_t g_des_region = 0;  // the US disc (des_region.cpp)
const char* g_last_hle_name = nullptr;

void ps3_diag_report(FILE* out, unsigned sample_sec) {
    (void)out;
    (void)sample_sec;
}
unsigned ppu_boot_frames_presented(void) { return 0; }
uint32_t vm_rsx_local_size(void) { return 256u << 20; }  // (ps3recomp vm.h: a PS3's local memory)
uint32_t vm_sysmem_size(void) { return 256u << 20; }     // (ps3recomp vm.h: a PS3's memory window)
long long ps3_ms_since_frame(void) { return -1; }
uint64_t des_frame_count(void) { return 0; }  // (des_options.cpp: the frame rate counter's frames)
uint64_t des_frame_at_us(uint64_t n) {
    (void)n;
    return 0;
}
int des_frame_target_mhz(void) { return 0; }

int des_hud_edit_view(float view[4]) {
    (void)view;
    return 0;
}
int des_hud_edit_place(int part, float* dx, float* dy, float size, float box[4]) {
    (void)part;
    (void)dx;
    (void)dy;
    (void)size;
    (void)box;
    return 0;
}

int des_menu_mouse_open(void) { return 0; }
void des_menu_mouse_view(float view[4]) { (void)view; }
int des_menu_mouse_hit(float x, float y, float box[4]) {
    (void)x;
    (void)y;
    (void)box;
    return 0;
}
void des_menu_mouse_click(float x, float y) {
    (void)x;
    (void)y;
}
void des_menu_mouse_release(void) {}
void des_menu_mouse_back(void) {}
void des_menu_mouse_middle(float x, float y) {
    (void)x;
    (void)y;
}
void des_menu_mouse_drag(float x, float y) {
    (void)x;
    (void)y;
}
void des_menu_mouse_wheel(int notches, float x, float y) {
    (void)notches;
    (void)x;
    (void)y;
}

int des_prework_status(float* fraction, char* text, int cap) {
    (void)fraction;
    (void)text;
    (void)cap;
    return 0;
}
void des_prework_skip(void) {}

}  // extern "C"

// Seamless co-op (des_seamless.cpp, des_seamless_save.cpp): off, never started; or as a page set it
// (set_test_status: ui_render_compare's Co-op pages, a made-up host or guest).
namespace des::seamless {
namespace {
Status gTestStatus;
}
void set_test_status(const Status& s) { gTestStatus = s; }
Mode mode() { return gTestStatus.mode; }
bool start(const std::string&, std::string* why) {
    if (why) *why = "not in ui_render_compare";
    return false;
}
void held(const std::string&) {}
Status status() { return gTestStatus; }
void party_size_changed() {}
std::string set_public_address(const std::string&, std::string*) { return "not in ui_render_compare"; }
std::string new_code() { return "not in ui_render_compare"; }
std::string check_code(const std::string&, std::string*) { return "not in ui_render_compare"; }
void leave() {}
void leave_now() {}
bool left() { return true; }
std::vector<Character> characters_to_bring() { return {}; }
std::string bring_character(int) { return "not in ui_render_compare"; }

// Who is in the session (des_seamless_roster.cpp): none; or as a page set it (set_test_session: made-up players).
namespace {
std::vector<SessionPlayer> gTestPlayers;
int gTestSeconds = 0;
}  // namespace
void set_test_session(const std::vector<SessionPlayer>& players, int seconds) {
    gTestPlayers = players;
    gTestSeconds = seconds;
}
std::vector<SessionPlayer> session_players() { return gTestPlayers; }
int session_seconds() { return gTestSeconds; }
std::string area_label(uint32_t area) {
    if (area == 0xFFFFFFFFu) return "";
    if ((area >> 24) == 1) return "The Nexus";
    return "Boletarian Palace 1-" + std::to_string(((area >> 16) & 0xFF) + 1);  // (the made-up players are there)
}
// Warp and Unstuck: never asked here; Warp as for a player standing in the same area, Unstuck before the arrival
std::string warp_blocked(uint32_t) { return {}; }
std::string unstuck_blocked() { return "Where you arrived here isn't known yet."; }
void ask_warp(uint32_t) {}
void ask_unstuck() {}
int take_move(float*, std::string*) { return 0; }
std::string last_move() { return {}; }
bool notices_waiting() { return false; }
std::vector<Notice> take_notices() { return {}; }
}  // namespace des::seamless
