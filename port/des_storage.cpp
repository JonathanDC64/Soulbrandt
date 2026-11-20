/* Send to Storage (DES_STORAGE, off unless set).
 *
 * The inventory's command menu is the dialog whose pieces include
 * grayout_drop00 and grayout_drop01: Use and Drop. With the knob on, a line
 * is added under them, labelled "Send to Storage". Choosing it calls the
 * game's own deposit (func_00924BB8): the box at
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
 * menu's own two commands stay where they are. DES_GAME_CALLS resolves Storage
 * functions through verified regional address mappings.
 *
 * All entry points and state below run on the game's main thread. DES_STORAGE_LOG=1
 * prints layout and transfer details; DES_STORAGE_LOG=2 adds navigation traces.
 * Dialog, inventory and PGD offsets were read from the US program and still
 * need separate validation on other discs. */

#include "ps3emu/ppu_lift.h"
#include "des_region.h"
#include "des_game_calls.h"
#include "des_storage.h"
#include "des_storage_navigation.h"
#include "ps3emu/ps3_knob.h"
#include <cmath>
#include <cstdarg>
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

namespace {

constexpr uint32_t kPgdRoot = 0x01B4EF9Cu;
constexpr uint32_t kInventoryMenuSlot = 0x019ADF0Cu; /* TOC slot holding the inventory menu object */
constexpr uint32_t kPgdInv = 0x128u;     /* the inventory object the deposit takes */
constexpr uint32_t kPgdItems = 0x1F4u;   /* the list: +0x0C slots, +0x10 entries of 0x20 */
constexpr uint32_t kPgdItemCount = 0x0Cu;
constexpr uint32_t kPgdItemRecords = 0x10u;
constexpr uint32_t kPgdItemStride = 0x20u;
constexpr uint32_t kEntryCategory = 0x00u, kEntryId = 0x04u, kEntryCount = 0x08u;
constexpr uint32_t kPgdBox = 0x478u;     /* the storage box */
constexpr uint32_t kBoxRecords = 0x0Cu;   /* 2048 records follow the 12-byte header */
constexpr uint32_t kBoxRecordSize = 0x14u;
constexpr uint32_t kBoxRecordCount = 2048u;
constexpr uint32_t kBoxRecordFlags = 0x08u;
constexpr uint32_t kBoxRecordOccupied = 0x00010000u;
constexpr uint32_t kBoxHighWater = 0xA00Cu;
constexpr uint32_t kCmdRow = 0xF4u;
constexpr uint32_t kDlgRow = 0x134u, kDlgItems = 0x138u, kDlgOrder = 0x13Cu;
constexpr uint32_t kDialogActive = 0x94u;
constexpr uint32_t kKids = 0x0Cu, kKidCount = 0x10u;
constexpr uint32_t kObjFlags = 0x18u, kObjRecord = 0x1Cu;
constexpr uint32_t kObjDrawn = 0x40000000u;
constexpr uint32_t kButtonTextPointer = 0x60u;
constexpr uint32_t kRect = 0x74u, kLayout = 0x84u;
constexpr uint32_t kToc = 0x019B5040u;
constexpr uint32_t kMenuLanguage = 0x019D8C88u;
constexpr uint32_t kMenuRoot = 0x019ADD50u;
constexpr uint32_t kMenuOwnerDelta = 0x8000u;
constexpr uint32_t kPickerMenuState = 0x14u;
constexpr uint32_t kPickerSlot = 2u;
constexpr uint32_t kPickerMode = 9u;
constexpr uint32_t kPickerText = 0x820u;
constexpr uint32_t kPickerAmount = 0x830u;
constexpr uint32_t kPickerPromptId = 11000u;
constexpr uint32_t kPickerRangeId = 0xEAC4u;
constexpr uint32_t kPickerSetupDefault = 0x95u, kPickerSetupAlternate = 0x85u;
constexpr uint32_t kPickerSetupSignal = 0x89u, kPickerUpdateSignal = 0x68u;
constexpr uint32_t kInventoryDirtyA = 0x140u;
constexpr uint32_t kInventoryDirtyB = 0x141u;
constexpr uint32_t kMenuHolderDelta = 0x7FFCu;
constexpr uint32_t kInventoryOkMessage = 2000u;
constexpr uint32_t kInventoryCancelMessage = 2002u;
constexpr uint32_t kMenuCancelSlot = 0x4Eu;
constexpr uint32_t kMenuCloseSlot = 0x4Fu;
constexpr uint32_t kMenuReturnState = 11001u;
constexpr uint32_t kDialogInput = 0xA0u;
constexpr uint32_t kDialogInputEnabled = 0x80000000u;

struct StorageText {
    const char16_t* send_label;
    const char16_t* quantity_question;
};
constexpr StorageText kStorageTexts[] = {
    {u"Send to Storage", u"How many items to send to Storage?"},
    {u"Envoyer au stockage", u"Combien d'objets envoyer au stockage ?"},
    {u"Ins Lager senden", u"Wie viele Gegenst\u00E4nde ins Lager senden?"},
    {u"Enviar al almac\u00E9n", u"\u00BFCu\u00E1ntos objetos enviar al almac\u00E9n?"},
    {u"Invia al deposito", u"Quanti oggetti inviare al deposito?"},
    {u"\u5009\u5EAB\u306B\u9001\u308B", u"\u4F55\u500B\u306E\u30A2\u30A4\u30C6\u30E0\u3092\u5009\u5EAB\u306B\u9001\u308A\u307E\u3059\u304B\uFF1F"},
    {u"\u9001\u81F3\u5009\u5EAB", u"\u8981\u9001\u591A\u5C11\u500B\u7269\u54C1\u81F3\u5009\u5EAB\uFF1F"},
    {u"\uCC3D\uACE0\uB85C \uBCF4\uB0B4\uAE30", u"\uC544\uC774\uD15C\uC744 \uBA87 \uAC1C \uCC3D\uACE0\uB85C \uBCF4\uB0BC\uAE4C\uC694?"},
};
uint32_t s_label_ptr = 0, s_quantity_question = 0, s_cursor4_name = 0;
bool s_send_disabled = false;

bool guest_ok(uint32_t a, uint32_t align)
{
    return a >= 0x00010000u && a < 0xD0000000u && (a & (align - 1u)) == 0;
}

// Custom pieces exist only during drawing, outside native update and teardown.
struct DrawWrite { uint32_t at, before, after, mask; unsigned size; };
std::vector<DrawWrite> s_draw_writes;
std::vector<DrawWrite> s_layout_writes;
bool s_drawing = false;
bool s_layout_initializing = false;
bool s_layout_cache_active = false;
struct LayoutBlock { uint32_t at, size; uint64_t used_generation; };
std::vector<LayoutBlock> s_layout_blocks;
uint64_t s_layout_generation = 0;
constexpr size_t kMaxLayoutBlocks = 64;
constexpr size_t kDrawNoop = static_cast<size_t>(-1);
struct OwnedAllocation { uint32_t at, size; };
std::vector<OwnedAllocation> s_owned_allocations;
bool owned_address(uint32_t at, unsigned size)
{
    for (const auto& allocation : s_owned_allocations)
        if (at >= allocation.at && (uint64_t)at + size <= (uint64_t)allocation.at + allocation.size) return true;
    return false;
}
std::vector<size_t> s_draw_starts;
void draw_write(uint32_t at, uint32_t value, unsigned size)
{
    uint32_t old = size == 4 ? vm_read32(at) : size == 2 ? vm_read16(at) : vm_read8(at);
    // New layout data must remain valid after the native renderer queues it.
    // Roll back native bindings and later per-frame edits, never initialization
    // of the storage-owned clone/shape/child arrays to uninitialized bytes.
    const uint32_t mask = size == 4 ? 0xFFFFFFFFu : size == 2 ? 0xFFFFu : 0xFFu;
    if (s_drawing && !(s_layout_initializing && owned_address(at, size)))
        s_draw_writes.push_back({at, old, value, mask, size});
    if (size == 4) vm_write32(at, value);
    else if (size == 2) vm_write16(at, (uint16_t)value);
    else vm_write8(at, (uint8_t)value);
}
void draw_write_flags(uint32_t at, uint32_t value, uint32_t mask)
{
    const uint32_t old = vm_read32(at);
    const uint32_t next = (old & ~mask) | (value & mask);
    if (s_drawing && !(s_layout_initializing && owned_address(at, 4)))
        s_draw_writes.push_back({at, old & mask, next & mask, mask, 4});
    vm_write32(at, next);
}
void storage_write32(uint32_t at, uint32_t value) { draw_write(at, value, 4); }
void storage_write16(uint32_t at, uint16_t value) { draw_write(at, value, 2); }
void storage_write8(uint32_t at, uint8_t value) { draw_write(at, value, 1); }
void storage_write_flags(uint32_t at, uint32_t value, uint32_t mask) { draw_write_flags(at, value, mask); }
void replay_draw_write(const DrawWrite& write)
{
    const uint32_t full_mask = write.size == 4 ? 0xFFFFFFFFu : write.size == 2 ? 0xFFFFu : 0xFFu;
    if (write.mask != full_mask) draw_write_flags(write.at, write.after, write.mask);
    else draw_write(write.at, write.after, write.size);
}
void undo_draw_writes_to(size_t start)
{
    for (size_t i = s_draw_writes.size(); i > start; --i) {
        const DrawWrite& write = s_draw_writes[i - 1];
        const uint32_t full_mask = write.size == 4 ? 0xFFFFFFFFu : write.size == 2 ? 0xFFFFu : 0xFFu;
        if (write.mask != full_mask) {
            const uint32_t current = vm_read32(write.at);
            vm_write32(write.at, (current & ~write.mask) | (write.before & write.mask));
        } else if (write.size == 4) vm_write32(write.at, write.before);
        else if (write.size == 2) vm_write16(write.at, (uint16_t)write.before);
        else vm_write8(write.at, (uint8_t)write.before);
    }
    s_draw_writes.resize(start);
}
void restore_draw()
{
    if (s_draw_starts.empty()) return;
    const size_t start = s_draw_starts.back();
    s_draw_starts.pop_back();
    if (start == kDrawNoop) return;
    undo_draw_writes_to(start);
    s_drawing = false;
    for (const size_t frame : s_draw_starts)
        if (frame != kDrawNoop) { s_drawing = true; break; }
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

int storage_log_level()
{
    static int s_log = 0;
    PS3_KNOB(s_log, ps3_env_int("DES_STORAGE_LOG", 0));
    return s_log < 0 ? 0 : s_log > 2 ? 2 : s_log;
}

bool navigation_trace()
{
    return storage_log_level() >= 2;
}

bool logging()
{
    return storage_log_level() >= 1;
}

void log_detail(const char* format, ...)
{
    if (!logging()) return;
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
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

uint32_t allocate_guest_block(ppu_context* ctx, uint32_t size)
{
    const uint64_t g3 = ctx->gpr[3];
    ctx->gpr[3] = size;
    ds_malloc(ctx);
    const uint32_t address = (uint32_t)ctx->gpr[3];
    ctx->gpr[3] = g3;
    if (!guest_ok(address, 4)) return 0;
    s_owned_allocations.push_back({address, size});
    return address;
}

uint32_t guest_alloc(ppu_context* ctx, uint32_t size)
{
    if (s_layout_cache_active) {
        for (auto& block : s_layout_blocks) {
            if (block.size == size && block.used_generation != s_layout_generation) {
                block.used_generation = s_layout_generation;
                return block.at;
            }
        }
        if (s_layout_blocks.size() >= kMaxLayoutBlocks) return 0;
    }
    const uint32_t address = allocate_guest_block(ctx, size);
    if (address && s_layout_cache_active) s_layout_blocks.push_back({address, size, s_layout_generation});
    return address;
}

uint32_t guest_text(ppu_context* ctx, const char* text)
{
    const uint32_t n = (uint32_t)strlen(text) + 1;
    const uint32_t t = guest_alloc(ctx, 2 * n);
    for (uint32_t i = 0; t && i < n; i++) vm_write16(t + 2 * i, (uint16_t)(uint8_t)text[i]);
    return t;
}

uint32_t guest_text(ppu_context* ctx, const char16_t* text)
{
    uint32_t n = 0;
    while (text[n]) ++n;
    const uint32_t t = guest_alloc(ctx, 2u * (n + 1u));
    for (uint32_t i = 0; t && i <= n; ++i) vm_write16(t + 2u * i, (uint16_t)text[i]);
    return t;
}

const StorageText& storage_text()
{
    const uint32_t language = vm_read32(DES_AT(kMenuLanguage));
    switch (des_region()) {
    case DesRegion::US:
        return language == 0 ? kStorageTexts[5] : kStorageTexts[0];
    case DesRegion::EU:
        switch (language) {
        case 6: return kStorageTexts[2];
        case 7: return kStorageTexts[1];
        case 8: return kStorageTexts[3];
        case 9: return kStorageTexts[4];
        default: return kStorageTexts[0];
        }
    case DesRegion::JP:
        return kStorageTexts[5];
    case DesRegion::ASIA:
        return language == 2 ? kStorageTexts[7] : language == 3 ? kStorageTexts[6] : kStorageTexts[0];
    }
    return kStorageTexts[0];
}

bool ensure_storage_texts(ppu_context* ctx)
{
    const StorageText& text = storage_text();
    if (!s_label_ptr) s_label_ptr = guest_text(ctx, text.send_label);
    if (!s_quantity_question) s_quantity_question = guest_text(ctx, text.quantity_question);
    if (!s_cursor4_name) s_cursor4_name = guest_text(ctx, u"cursor4_0");
    return s_label_ptr && s_quantity_question && s_cursor4_name;
}

uint32_t pgd()
{
    const uint32_t root = vm_read32(DES_AT(kPgdRoot));
    return guest_ok(root, 4) && guest_ok(vm_read32(root + 8), 4) ? vm_read32(root + 8) : 0;
}

uint32_t inventory()
{
    const uint32_t menus = vm_read32(DES_AT(kInventoryMenuSlot));
    const uint32_t holder = guest_ok(menus, 4) ? vm_read32(menus - kMenuOwnerDelta) : 0;
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
bool storage_record_matches(uint32_t box, const Entry& item)
{
    if (!guest_ok(box + kBoxHighWater, 4) || vm_read32(box + kBoxHighWater) > kBoxRecordCount) return false;
    for (uint32_t i = 0; i < kBoxRecordCount; ++i) {
        const uint32_t record = box + kBoxRecords + i * kBoxRecordSize;
        if (!(vm_read32(record + kBoxRecordFlags) & kBoxRecordOccupied)) continue;
        if (vm_read32(record) == item.cat && vm_read32(record + 4) == item.id) return true;
    }
    return false;
}

bool storage_has_free_record(uint32_t box)
{
    if (!guest_ok(box + kBoxHighWater, 4) || vm_read32(box + kBoxHighWater) > kBoxRecordCount) return false;
    for (uint32_t i = 0; i < kBoxRecordCount; ++i) {
        const uint32_t record = box + kBoxRecords + i * kBoxRecordSize;
        if (!(vm_read32(record + kBoxRecordFlags) & kBoxRecordOccupied)) return true;
    }
    return false;
}

int storage_count(ppu_context* ctx, uint32_t box, const Entry& item)
{
    if (!guest_ok(box, 4)) return -1;
    return (int32_t)call_game(ctx, func_00924D58, {box, item.cat, item.id});
}

int storage_room(ppu_context* ctx, uint32_t box, const Entry& item)
{
    static uint32_t probe = 0;
    if (s_send_disabled || !guest_ok(box, 4)) return 0;
    const int held = storage_count(ctx, box, item);
    if (held < 0) return 0;
    if (held == 0 && !storage_record_matches(box, item) && !storage_has_free_record(box)) return 0;
    // The limit helper reads only the selected record's item key. Give it a
    // private one-record view rather than changing any live box entry.
    if (!probe) probe = guest_alloc(ctx, 32);
    if (!probe) return 0;
    for (uint32_t i = 0; i < 32; i += 4) vm_write32(probe + i, 0);
    vm_write32(probe + 12, item.cat | item.id);
    const int maximum = (int32_t)call_game(ctx, func_00923FD8, {probe, 0});
    if (maximum <= 0 || held < 0 || held >= maximum) return 0;
    return maximum - held;
}

Entry entry_at(uint32_t p, int32_t idx)
{
    Entry e;
    const uint32_t list = p + kPgdItems, at = vm_read32(list + kPgdItemRecords);
    if (idx < 0 || idx >= (int32_t)vm_read32(list + kPgdItemCount) || !guest_ok(at, 4)) return e;
    const uint32_t q = at + kPgdItemStride * (uint32_t)idx;
    e.cat = vm_read32(q + kEntryCategory);
    e.id = vm_read32(q + kEntryId);
    e.count = (int32_t)vm_read32(q + kEntryCount);
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
void restore_shape_bindings_to(size_t count)
{
    while (s_shape_bindings.size() > count) {
        const ShapeBinding binding = s_shape_bindings.back();
        s_shape_bindings.pop_back();
        if (guest_ok(binding.obj, 4) && vm_read32(binding.obj + 0x14) == binding.after)
            vm_write32(binding.obj + 0x14, binding.before);
    }
}

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

bool shape_has_matching_rect(ppu_context* ctx, uint32_t obj, uint32_t shape)
{
    const uint32_t child = guest_ok(shape, 4) ? vm_read32(shape + 0x10) : 0;
    if (!guest_ok(child, 4)) return false;
    float bounds[4];
    if (!box_of(ctx, obj, bounds)) return false;
    for (unsigned i = 0; i < 4; ++i) {
        const float value = vm_readf(child + kRect + i * 4);
        if (!std::isfinite(value) || std::fabs(value - bounds[i]) > 2.0f) return false;
    }
    return true;
}

/* The menu file shares shape blocks between screens. Move a copy, or the item list moves too. */
bool own_shape(ppu_context* ctx, uint32_t obj)
{
    if (!copy_shape(ctx, obj)) return false;
    const uint32_t shape = vm_read32(obj + 0x14);
    if (!shape_has_matching_rect(ctx, obj, shape)) return true;
    const uint32_t child = vm_read32(shape + 0x10);
    const uint32_t child2 = guest_alloc(ctx, 0x100);
    if (!child2) return false;
    copy_bytes(child2, child, 0x100);
    storage_write32(shape + 0x10, child2);
    return true;
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
float s_top = 0, s_bot = 0, s_drop_y = 0, s_gap = 0;
DesStorageNavigation s_navigation;
int s_visible_row() { return s_navigation.visible(); }
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
    bool cancel_when_closed = false;
};
InventoryTransfer s_inventory_transfer;
bool player_inventory_menu();
void sync_send_help_blank()
{
    // The two help slots are shared with every command menu, including Reclaim.
    // Blank them only while Send is the player's own row or its quantity picker
    // is still live. A blank this function installed is put back when that
    // ends; a slot holding anything else is left alone, so a stale Asking
    // cannot empty Reclaim's footer.
    const bool inventory = player_inventory_menu();
    const bool dialog_active = on() && s_dlg && vm_read8(s_dlg + kDialogActive);
    const bool active = inventory && dialog_active;
    const bool live_picker = inventory && s_inventory_transfer.state == InventoryAsk::Asking &&
                             !s_inventory_transfer.cancel_when_closed;
    const bool blank = (active && s_forced) || live_picker;
    const uint32_t blank_offset = s_help_blank_text - s_help_bank;
    for (unsigned i = 0; i < 2; ++i) {
        if (!s_help_slots[i]) continue;
        const uint32_t current = vm_read32(s_help_slots[i]);
        const bool ours = s_help_blank_text && current == blank_offset;
        if (!blank && !active && !ours) continue;
        const uint32_t offset = blank ? blank_offset :
            active && s_visible_row() >= 3 ? s_help_offsets[s_visible_row() == 3 ? 0 : 1] : s_help_offsets[i];
        vm_write32(s_help_slots[i], offset);
    }
    static int logged_row = -2;
    const int row = live_picker && !active ? -3 : active ? s_visible_row() : -1;
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
    log_detail("[des] storage: one-line help FMG category 80, sort entries %d/%d\n",
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

struct StorageLayoutPlan {
    uint32_t button = 0, last_plate = 0, drop_grey = 0, autos = 0, cursor = 0;
    float gap = 0, top = 0, bottom = 0;
    std::vector<uint32_t> allocations;
};

bool add_shape_allocations(ppu_context* ctx, StorageLayoutPlan& plan, uint32_t obj)
{
    if (!obj) return true;
    const uint32_t shape = vm_read32(obj + 0x14);
    if (!guest_ok(shape, 4)) return false;
    plan.allocations.push_back(0x40);
    if (shape_has_matching_rect(ctx, obj, shape)) plan.allocations.push_back(0x100);
    return true;
}

bool prepare_storage_layout(ppu_context* ctx, uint32_t dlg, StorageLayoutPlan* plan)
{
    if (!row_gap(ctx, dlg, &plan->gap, &plan->top, &plan->bottom)) return false;
    plan->button = find_named(dlg, "button1_0");
    plan->last_plate = find_named(dlg, "plate0_3");
    plan->drop_grey = find_named(dlg, "grayout_drop01");
    plan->autos = find_named(dlg, "button3_0");
    plan->cursor = find_named(dlg, "cursor3_0");
    const uint32_t record = plan->button ? vm_read32(plan->button + kObjRecord) : 0;
    const uint32_t cursor_record = plan->cursor ? vm_read32(plan->cursor + kObjRecord) : 0;
    const uint32_t text = plan->button ? vm_read32(plan->button + kButtonTextPointer) : 0;
    if (!plan->button || !guest_ok(record, 4) || !guest_ok(text, 2) ||
        !plan->autos || !plan->cursor || !guest_ok(cursor_record, 4)) return false;

    for (const char* name : {"cursor", "cursor3_0", "edge_gradation03", "button2_0", "button3_0", "BG", "R03_frame"})
        if (!add_shape_allocations(ctx, *plan, find_named(dlg, name))) return false;
    plan->allocations.push_back(0x120); // cloned Drop button
    plan->allocations.push_back(0x80);  // cloned Drop record
    if (!add_shape_allocations(ctx, *plan, plan->button)) return false;
    if (plan->last_plate) {
        plan->allocations.push_back(0x120);
        if (!add_shape_allocations(ctx, *plan, plan->last_plate)) return false;
    }
    if (plan->drop_grey) {
        plan->allocations.push_back(0x120);
        if (!add_shape_allocations(ctx, *plan, plan->drop_grey)) return false;
    }
    plan->allocations.push_back(0x120); // Auto Sort label clone
    if (!add_shape_allocations(ctx, *plan, plan->autos)) return false;
    plan->allocations.push_back(0x120); // fifth cursor spot
    plan->allocations.push_back(0x80);  // its cloned record
    if (!add_shape_allocations(ctx, *plan, plan->cursor)) return false;

    const uint32_t appends = 3u + (plan->last_plate ? 1u : 0u) + (plan->drop_grey ? 1u : 0u);
    const uint32_t children = vm_read32(dlg + kKidCount);
    if (children + appends > 64u) return false;
    for (uint32_t i = 0; i < appends; ++i) plan->allocations.push_back((children + 2u + i) * 4u);
    return true;
}

bool reserve_layout_allocations(ppu_context* ctx, const std::vector<uint32_t>& sizes)
{
    for (size_t i = 0; i < sizes.size(); ++i) {
        bool seen = false;
        size_t needed = 0, available = 0;
        for (size_t j = 0; j < sizes.size(); ++j) {
            if (sizes[j] == sizes[i]) {
                if (j < i) seen = true;
                ++needed;
            }
        }
        if (seen) continue;
        for (const auto& block : s_layout_blocks)
            if (block.size == sizes[i] && block.used_generation != s_layout_generation) ++available;
        while (available < needed) {
            if (s_layout_blocks.size() >= kMaxLayoutBlocks) return false;
            const uint32_t address = allocate_guest_block(ctx, sizes[i]);
            if (!address) return false;
            s_layout_blocks.push_back({address, sizes[i], s_layout_generation - 1});
            ++available;
        }
    }
    return true;
}

bool prepare_native_layout(ppu_context* ctx, uint32_t dlg, const StorageLayoutPlan& plan, int* shifted)
{
    for (const char* name : {"cursor", "cursor3_0", "edge_gradation03"}) {
        const uint32_t obj = find_named(dlg, name);
        if (obj && !own_shape(ctx, obj)) return false;
    }
    for (const char* name : {"button2_0", "button3_0"}) {
        const uint32_t obj = find_named(dlg, name);
        if (!obj) continue;
        if (!own_shape(ctx, obj)) return false;
        if (move_shape(obj, (int)plan.gap, false)) ++*shifted;
    }
    // The panel rect is four floats, unlike the buttons' packed halves.
    for (const char* name : {"BG", "R03_frame"}) {
        const uint32_t obj = find_named(dlg, name);
        if (!obj) continue;
        if (!own_shape(ctx, obj)) return false;
        if (shift_box(ctx, obj, plan.gap, true)) ++*shifted;
    }
    return true;
}

bool build_send_line(ppu_context* ctx, const StorageLayoutPlan& plan, uint32_t* line, int* moved)
{
    const uint32_t button = plan.button;
    const uint32_t rec = vm_read32(button + kObjRecord);
    const uint32_t clone = guest_alloc(ctx, 0x120);
    const uint32_t rec2 = guest_ok(rec, 4) ? guest_alloc(ctx, 0x80) : 0;
    if (!clone || (guest_ok(rec, 4) && !rec2)) return false;
    copy_bytes(clone, button, 0x120);
    if (rec2) {
        copy_bytes(rec2, rec, 0x80);
        storage_write32(clone + kObjRecord, rec2);
    }
    float from[4];
    if (!box_of(ctx, button, from)) {
        from[1] = plan.top - plan.gap;
        from[3] = plan.bottom - plan.gap;
    }
    if (!own_shape(ctx, clone)) return false;
    *moved = move_shape(clone, (int)plan.gap, false) ? 2 : 0;

    // The structural pointer is at button+0x60. Rebind it in the copied shape
    // too when that field resides inside the button's embedded shape block.
    const uint32_t source_shape = vm_read32(button + 0x14);
    const uint32_t source_text_at = button + kButtonTextPointer;
    s_text_at = source_text_at >= source_shape && source_text_at + 4 <= source_shape + 0x40u
        ? vm_read32(clone + 0x14) + source_text_at - source_shape : clone + kButtonTextPointer;
    storage_write32(clone + kButtonTextPointer, s_label_ptr);
    storage_write32(s_text_at, s_label_ptr);
    storage_write_flags(clone + kObjFlags, kObjDrawn, kObjDrawn);

    float placed[4] = {};
    const bool placed_ok = box_of(ctx, clone, placed);
    log_detail("[des] storage: clone box %d %.0f,%.0f,%.0f,%.0f (drop %.0f)\n", placed_ok ? 1 : 0,
               placed[0], placed[1], placed[2], placed[3], from[1]);
    if (placed_ok) { s_top = placed[1]; s_bot = placed[3]; }
    if (!placed_ok || std::fabs(placed[1] - from[1]) < 2.0f) {
        const uint32_t shape = vm_read32(button + 0x14);
        log_detail("[des] storage: the new line did not move, shape %08x\n", shape);
        if (guest_ok(shape, 4)) {
            log_detail("[des] storage: shape");
            for (uint32_t off = 0; off < 0x30u; off += 4) {
                const float value = vm_readf(shape + off);
                const uint32_t word = vm_read32(shape + off);
                if (std::isfinite(value) && std::fabs(value) > 1.0f && std::fabs(value) < 2000.0f)
                    log_detail(" %u:f%.1f", off, value);
                else log_detail(" %u:%08x", off, word);
            }
            log_detail("\n");
        }
        return false;
    }
    *line = clone;
    return true;
}

bool append_extra_plate(ppu_context* ctx, uint32_t dlg, const StorageLayoutPlan& plan, uint32_t* plate)
{
    if (!plan.last_plate) return true;
    const uint32_t clone = guest_alloc(ctx, 0x120);
    if (!clone) return false;
    copy_bytes(clone, plan.last_plate, 0x120);
    if (!own_shape(ctx, clone) || !shift_box(ctx, clone, plan.gap, false) || !append_kid(ctx, dlg, clone))
        return false;
    *plate = clone;
    return true;
}

bool append_send_grayout(ppu_context* ctx, uint32_t dlg, const StorageLayoutPlan& plan, uint32_t* grayout)
{
    if (!plan.drop_grey) return true;
    const uint32_t clone = guest_alloc(ctx, 0x120);
    float from[4] = {};
    if (!clone || !box_of(ctx, plan.drop_grey, from)) return false;
    copy_bytes(clone, plan.drop_grey, 0x120);
    if (!own_shape(ctx, clone)) return false;
    const uint32_t shape = vm_read32(clone + 0x14);
    const bool moved = move_shape(clone, (int)plan.gap, false) || shift_box(ctx, clone, plan.gap, false) ||
                       nudge_rect(shape, 0x40, from, plan.gap, false) ||
                       nudge_packed(shape, 0x40, from, plan.gap, false);
    if (!moved || !append_kid(ctx, dlg, clone)) return false;
    *grayout = clone;
    return true;
}

bool append_auto_label(ppu_context* ctx, uint32_t dlg, const StorageLayoutPlan& plan)
{
    const uint32_t clone = guest_alloc(ctx, 0x120);
    if (!clone) return false;
    copy_bytes(clone, plan.autos, 0x120);
    if (!own_shape(ctx, clone)) return false;
    storage_write_flags(clone + kObjFlags, kObjDrawn, kObjDrawn);
    return append_kid(ctx, dlg, clone);
}

bool append_auto_cursor(ppu_context* ctx, uint32_t dlg, const StorageLayoutPlan& plan, uint32_t* mark)
{
    const uint32_t clone = guest_alloc(ctx, 0x120);
    const uint32_t record = guest_alloc(ctx, 0x80);
    const uint32_t source_record = vm_read32(plan.cursor + kObjRecord);
    if (!clone || !record || !s_cursor4_name || !guest_ok(source_record, 4)) return false;
    float bounds[4];
    if (!box_of(ctx, plan.cursor, bounds)) return false;
    copy_bytes(clone, plan.cursor, 0x120);
    copy_bytes(record, source_record, 0x80);
    storage_write32(clone + kObjRecord, record);
    storage_write32(record, s_cursor4_name);
    if (!own_shape(ctx, clone)) return false;
    const uint32_t shape = vm_read32(clone + 0x14);
    const bool moved = nudge_rect(clone, 0x120, bounds, plan.gap, false) ||
                       nudge_rect(shape, 0x40, bounds, plan.gap, false) ||
                       nudge_packed(shape, 0x40, bounds, plan.gap, false) ||
                       shift_box(ctx, clone, plan.gap, false);
    if (!moved || !append_kid(ctx, dlg, clone)) return false;
    *mark = clone;
    return true;
}

bool ensure_line(ppu_context* ctx, uint32_t dlg)
{
    static uint32_t native_kids = 0, native_count = 0;
    if (s_dlg != dlg || native_kids != vm_read32(dlg + kKids) || native_count != vm_read32(dlg + kKidCount)) {
        restore_shape_bindings_to(0);
        native_kids = vm_read32(dlg + kKids);
        native_count = vm_read32(dlg + kKidCount);
        s_layout_writes.clear();
        s_navigation.reset();
        s_forced = 0;
        s_dlg = dlg;
        s_line = s_mark = s_plate = s_grayout = s_text_at = 0;
        /* keep s_label_ptr: the UTF-16 lives outside draw_writes and is reused */
        s_line_state = 0;
        s_have_target = 0;
    }
    if (s_line_state == 1) {
        for (const auto& write : s_layout_writes) replay_draw_write(write);
        return true;
    }
    if (s_line_state < 0) return false;
    if (!ensure_storage_texts(ctx)) {
        s_line_state = -1;
        log_detail("[des] storage: no guest memory for localized text\n");
        return false;
    }

    const size_t layout_start = s_draw_writes.size();
    const size_t binding_start = s_shape_bindings.size();
    ++s_layout_generation;
    struct LayoutCacheScope {
        bool before = s_layout_cache_active;
        LayoutCacheScope() { s_layout_cache_active = true; }
        ~LayoutCacheScope() { s_layout_cache_active = before; }
    } cache_scope;
    struct LayoutTransaction {
        size_t draw_start, binding_start;
        bool committed = false;
        ~LayoutTransaction() {
            if (committed) return;
            undo_draw_writes_to(draw_start);
            restore_shape_bindings_to(binding_start);
            s_layout_writes.clear();
            s_line = s_mark = s_plate = s_grayout = s_text_at = 0;
            s_have_target = 0;
            s_line_state = -1;
        }
    } transaction{layout_start, binding_start};
    struct InitializingLayout {
        bool before = s_layout_initializing;
        InitializingLayout() { s_layout_initializing = true; }
        ~InitializingLayout() { s_layout_initializing = before; }
    } initializing;
    const auto fail = []() { s_line_state = -1; return false; };

    log_pieces(ctx, dlg);
    StorageLayoutPlan plan;
    if (!prepare_storage_layout(ctx, dlg, &plan)) {
        log_detail("[des] storage: command menu does not match the supported layout\n");
        return fail();
    }
    if (!reserve_layout_allocations(ctx, plan.allocations)) {
        log_detail("[des] storage: could not reserve menu layout memory\n");
        return fail();
    }
    ensure_help_bank(ctx);
    s_drop_y = plan.top - plan.gap;
    s_gap = plan.gap;
    s_top = plan.top;
    s_bot = plan.bottom;
    s_have_target = 1;

    int shifted = 0, moved = 0;
    uint32_t line = 0;
    if (!prepare_native_layout(ctx, dlg, plan, &shifted) ||
        !build_send_line(ctx, plan, &line, &moved)) return fail();
    if (!append_extra_plate(ctx, dlg, plan, &s_plate) || !append_kid(ctx, dlg, line) ||
        !append_send_grayout(ctx, dlg, plan, &s_grayout) || !append_auto_label(ctx, dlg, plan)) return fail();
    s_line = line;
    if (!append_auto_cursor(ctx, dlg, plan, &s_mark)) return fail();

    float panel[4] = {};
    const uint32_t bg = find_named(dlg, "BG");
    if (bg && box_of(ctx, bg, panel))
        log_detail("[des] storage: panel %.0f,%.0f,%.0f,%.0f\n", panel[0], panel[1], panel[2], panel[3]);
    s_layout_writes.assign(s_draw_writes.begin() + layout_start, s_draw_writes.end());
    s_line_state = 1;
    transaction.committed = true;

    float line_box[4] = {};
    const bool got_line_box = box_of(ctx, line, line_box);
    log_detail("[des] storage: line added under Drop (moved %d, rows %d, dy %.0f, box %d %.0f,%.0f,%.0f,%.0f)\n",
               moved, shifted, plan.gap, got_line_box ? 1 : 0, line_box[0], line_box[1], line_box[2], line_box[3]);
    if (s_grayout) {
        float grey_box[4] = {};
        const bool got_grey_box = box_of(ctx, s_grayout, grey_box);
        log_detail("[des] storage: send grayout %d %.0f,%.0f,%.0f,%.0f\n", got_grey_box ? 1 : 0, grey_box[0],
                   grey_box[1], grey_box[2], grey_box[3]);
    }
    return true;
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
            log_detail("[des] storage: the cursor has no position to move\n");
        }
        return;
    }
}

void hide_obj(uint32_t obj)
{
    if (!obj || !guest_ok(obj, 4)) return;
    storage_write_flags(obj + kObjFlags, 0, kObjDrawn);
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
    storage_write_flags(s_line + kObjFlags, kObjDrawn, kObjDrawn);
}

bool send_blocked(ppu_context* ctx, uint32_t dlg);
void sync_send_grayout(ppu_context* ctx, uint32_t dlg);

bool command_open()
{
    return s_dlg && guest_ok(s_dlg, 4) && is_command(s_dlg) && vm_read8(s_dlg + kDialogActive) != 0 &&
           (vm_read32(s_dlg + kDialogInput) & kDialogInputEnabled) != 0;
}

bool player_inventory_menu()
{
    // func_0081E110's mode word, *(menus - 0x7FFC). Measured on the US copy:
    // the player's item command menu is 0. Thomas's Reclaim command menu is 1
    // and resolves that row through the player's inventory, a different item.
    const uint32_t root = vm_read32(DES_AT(kInventoryMenuSlot));
    const uint32_t holder = guest_ok(root, 4) ? vm_read32(root - kMenuHolderDelta) : 0;
    return guest_ok(holder, 4) && vm_read32(holder) == 0;
}

int sync_navigation(const char* phase, bool confirming = false)
{
    // Native dispatch may hide the command before invoking its callback. Keep
    // that callback's established selection until it has consumed the action.
    const bool dispatching = confirming && s_navigation.active() && s_dlg &&
                             guest_ok(s_dlg, 4) && is_command(s_dlg);
    if (!on() || !player_inventory_menu() || (!command_open() && !dispatching)) {
        s_navigation.reset();
        s_forced = 0;
        return 0;
    }
    const int native = (int32_t)vm_read32(s_dlg + kCmdRow);
    if (native < 0 || native > 3) return 0;
    const int previous = s_visible_row(), expected = s_navigation.expected;
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
                native, expected, previous, s_visible_row(), step);
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
    if (!guest_ok(dlg, 4)) return;
    static int s_alive = 0;
    if (!s_alive) {
        s_alive = 1;
        log_detail("[des] storage: draw hook\n");
    }
    if (!is_command(dlg) || vm_read8(dlg + kDialogActive) == 0) return;
    if (!player_inventory_menu()) {
        // Reclaim uses this command shape in mode 1. Do not add Send, do not
        // adopt that dialog, and do not drop a quantity picker already open.
        s_navigation.reset();
        s_forced = 0;
        return;
    }
    if (!ensure_line(ctx, dlg)) return;
    const int step = sync_navigation("draw");
    show_line();
    sync_send_grayout(ctx, dlg);
    // The fifth plate owns its shape. Tint its textured fill for Auto Sort;
    // the native animated cursor only has four stops.
    if (s_plate && s_visible_row() == 4) {
        const uint32_t shape = vm_read32(s_plate + 0x14);
        storage_write8(shape + 0x10, 255);
        storage_write8(shape + 0x11, 96);
        storage_write8(shape + 0x12, 96);
        storage_write8(shape + 0x13, 255);
    }
    sync_send_help_blank();

    if (s_text_at && s_label_ptr) storage_write32(s_text_at, s_label_ptr);
    if (step != 0)
        log_detail("[des] storage: selected visual row %d after native step %d\n", s_visible_row(), step);
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
        const bool show = i == 0 || slot == s_visible_row() || (s_visible_row() == 4 && slot == 3);
        storage_write_flags(obj + kObjFlags, show ? (flags | kObjDrawn) : (flags & ~kObjDrawn), kObjDrawn);
    }
    if (s_mark && guest_ok(s_mark, 4)) {
        const uint32_t flags = vm_read32(s_mark + kObjFlags);
        storage_write_flags(s_mark + kObjFlags, flags & ~kObjDrawn, kObjDrawn);
    }
    if (s_visible_row() == 4) {
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
        const float y = s_drop_y + (s_visible_row() - 1) * s_gap;
        const float h = s_bot > s_top ? s_bot - s_top : 36.0f;
        place_cursor(ctx, dlg, y, y + h);
    }
    /* Confirmation is dispatched only by the native command-choice callback. */
}

extern "C" void des_storage_on_draw(ppu_context* ctx, uint32_t dlg)
{
    // The HUD drains draw_end even when this feature does no work. Record a
    // no-op frame so that an early return stays paired with that drain, also
    // when another storage draw is already nested on the main thread.
    if (!on() && s_line_state != 1) {
        s_draw_starts.push_back(kDrawNoop);
        return;
    }
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
    // Deposit itself can unequip a weapon. Use the inventory's native
    // equipment/protected-item check before allowing a remote transfer.
    return call_game(ctx, func_008F88D0, {player + kPgdInv, slot}) == 0;
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
    const bool grey = s_send_disabled || send_blocked(ctx, dlg);
    const uint32_t flags = vm_read32(s_grayout + kObjFlags);
    storage_write_flags(s_grayout + kObjFlags, grey ? (flags | kObjDrawn) : (flags & ~kObjDrawn), kObjDrawn);
    static int last_grey = -1;
    if ((int)grey != last_grey) {
        last_grey = (int)grey;
        log_detail("[des] storage: send overlay %s (drop grey %d)\n", grey ? "on" : "off",
                   drop_greyed(dlg) ? 1 : 0);
    }
}

bool transferable(ppu_context* ctx, uint32_t player, int32_t slot)
{
    if (slot_movable(ctx, player, slot)) return true;
    fprintf(stderr, "[des] storage: equipped or protected item stays in inventory (slot %d)\n", slot);
    return false;
}

int inventory_count_delta(const Entry& before, const Entry& after)
{
    const int after_count = after.cat == before.cat && after.id == before.id ? after.count : 0;
    if (after_count < 0) return -1; // invalid state must fail verification without signed overflow
    return before.count - after_count;
}

enum class DepositDelta { Success, Refused, Mismatch };

DepositDelta verify_deposit_deltas(int native_result, int inventory_delta, int box_delta, int amount)
{
    if (native_result == 1 && inventory_delta == amount && box_delta == amount)
        return DepositDelta::Success;
    if (native_result == 0 && inventory_delta == 0 && box_delta == 0)
        return DepositDelta::Refused;
    return DepositDelta::Mismatch;
}

int deposit_inventory(ppu_context* ctx, const InventoryTransfer& pending)
{
    s_forced = 0;
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
    if (!p || !guest_ok(box, 4) || idx < 0 || e.count < 1 || e.count > 9999) {
        fprintf(stderr, "[des] storage: refused, box %08x slot %d x%d\n", box, idx, e.count);
        return 1;
    }
    if (!transferable(ctx, p, idx)) return 1;
    if (pending.amount > storage_room(ctx, box, e)) {
        fprintf(stderr, "[des] storage: box has no room for id %u x%d; inventory unchanged\n", e.id, pending.amount);
        return 1;
    }
    const int box_before = storage_count(ctx, box, e);
    if (box_before < 0) {
        s_send_disabled = true;
        fprintf(stderr, "[des] storage: cannot verify box count; Send disabled for this session\n");
        return 1;
    }
    const uint32_t result = call_game(ctx, func_00924BB8, {box, p + kPgdInv, idx, pending.amount});
    const Entry after = entry_at(p, idx);
    const int box_after = storage_count(ctx, box, e);
    const int inv_delta = inventory_count_delta(e, after);
    const int box_delta = box_after < 0 ? -1 : box_after - box_before;
    const DepositDelta outcome = verify_deposit_deltas((int)result, inv_delta, box_delta, pending.amount);
    if (outcome == DepositDelta::Mismatch) {
        s_send_disabled = true;
        fprintf(stderr,
                "[des] storage: deposit mismatch category %08x id %u requested %d result %u inventory_delta %d box_delta %d; Send disabled for this session\n",
                e.cat, e.id, pending.amount, result, inv_delta, box_delta);
        return 1;
    }
    if (outcome == DepositDelta::Refused) {
        fprintf(stderr, "[des] storage: box refused category %08x id %u x%d; inventory unchanged\n", e.cat, e.id,
                pending.amount);
        return 1;
    }
    vm_write8(inv + kInventoryDirtyA, 1);
    vm_write8(inv + kInventoryDirtyB, 1);
    s_forced = 0;
    s_navigation.reset();
    call_game(ctx, func_0072D970, {100});
    fprintf(stderr, "[des] storage: deposited category %08x id %u x%d\n", e.cat, e.id, pending.amount);
    return 1;
}

bool send_row_active()
{
    return s_navigation.send();
}

uint32_t menus_obj();

uint32_t menu_slot_value(uint32_t menus, uint32_t slot)
{
    return guest_ok(menus, 4) ? vm_read32(menus + kPickerMenuState + 4u * slot) : 0;
}

bool quantity_picker_open(uint32_t menus)
{
    return menu_slot_value(menus, kPickerSlot) == kPickerMode;
}

bool command_dialog_active(uint32_t dialog)
{
    // Dispatch clears the input bit before the choice callback, so command_open()
    // is false for the press that must release the picker. The active byte is
    // the game's own "this command dialog has the player again".
    return dialog && dialog == s_dlg && guest_ok(dialog, 4) && is_command(dialog) &&
           vm_read8(dialog + kDialogActive) != 0;
}

extern "C" int des_storage_command(ppu_context* ctx)
{
    if (navigation_trace())
        fprintf(stderr, "[storage-nav] confirm tick=%llu draw=%llu dlg=%08x visual=%d native=%d pad=%04x\n",
                (unsigned long long)s_nav_tick, (unsigned long long)s_nav_draw, (uint32_t)ctx->gpr[3], s_visible_row(),
                s_dlg ? (int32_t)vm_read32(s_dlg + kCmdRow) : -1, (unsigned)g_pad0_buttons);
    const bool in_player_inventory = player_inventory_menu();
    const uint32_t dialog = (uint32_t)ctx->gpr[3];
    // Armed and Ready are not modal. Outside mode 0 they must not stay armed
    // for a later inventory tick, and the native choice still runs.
    if (!in_player_inventory && (s_inventory_transfer.state == InventoryAsk::Armed ||
                                 s_inventory_transfer.state == InventoryAsk::Ready)) {
        s_inventory_transfer = {};
        return 0;
    }
    // A new command choice revokes an Armed request so Use/Drop can open
    // their own picker. The native command callback proves that input has
    // returned; retire the old owner only after the picker is no longer open.
    if (dialog == s_dlg && s_inventory_transfer.state == InventoryAsk::Armed && in_player_inventory)
        s_inventory_transfer = {};
    if (s_inventory_transfer.state == InventoryAsk::Asking) {
        // Message 2000/2002 still decide a picker that is modal. A later choice
        // on the player's command dialog means that picker already closed. EU/fr
        // cancel does that without message 2002 and can leave slot 2 at mode 9,
        // so the return slots alone never released ownership and the next Send
        // was swallowed. Reclaim is mode 1: it is not that choice, so the open
        // picker stays owned and the native action is not swallowed.
        if (command_dialog_active(dialog) && in_player_inventory) {
            s_inventory_transfer = {};
            log_detail("[des] storage: inventory quantity picker cancelled by native state\n");
        } else if (!in_player_inventory) {
            // Mark it before returning to mode 0. A tick is not required: the
            // next OK is consumed and cancelled, and the help blank is ours
            // to put back now.
            s_inventory_transfer.cancel_when_closed = true;
            sync_send_help_blank();
            return 0;
        } else return 1;
    }
    if (s_inventory_transfer.state == InventoryAsk::Ready) {
        if (!on()) s_inventory_transfer = {};
        else return 1;
    }
    if (!on() || !in_player_inventory || dialog != s_dlg || !is_command(s_dlg)) return 0;
    sync_navigation("confirm", true);
    const bool send = send_row_active();
    if (s_visible_row() > 2 && !send) {
        const int visual = s_visible_row();
        const uint32_t command = (uint32_t)s_navigation.dispatch();
        if (navigation_trace())
            fprintf(stderr, "[storage-nav] confirm visual row %d -> native row %u\n", visual, command);
        vm_write32(s_dlg + kCmdRow, command);
        return 0;
    }
    if (!send) return 0;
    if (s_send_disabled) {
        fprintf(stderr, "[des] storage: Send disabled after a deposit mismatch\n");
        call_game(ctx, func_0072D970, {200});
        return 1;
    }
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

uint32_t menus_obj()
{
    const uint32_t t = vm_read32(DES_AT(kMenuRoot));
    const uint32_t h = guest_ok(t, 4) ? vm_read32(t - kMenuOwnerDelta) : 0;
    const uint32_t m = guest_ok(h, 4) ? vm_read32(h) : 0;
    return guest_ok(m, 4) ? m : 0;
}

void inventory_confirmation_tick(ppu_context* ctx)
{
    auto& pending = s_inventory_transfer;
    if (pending.state == InventoryAsk::Idle) return;
    if (pending.state == InventoryAsk::Asking) {
        // The measured inventory quantity picker stays in mode 0. Any other
        // mode means this request is no longer the player's item menu: keep
        // the picker owned so its OK is still consumed, and cancel the transfer.
        if (!on() || pending.player != pgd() || pending.dialog != inventory() || !player_inventory_menu())
            pending.cancel_when_closed = true;
        // A cancelled request still owns OK, but the open picker may now be
        // Reclaim's. Do not replace that prompt with the Send question.
        const uint32_t menus = menus_obj();
        if (!pending.cancel_when_closed && quantity_picker_open(menus) && on() && pending.picker_text &&
            vm_read32(menus + kPickerText) != pending.picker_text)
            vm_write32(menus + kPickerText, pending.picker_text);
        return; // Keep ownership until native OK/CANCEL or a new command input.
    }
    // Armed would open a picker and Ready would deposit. Neither may run
    // after the menu leaves mode 0.
    if (pending.player != pgd() || pending.dialog != inventory() || !on() || !player_inventory_menu()) {
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
        if (!ensure_storage_texts(ctx)) { pending = {}; return; }
        const uint32_t root = vm_read32(DES_AT(kInventoryMenuSlot));
        const uint32_t holder = guest_ok(root, 4) ? vm_read32(root - kMenuHolderDelta) : 0;
        const uint32_t mode = guest_ok(root, 4) && guest_ok(holder, 4) ? vm_read32(holder) : 2;
        call_game(ctx, func_00720F50,
                  {mode == 0 ? kPickerSetupDefault : mode == 1 ? kPickerSetupAlternate : 0, 1});
        call_game(ctx, func_00720F50, {kPickerSetupSignal, 1});
        call_game(ctx, func_00720F50, {kPickerUpdateSignal, 1});
        call_game(ctx, func_00720F50, {kPickerSetupSignal, 1});
        call_game(ctx, func_007731F0, {kPickerPromptId, 1, 2, maximum, 1, maximum, kPickerRangeId});
        call_game(ctx, func_00720F50, {2, 9});
        call_game(ctx, func_00720F50, {kPickerUpdateSignal, 1});
        vm_write32(m + kPickerText, s_quantity_question);
        pending.picker_text = s_quantity_question;
        pending.cancel_when_closed = false;
        call_game(ctx, func_0072D970, {100});
        pending.state = InventoryAsk::Asking;
        log_detail("[des] storage: inventory quantity picker for id %u x%d (box room %d, picker max %d)\n",
                   pending.item.id, pending.item.count, room, maximum);
        return;
    }
    if (pending.state == InventoryAsk::Ready) {
        const InventoryTransfer transfer = pending;
        pending = {}; // consume authorization before the native operation
        deposit_inventory(ctx, transfer);
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
    if (message == (int)kInventoryCancelMessage) {
        pending = {};
        log_detail("[des] storage: inventory quantity picker cancelled by native state\n");
        return 0;
    }
    if (message != (int)kInventoryOkMessage) return 0;
    if (pending.cancel_when_closed || !on() || !player_inventory_menu() || pending.player != pgd() ||
        pending.dialog != inventory()) {
        pending = {};
        log_detail("[des] storage: inventory quantity picker OK consumed; transfer cancelled\n");
        return 1;
    }
    const uint32_t m = menus_obj();
    const int amount = m ? (int32_t)vm_read32(m + kPickerAmount) : 0;
    if (m && pending.picker_text && vm_read32(m + kPickerText) != pending.picker_text)
        log_detail("[des] storage: quantity OK with rewritten prompt, still ours (amount %d)\n", amount);
    const uint32_t box = pending.player ? vm_read32(pending.player + kPgdBox) : 0;
    const int room = storage_room(ctx, box, pending.item);
    if (amount < 1 || amount > pending.item.count || amount > room) {
        pending = {};
        log_detail("[des] storage: inventory quantity confirmed %d, ignored\n", amount);
        return 1;
    }
    pending.amount = amount;
    pending.state = InventoryAsk::Ready;
    vm_write8(pending.dialog + kInventoryDirtyA, 1);
    vm_write8(pending.dialog + kInventoryDirtyB, 1);
    log_detail("[des] storage: inventory quantity confirmed %d\n", amount);
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

extern "C" int des_storage_mouse_row(uint32_t dialog)
{
    if (!on() || !player_inventory_menu() || dialog != s_dlg || s_line_state != 1 || !guest_ok(dialog, 4) ||
        !is_command(dialog) || vm_read8(dialog + kDialogActive) == 0) return -1;
    return s_navigation.active() ? s_visible_row() : (int32_t)vm_read32(dialog + kCmdRow);
}

extern "C" uint32_t des_storage_mouse_spot(uint32_t dialog, int row, int column)
{
    if (!on() || !player_inventory_menu() || dialog != s_dlg || s_line_state != 1 || !guest_ok(dialog, 4) ||
        !is_command(dialog) || column != 0 || row < 0 || row > 4) return 0;
    // Cursor visuals can move when Auto Sort is selected. Row pieces keep
    // stable geometry and remain available even when native drawing hides them.
    if (row == 2) return guest_ok(s_line, 4) ? s_line : 0;
    const char* names[] = {"button0_0", "button1_0", nullptr, "button2_0", "button3_0"};
    return names[row] ? find_named(dialog, names[row]) : 0;
}

extern "C" int des_storage_mouse_select(ppu_context*, uint32_t dialog, int row, int column)
{
    if (!des_storage_mouse_spot(dialog, row, column) || !s_navigation.select_visible(row)) return 0;
    s_forced = s_navigation.send();
    vm_write32(dialog + kCmdRow, (uint32_t)s_navigation.projected());
    if (navigation_trace())
        fprintf(stderr, "[storage-nav] mouse dlg=%08x visual=%d projected=%d\n", dialog, row,
                s_navigation.projected());
    return 1;
}
