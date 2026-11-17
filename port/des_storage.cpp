/* Send to Storage (DES_STORAGE, off unless set).
 *
 * The inventory's command menu is the dialog whose pieces include
 * grayout_drop00 and grayout_drop01: Use and Drop. With the knob on, a line
 * is added under them, labelled "Send to Storage". Choosing it calls the
 * game's own deposit (currently the US lift's func_00924BB8): the box at
 * PGD+0x478, the inventory at PGD+0x128, the selected slot, and the chosen
 * quantity. The picker and final deposit check the native storage count and
 * limit first. Send greys with Drop's overlay when the item is equipped or
 * protected; Cross then plays the cancel sound and stays on the command, the
 * same as greyed Drop.
 *
 * The quantity picker is Drop's (message 2000). While that picker is ours,
 * 2000 is consumed and deposited; falling through to native 2000 drops the
 * stack on the ground. The tick must keep ownership after the picker leaves
 * mode 9 or rewrites its prompt, because that happens on OK before 2000 arrives.
 *
 * Use and Drop are unchanged. The line is a copy of the Drop piece, so the
 * menu's own two commands stay where they are. A disc whose lift does not
 * name that deposit function does not get the line; other disc addresses are
 * not yet resolved here.
 *
 * DES_STORAGE_LOG=1 also prints the command menu's pieces the first time it
 * opens. DES_STORAGE_NAV_TRACE=1 records native selection transitions and
 * confirmation, with menu ticks and draws, for timing diagnostics. */

#include "ps3emu/ppu_lift.h"
#include "des_region.h"
#include "des_game_calls.h"
#include "des_storage_navigation.h"
#include "ps3emu/ps3_knob.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#ifndef PS3_GAME_SPLIT
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
#endif
extern "C" void ds_malloc(ppu_context* ctx);
extern "C" int des_hud_obj_rect(ppu_context* ctx, uint32_t obj, float out[4]);
extern "C" volatile unsigned g_pad0_buttons, g_pad0_reads, g_pad0_empty_reads;
#ifdef PS3_GAME_SPLIT
extern "C" void (*des_lift_find(const char* name))(ppu_context*);
#endif
extern "C" int des_storage_command(ppu_context* ctx);

