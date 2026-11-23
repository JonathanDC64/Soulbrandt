// The user's pads as the runtime reads them, for the menus, the crash dialog and the report (cellPad.h's
// cellPad_host_pads, declared here: des_ui does not take ps3recomp's include paths).
#pragma once

extern "C" {
typedef struct {  // cellPad.h's CellPadHostPad, field for field: cellPad_host_pads fills an array of these
    char id[16];                   // its PAD_CONTROLLER name: "controller1", "playstation2", ...
    unsigned short buttons;        // CELL_PAD_CTRL_*
    unsigned char lx, ly, rx, ry;  // 0-255, centre 128, up = 0
    unsigned char l2, r2;
    unsigned char extra;  // its buttons beyond the PS3's (pad_map.h's PAD_EXTRA_*: paddles, back buttons, Fn)
} CellPadHostPad;
// Every pad of the user's, read now, whatever reads them in this build (XInput or SDL, and the Sony HID reader); it
// works before the game has started its pads. Returns how many, up to max.
int cellPad_host_pads(CellPadHostPad* out, int max);
// 1 when SDL reads the pads in this build (SDL3 on Windows, SDL2 elsewhere), 0 XInput. Sony's pads on Windows are
// read over HID either way.
int cellPad_host_reader_sdl(void);
}
// (The runtime writes whole entries of its own size: an array of a smaller copy overflows with a second pad.)
static_assert(sizeof(CellPadHostPad) == 26, "cellPad.h's CellPadHostPad");
