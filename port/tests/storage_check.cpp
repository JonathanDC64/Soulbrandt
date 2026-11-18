// Compile the entire production implementation over a synthetic guest memory.
// The game's count/limit/deposit calls are stand-ins; no function is extracted
// by text and Entry/InventoryTransfer are the production types.
#include "../des_storage.cpp"

#include <cstdio>
#include <cstdlib>
#include <limits>

extern "C" {
uint8_t g_des_region = 0;
void (*g_trampoline_fn)(void*) = nullptr;
volatile unsigned g_pad0_buttons = 0, g_pad0_reads = 0, g_pad0_empty_reads = 0;
}

namespace {
int failures = 0, checks = 0;
int held_count = 48, item_limit = 99;
uint32_t allocated = 0x02000000;
int allocation_fail_after = -1;
int allocations = 0;
constexpr uint32_t test_box = 0x03000000;
constexpr uint32_t test_player = 0x41000000, test_dialog = 0x42000000;
constexpr uint32_t test_entries = 0x43000000;
int deposit_result = 1, inventory_delta = 1, box_delta = 1, deposit_calls = 0;
int bad_native_arguments = 0;
bool invalid_post_count = false, deposited_for_query = false;
bool inspect_layout_allocations = false;
int allocations_during_partial_layout = 0;
std::map<uint32_t, uint8_t> layout_before_allocation;

#define CHECK(expression) do { ++checks; if (!(expression)) { \
    std::fprintf(stderr, "storage_check:%d: %s\n", __LINE__, #expression); ++failures; \
} } while (0)

void native_count(ppu_context* ctx) {
    if (ctx->gpr[3] != test_box || ctx->gpr[4] != 0x40000000 || ctx->gpr[5] != 1000)
        ++bad_native_arguments;
    ctx->gpr[3] = invalid_post_count && deposited_for_query ? std::numeric_limits<int32_t>::min() : held_count;
}
void native_limit(ppu_context* ctx) {
    if (ctx->gpr[4] != 0 || vm_read32(ctx->gpr[3] + 12) != (0x40000000u | 1000))
        ++bad_native_arguments;
    ctx->gpr[3] = item_limit;
}
void native_movable(ppu_context* ctx) { ctx->gpr[3] = 0; }
void native_deposit(ppu_context* ctx) {
    ++deposit_calls;
    deposited_for_query = true;
    const uint32_t amount = (uint32_t)ctx->gpr[6];
    vm_write32(test_entries + 8, vm_read32(test_entries + 8) - amount * inventory_delta);
    held_count += (int)amount * box_delta;
    ctx->gpr[3] = deposit_result;
}

void full_box(bool occupied) {
    for (uint32_t i = 0; i < 2048; ++i)
        vm_write32(test_box + 12 + i * 20 + 8, occupied ? 0x00010000u : 0);
}

void set_menu_mode(uint32_t mode) {
    // func_0081E110's holder: *(menus - 0x7FFC) is the mode word.
    // Mode 0 is the player's item command menu. Mode 1 is Thomas's Reclaim.
    const uint32_t root = 0x45008000;
    vm_write32(DES_AT(kInventoryMenuSlot), root);
    vm_write32(root - kMenuHolderDelta, 0x45000020);
    vm_write32(0x45000020, mode);
}

void player_inventory(int count) {
    // Synthetic pointer chains used by production pgd(), inventory(), selected()
    // and entry_at(). Constants here are guest objects made by this test.
    vm_write32(DES_AT(kPgdRoot), 0x44000000);
    vm_write32(0x44000008, test_player);
    vm_write32(DES_AT(0x019ADF0Cu), 0x45008000);
    vm_write32(0x45000000, 0x46000000);
    vm_write32(0x46000000, test_dialog);
    vm_write32(test_player + kPgdBox, test_box);
    vm_write32(test_player + kPgdItems + 12, 1);
    vm_write32(test_player + kPgdItems + 16, test_entries);
    vm_write32(test_entries, 0x40000000);
    vm_write32(test_entries + 4, 1000);
    vm_write32(test_entries + 8, count);
    vm_write32(test_dialog + kDlgRow, 0);
    vm_write32(test_dialog + kDlgOrder, 0x47000000);
    vm_write32(test_dialog + kDlgItems, 0x47000010);
    vm_write32(0x47000004, 0x47000020);
    vm_write32(0x47000014, 0x47000030);
    vm_write32(0x47000020, 0);
    vm_write32(0x47000030, 0);
    set_menu_mode(0);
}