namespace {

constexpr uint32_t kPgdRoot = 0x01B4EF9Cu;
constexpr uint32_t kPgdInv = 0x128u;     /* the inventory object the deposit takes */
constexpr uint32_t kPgdItems = 0x1F4u;   /* the list: +0x0C slots, +0x10 entries of 0x20 */
constexpr uint32_t kPgdBox = 0x478u;     /* the storage box */
constexpr uint32_t kCmdRow = 0xF4u;
constexpr uint32_t kDlgRow = 0x134u, kDlgItems = 0x138u, kDlgOrder = 0x13Cu;
constexpr uint32_t kKids = 0x0Cu, kKidCount = 0x10u;
constexpr uint32_t kObjFlags = 0x18u, kObjRecord = 0x1Cu;
constexpr uint32_t kObjDrawn = 0x40000000u;
constexpr uint32_t kRect = 0x74u, kLayout = 0x84u;
constexpr uint32_t kToc = 0x019B5040u;

bool guest_ok(uint32_t a, uint32_t align)
{
    return a >= 0x00010000u && a < 0xD0000000u && (a & (align - 1u)) == 0;
}

// Custom pieces exist only during drawing, outside native update and teardown.
struct DrawWrite { uint32_t at, before, after; unsigned size; };
thread_local std::vector<DrawWrite> s_draw_writes;
std::vector<DrawWrite> s_layout_writes;
thread_local bool s_drawing = false;
thread_local bool s_layout_initializing = false;
struct OwnedAllocation { uint32_t at, size; };
std::vector<OwnedAllocation> s_owned_allocations;
bool owned_address(uint32_t at, unsigned size)
{
    for (const auto& allocation : s_owned_allocations)
        if (at >= allocation.at && (uint64_t)at + size <= (uint64_t)allocation.at + allocation.size) return true;
    return false;
}
thread_local std::vector<size_t> s_draw_starts;
void draw_write(uint32_t at, uint32_t value, unsigned size)
{
    uint32_t old = size == 4 ? vm_read32(at) : size == 2 ? vm_read16(at) : vm_read8(at);
    // New layout data must remain valid after the native renderer queues it.
    // Roll back native bindings and later per-frame edits, never initialization
    // of the storage-owned clone/shape/child arrays to uninitialized bytes.
    if (s_drawing && !(s_layout_initializing && owned_address(at, size)))
        s_draw_writes.push_back({at, old, value, size});
    if (size == 4) vm_write32(at, value);
    else if (size == 2) vm_write16(at, (uint16_t)value);
    else vm_write8(at, (uint8_t)value);
}
void storage_write32(uint32_t at, uint32_t value) { draw_write(at, value, 4); }
void storage_write16(uint32_t at, uint16_t value) { draw_write(at, value, 2); }
void storage_write8(uint32_t at, uint8_t value) { draw_write(at, value, 1); }
void restore_draw()
{
    const size_t start = s_draw_starts.back();
    s_draw_starts.pop_back();
    for (auto i = s_draw_writes.rbegin(); i != s_draw_writes.rend() - start; ++i) {
        if (i->size == 4) vm_write32(i->at, i->before);
        else if (i->size == 2) vm_write16(i->at, (uint16_t)i->before);
        else vm_write8(i->at, (uint8_t)i->before);
    }
    s_draw_writes.resize(start);
    s_drawing = !s_draw_starts.empty();
}

float vm_readf(uint32_t ea)
{
    const uint32_t bits = vm_read32(ea);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

void vm_writef(uint32_t ea, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, 4);
    storage_write32(ea, bits);
}

bool on()
{
    static int s_on = 0;
    PS3_KNOB(s_on, ps3_env_int("DES_STORAGE", 0));
    return s_on != 0;
}

bool logging()
{
    static int s_log = 0;
    PS3_KNOB(s_log, ps3_env_int("DES_STORAGE_LOG", 0));
    return s_log != 0;
}

bool navigation_trace()
{
    static int enabled = 0;
    PS3_KNOB(enabled, ps3_env_int("DES_STORAGE_NAV_TRACE", 0));
    return enabled != 0;
}

void drain(ppu_context* ctx)
{
    while (g_trampoline_fn) {
        void (*tf)(void*) = g_trampoline_fn;
        g_trampoline_fn = nullptr;
        tf(ctx);
    }
}

uint32_t call_game(ppu_context* ctx, void (*fn)(ppu_context*), std::initializer_list<int64_t> a)
{
    uint64_t g[32];
    double f[32];
    uint8_t v[32][16];
    memcpy(g, ctx->gpr, sizeof g);
    memcpy(f, ctx->fpr, sizeof f);
    memcpy(v, ctx->vr, sizeof v);
    const uint64_t lr = ctx->lr, ctr = ctx->ctr;
    const uint32_t cr = ppu_cr_pack(ctx), xer = ctx->xer;
    ctx->gpr[1] -= 0x1000;
    ctx->gpr[2] = DES_AT(kToc);
    int i = 0;
    for (int64_t x : a)
        if (i < 8) ctx->gpr[3 + i++] = (uint64_t)x;
    ctx->lr = DES_AT(0x00724E40u);
    fn(ctx);
    drain(ctx);
    const uint32_t r3 = (uint32_t)ctx->gpr[3];
    memcpy(ctx->gpr, g, sizeof g);
    memcpy(ctx->fpr, f, sizeof f);
    memcpy(ctx->vr, v, sizeof v);
    ctx->lr = lr;
    ctx->ctr = ctr;
    ppu_cr_unpack(ctx, cr);
    ctx->xer = xer;
    return r3;
}

uint32_t guest_alloc(ppu_context* ctx, uint32_t size)
{
    uint64_t g3 = ctx->gpr[3];
    ctx->gpr[3] = size;
    ds_malloc(ctx);
    const uint32_t a = (uint32_t)ctx->gpr[3];
    ctx->gpr[3] = g3;
    if (!guest_ok(a, 4)) return 0;
    s_owned_allocations.push_back({a, size});
    return a;
}

uint32_t guest_text(ppu_context* ctx, const char* text)
{
    const uint32_t n = (uint32_t)strlen(text) + 1;
    const uint32_t t = guest_alloc(ctx, 2 * n);
    for (uint32_t i = 0; t && i < n; i++) storage_write16(t + 2 * i, (uint16_t)(uint8_t)text[i]);
    return t;
}

void (*deposit_fn())(ppu_context*)
{
    static int s_tried = 0;
    static void (*s_fn)(ppu_context*) = nullptr;
    if (s_tried) return s_fn;
    s_tried = 1;
#ifdef PS3_GAME_SPLIT
    s_fn = des_lift_find("func_00924BB8");
#endif
    if (!s_fn)
        fprintf(stderr, "[des] storage: this disc's lift has no deposit function, so the line stays hidden\n");
    return s_fn;
}

uint32_t pgd()
{
    const uint32_t root = vm_read32(DES_AT(kPgdRoot));
    return guest_ok(root, 4) && guest_ok(vm_read32(root + 8), 4) ? vm_read32(root + 8) : 0;
}

uint32_t inventory()
{
    const uint32_t menus = vm_read32(DES_AT(0x019ADF0Cu));
    const uint32_t holder = guest_ok(menus, 4) ? vm_read32(menus - 0x8000u) : 0;
    const uint32_t inv = guest_ok(holder, 4) ? vm_read32(holder) : 0;
    return guest_ok(inv, 4) ? inv : 0;
}

/* The dialog's selection, as Use reads it: the row to the list's order to the item's slot. */
int32_t selected(uint32_t dlg)
{
    const int32_t row = (int32_t)vm_read32(dlg + kDlgRow);
    const uint32_t order = vm_read32(dlg + kDlgOrder), items = vm_read32(dlg + kDlgItems);
    if (row < 0 || !guest_ok(order, 4) || !guest_ok(items, 4)) return -1;
    const uint32_t ov = vm_read32(order + 4), iv = vm_read32(items + 4);
    if (!guest_ok(ov, 4) || !guest_ok(iv, 4)) return -1;
    const int32_t m = (int32_t)vm_read32(ov + 4u * (uint32_t)row);
    return m < 0 ? -1 : (int32_t)vm_read32(iv + 4u * (uint32_t)m);
}

struct Entry { uint32_t cat = 0, id = 0; int32_t count = 0; };

// The native deposit reports success even when its box update clamps a stack,
// then removes the entire requested quantity. Ask the game's count and limit
// helpers first; a successful return alone cannot authorize removing items.
int storage_room(ppu_context* ctx, uint32_t box, const Entry& item)
{
#ifdef PS3_GAME_SPLIT
    static auto count = des_lift_find("func_00924D58");
    static auto limit = des_lift_find("func_00923FD8");
    static uint32_t probe = 0;
    if (!count || !limit || !guest_ok(box, 4)) return 0;
    // The limit helper reads only the selected record's item key. Give it a
    // private one-record view rather than changing any live box entry.
    if (!probe) probe = guest_alloc(ctx, 32);
    if (!probe) return 0;
    for (uint32_t i = 0; i < 32; i += 4) storage_write32(probe + i, 0);
    storage_write32(probe + 12, item.cat | item.id);
    const int maximum = (int32_t)call_game(ctx, limit, {probe, 0});
    const int held = (int32_t)call_game(ctx, count, {box, item.cat, item.id});
    if (maximum <= 0 || held < 0 || held >= maximum) return 0;
    return maximum - held;
#else
    return 0;
#endif
}

Entry entry_at(uint32_t p, int32_t idx)
{
    Entry e;
    const uint32_t list = p + kPgdItems, at = vm_read32(list + 0x10);
    if (idx < 0 || idx >= (int32_t)vm_read32(list + 0x0C) || !guest_ok(at, 4)) return e;
    const uint32_t q = at + 0x20u * (uint32_t)idx;
    e.cat = vm_read32(q);
    e.id = vm_read32(q + 4);
    e.count = (int32_t)vm_read32(q + 8);
    return e;
}

std::string piece_name(uint32_t obj)
{
    std::string s;
    const uint32_t rec = guest_ok(obj, 4) ? vm_read32(obj + kObjRecord) : 0;
    const uint32_t name = guest_ok(rec, 4) ? vm_read32(rec) : 0;
    if (!guest_ok(name, 2)) return s;
    for (uint32_t i = 0; i < 48; i++) {
        const uint16_t c = vm_read16(name + 2 * i);
        if (!c) break;
        s += c < 0x80 && c >= 0x20 ? (char)c : '~';
    }
    return s;
}

uint32_t kid(uint32_t dlg, uint32_t i)
{
    const uint32_t arr = vm_read32(dlg + kKids);
    if (!guest_ok(arr, 4)) return 0;
    const uint32_t obj = vm_read32(arr + 4 * i);
    return guest_ok(obj, 4) ? obj : 0;
}

uint32_t kid_count(uint32_t dlg)
{
    const uint32_t n = vm_read32(dlg + kKidCount);
    return n > 64u ? 64u : n;
}

bool text_is(uint32_t p, const char* s)
{
    if (!guest_ok(p, 2)) return false;
    for (; *s; s++, p += 2)
        if (vm_read16(p) != (uint8_t)*s) return false;
    return vm_read16(p) == 0;
}

bool read_text(uint32_t p, char* out, int cap)
{
    if (!guest_ok(p, 2) || cap < 2) return false;
    int n = 0;
    for (; n < cap - 1; n++) {
        const uint16_t c = vm_read16(p + 2u * (uint32_t)n);
        if (!c) break;
        if (c < 32 || c > 126) return false;
        out[n] = (char)c;
    }
    if (n < 2 || vm_read16(p + 2u * (uint32_t)n) != 0) return false;
    out[n] = 0;
    return true;
}

bool box_of(ppu_context* ctx, uint32_t obj, float out[4])
{
    if (!des_hud_obj_rect(ctx, obj, out)) return false;
    return out[2] > out[0] && out[3] > out[1];
}

uint32_t find_named(uint32_t dlg, const char* name)
{
    const uint32_t n = kid_count(dlg);
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t obj = kid(dlg, i);
        if (obj && piece_name(obj) == name) return obj;
    }
    return 0;
}

bool name_is(uint32_t obj, const char* s)
{
    const uint32_t rec = guest_ok(obj, 4) ? vm_read32(obj + kObjRecord) : 0;
    uint32_t name = guest_ok(rec, 4) ? vm_read32(rec) : 0;
    if (!guest_ok(name, 2)) return false;
    for (; *s; s++, name += 2)
        if (vm_read16(name) != (uint8_t)*s) return false;
    return vm_read16(name) == 0;
}

bool is_command(uint32_t dlg)
{
    const uint32_t n = vm_read32(dlg + kKidCount);
    if (n == 0 || n > 400u) return false;
    bool a = false, b = false;
    for (uint32_t i = 0; i < n && !(a && b); i++) {
        const uint32_t obj = kid(dlg, i);
        a = a || name_is(obj, "grayout_drop00");
        b = b || name_is(obj, "grayout_drop01");
    }
    return a && b;
}

/* A pointer to the exact word, in the object or in one block it points at. */
struct Hit { uint32_t obj = 0, base = 0, off = 0; };

Hit find_word(uint32_t obj, const char* word)
{
    Hit h;
    if (!obj) return h;
    const uint32_t rec = vm_read32(obj + kObjRecord);
        const uint32_t blocks[2] = {obj, guest_ok(rec, 4) ? rec : 0};
        const uint32_t sizes[2] = {0x180u, 0x80u};
        for (int b = 0; b < 2; b++) {
            if (!blocks[b]) continue;
            for (uint32_t off = 0; off + 4 <= sizes[b]; off += 4) {
                const uint32_t p = vm_read32(blocks[b] + off);
                if (text_is(p, word)) return Hit{obj, blocks[b], off};
                if (!guest_ok(p, 4) || p == blocks[0] || p == blocks[1]) continue;
                for (uint32_t k = 0; k + 4 <= 0x40u; k += 4)
                    if (text_is(vm_read32(p + k), word)) return Hit{obj, p, k};
            }
        }
    return h;
}

