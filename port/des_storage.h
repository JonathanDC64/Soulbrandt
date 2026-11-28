/* Small C interface for the inventory command menu's native mouse driver. */
#pragma once

#include <cstdint>

struct ppu_context;

extern "C" {

/* The active command menu's visible row (0..4), or -1 for another dialog. */
int des_storage_mouse_row(uint32_t dialog);

/* Guest object backing the requested visual row's stable hitbox, or 0 if absent.
 * The storage command menu currently has one column (column 0). */
uint32_t des_storage_mouse_spot(uint32_t dialog, int row, int column);

/* Apply a visual row selection directly and publish its native projection. */
int des_storage_mouse_select(ppu_context* ctx, uint32_t dialog, int row, int column);

}