constexpr uint32_t test_menus = 0x49000000;
constexpr uint32_t test_command = 0x50000000;
void put_text(uint32_t at, const char* text) {
    for (uint32_t i = 0;; ++i) {
        vm_write16(at + 2 * i, (uint8_t)text[i]);
        if (!text[i]) break;
    }
}
uint32_t command_layout(uint32_t shift = 0) {
    const char* names[] = {"button0_0", "button1_0", "button2_0", "button3_0",
        "cursor0_0", "cursor1_0", "cursor2_0", "cursor3_0", "plate0_3",
        "grayout_drop00", "grayout_drop01", "edge_gradation03", "BG", "R03_frame", "cursor"};
    const uint32_t children = 0x53000000 + shift, dialog = test_command + shift;
    vm_write32(dialog + kKids, children);
    vm_write32(dialog + kKidCount, sizeof names / sizeof names[0]);
    vm_write8(dialog + 0x94, 1);
    vm_write32(dialog + kDialogInput, kDialogInputEnabled);
    for (uint32_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
        const uint32_t obj = 0x50010000 + shift + i * 0x200;
        const uint32_t record = 0x51000000 + shift + i * 0x100;
        const uint32_t name = 0x52000000 + shift + i * 0x100;
        const uint32_t shape = obj + 0x30;
        vm_write32(children + 4 * i, obj);
        vm_write32(obj + kObjRecord, record);
        vm_write32(obj + 0x14, shape);
        vm_write32(obj + kObjFlags, kObjDrawn);
        vm_write32(record, name);
        put_text(name, names[i]);
        const int row = i < 8 ? i % 4 : i == 8 || i == 11 ? 3 : i == 10 ? 1 : 0;
        const uint32_t y = 300 + row * 89;
        vm_write32(shape + 8, (496u << 16) | y);
        vm_write32(shape + 12, (786u << 16) | (y + 36));
        if (i == 8 || i == 12 || i == 13) {
            vm_writef(shape + 0x14, 496); vm_writef(shape + 0x18, (float)y);
            vm_writef(shape + 0x1C, 786); vm_writef(shape + 0x20, (float)y + 36);
        }
        if (i == 1) {
            put_text(0x54000000 + shift, "Jeter"); // no English "Drop" anywhere in this layout
            vm_write32(obj + 0x60, 0x54000000 + shift);
        }
    }
    set_menu_mode(0);
    return dialog;
}

std::map<uint32_t, uint8_t> native_layout_memory() {
    std::map<uint32_t, uint8_t> result;
    for (const auto& byte : storage_mem())
        if (byte.first >= 0x50000000 && byte.second) result.insert(byte);
    return result;
}

void asking_picker(const Entry& item) {
    vm_write32(DES_AT(kMenuRoot), 0x48008000);
    vm_write32(0x48000000, 0x48010000);
    vm_write32(0x48010000, test_menus);
    vm_write32(test_menus + kPickerMenuState + 4 * kPickerSlot, kPickerMode);
    vm_write32(test_menus + kPickerAmount, 1);
    s_inventory_transfer = {};
    s_inventory_transfer.state = InventoryAsk::Asking;
    s_inventory_transfer.player = test_player;
    s_inventory_transfer.dialog = test_dialog;
    s_inventory_transfer.slot = 0;
    s_inventory_transfer.item = item;
}
}

extern "C" void ds_malloc(ppu_context* ctx) {
    if (inspect_layout_allocations && native_layout_memory() != layout_before_allocation)
        ++allocations_during_partial_layout;
    if (allocation_fail_after == allocations++) { ctx->gpr[3] = 0; return; }
    const auto size = (uint32_t)ctx->gpr[3];
    ctx->gpr[3] = allocated;
    allocated += (size + 15) & ~15u;
}
extern "C" int des_hud_obj_rect(ppu_context*, uint32_t obj, float out[4]) {
    const uint32_t shape = vm_read32(obj + 0x14);
    if (!shape) return 0;
    float rect[4];
    for (int i = 0; i < 4; ++i) rect[i] = vm_readf(shape + 0x14 + 4 * i);
    if (rect[2] > rect[0] && rect[3] > rect[1]) {
        std::memcpy(out, rect, sizeof rect);
        return 1;
    }
    const uint32_t xy = vm_read32(shape + 8), rb = vm_read32(shape + 12);
    out[0] = (float)(xy >> 16); out[1] = (float)(xy & 0xFFFF);
    out[2] = (float)(rb >> 16); out[3] = (float)(rb & 0xFFFF);
    return out[2] > out[0] && out[3] > out[1];
}
namespace des_game_calls {
void f_008F88D0(ppu_context* ctx) { native_movable(ctx); }
void f_00923FD8(ppu_context* ctx) { native_limit(ctx); }
void f_00924BB8(ppu_context* ctx) { native_deposit(ctx); }
void f_00924D58(ppu_context* ctx) { native_count(ctx); }
void f_005C2810(ppu_context* ctx) { ctx->gpr[3] = 0; }
void f_00720F50(ppu_context*) {}
void f_0072D970(ppu_context*) {}
void f_007731F0(ppu_context*) {}
}