void copy_bytes(uint32_t dst, uint32_t src, uint32_t n);

/* The piece's box is two packed pairs at shape+8 and shape+12: x,y then right,bottom.
 * A window's bottom is whichever halfword in its shape equals the box's bottom. */
bool move_shape(uint32_t obj, int add, bool bottom_only)
{
    const uint32_t shape = guest_ok(obj, 4) ? vm_read32(obj + 0x14) : 0;
    if (!guest_ok(shape, 4) || add == 0) return false;
    const uint32_t xy = vm_read32(shape + 8), rb = vm_read32(shape + 12);
    const int y = (int)(xy & 0xffffu), bot = (int)(rb & 0xffffu);
    if (y < 50 || y > 900 || bot < 50 || bot > 900) return false;
    if (!bottom_only) storage_write32(shape + 8, (xy & 0xffff0000u) | (uint16_t)(y + add));
    storage_write32(shape + 12, (rb & 0xffff0000u) | (uint16_t)(bot + add));
    return true;
}

/* A rect stored as four floats (left, top, right, bottom) that is this piece's box.
 * Buttons keep theirs as packed halves at shape+8. Plates keep theirs at shape+0x1C.
 * The panel keeps its at shape+0x14. A frame or cursor keeps its on the dialog at
 * *(shape+0x10), the same +0x74 rect every dialog has. */
bool shift_box(ppu_context* ctx, uint32_t obj, float add, bool bottom_only)
{
    float box[4];
    if (!obj || add == 0.0f || !box_of(ctx, obj, box)) return false;
    /* Panels and frames do not have a button's packed rect at +8.
     * In particular, do not interpret their colour/state words as coordinates. */
    const uint32_t shape = vm_read32(obj + 0x14);
    const uint32_t child = guest_ok(shape, 4) ? vm_read32(shape + 0x10) : 0;
    const uint32_t cands[] = {
        guest_ok(shape, 4) ? shape + 0x14u : 0u,
        guest_ok(shape, 4) ? shape + 0x1Cu : 0u,
        guest_ok(child, 4) ? child + 0x74u : 0u,
    };
    for (uint32_t at : cands) {
        if (!at) continue;
        float r[4];
        for (int k = 0; k < 4; k++) r[k] = vm_readf(at + 4u * (uint32_t)k);
        if (std::fabs(r[0] - box[0]) > 2.0f || std::fabs(r[1] - box[1]) > 2.0f) continue;
        if (std::fabs(r[2] - box[2]) > 2.0f || std::fabs(r[3] - box[3]) > 2.0f) continue;
        if (!bottom_only) vm_writef(at + 4, r[1] + add);
        vm_writef(at + 12, r[3] + add);
        return true;
    }
    return false;
}

struct ShapeBinding { uint32_t obj, before, after; };
std::vector<ShapeBinding> s_shape_bindings;

uint32_t copy_shape(ppu_context* ctx, uint32_t obj)
{
    const uint32_t shape = guest_ok(obj, 4) ? vm_read32(obj + 0x14) : 0;
    if (!guest_ok(shape, 4)) return 0;
    const uint32_t copy = guest_alloc(ctx, 0x40);
    if (!copy) return 0;
    copy_bytes(copy, shape, 0x40);
    // Native draw work may outlive this dialog callback. Keep the owned
    // binding stable; restoring it here races the later text/geometry pass.
    vm_write32(obj + 0x14, copy);
    s_shape_bindings.push_back({obj, shape, copy});
    return copy;
}

/* The menu file shares shape blocks between screens. Move a copy, or the item list moves too. */
void own_shape(ppu_context* ctx, uint32_t obj)
{
    if (!copy_shape(ctx, obj)) return;
    const uint32_t shape = vm_read32(obj + 0x14);
    const uint32_t child = guest_ok(shape, 4) ? vm_read32(shape + 0x10) : 0;
    if (!guest_ok(child, 4)) return;
    float bounds[4];
    if (!box_of(ctx, obj, bounds)) return;
    for (unsigned i = 0; i < 4; ++i) {
        const float value = vm_readf(child + kRect + i * 4);
        if (!std::isfinite(value) || std::fabs(value - bounds[i]) > 2.0f) return;
    }
    const uint32_t child2 = guest_alloc(ctx, 0x100);
    if (!child2) return;
    copy_bytes(child2, child, 0x100);
    storage_write32(shape + 0x10, child2);
}

bool insert_kid(ppu_context* ctx, uint32_t dlg, uint32_t obj, uint32_t index)
{
    const uint32_t arr = vm_read32(dlg + kKids);
    const uint32_t n = vm_read32(dlg + kKidCount);
    if (!guest_ok(arr, 4) || n > 64u) return false;
    if (index > n) index = n;
    const uint32_t na = guest_alloc(ctx, (n + 2u) * 4u);
    if (!na) return false;
    for (uint32_t i = 0; i < index; i++) storage_write32(na + 4 * i, vm_read32(arr + 4 * i));
    storage_write32(na + 4 * index, obj);
    for (uint32_t i = index; i < n; i++) storage_write32(na + 4 * (i + 1), vm_read32(arr + 4 * i));
    storage_write32(dlg + kKids, na);
    storage_write32(dlg + kKidCount, n + 1);
    return true;
}

bool append_kid(ppu_context* ctx, uint32_t dlg, uint32_t obj)
{
    return insert_kid(ctx, dlg, obj, vm_read32(dlg + kKidCount));
}

uint32_t kid_index(uint32_t dlg, const char* name)
{
    const uint32_t n = kid_count(dlg);
    for (uint32_t i = 0; i < n; i++)
        if (name_is(kid(dlg, i), name)) return i;
    return n;
}

void copy_bytes(uint32_t dst, uint32_t src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i += 4) storage_write32(dst + i, vm_read32(src + i));
}

/* A piece of our own, moved by add. Its shape is a copy, and so is the dialog a
 * frame or cursor keeps its rect on, so the original stays where it was. The
 * box is measured on the original: the copy is not in the menu yet. */
bool nudge_rect(uint32_t base, uint32_t bytes, const float box[4], float add, bool bottom_only)
{
    if (!guest_ok(base, 4)) return false;
    for (uint32_t off = 0; off + 16 <= bytes; off += 4) {
        float r[4];
        bool match = true;
        for (int k = 0; k < 4; k++) {
            r[k] = vm_readf(base + off + 4u * (uint32_t)k);
            if (!std::isfinite(r[k]) || !std::isfinite(box[k]) ||
                std::fabs(r[k] - box[k]) > 2.0f) match = false;
        }
        if (!match) continue;
        if (!bottom_only) vm_writef(base + off + 4, r[1] + add);
        vm_writef(base + off + 12, r[3] + add);
        return true;
    }
    return false;
}

/* The same rect as four big-endian int16s, the way the menu file stores one. */
bool nudge_packed(uint32_t base, uint32_t bytes, const float box[4], float add, bool bottom_only)
{
    if (!guest_ok(base, 2)) return false;
    const int want[4] = {(int)box[0], (int)box[1], (int)box[2], (int)box[3]};
    for (uint32_t off = 0; off + 8 <= bytes; off += 2) {
        int v[4];
        bool match = true;
        for (int k = 0; k < 4; k++) {
            v[k] = (int)(int16_t)vm_read16(base + off + 2u * (uint32_t)k);
            if (std::abs(v[k] - want[k]) > 1) match = false;
        }
        if (!match) continue;
        if (!bottom_only) storage_write16(base + off + 2, (uint16_t)(v[1] + (int)add));
        storage_write16(base + off + 6, (uint16_t)(v[3] + (int)add));
        return true;
    }
    return false;
}

uint32_t s_line = 0;
uint32_t s_mark = 0;   /* the highlight on the new row, shown only while it is chosen */
uint32_t s_plate = 0;  /* the plate under Auto Sort */
uint32_t s_grayout = 0; /* Drop's disabled overlay, shifted onto Send */
uint32_t s_text_at = 0; /* the word that holds the line's text pointer */
uint32_t s_label_ptr = 0;
float s_top = 0, s_bot = 0, s_drop_y = 0, s_gap = 0;
DesStorageNavigation s_navigation;
int& s_vis = s_navigation.row;
int s_forced = 0;
uint64_t s_nav_tick = 0, s_nav_draw = 0;

int s_have_target = 0;
uint32_t s_dlg = 0;
int s_line_state = 0;   /* 0 not tried, 1 added, -1 could not */
uint32_t s_help_bank = 0;
uint32_t s_help_blank_text = 0; /* empty guest string used while Send is selected */
uint32_t s_help_slots[2] = {}, s_help_offsets[2] = {};
enum class InventoryAsk { Idle, Armed, Asking, Ready };
struct InventoryTransfer {
    InventoryAsk state = InventoryAsk::Idle;
    uint32_t player = 0, dialog = 0;
    int32_t slot = -1;
    Entry item;
    int32_t amount = 0;
    uint32_t picker_text = 0;
    int closed_ticks = 0;
    bool cancel_requested = false;
};
InventoryTransfer s_inventory_transfer;
void sync_send_help_blank()
{
    const bool active = on() && s_dlg && vm_read8(s_dlg + 0x94);
    const bool picker = s_inventory_transfer.state != InventoryAsk::Idle;
    const bool blank = (active && s_forced) || picker;
    for (unsigned i = 0; i < 2; ++i) {
        if (!s_help_slots[i]) continue;
        const uint32_t offset = blank ? s_help_blank_text - s_help_bank :
            active && s_vis >= 3 ? s_help_offsets[s_vis == 3 ? 0 : 1] : s_help_offsets[i];
        vm_write32(s_help_slots[i], offset);
    }
    static int logged_row = -2;
    const int row = picker && !active ? -3 : active ? s_vis : -1;
    if (logging() && row != logged_row && row != -1) {
        fprintf(stderr, "[des] storage: footer visual row %d -> %s\n",
                row == -3 ? 2 : row,
                blank ? (row == -3 ? "blank (quantity picker)" : "blank")
                      : row == 3 ? "Reorganize" : row == 4 ? "Auto Sort" : "native Use/Drop");
        logged_row = row;
    }
}

void ensure_help_bank(ppu_context* ctx)
{
    const uint32_t bank = call_game(ctx, func_005C2810, {80}); // native one-line help category
    if (!guest_ok(bank, 4) || bank == s_help_bank) return;
    const uint32_t groups = vm_read32(bank + 0x0C), table = vm_read32(bank + 0x14);
    if (!guest_ok(table, 4) || groups > 4096) return;
    for (unsigned i = 0; i < 2; ++i) {
        s_help_slots[i] = 0;
        const uint32_t id = 11005 + i;
        for (uint32_t g = 0; g < groups; ++g) {
            const uint32_t at = bank + 0x1C + 12 * g;
            const uint32_t first = vm_read32(at + 4), last = vm_read32(at + 8);
            if (id < first || id > last) continue;
            s_help_slots[i] = table + 4 * (vm_read32(at) + id - first);
            s_help_offsets[i] = vm_read32(s_help_slots[i]);
            break;
        }
    }
    s_help_bank = bank;
    fprintf(stderr, "[des] storage: one-line help FMG category 80, sort entries %d/%d\n",
            s_help_slots[0] != 0, s_help_slots[1] != 0);
}

void log_pieces(ppu_context* ctx, uint32_t dlg)
{
    static uint32_t s_logged = 0;
    if (!logging() || s_logged == dlg) return;
    s_logged = dlg;
    const uint32_t n = kid_count(dlg);
    fprintf(stderr, "[des] storage: command menu %08x, %u pieces, row %d\n", dlg, n, (int)vm_read32(dlg + kCmdRow));
    char text[24];
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t obj = kid(dlg, i);
        if (!obj) continue;
        float r[4] = {};
        const bool got = box_of(ctx, obj, r);
        fprintf(stderr, "[des] storage: piece %s%s", piece_name(obj).c_str(),
                (vm_read32(obj + kObjFlags) & kObjDrawn) ? "" : " (hidden)");
        if (got) fprintf(stderr, " box %.0f,%.0f,%.0f,%.0f", r[0], r[1], r[2], r[3]);
        fprintf(stderr, "\n");
        const uint32_t rec = vm_read32(obj + kObjRecord);
        const uint32_t blocks[2] = {obj, guest_ok(rec, 4) ? rec : 0};
        const uint32_t sizes[2] = {0x80u, 0x40u};
        for (int b = 0; b < 2; b++) {
            if (!blocks[b]) continue;
            for (uint32_t off = 0; off + 4 <= sizes[b]; off += 4) {
                const uint32_t p = vm_read32(blocks[b] + off);
                if (!read_text(p, text, (int)sizeof text)) continue;
                fprintf(stderr, "[des] storage: text +%u \"%s\"\n", off, text);
            }
        }
    }
}

/* The gap between the two command rows, and where a third row sits. */
bool row_gap(ppu_context* ctx, uint32_t dlg, float* dy, float* top, float* bot)
{
    uint32_t a = find_named(dlg, "plate0_0"), b = find_named(dlg, "plate0_1");
    if (!a || !b) {
        a = find_named(dlg, "button0_0");
        b = find_named(dlg, "button1_0");
    }
    if (!a || !b) {
        a = find_named(dlg, "grayout_drop00");
        b = find_named(dlg, "grayout_drop01");
    }
    float ra[4], rb[4];
    if (!a || !b || !box_of(ctx, a, ra) || !box_of(ctx, b, rb)) return false;
    if (ra[1] > rb[1]) {
        for (int k = 0; k < 4; k++) {
            const float t = ra[k];
            ra[k] = rb[k];
            rb[k] = t;
        }
    }
    float gap = rb[1] - ra[1];
    if (gap < 8.0f || gap > 140.0f) gap = rb[3] - rb[1];
    if (gap < 8.0f || gap > 140.0f) gap = 24.0f;
    *dy = gap;
    *top = rb[1] + gap;
    *bot = rb[3] + gap;
    return true;
}

bool line_kept(uint32_t dlg)
{
    if (!s_line) return false;
    const uint32_t n = kid_count(dlg);
    for (uint32_t i = 0; i < n; i++)
        if (kid(dlg, i) == s_line) return true;
    return false;
}