int main(int argc, char** argv) {
    ppu_context ctx;
    ctx.gpr[1] = 0x04000000;
    const Entry item{0x40000000, 1000, 63};
    if (argc == 3 && !std::strcmp(argv[1], "--layout-failure")) {
        const int failure = std::atoi(argv[2]);
        command_layout();
        storage_environment()["DES_STORAGE"] = "1";
        const auto native_before = native_layout_memory();
        layout_before_allocation = native_before;
        inspect_layout_allocations = true;
        allocation_fail_after = failure;
        s_draw_starts.push_back(s_draw_writes.size()); s_drawing = true;
        const bool built = ensure_line(&ctx, test_command);
        CHECK(allocations_during_partial_layout == 0); // reserve before publishing native edits
        inspect_layout_allocations = false;
        if (!built) {
            CHECK(s_line_state == -1);
            CHECK(native_layout_memory() == native_before); // no partial native layout
            const int attempted = allocations;
            CHECK(!ensure_line(&ctx, test_command));
            CHECK(allocations == attempted); // no retry/leak on every frame
        }
        if (failure == 64) CHECK(built); // the fixture must exercise a real successful layout
        if (failure == 64 && built) {
            CHECK(vm_read32(s_line + kButtonTextPointer) == s_label_ptr);
            CHECK(vm_read32(vm_read32(s_line + 0x14) + 0x30) == s_label_ptr);
            float boxes[5][4] = {};
            for (int row = 0; row < 5; ++row) {
                const uint32_t spot = des_storage_mouse_spot(test_command, row, 0);
                CHECK(spot && box_of(&ctx, spot, boxes[row]));
                if (row) CHECK(boxes[row][1] > boxes[row - 1][1]);
                CHECK(des_storage_mouse_select(&ctx, test_command, row, 0) == 1);
                CHECK(des_storage_mouse_row(test_command) == row);
                CHECK(sync_navigation("test") == 0); // our direct write is not a second movement
                CHECK(des_storage_mouse_row(test_command) == row);
            }
            restore_draw();
            s_draw_starts.push_back(s_draw_writes.size()); s_drawing = true;
            const int allocations_before_next_frame = allocations;
            on_draw(&ctx, test_command); // Auto Sort's selected cursor must not move another row's hitbox
            CHECK(allocations == allocations_before_next_frame);
            for (int row = 0; row < 5; ++row) {
                float after[4] = {};
                CHECK(box_of(&ctx, des_storage_mouse_spot(test_command, row, 0), after));
                CHECK(!std::memcmp(after, boxes[row], sizeof after));
            }
        }
        restore_draw();
        if (built) restore_shape_bindings_to(0); // bindings may outlive the renderer's draw callback
        CHECK(native_layout_memory() == native_before);
        if (failure == 64 && built) {
            const int allocated_once = allocations;
            for (uint32_t session = 1; session <= 10; ++session) {
                const uint32_t dialog = command_layout(session * 1048576u);
                const auto before_session = native_layout_memory();
                s_draw_starts.push_back(s_draw_writes.size()); s_drawing = true;
                CHECK(ensure_line(&ctx, dialog));
                restore_draw();
                restore_shape_bindings_to(0);
                CHECK(native_layout_memory() == before_session);
                CHECK(allocations == allocated_once); // reuse across newly allocated native dialogs
            }
        }
        std::printf("storage_check layout allocation=%d built=%d allocated=%d: %d checks, %d failures\n",
                    failure, built ? 1 : 0, allocations, checks, failures);
        return failures ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    if (argc == 2 && !std::strcmp(argv[1], "--allocation-failure")) {
        allocation_fail_after = 0;
        CHECK(storage_room(&ctx, test_box, item) == 0);
        CHECK(allocations == 1);
        std::printf("storage_check allocation failure: %d checks, %d failures\n", checks, failures);
        return failures ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    CHECK(storage_room(&ctx, test_box, item) == 51);
    held_count = 98;
    CHECK(storage_room(&ctx, test_box, item) == 1);
    held_count = 99;
    CHECK(storage_room(&ctx, test_box, item) == 0);
    held_count = 100;
    CHECK(storage_room(&ctx, test_box, item) == 0);
    held_count = -1;
    CHECK(storage_room(&ctx, test_box, item) == 0);
    held_count = 0; item_limit = 0;
    CHECK(storage_room(&ctx, test_box, item) == 0);
    item_limit = -1;
    CHECK(storage_room(&ctx, test_box, item) == 0);
    item_limit = 999; held_count = 990;
    CHECK(storage_room(&ctx, test_box, item) == 9);
    item_limit = 99;
    CHECK(storage_room(&ctx, 0, item) == 0);
    held_count = 0;
    full_box(true);
    CHECK(storage_room(&ctx, test_box, item) == 0); // no free record for a new item
    held_count = 48;
    CHECK(storage_room(&ctx, test_box, item) == 51); // an existing stack can merge
    held_count = 0;
    vm_write32(test_box + 12 + 2047 * 20 + 8, 0);
    CHECK(storage_room(&ctx, test_box, item) == 99); // final record is usable
    full_box(false);
    player_inventory(63);
    storage_environment()["DES_STORAGE"] = "1";
    InventoryTransfer pending;
    pending.player = test_player;
    pending.dialog = test_dialog;
    pending.slot = 0;
    pending.item = item;
    pending.amount = 51;
    held_count = 48;
    deposit_inventory(&ctx, pending);
    CHECK(deposit_calls == 1);
    CHECK(vm_read32(test_entries + 8) == 12);
    CHECK(held_count == 99);
    player_inventory(63);
    pending.amount = 52;
    held_count = 48;
    deposit_inventory(&ctx, pending);
    CHECK(deposit_calls == 1); // precheck prevents the observed clamp loss
    CHECK(vm_read32(test_entries + 8) == 63);
    CHECK(held_count == 48);
    CHECK(bad_native_arguments == 0);
    pending.amount = 1;
    full_box(true); held_count = 0;
    const int calls_before_full = deposit_calls;
    deposit_inventory(&ctx, pending);
    CHECK(deposit_calls == calls_before_full);
    CHECK(vm_read32(test_entries + 8) == 63 && held_count == 0);
    full_box(false); held_count = 48;
    deposit_result = 0; inventory_delta = 0; box_delta = 0;
    deposit_inventory(&ctx, pending);
    CHECK(!s_send_disabled);
    CHECK(vm_read32(test_entries + 8) == 63 && held_count == 48);
    deposit_result = 1; inventory_delta = 1; box_delta = 0;
    deposit_inventory(&ctx, pending); // reproduce a lying native success
    CHECK(s_send_disabled);
    CHECK(vm_read32(test_entries + 8) == 62 && held_count == 48);
    const int calls_after_mismatch = deposit_calls;
    pending.item.count = 62;
    storage_environment()["DES_STORAGE"] = "0";
    CHECK(!on());
    storage_environment()["DES_STORAGE"] = "1";
    CHECK(on() && s_send_disabled); // toggling cannot reset the session fail-safe
    deposit_inventory(&ctx, pending);
    CHECK(deposit_calls == calls_after_mismatch);
    CHECK(vm_read32(test_entries + 8) == 62 && held_count == 48);
    s_send_disabled = false; // a fresh synthetic session for the remaining tests
    player_inventory(63); pending.item = item;
    deposited_for_query = false; invalid_post_count = true;
    deposit_result = 0; inventory_delta = box_delta = 0;
    deposit_inventory(&ctx, pending);
    CHECK(s_send_disabled);
    CHECK(vm_read32(test_entries + 8) == 63 && held_count == 48);
    CHECK(inventory_count_delta(item, {item.cat, item.id, std::numeric_limits<int32_t>::min()}) == -1);
    s_send_disabled = false; invalid_post_count = false;
    deposit_result = 1; inventory_delta = 1; box_delta = 1;
    asking_picker(item);
    storage_environment()["DES_STORAGE"] = "0";
    const int calls_before_picker = deposit_calls;
    for (int tick = 0; tick < 40; ++tick) inventory_confirmation_tick(&ctx);
    CHECK(s_inventory_transfer.state == InventoryAsk::Asking);
    CHECK(s_inventory_transfer.cancel_when_closed);
    ctx.gpr[3] = test_dialog; ctx.gpr[4] = 2000;
    CHECK(des_storage_inventory_message(&ctx) == 1); // never fall through to Drop
    CHECK(s_inventory_transfer.state == InventoryAsk::Idle);
    CHECK(deposit_calls == calls_before_picker);
    CHECK(vm_read32(test_entries + 8) == 63 && held_count == 48);
    // A confirm button's identity must not revoke ownership (Circle confirms
    // on some discs). Messages and native menu state determine the outcome.
    storage_environment()["DES_STORAGE"] = "1";
    asking_picker(item); g_pad0_buttons = 0x2000;
    inventory_confirmation_tick(&ctx);
    CHECK(s_inventory_transfer.state == InventoryAsk::Asking);
    CHECK(!s_inventory_transfer.cancel_when_closed);
    ctx.gpr[3] = test_dialog + 16;
    CHECK(des_storage_inventory_message(&ctx) == 0);
    CHECK(s_inventory_transfer.state == InventoryAsk::Asking);
    ctx.gpr[3] = test_dialog;
    vm_write8(test_dialog + kInventoryDirtyA, 0);
    vm_write8(test_dialog + kInventoryDirtyB, 0);
    s_draw_starts.push_back(s_draw_writes.size()); s_drawing = true;
    CHECK(des_storage_inventory_message(&ctx) == 1);
    restore_draw();
    CHECK(vm_read8(test_dialog + kInventoryDirtyA) == 1);
    CHECK(vm_read8(test_dialog + kInventoryDirtyB) == 1); // game state survives the draw undo
    CHECK(s_inventory_transfer.state == InventoryAsk::Ready);
    inventory_confirmation_tick(&ctx);
    CHECK(vm_read32(test_entries + 8) == 62 && held_count == 49);
    CHECK(deposit_calls == calls_before_picker + 1);
    player_inventory(63); held_count = 48;
    asking_picker(item); ctx.gpr[4] = 2002;
    CHECK(des_storage_inventory_message(&ctx) == 0); // native cancellation continues
    CHECK(s_inventory_transfer.state == InventoryAsk::Idle);
    ctx.gpr[4] = 2000;
    CHECK(des_storage_inventory_message(&ctx) == 0); // unowned native Drop stays native
    CHECK(deposit_calls == calls_before_picker + 1);
    g_pad0_buttons = 0;
    // Native calls must not leak register writes into the surrounding hook.
    for (int i = 0; i < 32; ++i) ctx.gpr[i] = (uint64_t)i + 123;
    ctx.gpr[1] = 0x04000000;
    ctx.cr = 123; ctx.xer = 456; ctx.lr = 789; ctx.ctr = 987;
    const ppu_context before_call = ctx;
    CHECK(call_game(&ctx, native_count, {test_box, item.cat, item.id}) == 48);
    CHECK(!std::memcmp(&ctx, &before_call, sizeof ctx));
    constexpr uint32_t flags_at = 0x4A000018;
    vm_write32(flags_at, 8);
    s_draw_starts.push_back(s_draw_writes.size()); s_drawing = true;
    storage_write_flags(flags_at, kObjDrawn, kObjDrawn);
    vm_write32(flags_at, vm_read32(flags_at) | 0x80); // native draw changes an unrelated bit
    restore_draw();
    CHECK(vm_read32(flags_at) == 0x88);
    // EU/fr cancel closes the picker without message 2002. The command dialog
    // is active again, dispatch has cleared the input bit, and slot 2 can
    // still say mode 9. A choice then must drop ownership. While the dialog
    // is inactive the picker stays modal and the choice is still swallowed.
    storage_environment()["DES_STORAGE"] = "1";
    CHECK(on());
    const uint32_t command = command_layout(0x1000);
    vm_write32(command + kCmdRow, 0);
    vm_write8(command + kDialogActive, 1);
    vm_write32(command + kDialogInput, kDialogInputEnabled);
    des_storage_on_draw(&ctx, command);
    des_storage_draw_end();
    vm_write8(command + kDialogActive, 0);
    vm_write32(command + kDialogInput, 0);
    asking_picker(item);
    vm_write32(test_menus + kPickerMenuState + 4 * kPickerSlot, kPickerMode);
    vm_write32(test_menus + kPickerMenuState + 4 * kMenuCancelSlot, 0);
    vm_write32(test_menus + kPickerMenuState + 4 * kMenuCloseSlot, 0);
    ctx.gpr[3] = command;
    CHECK(des_storage_command(&ctx) == 1);
    CHECK(s_inventory_transfer.state == InventoryAsk::Asking);
    vm_write8(command + kDialogActive, 1);
    des_storage_command(&ctx); // cursor is not on Send; native choice continues
    CHECK(s_inventory_transfer.state != InventoryAsk::Asking);
    const int calls_after_release = deposit_calls;
    ctx.gpr[3] = test_dialog;
    ctx.gpr[4] = 2000;
    CHECK(des_storage_inventory_message(&ctx) == 0); // the closed picker no longer owns OK
    CHECK(deposit_calls == calls_after_release);
    // Thomas's Reclaim is menu mode 1 and reuses this command shape. Send must
    // not decorate, navigate, or confirm there. A picker already open stays owned.
    storage_environment()["DES_STORAGE"] = "1";
    s_inventory_transfer = {};
    const uint32_t menu = command_layout(0x2000);
    const uint32_t native_kids = vm_read32(menu + kKidCount);
    vm_write32(menu + kCmdRow, 0);
    des_storage_on_draw(&ctx, menu);
    CHECK(vm_read32(menu + kKidCount) > native_kids);
    CHECK(des_storage_mouse_spot(menu, 2, 0) != 0);
    des_storage_draw_end();
    CHECK(vm_read32(menu + kKidCount) == native_kids);
    set_menu_mode(1);
    vm_write32(menu + kCmdRow, 1);
    vm_write8(menu + kDialogActive, 1);
    vm_write32(menu + kDialogInput, kDialogInputEnabled);
    des_storage_on_draw(&ctx, menu);
    CHECK(vm_read32(menu + kKidCount) == native_kids); // four native rows, no Send
    CHECK(vm_read32(menu + kCmdRow) == 1); // navigation does not rewrite Reclaim
    CHECK(des_storage_mouse_row(menu) < 0);
    CHECK(des_storage_mouse_spot(menu, 2, 0) == 0);
    ctx.gpr[3] = menu;
    CHECK(des_storage_command(&ctx) == 0);
    CHECK(s_inventory_transfer.state == InventoryAsk::Idle);
    des_storage_draw_end();
    asking_picker(item);
    vm_write8(menu + kDialogActive, 1);
    ctx.gpr[3] = menu;
    CHECK(des_storage_command(&ctx) == 1); // Reclaim is not the inventory choice that releases
    CHECK(s_inventory_transfer.state == InventoryAsk::Asking);
    s_inventory_transfer = {};
    player_inventory(63);
    held_count = 48;
    full_box(false);
    s_send_disabled = false;
    set_menu_mode(0);
    vm_write8(menu + kDialogActive, 1);
    vm_write32(menu + kDialogInput, kDialogInputEnabled);
    vm_write32(menu + kCmdRow, 0);
    des_storage_on_draw(&ctx, menu);
    const uint32_t grey = find_named(menu, "grayout_drop01");
    CHECK(grey != 0);
    vm_write32(grey + kObjFlags, vm_read32(grey + kObjFlags) & ~kObjDrawn);
    CHECK(des_storage_mouse_select(&ctx, menu, 2, 0) == 1);
    ctx.gpr[3] = menu;
    CHECK(des_storage_command(&ctx) == 1);
    CHECK(s_inventory_transfer.state == InventoryAsk::Armed); // Send still acts in the player's menu
    des_storage_draw_end();
    s_inventory_transfer = {};
    ctx = before_call;
    storage_environment()["DES_STORAGE"] = "0";
    const int allocations_before_draw = allocations;
    des_storage_on_draw(&ctx, 0);
    des_storage_on_draw(&ctx, 0); // a nested skipped draw must pair with its own end
    des_storage_draw_end();
    des_storage_draw_end();
    CHECK(s_draw_starts.empty());
    CHECK(s_draw_writes.empty());
    CHECK(allocations == allocations_before_draw);
    CHECK(!std::memcmp(&ctx, &before_call, sizeof ctx));
    std::printf("storage_check: %d checks, %d failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