bool ensure_line(ppu_context* ctx, uint32_t dlg)
{
    static uint32_t native_kids = 0, native_count = 0;
    if (s_dlg != dlg || native_kids != vm_read32(dlg + kKids) || native_count != vm_read32(dlg + kKidCount)) {
        native_kids = vm_read32(dlg + kKids);
        native_count = vm_read32(dlg + kKidCount);
        s_layout_writes.clear();
        s_navigation.reset();
        s_forced = 0;
        s_dlg = dlg;
        s_line = 0;
        s_mark = 0;
        s_plate = 0;
        s_grayout = 0;
        s_text_at = 0;
        /* keep s_label_ptr: the UTF-16 lives outside draw_writes and is reused */
        s_line_state = 0;
        s_have_target = 0;
    }
    if (s_line_state == 1) {
        for (const auto& w : s_layout_writes) draw_write(w.at, w.after, w.size);
        return true;
    }
    if (s_line_state < 0) return false;
    const size_t layout_start = s_draw_writes.size();
    struct InitializingLayout {
        bool before = s_layout_initializing;
        InitializingLayout() { s_layout_initializing = true; }
        ~InitializingLayout() { s_layout_initializing = before; }
    } initializing;
    log_pieces(ctx, dlg);
    float dy = 0, top = 0, bot = 0;
    if (!row_gap(ctx, dlg, &dy, &top, &bot)) {
        s_line_state = -1;
        fprintf(stderr, "[des] storage: the two command rows have no boxes\n");
        return false;
    }
    s_top = top;
    s_bot = bot;
    s_drop_y = top - dy;
    s_gap = dy;
    s_have_target = 1;
    for (const char* name : {"cursor", "cursor3_0", "edge_gradation03"})
        own_shape(ctx, find_named(dlg, name));
    const uint32_t button = find_named(dlg, "button1_0");
    Hit hit = find_word(button, "Drop");
    if (!hit.obj) {
        s_line_state = -1;
        fprintf(stderr, "[des] storage: the Drop line has no text pointer, so no line was added\n");
        return false;
    }
    /* Allocate the label outside draw_writes when possible. guest_text during
     * s_drawing records every UTF-16 store; a missed layout replay then leaves
     * the highlighted row blank after restore_draw. */
    if (!s_label_ptr) {
        const bool drawing = s_drawing;
        s_drawing = false;
        if (!s_label_ptr) s_label_ptr = guest_text(ctx, "Send to Storage");
        s_drawing = drawing;
    }
    ensure_help_bank(ctx);
    const uint32_t label = s_label_ptr;
    if (!label) {
        s_line_state = -1;
        fprintf(stderr, "[des] storage: no guest memory for the line\n");
        return false;
    }
    int shifted = 0;
    for (const char* below : {"button2_0", "button3_0"}) {
        const uint32_t obj = find_named(dlg, below);
        if (!obj) continue;
        own_shape(ctx, obj);
        if (move_shape(obj, (int)dy, false)) shifted++;
    }
    /* One new row. The panel's rect is four floats, not the button's packed halves. */
    for (const char* frame : {"BG", "R03_frame"}) {
        const uint32_t obj = find_named(dlg, frame);
        if (!obj) continue;
        own_shape(ctx, obj);
        if (shift_box(ctx, obj, dy, true)) shifted++;
    }
    const uint32_t clone = guest_alloc(ctx, 0x120);
    const uint32_t rec = vm_read32(hit.obj + kObjRecord);
    const uint32_t rec2 = guest_ok(rec, 4) ? guest_alloc(ctx, 0x80) : 0;
    if (!clone || (guest_ok(rec, 4) && !rec2)) {
        s_line_state = -1;
        fprintf(stderr, "[des] storage: no guest memory for the line\n");
        return false;
    }
    copy_bytes(clone, hit.obj, 0x120);
    if (rec2) {
        copy_bytes(rec2, rec, 0x80);
        storage_write32(clone + kObjRecord, rec2);
    }
    uint32_t text_base = hit.base == hit.obj ? clone : rec2;
    if (hit.base != hit.obj && hit.base != rec) {
        // find_word may return a shape field at +0x30. Keep the full searched
        // block: a 0x20 copy truncated the font state and wrote past its end.
        const uint32_t nb = guest_alloc(ctx, 0x40);
        if (!nb) {
            s_line_state = -1;
            return false;
        }
        copy_bytes(nb, hit.base, 0x40);
        for (uint32_t off = 0; off + 4 <= 0x120u; off += 4)
            if (vm_read32(clone + off) == hit.base) storage_write32(clone + off, nb);
        if (rec2) {
            for (uint32_t off = 0; off + 4 <= 0x80u; off += 4)
                if (vm_read32(rec2 + off) == hit.base) storage_write32(rec2 + off, nb);
        }
        text_base = nb;
    }
    float from[4];
    if (!box_of(ctx, hit.obj, from)) {
        from[1] = top - dy;
        from[3] = bot - dy;
    }
    own_shape(ctx, clone);
    const int moved = move_shape(clone, (int)dy, false) ? 2 : 0;
    if (text_base) {
        // The native Drop shape is embedded at object+0x30. After copying
        // its shape, the renderer reads the copy, not that old embedded word.
        const uint32_t source_shape = vm_read32(hit.obj + 0x14);
        const uint32_t original_at = hit.base + hit.off;
        s_text_at = original_at >= source_shape && original_at < source_shape + 0x40
            ? vm_read32(clone + 0x14) + original_at - source_shape : text_base + hit.off;
        storage_write32(s_text_at, label);
        storage_write32(text_base + hit.off, label);
    }
    storage_write32(clone + kObjFlags, vm_read32(clone + kObjFlags) | kObjDrawn);
    float placed[4] = {};
    const bool placed_ok = box_of(ctx, clone, placed);
    fprintf(stderr, "[des] storage: clone box %d %.0f,%.0f,%.0f,%.0f (drop %.0f) text %d\n", placed_ok ? 1 : 0,
            placed[0], placed[1], placed[2], placed[3], from[1],
            text_base && text_is(vm_read32(text_base + hit.off), "Send to Storage") ? 1 : 0);
    fprintf(stderr, "[des] storage: button words");
    for (uint32_t off = 0; off < 0x80u; off += 4) {
        const uint32_t w = vm_read32(hit.obj + off);
        if (w > 0 && w < 30000u) fprintf(stderr, " %u:%u", off, w);
    }
    fprintf(stderr, "\n");
    if (placed_ok) {
        s_top = placed[1];
        s_bot = placed[3];
    }
    if (!placed_ok || std::fabs(placed[1] - from[1]) < 2.0f) {
        s_line_state = -1;
        const uint32_t shape = vm_read32(hit.obj + 0x14);
        fprintf(stderr, "[des] storage: the new line did not move, shape %08x\n", shape);
        if (guest_ok(shape, 4)) {
            fprintf(stderr, "[des] storage: shape");
            for (uint32_t off = 0; off < 0x30u; off += 4) {
                const float f = vm_readf(shape + off);
                const uint32_t w = vm_read32(shape + off);
                if (std::isfinite(f) && std::fabs(f) > 1.0f && std::fabs(f) < 2000.0f) fprintf(stderr, " %u:f%.1f", off, f);
                else fprintf(stderr, " %u:%08x", off, w);
            }
            fprintf(stderr, "\n");
        }
        return false;
    }
    // Append additions so native child indices remain stable. Draw the extra
    // plate before the appended labels; repeat Auto Sort over its fifth plate.
    const uint32_t last_plate = find_named(dlg, "plate0_3");
    const uint32_t extra_plate = last_plate ? guest_alloc(ctx, 0x120) : 0;
    if (extra_plate) {
        copy_bytes(extra_plate, last_plate, 0x120);
        copy_shape(ctx, extra_plate);
        if (shift_box(ctx, extra_plate, dy, false) && append_kid(ctx, dlg, extra_plate)) s_plate = extra_plate;
    }
    if (!append_kid(ctx, dlg, clone)) {
        s_line_state = -1;
        return false;
    }
    /* Same overlay Drop uses when the item cannot be discarded, one row down. */
    const uint32_t drop_grey = find_named(dlg, "grayout_drop01");
    if (drop_grey) {
        const uint32_t grey = guest_alloc(ctx, 0x120);
        if (grey) {
            float from[4] = {};
            box_of(ctx, drop_grey, from);
            copy_bytes(grey, drop_grey, 0x120);
            own_shape(ctx, grey);
            const uint32_t shape = vm_read32(grey + 0x14);
            if (move_shape(grey, (int)dy, false) || shift_box(ctx, grey, dy, false) ||
                nudge_rect(shape, 0x40, from, dy, false) || nudge_packed(shape, 0x40, from, dy, false)) {
                if (append_kid(ctx, dlg, grey)) s_grayout = grey;
            }
        }
    }
    const uint32_t autos = find_named(dlg, "button3_0");
    const uint32_t auto_label = autos ? guest_alloc(ctx, 0x120) : 0;
    if (auto_label) {
        copy_bytes(auto_label, autos, 0x120);
        copy_shape(ctx, auto_label);
        storage_write32(auto_label + kObjFlags, vm_read32(auto_label + kObjFlags) | kObjDrawn);
        if (!append_kid(ctx, dlg, auto_label)) return false;
    }
    s_line = clone;
    /* A fifth draw-only cursor spot also exposes the row to the mouse adapter. */
    const uint32_t cursor_src = find_named(dlg, "cursor3_0");
    if (cursor_src) {
        const uint32_t extra = guest_alloc(ctx, 0x120);
        const uint32_t record = guest_alloc(ctx, 0x80);
        const uint32_t name = guest_text(ctx, "cursor4_0");
        if (extra && record && name) {
            float bounds[4];
            box_of(ctx, cursor_src, bounds);
            copy_bytes(extra, cursor_src, 0x120);
            copy_bytes(record, vm_read32(cursor_src + kObjRecord), 0x80);
            storage_write32(extra + kObjRecord, record);
            storage_write32(record, name);
            own_shape(ctx, extra);
            const uint32_t shape = vm_read32(extra + 0x14);
            if (nudge_rect(extra, 0x120, bounds, dy, false) ||
                nudge_rect(shape, 0x40, bounds, dy, false) ||
                nudge_packed(shape, 0x40, bounds, dy, false) ||
                shift_box(ctx, extra, dy, false)) {
                if (append_kid(ctx, dlg, extra)) s_mark = extra;
            }
        }
    }
    float panel[4] = {};
    const uint32_t bg = find_named(dlg, "BG");
    if (bg && box_of(ctx, bg, panel))
        fprintf(stderr, "[des] storage: panel %.0f,%.0f,%.0f,%.0f\n", panel[0], panel[1], panel[2], panel[3]);
    s_layout_writes.assign(s_draw_writes.begin() + layout_start, s_draw_writes.end());
    s_line_state = 1;
    float cb[4] = {};
    const bool got = box_of(ctx, clone, cb);
    const uint32_t shown = text_base ? vm_read32(text_base + hit.off) : 0;
    fprintf(stderr, "[des] storage: line added under Drop (moved %d, rows %d, dy %.0f, box %d %.0f,%.0f,%.0f,%.0f, text %d)\n",
            moved, shifted, dy, got ? 1 : 0, cb[0], cb[1], cb[2], cb[3], text_is(shown, "Send to Storage") ? 1 : 0);
    if (s_grayout) {
        float gb[4] = {};
        const bool grey_ok = box_of(ctx, s_grayout, gb);
        fprintf(stderr, "[des] storage: send grayout %d %.0f,%.0f,%.0f,%.0f\n", grey_ok ? 1 : 0, gb[0], gb[1], gb[2],
                gb[3]);
    }
    return true;
}

float cursor_top(ppu_context* ctx, uint32_t dlg)
{
    const uint32_t n = kid_count(dlg);
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t obj = kid(dlg, i);
        if (!obj || !name_is(obj, "cursor")) continue;
        float r[4];
        if (box_of(ctx, obj, r)) return r[1];
    }
    return -1.0f;
}

void place_cursor(ppu_context* ctx, uint32_t dlg, float top, float bot)
{
    static int s_miss = 0;
    (void)bot;
    const uint32_t n = kid_count(dlg);
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t obj = kid(dlg, i);
        if (!obj || !name_is(obj, "cursor")) continue;
        float r[4];
        if (!box_of(ctx, obj, r) || std::fabs(r[1] - top) <= 2.0f) return;
        const float add = top - r[1];
        const uint32_t shape = vm_read32(obj + 0x14);
        /* Match a complete rect in owned storage. A matching Y alone may
         * instead be a pointer halfword, colour, or unrelated state. */
        if (nudge_rect(shape, 0x40, r, add, false) ||
            nudge_packed(shape, 0x40, r, add, false) ||
            nudge_rect(obj, 0x120, r, add, false) ||
            nudge_packed(obj, 0x120, r, add, false) ||
            nudge_rect(vm_read32(shape + 0x10) + kRect, 16, r, add, false)) return;
        if (!s_miss) {
            s_miss = 1;
            fprintf(stderr, "[des] storage: the cursor has no position to move\n");
        }
        return;
    }
}

void hide_obj(uint32_t obj)
{
    if (!obj || !guest_ok(obj, 4)) return;
    storage_write32(obj + kObjFlags, vm_read32(obj + kObjFlags) & ~kObjDrawn);
}

void hide_line()
{
    hide_obj(s_line);
    hide_obj(s_mark);
    hide_obj(s_plate);
    hide_obj(s_grayout);
}

void show_line()
{
    if (!s_line || !guest_ok(s_line, 4)) return;
    storage_write32(s_line + kObjFlags, vm_read32(s_line + kObjFlags) | kObjDrawn);
}

bool send_blocked(ppu_context* ctx, uint32_t dlg);
void sync_send_grayout(ppu_context* ctx, uint32_t dlg);

bool command_open()
{
    return s_dlg && guest_ok(s_dlg, 4) && is_command(s_dlg) && vm_read8(s_dlg + 0x94) != 0;
}

int sync_navigation(const char* phase, bool confirming = false)
{
    // Native dispatch may hide the command before invoking its callback. Keep
    // that callback's established selection until it has consumed the action.
    const bool dispatching = confirming && s_navigation.active() && s_dlg &&
                             guest_ok(s_dlg, 4) && is_command(s_dlg);
    if (!on() || (!command_open() && !dispatching)) {
        s_navigation.reset();
        s_forced = 0;
        return 0;
    }
    const int native = (int32_t)vm_read32(s_dlg + kCmdRow);
    if (native < 0 || native > 3) return 0;
    const int previous = s_vis, expected = s_navigation.expected;
    const int step = s_navigation.observe(native);
    s_forced = s_navigation.send();
    // Publish the projection immediately: another draw or a confirm cannot
    // consume the same native transition a second time.
    vm_write32(s_dlg + kCmdRow, (uint32_t)s_navigation.projected());
    s_navigation.expected = s_navigation.projected();
    if (step && navigation_trace())
        fprintf(stderr, "[storage-nav] phase=%s tick=%llu draw=%llu dlg=%08x reads=%u empty=%u pad=%04x native=%d expected=%d from=%d to=%d step=%d\n",
                phase, (unsigned long long)s_nav_tick, (unsigned long long)s_nav_draw, s_dlg,
                (unsigned)g_pad0_reads, (unsigned)g_pad0_empty_reads, (unsigned)g_pad0_buttons,
                native, expected, previous, s_vis, step);
    return step;
}

/* Visual row: 0 Use, 1 Drop, 2 Send to Storage, 3 Reorganize, 4 Auto Sort.
 * The game's own row skips 2 (Send sits between Drop and Reorganize). */

void on_draw(ppu_context* ctx, uint32_t dlg)
{
    ++s_nav_draw;
    sync_send_help_blank();
    if (!on()) {
        if (is_command(dlg)) hide_line();
        s_navigation.reset();
        s_forced = 0;
        return;
    }
    if (!deposit_fn() || !guest_ok(dlg, 4)) return;
    static int s_alive = 0;
    if (!s_alive) {
        s_alive = 1;
        fprintf(stderr, "[des] storage: draw hook\n");
    }
    if (!is_command(dlg) || vm_read8(dlg + 0x94) == 0) return;
    if (!ensure_line(ctx, dlg)) return;
    const int step = sync_navigation("draw");
    show_line();
    sync_send_grayout(ctx, dlg);
    // The fifth plate owns its shape. Tint its textured fill for Auto Sort;
    // the native animated cursor only has four stops.
    if (s_plate && s_vis == 4) {
        const uint32_t shape = vm_read32(s_plate + 0x14);
        storage_write8(shape + 0x10, 255);
        storage_write8(shape + 0x11, 96);
        storage_write8(shape + 0x12, 96);
        storage_write8(shape + 0x13, 255);
    }
    const int game_row = (int)(int32_t)vm_read32(dlg + kCmdRow);
    const float cy = cursor_top(ctx, dlg);
    sync_send_help_blank();

    if (s_text_at && s_label_ptr) storage_write32(s_text_at, s_label_ptr);
    if (step != 0) {
        fprintf(stderr, "[des] storage: row %d (cursor %.0f, game %d)\n", s_vis, cy, game_row);
        if (s_vis == 2 && step > 0) {
            const uint32_t p = pgd();
            const int32_t idx = selected(inventory());
            const Entry e = p ? entry_at(p, idx) : Entry{};
            fprintf(stderr, "[des] storage: Send to Storage, slot %d category %08x id %u x%d\n", idx, e.cat, e.id,
                    e.count);
            const uint32_t live = s_text_at ? vm_read32(s_text_at) : 0;
            char got[24] = {};
            read_text(live, got, (int)sizeof got);
            fprintf(stderr, "[des] storage: label ptr %08x text_at %08x live %08x \"%s\" drawn %d\n", s_label_ptr,
                    s_text_at, live, got,
                    s_line && guest_ok(s_line, 4) ? (int)((vm_read32(s_line + kObjFlags) & kObjDrawn) != 0) : -1);
        }
    }
    /* The red bar follows the game's row onto a fixed slot. Send is the third slot
     * and Reorganize the fourth. Auto Sort is past those four, so the bar stays on
     * the fourth slot until its own highlight is shown, and the confirm puts the
     * game's row back before the original command runs. */
    const char* slots[] = {"cursor", "cursor0_0", "cursor1_0", "cursor2_0", "cursor3_0", "cursor4_0"};
    for (int i = 0; i < 6; i++) {
        const uint32_t obj = find_named(dlg, slots[i]);
        if (!obj) continue;
        const uint32_t flags = vm_read32(obj + kObjFlags);
        const int slot = i - 1; /* "cursor" is the moving bar, not one of the four slots */
        const bool show = i == 0 || slot == s_vis || (s_vis == 4 && slot == 3);
        storage_write32(obj + kObjFlags, show ? (flags | kObjDrawn) : (flags & ~kObjDrawn));
    }
    if (s_mark && guest_ok(s_mark, 4)) {
        const uint32_t flags = vm_read32(s_mark + kObjFlags);
        storage_write32(s_mark + kObjFlags, flags & ~kObjDrawn);
    }
    if (s_vis == 4) {
        for (const char* name : {"cursor3_0", "edge_gradation03"}) {
            const uint32_t obj = find_named(dlg, name);
            float r[4];
            if (!obj || !box_of(ctx, obj, r)) continue;
            const uint32_t shape = vm_read32(obj + 0x14);
            if (!shift_box(ctx, obj, s_gap, false)) {
                if (!nudge_rect(shape, 0x40, r, s_gap, false))
                    nudge_packed(shape, 0x40, r, s_gap, false);
            }
        }
    }
    if (s_have_target && s_gap > 0.0f) {
        const float y = s_drop_y + (s_vis - 1) * s_gap;
        const float h = s_bot > s_top ? s_bot - s_top : 36.0f;
        place_cursor(ctx, dlg, y, y + h);
    }
    /* Confirmation is dispatched only by the native command-choice callback. */
}

extern "C" void des_storage_on_draw(ppu_context* ctx, uint32_t dlg)
{
    uint64_t g[32];
    double f[32];
    uint8_t v[32][16];
    memcpy(g, ctx->gpr, sizeof g);
    memcpy(f, ctx->fpr, sizeof f);
    memcpy(v, ctx->vr, sizeof v);
    const uint64_t lr = ctx->lr, ctr = ctx->ctr, sp = ctx->gpr[1];
    const uint32_t cr = ppu_cr_pack(ctx), xer = ctx->xer;
    s_draw_starts.push_back(s_draw_writes.size());
    s_drawing = true;
    on_draw(ctx, dlg);
    memcpy(ctx->gpr, g, sizeof g);
    memcpy(ctx->fpr, f, sizeof f);
    memcpy(ctx->vr, v, sizeof v);
    ctx->lr = lr;
    ctx->ctr = ctr;
    ctx->gpr[1] = sp;
    ppu_cr_unpack(ctx, cr);
    ctx->xer = xer;
}

extern "C" void des_storage_draw_end() { restore_draw(); }

bool slot_movable(ppu_context* ctx, uint32_t player, int32_t slot)
{
#ifdef PS3_GAME_SPLIT
    // Deposit itself can unequip a weapon. Use the inventory's native
    // equipment/protected-item check before allowing a remote transfer.
    static auto check = des_lift_find("func_008F88D0");
    if (check && call_game(ctx, check, {player + kPgdInv, slot}) == 0) return true;
#endif
    return false;
}

bool drop_greyed(uint32_t dlg)
{
    const uint32_t grey = find_named(dlg, "grayout_drop01");
    return grey && (vm_read32(grey + kObjFlags) & kObjDrawn);
}

bool send_blocked(ppu_context* ctx, uint32_t dlg)
{
    if (drop_greyed(dlg)) return true;
    const uint32_t p = pgd();
    const uint32_t inv = inventory();
    const int32_t slot = inv ? selected(inv) : -1;
    return !p || slot < 0 || !slot_movable(ctx, p, slot);
}

void sync_send_grayout(ppu_context* ctx, uint32_t dlg)
{
    if (!s_grayout || !guest_ok(s_grayout, 4)) return;
    const bool grey = send_blocked(ctx, dlg);
    const uint32_t flags = vm_read32(s_grayout + kObjFlags);
    storage_write32(s_grayout + kObjFlags, grey ? (flags | kObjDrawn) : (flags & ~kObjDrawn));
    static int last_grey = -1;
    if ((int)grey != last_grey) {
        last_grey = (int)grey;
        fprintf(stderr, "[des] storage: send overlay %s (drop grey %d)\n", grey ? "on" : "off", drop_greyed(dlg) ? 1 : 0);
    }
}

bool transferable(ppu_context* ctx, uint32_t player, int32_t slot)
{
    if (slot_movable(ctx, player, slot)) return true;
    fprintf(stderr, "[des] storage: equipped or protected item stays in inventory (slot %d)\n", slot);
    return false;
}

int deposit_inventory(ppu_context* ctx, const InventoryTransfer& pending)
{
    s_forced = 0;
    void (*fn)(ppu_context*) = deposit_fn();
    const uint32_t p = pgd();
    const uint32_t inv = inventory();
    const int32_t idx = inv ? selected(inv) : -1;
    const Entry e = p ? entry_at(p, idx) : Entry{};
    if (p != pending.player || inv != pending.dialog || idx != pending.slot ||
        e.cat != pending.item.cat || e.id != pending.item.id || e.count != pending.item.count ||
        pending.amount < 1 || pending.amount > e.count) {
        fprintf(stderr, "[des] storage: inventory selection changed; transfer cancelled\n");
        return 1;
    }
    const uint32_t box = p ? vm_read32(p + kPgdBox) : 0;
    if (!fn || !p || !guest_ok(box, 4) || idx < 0 || e.count < 1 || e.count > 9999) {
        fprintf(stderr, "[des] storage: refused, box %08x slot %d x%d\n", box, idx, e.count);
        return 1;
    }
    if (!transferable(ctx, p, idx)) return 1;
    if (pending.amount > storage_room(ctx, box, e)) {
        fprintf(stderr, "[des] storage: box has no room for id %u x%d; inventory unchanged\n", e.id, pending.amount);
        return 1;
    }
    const uint32_t ok = call_game(ctx, fn, {box, p + kPgdInv, idx, pending.amount});
    const Entry after = entry_at(p, idx);
    if (ok == 1 && (after.id != e.id || after.count != e.count)) {
        storage_write8(inv + 0x140, 1);
        storage_write8(inv + 0x141, 1);
        s_forced = 0;
        s_navigation.reset();
        call_game(ctx, func_0072D970, {100});
        // The native picker already owns its return transition. Re-entering
        // inventory mode here can leave its old quantity dialog alive.
        fprintf(stderr, "[des] storage: deposited category %08x id %u x%d\n", e.cat, e.id, pending.amount);
    } else {
        fprintf(stderr, "[des] storage: box refused category %08x id %u x%d (return %u, now id %u x%d)\n", e.cat,
                e.id, e.count, ok, after.id, after.count);
    }
    return 1;
}

bool send_row_active()
{
    return s_navigation.send();
}

extern "C" int des_storage_command(ppu_context* ctx)
{
    if (navigation_trace())
        fprintf(stderr, "[storage-nav] confirm tick=%llu draw=%llu dlg=%08x visual=%d native=%d pad=%04x\n",
                (unsigned long long)s_nav_tick, (unsigned long long)s_nav_draw, (uint32_t)ctx->gpr[3], s_vis,
                s_dlg ? (int32_t)vm_read32(s_dlg + kCmdRow) : -1, (unsigned)g_pad0_buttons);
    // A new command choice revokes an Armed request so Use/Drop can open
    // their own picker. Asking/Ready still own message 2000; clearing them
    // here would let that 2000 fall through to native Drop.
    if ((uint32_t)ctx->gpr[3] == s_dlg && s_inventory_transfer.state == InventoryAsk::Armed)
        s_inventory_transfer = {};
    if (!on() || (uint32_t)ctx->gpr[3] != s_dlg || !is_command(s_dlg)) return 0;
    if (s_inventory_transfer.state == InventoryAsk::Asking ||
        s_inventory_transfer.state == InventoryAsk::Ready) return 1;
    sync_navigation("confirm", true);
    const bool send = send_row_active();
    if (s_vis > 2 && !send) {
        const uint32_t command = (uint32_t)s_navigation.dispatch();
        fprintf(stderr, "[des] storage: confirm visual row %d -> native row %u\n", s_vis, command);
        storage_write32(s_dlg + kCmdRow, command);
        return 0;
    }
    if (!send) return 0;
    const uint32_t p = pgd(), dlg = inventory();
    const int32_t slot = dlg ? selected(dlg) : -1;
    const Entry item = p ? entry_at(p, slot) : Entry{};
    if (!p || !dlg || slot < 0 || item.count < 1 || item.count > 9999) return 1;
    if (send_blocked(ctx, s_dlg)) {
        fprintf(stderr, "[des] storage: equipped or protected item stays in inventory (slot %d)\n", slot);
        call_game(ctx, func_0072D970, {200});
        return 1;
    }
    const uint32_t box = vm_read32(p + kPgdBox);
    if (storage_room(ctx, box, item) < 1) {
        fprintf(stderr, "[des] storage: box has no room for id %u; inventory unchanged\n", item.id);
        call_game(ctx, func_0072D970, {200});
        return 1;
    }
    s_inventory_transfer = {InventoryAsk::Armed, p, dlg, slot, item};
    return 1;
}

constexpr uint32_t kMenusRoot = 0x019ADD50u;

uint32_t menus_obj()
{
    const uint32_t t = vm_read32(DES_AT(kMenusRoot));
    const uint32_t h = guest_ok(t, 4) ? vm_read32(t - 0x8000u) : 0;
    const uint32_t m = guest_ok(h, 4) ? vm_read32(h) : 0;
    return guest_ok(m, 4) ? m : 0;
}

void inventory_confirmation_tick(ppu_context* ctx)
{
    auto& pending = s_inventory_transfer;
    if (pending.state == InventoryAsk::Idle) return;
    if (pending.player != pgd() || pending.dialog != inventory() || !on()) {
        pending = {};
        return;
    }
    const uint32_t m = menus_obj();
    if (!m) { pending = {}; return; }
    if (pending.state == InventoryAsk::Armed) {
        if (g_pad0_buttons & 0x6000u) return;
        const int room = storage_room(ctx, vm_read32(pending.player + kPgdBox), pending.item);
        const int maximum = room < pending.item.count ? room : pending.item.count;
        if (maximum < 1) { pending = {}; return; }
        const uint32_t text = guest_text(ctx, "How many items to send to Storage?");
        if (!text) { pending = {}; return; }
        const uint32_t root = vm_read32(DES_AT(0x019ADF0Cu));
        const uint32_t holder = guest_ok(root, 4) ? vm_read32(root - 0x7FFCu) : 0;
        const uint32_t mode = guest_ok(holder, 4) ? vm_read32(holder) : 0;
        call_game(ctx, func_00720F50, {mode == 1 ? 0x85 : 0x95, 1});
        call_game(ctx, func_00720F50, {0x89, 1});
        call_game(ctx, func_00720F50, {0x68, 1});
        call_game(ctx, func_007731F0, {11000, 1, 2, maximum, 1, maximum, 0xEAC4});
        call_game(ctx, func_00720F50, {2, 9});
        call_game(ctx, func_00720F50, {0x68, 1});
        storage_write32(m + 0x820, text);
        pending.picker_text = text;
        pending.closed_ticks = 0;
        call_game(ctx, func_0072D970, {100});
        pending.state = InventoryAsk::Asking;
        fprintf(stderr, "[des] storage: inventory quantity picker for id %u x%d (box room %d, picker max %d)\n",
                pending.item.id, pending.item.count, room, maximum);
        return;
    }
    if (pending.state == InventoryAsk::Ready) {
        const InventoryTransfer transfer = pending;
        pending = {}; // consume authorization before the native operation
        deposit_inventory(ctx, transfer);
        return;
    }
    if (pending.state != InventoryAsk::Asking) return;
    // OK closes the picker (slot 2 leaves 9, prompt text reverts) before
    // inventory message 2000. Clearing Asking on those signals lets native
    // 2000 drop the stack. Only an explicit cancellation releases ownership.
    // Circle can be released before the close transition reaches this tick.
    if (g_pad0_buttons & 0x2000u) pending.cancel_requested = true;
    if ((int32_t)vm_read32(m + 0x14 + 4 * 2) == 9) {
        pending.closed_ticks = 0;
        if (pending.picker_text && vm_read32(m + 0x820) != pending.picker_text)
            storage_write32(m + 0x820, pending.picker_text);
        return;
    }
    pending.closed_ticks++;
    if (pending.closed_ticks == 1)
        fprintf(stderr, "[des] storage: quantity picker left mode 9, waiting for 2000\n");
    if (pending.cancel_requested) {
        pending = {};
        fprintf(stderr, "[des] storage: inventory quantity picker cancelled\n");
        return;
    }
}

// The native quantity picker's OK normally means Drop (inventory message 2000).
// Consume only the picker owned by this storage request, leaving Use/Drop intact.
extern "C" int des_storage_inventory_message(ppu_context* ctx)
{
    auto& pending = s_inventory_transfer;
    if (pending.state != InventoryAsk::Asking || (uint32_t)ctx->gpr[3] != pending.dialog) return 0;
    const int message = (int32_t)ctx->gpr[4];
    if (message != 2000) {
        if (message == 2001 || message == 2004) {
            pending = {};
            fprintf(stderr, "[des] storage: inventory quantity picker cancelled\n");
        }
        return 0;
    }
    const uint32_t m = menus_obj();
    const int amount = m ? (int32_t)vm_read32(m + 0x830) : 0;
    if (m && pending.picker_text && vm_read32(m + 0x820) != pending.picker_text)
        fprintf(stderr, "[des] storage: quantity OK with rewritten prompt, still ours (amount %d)\n", amount);
    if (amount < 1 || amount > pending.item.count) {
        pending = {};
        fprintf(stderr, "[des] storage: inventory quantity confirmed %d, ignored\n", amount);
        return 1;
    }
    pending.amount = amount;
    pending.state = InventoryAsk::Ready;
    storage_write8(pending.dialog + 0x140, 1);
    storage_write8(pending.dialog + 0x141, 1);
    fprintf(stderr, "[des] storage: inventory quantity confirmed %d\n", amount);
    return 1;
}

void ensure_help_blank_text(ppu_context* ctx)
{
    if (!s_help_blank_text) s_help_blank_text = guest_text(ctx, "");
}


void storage_tick(ppu_context* ctx)
{
    ++s_nav_tick;
    sync_navigation("tick");
    if (on()) ensure_help_blank_text(ctx);
    sync_send_help_blank();
    inventory_confirmation_tick(ctx);
    if (!on()) {
        for (auto it = s_shape_bindings.rbegin(); it != s_shape_bindings.rend(); ++it)
            if (vm_read32(it->obj + 0x14) == it->after) vm_write32(it->obj + 0x14, it->before);
        if (!s_shape_bindings.empty()) {
            s_shape_bindings.clear();
            s_layout_writes.clear();
            s_line_state = 0;
        }
        s_forced = 0;
        return;
    }
}
} // namespace

extern "C" void des_storage_tick(ppu_context* ctx) { storage_tick(ctx); }
