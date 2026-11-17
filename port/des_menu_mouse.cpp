/* The mouse in the game's own menus (docs/114): the game's side.
 *
 * What a menu is, to the game (read in the census runs, docs/114 section 2):
 * - Every menu screen is a tree of EzMenu dialogs. A piece of one dialog can
 *   host another dialog (its word at +0x48 points at it, or at an object
 *   whose first word points just past it), and a hosted dialog's rect (+0x74)
 *   is in its host's layout (+0x84 maps onto +0x74). The roots are in the
 *   1280x720 menu canvas.
 * - Each menu that has a selection has a cursor dialog, hosted by a piece
 *   named "cursor" (or "list_cursor", "tab_cursor"...): a DsMenuDlgCursor
 *   (vtable 0x01867F50), or on the profile list a plain dialog with the same
 *   0,0,32,32 layout. The cursor dialog takes the rect of the selected spot.
 *   The spots are pieces of the same dialog named after the host piece and
 *   their place in the grid: "cursor0_0", "cursor1_0", "list_cursor2_0",
 *   "cursor_button0_0" (row, then column).
 * - The host piece is drawn (bit 0x40000000 of its +0x18) only while its menu
 *   has the selection: the pause menu's tab bar hides its "cursor" while a
 *   page is open and keeps a "stay_cursor" on the page's tab.
 *
 * So, with no table of screens: once a frame the draws of the frame before
 * (des_hud.cpp's wrapper of the dialog draw, 0x00F60BD0, calls
 * des_mm_draw_enter for each) give the menus on screen, their spots in canvas
 * units and the spot each cursor is on. The UI (port/ui/src/game_menu.cpp)
 * reads them to draw its highlight and sends the clicks here.
 *
 * A click on a spot walks the game's cursor there with the pad's own presses,
 * then presses Cross. A walk goes in rounds: the presses that should reach
 * the spot's row (down or up), then its column (right or left; R1 and L1, or
 * R2 and L2, where those move it instead: tabs, and the pause menu's pages),
 * each round once the cursor has settled after the last. What each menu's
 * buttons do is learnt as it goes (a menu whose rows run across, a column of
 * tabs that pages with R1). A right click is Circle, a wheel notch up or down.
 * Beside a menu's grid: its arrows ("arrow_left", ...), held while the mouse
 * button is; its values with arrows ("arrow0_0": a click on either half is
 * that side's arrow); its sliders (a "move" track with a "slider" thumb: slid
 * to the pointer, and dragged). Each belongs to the grid spot on its row, and
 * one on another row than the cursor's walks there first.
 * The presses go in through cellPad's title hook (des_mm_pad_buttons, from
 * des_options.cpp), so the game sees them exactly as a pad's, sounds and all.
 *
 * Experimental: off unless DES_MENU_MOUSE=1 (Settings, Controls). DES_MENU_MOUSE_LOG=1
 * prints the menus each time they change (and those left out, and why), and
 * each walk and slide; 2 the arrow pieces too; 3 every visible dialog with its
 * pieces. DES_MENU_MOUSE_PRESS is the frames a press is held. A test run clicks
 * with the UI's DES_UI_MOUSE (des_ui.cpp). */

#include "ps3emu/ppu_lift.h"
#include "des_region.h"      /* DES_AT: the running disc's addresses */
#include "des_game_calls.h"
#include "ps3emu/ps3_knob.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" unsigned des_flip_count(void);                                       /* des_options.cpp */
extern "C" int des_hud_obj_rect(ppu_context* ctx, uint32_t obj, float out[4]);  /* des_hud.cpp */
extern "C" float des_hud_side_width(void);                                      /* des_aspect.cpp */
extern "C" float des_hud_top_height(void);                                      /* des_aspect.cpp */
extern "C" float des_frame_step(void);                                          /* des_options.cpp: this frame's game s */
extern "C" volatile unsigned g_pad0_reads, g_pad0_empty_reads;                  /* cellPad.c */
extern "C" void ps3_indirect_call(ppu_context* ctx);   /* ppu_loader.cpp: a call to the guest address in CTR */
#ifndef PS3_GAME_SPLIT   /* DES_SPLIT: the game DLL's (ps3emu/game_split.h) */
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
#endif

namespace {

constexpr uint32_t kDlgChildren = 0x0Cu, kDlgChildCount = 0x10u;
constexpr uint32_t kDlgRect = 0x74u, kDlgLayout = 0x84u, kDlgVisible = 0x94u;
constexpr uint32_t kObjFlags = 0x18u, kObjRecord = 0x1Cu, kObjHosted = 0x48u;
constexpr uint32_t kObjDrawn = 0x40000000u;
/* The renderer a dialog is drawn with (0x00F60BD0's r4) keeps its current matrix here (4x4 floats, by rows; its get,
 * vtable +0x4C, is 0x00717E30). As a dialog's draw begins, it maps the space the dialog's rect is in (its host's
 * layout) to the canvas, with the host piece's slide: identity for a root. */
constexpr uint32_t kRendMatrix = 0x30u;
constexpr uint32_t kCursorVt = 0x01867F50u;   /* DsMenuDlgCursor */
constexpr float kCursorLayout = 32.0f;         /* a cursor dialog's layout: 0,0,32,32 (the class or a plain dialog) */
constexpr uint32_t kMaxKids = 256;
constexpr float kCanvasW = 1280.0f, kCanvasH = 720.0f;

/* The pad's buttons as cellPad's title hook takes them: (DIGITAL2 << 8) | DIGITAL1. */
constexpr unsigned kPadUp = 0x0010u, kPadRight = 0x0020u, kPadDown = 0x0040u, kPadLeft = 0x0080u;
constexpr unsigned kPadL2 = 0x0100u, kPadR2 = 0x0200u, kPadL1 = 0x0400u, kPadR1 = 0x0800u;
constexpr unsigned kPadCircle = 0x2000u, kPadCross = 0x4000u, kPadStart = 0x0008u;
constexpr uint32_t kTitleVt = 0x0186A968u;     /* DsMenuDlgTitle: "PRESS START BUTTON", no cursor */
/* Menus a click selects at once (select_now), as their own input code does it. */
constexpr uint32_t kStartTabVt = 0x018692C0u;  /* DsMenuDlgStartTab: the pause menu's bar */
constexpr uint32_t kSystemVt = 0x0186EF60u;    /* DsMenuDlgSystem: Options, Load Profile, Quit Game */
constexpr int kBarTabVar = 0x4B;               /* the menu variable (0x00720F20/0x00720F50) that holds the bar's tab */
constexpr int kTabs = 5;                       /* the bar's tabs (its step, 0x0079B2C0, wraps at 4) */
constexpr int kDirectWait = 10;                /* passes a direct select may take to show before the walk does it */
constexpr uint32_t kDlgInput = 0xA0u;          /* bit 31 set while the dialog takes input (0x00F55A18 asks it) */
constexpr uint32_t kListPlaceFn = 0x007814F0u; /* DsMenuDlgListbox's cursor placement, its vt+0x8C: a list keeps it */
constexpr uint32_t kListRows = 0xF4u, kListRow = 0x100u, kListItems = 0x104u, kListTop = 0x108u, kListPlace = 0x111u;
/* DsMenuDlgButtonGroup's hook before a step (its vt+0x88, "return 0"): a group (the inventory's command menu, ...)
 * keeps it, or its input (vt+0x8C: the equipment's slots keep it, with a hook of their own). Its row +0xF4 and
 * column +0xF8, of +0xFC by +0x100; its placement of the cursor from them. */
constexpr uint32_t kGroupNoHookFn = 0x00776C98u, kGroupInputFn = 0x00777C90u, kGroupPlaceFn = 0x00776F68u;
constexpr uint32_t kGroupRow = 0xF4u, kGroupCol = 0xF8u, kGroupRows = 0xFCu, kGroupCols = 0x100u;
/* DsMenuDlgOption: its step up (this, _, from, wrap) and its placement of the cursor from menu variable 0x51; its
 * step right (this, _, row, value, wrap: from OK it goes to DEFAULT, row 12, with the help line, the cursor and the
 * tick) and a row's redraw (this, row), which its input runs after a step right. */
constexpr uint32_t kOptionVt = 0x0186A398u, kOptionUpFn = 0x007CC598u, kOptionPlaceFn = 0x007CA320u;
constexpr uint32_t kOptionRightFn = 0x007CA7E0u, kOptionRowFn = 0x007CAA70u;
constexpr int kOptionRowVar = 0x51;
constexpr int kOptionOk = 11;                  /* the row of OK (and DEFAULT, its column 1) */
constexpr uint32_t kProfileVt = 0x0186BE58u;   /* DsMenuDlgSelectProfile, the profile list: its row is +0xF8 */
constexpr uint32_t kPromptVt = 0x01869FB8u;    /* the system layer's prompts (yes/no, Drop's count): button +0x110 */
constexpr uint32_t kGetMenuVarFn = 0x00720F20u;
/* The pause menu's pages, by the bar's tab. The game's page switch (0x007234E0(the page's variable)) closes only
 * the new page's two neighbours, so a jump also closes the page it leaves. A page takes R2 and L2 only once open
 * (its state variable at its idle value) and while its own dialog has the pad. */
constexpr uint32_t kPageSwitchFn = 0x007234E0u;
struct PausePage { int open_var, state_var, idle; };   /* the switch's variable; the page's state, open and idle */
constexpr PausePage kPages[] = {{2, 2, 2}, {3, 3, 1}, {0x15, 0x15, 1}, {0x16, 0x16, 1}, {0x13, 0x2A, 3}};
static_assert(sizeof kPages / sizeof kPages[0] == kTabs, "a page for each of the bar's tabs");
/* Each page's own close (Inventory, Equipment, Status, World Tendency, System, and Options with System), as the
 * switch calls them (with 0: the pause menu stays). */
constexpr uint32_t kInvCloseFn = 0x0081FA40u, kEquipCloseFn = 0x00736BB0u, kStatusCloseFn = 0x0082A440u;
constexpr uint32_t kWorldCloseFn = 0x00841A90u, kSystemCloseFn = 0x008ABD18u, kOptionCloseFn = 0x007C8260u;
/* The pages' own dialogs (the inventory's list and its categories, the equipment's slots); System's is kSystemVt. */
constexpr uint32_t kInventoryVt = 0x0186C318u, kInvTabsVt = 0x01866570u, kEquipSlotsVt = 0x01866068u;
/* The category tabs (DsMenuDlgEquipListTab) of the inventory, the storage and the shops: the menu variable +0x108
 * holds the category shown, of tabs +0xF4 to +0xF4 + +0xF8; +0x104 points to a word for each, not 0 where a step
 * passes it over; a step sets +0xFC and refuses while it is set, and the tabs' update clears it (one step a frame).
 * The dialog that owns them (+0x144 names the same variable) changes category with a call of its own (this, right,
 * wrap). Each begins with their shared step (0x007413E8): it keeps the list's place (+0x108) in menu variable 0x12D
 * + the category left, steps the tabs and takes the new category's place; then the class builds its list. */
constexpr uint32_t kTabVar = 0x108u, kTabFirst = 0xF4u, kTabCount = 0xF8u, kTabStepped = 0xFCu, kTabSkip = 0x104u;
constexpr uint32_t kCatVar = 0x144u, kCatPlace = 0x108u;
constexpr int kCatPlaceVars = 0x12D;
/* The owners and their category change: the inventory (DsMenuDlgInventory: its input takes left and right in its
 * state 2, +0x158, and none while 0x00778AB8 says so), Thomas's Deposit Item (DsMenuDlgDepository) and Take Item
 * (DsMenuDlgRepository, in its state 2, +0x16C), a merchant (DsMenuDlgBasicShop) and the blacksmith's Repair
 * (DsMenuDlgFixShop). After the call the shops' inputs set the list's +0x111 (its update places the cursor), and
 * Repair's own call plays the tick. */
constexpr uint32_t kInvCategoryFn = 0x00820DD0u, kMenuBusyFn = 0x00778AB8u, kInvState = 0x158u;
constexpr uint32_t kDepositVt = 0x0186C8A0u, kDepositCategoryFn = 0x00835E10u;
constexpr uint32_t kTakeVt = 0x0186C9F8u, kTakeCategoryFn = 0x00839748u, kTakeState = 0x16Cu;
constexpr uint32_t kShopVt = 0x0186DC08u, kShopCategoryFn = 0x008725F8u;
constexpr uint32_t kRepairVt = 0x0186DD88u, kRepairCategoryFn = 0x00877980u;
constexpr uint32_t kPromptButton = 0x110u, kPromptState = 0xF8u;
constexpr int kPromptKindVar = 0x11;           /* its kind: 0 and 1 take no left or right, 6 and 9 place no cursor */

/* The buttons that step a menu's columns, next then back, in the order they are tried. */
constexpr unsigned kColPairs[3][2] = {{kPadRight, kPadLeft}, {kPadR1, kPadL1}, {kPadR2, kPadL2}};
constexpr unsigned kStayPairs[2][2] = {{kPadR2, kPadL2}, {kPadR1, kPadL1}};

constexpr int kRoundsMax = 8;    /* a walk's rounds of presses */
constexpr int kPressesMax = 12;  /* a round's */
constexpr int kSettle = 3;       /* frames the cursor stays on one spot before it counts as settled */
constexpr int kLostMax = 10;     /* frames a walk's menu may be gone (a redraw) before the walk ends */
constexpr int kOffSpotMax = 120; /* frames the cursor may be between spots (sliding, a page turning) */
/* Game seconds after a stay menu's page turns in which its page button may go unheard: the game takes the next only
 * once the new page is up (0.19 s at a high frame rate, up to 0.37 s at 30 fps). Within them a press that did not
 * turn the page is pressed again; after them it counts as not working, as in any menu. */
constexpr float kTurnWait = 1.0f;

bool guest_ok(uint32_t a, uint32_t align)
{
    return a >= 0x00010000u && a < 0xD0000000u && (a & (align - 1u)) == 0;
}

float rdf(uint32_t ea)
{
    const uint32_t b = vm_read32(ea);
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* A piece's name in the layout file (UTF-16), as ASCII; cached by the piece and its record. */
std::unordered_map<uint32_t, std::pair<uint32_t, std::string>> s_names;

const std::string& name_of(uint32_t obj)
{
    const uint32_t rec = vm_read32(obj + kObjRecord);
    if (s_names.size() > 8192) s_names.clear();
    auto& e = s_names[obj];
    if (e.first == rec && !e.second.empty()) return e.second;
    e.first = rec;
    e.second.clear();
    const uint32_t name = guest_ok(rec, 4) ? vm_read32(rec) : 0u;
    if (guest_ok(name, 2))
        for (uint32_t i = 0; i < 48; i++) {
            const uint16_t c = vm_read16(name + 2 * i);
            if (!c) break;
            e.second += c < 0x80 && c >= 0x20 ? (char)c : '~';
        }
    if (e.second.empty()) e.second = "?";
    return e.second;
}

bool starts_with(const std::string& s, const std::string& p)
{
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool ends_with(const std::string& s, const char* p)
{
    const size_t n = strlen(p);
    return s.size() >= n && s.compare(s.size() - n, n, p) == 0;
}

/* "<prefix><r>_<c>": r and c and the prefix; false if the name does not end so. */
bool grid_name(const std::string& n, std::string& prefix, int& r, int& c)
{
    size_t i = n.size();
    const size_t e = i;
    while (i > 0 && isdigit((unsigned char)n[i - 1])) i--;
    if (i == e || i < 2 || n[i - 1] != '_') return false;
    c = atoi(n.c_str() + i);
    const size_t e2 = --i;
    while (i > 0 && isdigit((unsigned char)n[i - 1])) i--;
    if (i == e2) return false;
    r = atoi(n.c_str() + i);
    prefix = n.substr(0, i);
    return true;
}

struct Drawn {
    uint32_t dlg, vt;
    float cur[4], lay[4];
    bool vis;
    float at[4];   /* the renderer's matrix as its draw began (kRendMatrix), as a map: scale x, y, then offset x, y */
    bool at_ok;    /* (a plain scale and offset) */
};

/* A dialog that looks like a menu's cursor where no piece holds it (build_menus then finds its menu by the spot it
 * sits on): the cursor class, or a plain dialog with the cursor's layout. */
bool cursor_like(const Drawn& d)
{
    return d.vis && (d.vt == DES_AT(kCursorVt) || (d.lay[0] == 0.0f && d.lay[1] == 0.0f && d.lay[2] == kCursorLayout &&
                                           d.lay[3] == kCursorLayout));
}

/* The dialog has a piece named "...cursor": it can hold a menu. Kept by its list of pieces, which seldom changes, so
 * the frames with no menu cost two reads a dialog. */
struct Kids {
    uint32_t arr, n;
    bool cursor;
};
std::unordered_map<uint32_t, Kids> s_kids;

bool has_cursor_piece(uint32_t dlg)
{
    const uint32_t arr = vm_read32(dlg + kDlgChildren), n = vm_read32(dlg + kDlgChildCount);
    if (s_kids.size() > 4096) s_kids.clear();
    auto found = s_kids.find(dlg);
    if (found != s_kids.end() && found->second.arr == arr && found->second.n == n) return found->second.cursor;
    Kids k{arr, n, false};
    if (guest_ok(arr, 4))
        for (uint32_t i = 0; i < n && i < kMaxKids && !k.cursor; i++) {
            const uint32_t obj = vm_read32(arr + 4 * i);
            k.cursor = guest_ok(obj, 4) && ends_with(name_of(obj), "cursor");
        }
    s_kids[dlg] = k;
    return k.cursor;
}

/* A dialog's own map, layout onto its rect: x scale, y scale, x offset, y offset. */
void own_map(const Drawn& d, float m[4])
{
    const float lw = d.lay[2] - d.lay[0], lh = d.lay[3] - d.lay[1];
    m[0] = lw != 0.0f ? (d.cur[2] - d.cur[0]) / lw : 1.0f;
    m[1] = lh != 0.0f ? (d.cur[3] - d.cur[1]) / lh : 1.0f;
    m[2] = d.cur[0] - d.lay[0] * m[0];
    m[3] = d.cur[1] - d.lay[1] * m[1];
}

/* out = a after m: a(m(p)). */
void compose(const float a[4], const float m[4], float out[4])
{
    out[0] = a[0] * m[0];
    out[1] = a[1] * m[1];
    out[2] = a[0] * m[2] + a[2];
    out[3] = a[1] * m[3] + a[3];
}

void apply(const float m[4], const float r[4], float out[4])
{
    out[0] = r[0] * m[0] + m[2], out[2] = r[2] * m[0] + m[2];
    out[1] = r[1] * m[1] + m[3], out[3] = r[3] * m[1] + m[3];
}

bool same_box(const float a[4], const float b[4])
{
    for (int k = 0; k < 4; k++)
        if (std::fabs(a[k] - b[k]) > 1.0f) return false;
    return true;
}

bool inside(const float b[4], float x, float y)
{
    return x >= b[0] && x < b[2] && y >= b[1] && y < b[3];
}

bool overlap(const float a[4], const float b[4])
{
    return std::min(a[2], b[2]) - std::max(a[0], b[0]) > 2.0f && std::min(a[3], b[3]) - std::max(a[1], b[1]) > 2.0f;
}

/* An arrow piece's direction by its name ("arrow_left", ...); 0 for any other piece. */
int arrow_of(const std::string& n)
{
    return n == "arrow_left" ? -1 : n == "arrow_right" ? 1 : n == "arrow_up" ? -2 : n == "arrow_down" ? 2 : 0;
}

struct Spot {
    int r, c;       /* its place in the grid; -1 for an arrow or a slider */
    float box[4];   /* canvas */
    int arrow = 0;  /* an arrow beside the menu (a number to change, pages to turn): -1 left, 1 right, -2 up, 2 down */
    uint32_t slider = 0;   /* a slider's dialog (a value along a track: the face's age), its thumb and track (x) */
    float thumb = 0.0f, track0 = 0.0f, track1 = 0.0f;
    bool field = false;    /* a value with arrows ("arrow0_0": the options' values): its halves are the arrows */
};

/* A spot of the menu's grid, not an arrow, a slider or a value beside it. */
bool grid_spot(const Spot& s)
{
    return !s.arrow && !s.slider && !s.field;
}
struct Menu {
    uint32_t owner;           /* the dialog that holds the spots */
    uint64_t key;             /* the owner's class and the host piece's name: what is learnt about it is kept by this */
    std::string host;         /* the host piece's name, less "stay_": the spots are named after it */
    bool stay;                /* the cursor only marks the page that is open (the pause menu's tab bar) */
    bool tabs;                /* a row of tabs: a click switches, with no Cross */
    bool start;               /* the title's "PRESS START BUTTON": a click anywhere is Start */
    int z;                    /* the owner's place in the draw order */
    std::vector<Spot> spots;
    int cur = -1;             /* the spot the cursor is on; -1 between spots */
};

/* ---- The game's thread: the frame's draws and the menus in them. ---- */

int s_on = 0, s_log = 0, s_walk_only = 0;
std::vector<Drawn> s_draws;
thread_local unsigned t_pad_reads = 0;   /* the pad reads this thread made (des_mm_pad_buttons, each read of pad 0) */
std::vector<Menu> s_menus;
std::string s_menu_sig;

struct Piece {
    uint32_t obj, host;
    bool drawn;
};

std::vector<Piece> pieces_of(uint32_t dlg)
{
    std::vector<Piece> v;
    const uint32_t arr = vm_read32(dlg + kDlgChildren);
    uint32_t n = vm_read32(dlg + kDlgChildCount);
    if (!guest_ok(arr, 4)) return v;
    if (n > kMaxKids) n = kMaxKids;
    v.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t obj = vm_read32(arr + 4 * i);
        if (guest_ok(obj, 4))
            v.push_back({obj, vm_read32(obj + kObjHosted), (vm_read32(obj + kObjFlags) & kObjDrawn) != 0});
    }
    return v;
}

/* A menu the rules below left out (logged only): why, and the menu it went for. */
struct LeftOut {
    Menu m;
    const char* why;   /* "under" its spots, or "beneath" a menu over it */
    uint32_t by;
};

void log_spots(const Menu& m)
{
    for (size_t k = 0; k < m.spots.size() && k < 32; k++) {
        const Spot& s = m.spots[k];
        if (s.arrow)
            fprintf(stderr, " arrow%+d@%.0f,%.0f,%.0f,%.0f", s.arrow, s.box[0], s.box[1], s.box[2], s.box[3]);
        else if (s.slider)
            fprintf(stderr, " slider@%.0f,%.0f,%.0f,%.0f(%.0f..%.0f)", s.box[0], s.box[1], s.box[2], s.box[3],
                    s.track0, s.track1);
        else if (s.field)
            fprintf(stderr, " field@%.0f,%.0f,%.0f,%.0f", s.box[0], s.box[1], s.box[2], s.box[3]);
        else
            fprintf(stderr, " %s%d_%d@%.0f,%.0f,%.0f,%.0f", (int)k == m.cur ? "*" : "", s.r, s.c, s.box[0],
                    s.box[1], s.box[2], s.box[3]);
    }
    fprintf(stderr, "\n");
}

/* The menus each time they change, and those left out (a menu over the one the player looks at shows here), with
 * the pass's draws. A left-out line reads "vt 0x...," so a test's "vt 0x... z" finds only the menus kept. */
void log_menus(const std::vector<Menu>& menus, const std::vector<LeftOut>& left, size_t draws)
{
    std::string sig;
    char b[160];
    for (const Menu& m : menus) {
        snprintf(b, sizeof b, "%08X/%d/%zu/%d ", m.owner, m.stay, m.spots.size(), m.cur);
        sig += b;
    }
    for (const LeftOut& l : left) {
        snprintf(b, sizeof b, "-%08X/%zu/%s/%08X ", l.m.owner, l.m.spots.size(), l.why, l.by);
        sig += b;
    }
    /* Also once the spots have come to rest in new places (the menus slide in, and the spots with them). */
    std::string at;
    for (const Menu& m : menus)
        for (const Spot& s : m.spots) {
            snprintf(b, sizeof b, "%.0f,%.0f ", s.box[0], s.box[1]);
            at += b;
        }
    static std::string at_logged, at_before;
    static unsigned at_still = 0;
    at_still = at == at_before ? at_still + 1 : 0;
    at_before = at;
    if (sig == s_menu_sig && (at == at_logged || at_still < 3)) return;
    s_menu_sig = sig;
    at_logged = at;
    fprintf(stderr, "[mm] f%u menus: %zu (%zu left out; %zu draws)\n", des_flip_count(), menus.size(), left.size(),
            draws);
    for (const Menu& m : menus) {
        fprintf(stderr, "[mm]   0x%08X vt 0x%08X%s%s z %d, %zu spots, on %d:", m.owner, vm_read32(m.owner),
                m.stay ? " (stay)" : "", m.tabs ? " (tabs)" : "", m.z, m.spots.size(), m.cur);
        log_spots(m);
    }
    for (const LeftOut& l : left) {
        fprintf(stderr, "[mm]   (left out: %s 0x%08X) 0x%08X vt 0x%08X, z %d, %zu spots, on %d:", l.why, l.by,
                l.m.owner, vm_read32(l.m.owner), l.m.z, l.m.spots.size(), l.m.cur);
        log_spots(l.m);
    }
}

void build_menus(ppu_context* ctx)
{
    const std::vector<Drawn>& ds = s_draws;
    const size_t n = ds.size();
    std::vector<Menu> menus;
    std::vector<LeftOut> left;   /* (logged only) */
    bool any_cursor = false;
    for (const Drawn& d : ds) any_cursor |= d.vis && (d.vt == DES_AT(kTitleVt) || has_cursor_piece(d.dlg));
    if (any_cursor || s_log >= 3) {
        std::vector<std::vector<Piece>> pcs(n);
        for (size_t i = 0; i < n; i++)
            if (ds[i].vis) pcs[i] = pieces_of(ds[i].dlg);
        /* A piece's rect in its dialog's layout (the game's own call, des_hud.cpp), once a frame each. */
        std::unordered_map<uint32_t, std::array<float, 5>> rects;   /* [4]: 1 if it has one */
        auto rect_of = [&](uint32_t obj, float out[4]) {
            auto it = rects.find(obj);
            if (it == rects.end()) {
                std::array<float, 5> r{};
                r[4] = des_hud_obj_rect(ctx, obj, r.data()) ? 1.0f : 0.0f;
                it = rects.emplace(obj, r).first;
            }
            memcpy(out, it->second.data(), 4 * sizeof(float));
            return it->second[4] != 0.0f;
        };
        /* Each dialog's host: a piece of a dialog drawn before it (a host draws before what it holds), best first:
         * 1. its host word is the dialog;
         * 2. it is an object whose first word points just past the dialog, and the piece has the dialog's rect;
         * 3. it has a host of its own and the dialog's rect (the inventory's tabs, held through another object);
         * 4. an object whose first word points just past the dialog, whatever its rect;
         * 5. a drawn piece with the dialog's rect, whatever its host word holds.
         * (4 alone named the stats panel the host of a new Create Profile dialog: the game made it where an old one
         * was, and the panel holds an object that still points there. Its spots were then 777 px off.)
         * (5 is for the inventory opened on a tab other than the first: then its pieces' host words hold no pointer,
         * so its tabs and the rows' icons had no host. The tabs then sat at the canvas's corner, and as a menu not
         * part of the inventory they left the inventory out.)
         * Each rule first takes only a piece at the dialog's place in the canvas, by the game's matrices (Drawn::at),
         * and then, for a dialog still without a host, any piece. Host words go stale, and the same rect is in many
         * dialogs: by rule 3 the inventory's arrows were held by the pause menu's, a list's second row by its first;
         * by rule 4 a pause menu made anew (after a while in play) was held by one of the bar's small arrow guides.
         * (While a host piece slides its rect stays put, so the place can miss for those frames.) */
        std::vector<int> parent(n, -1), via(n, -1);
        std::vector<char> rule(n, 0);    /* the rule that named it (the census shows it) */
        std::vector<char> placed(n, 0);  /* (named in the first pass, at its place) */
        /* Each dialog's layout in the canvas by the game's matrix: its own map after the renderer's. */
        std::vector<std::array<float, 4>> game(n);
        for (size_t i = 0; i < n; i++)
            if (ds[i].at_ok) {
                float m[4];
                own_map(ds[i], m);
                compose(ds[i].at, m, game[i].data());
            }
        auto has_rect = [&](size_t j, size_t k, size_t i) {
            float r[4];
            return rect_of(pcs[j][k].obj, r) && same_box(r, ds[i].cur);
        };
        auto at_place = [&](size_t j, size_t k, size_t i) {
            float r[4], box[4], want[4];
            if (!ds[i].at_ok || !ds[j].at_ok || !rect_of(pcs[j][k].obj, r)) return false;
            apply(game[j].data(), r, box);
            apply(ds[i].at, ds[i].cur, want);
            return same_box(box, want);
        };
        for (int first = 1; first >= 0; first--) {
            auto fits = [&](size_t j, size_t k, size_t i) { return !first || at_place(j, k, i); };
            std::vector<int> loose(n, -1), loose_via(n, -1);
            auto name = [&](size_t i, size_t j, size_t k, int r) {
                parent[i] = (int)j;
                via[i] = (int)k;
                rule[i] = (char)r;
                placed[i] = (char)first;
            };
            for (size_t i = 0; i < n; i++) {   /* 1 and 2, and the first 4 */
                if (!ds[i].vis || parent[i] >= 0) continue;
                for (size_t j = 0; j < i && parent[i] < 0; j++)
                    for (size_t k = 0; k < pcs[j].size(); k++) {
                        const uint32_t h = pcs[j][k].host;
                        int r = h == ds[i].dlg ? 1 : 0;
                        if (!r && guest_ok(h, 4) && h != ds[j].dlg) {
                            const uint32_t w = vm_read32(h);
                            if (w > ds[i].dlg && w <= ds[i].dlg + 0x200u) {
                                if (has_rect(j, k, i)) r = 2;
                                else if (loose[i] < 0 && fits(j, k, i)) loose[i] = (int)j, loose_via[i] = (int)k;
                            }
                        }
                        if (r && fits(j, k, i)) {
                            name(i, j, k, r);
                            break;
                        }
                    }
            }
            for (size_t i = 0; i < n; i++) {   /* 3 */
                if (!ds[i].vis || parent[i] >= 0) continue;
                for (size_t j = 0; j < i && parent[i] < 0; j++)
                    for (size_t k = 0; k < pcs[j].size(); k++) {
                        if (!guest_ok(pcs[j][k].host, 4) || !has_rect(j, k, i) || !fits(j, k, i)) continue;
                        name(i, j, k, 3);
                        break;
                    }
            }
            for (size_t i = 0; i < n; i++)   /* 4 */
                if (ds[i].vis && parent[i] < 0 && loose[i] >= 0) name(i, (size_t)loose[i], (size_t)loose_via[i], 4);
            for (size_t i = 0; i < n; i++) {   /* 5 */
                if (!ds[i].vis || parent[i] >= 0) continue;
                for (size_t j = 0; j < i && parent[i] < 0; j++)
                    for (size_t k = 0; k < pcs[j].size(); k++) {
                        if (!pcs[j][k].drawn || !has_rect(j, k, i) || !fits(j, k, i)) continue;
                        name(i, j, k, 5);
                        break;
                    }
            }
        }
        /* Each dialog's layout in the canvas: the game's (above), with the host piece's slide; else (no renderer) its
         * own map after its host's. (Placed by its hosts alone, a dialog with a wrong host was drawn somewhere else:
         * the pause menu made anew had its tabs squeezed into slivers 12 px wide under the bar's arrow guide.) */
        std::vector<std::array<float, 4>> xf(n);
        std::vector<char> done(n, 0);
        for (size_t i = 0; i < n; i++)
            if (ds[i].at_ok) xf[i] = game[i], done[i] = 1;
        for (size_t i = 0; i < n; i++) {
            std::vector<int> chain;
            for (int k = (int)i; k >= 0 && !done[k] && chain.size() < 16; k = parent[k]) chain.push_back(k);
            for (size_t c = chain.size(); c-- > 0;) {
                const int k = chain[c];
                float m[4];
                own_map(ds[k], m);
                if (parent[k] >= 0 && done[parent[k]]) compose(xf[parent[k]].data(), m, xf[k].data());
                else memcpy(xf[k].data(), m, sizeof m);
                done[k] = 1;
            }
        }
        if (s_log >= 3) {   /* every visible dialog and its pieces, each time the set changes: to learn a screen */
            static std::string last;
            std::string sig;
            char b[240];
            for (size_t i = 0; i < n; i++)
                if (ds[i].vis) {
                    snprintf(b, sizeof b, "%08X ", ds[i].dlg);
                    sig += b;
                }
            if (sig != last) {
                last = sig;
                for (size_t i = 0; i < n; i++) {
                    if (!ds[i].vis) continue;
                    const int p = parent[i];
                    fprintf(stderr, "[mm] dialog 0x%08X vt 0x%08X cur %.0f,%.0f,%.0f,%.0f lay %.0f,%.0f,%.0f,%.0f parent %d",
                            ds[i].dlg, ds[i].vt, ds[i].cur[0], ds[i].cur[1], ds[i].cur[2], ds[i].cur[3], ds[i].lay[0],
                            ds[i].lay[1], ds[i].lay[2], ds[i].lay[3], p);
                    if (p >= 0)
                        fprintf(stderr, " (0x%08X via %s, rule %d%s)", ds[p].dlg, name_of(pcs[p][via[i]].obj).c_str(),
                                rule[i], placed[i] ? ", at its place" : "");
                    if (ds[i].at_ok)
                        fprintf(stderr, " drawn at x%.3g,%.3g %+.1f,%+.1f", ds[i].at[0], ds[i].at[1], ds[i].at[2],
                                ds[i].at[3]);
                    fprintf(stderr, "\n");
                    for (const Piece& p : pcs[i]) {
                        float lay[4] = {0, 0, 0, 0}, box[4] = {0, 0, 0, 0};
                        if (rect_of(p.obj, lay)) apply(xf[i].data(), lay, box);
                        fprintf(stderr, "[mm]     %-28s %s fl 0x%08X canvas %.0f,%.0f,%.0f,%.0f hosts 0x%08X",
                                name_of(p.obj).c_str(), p.drawn ? "drawn" : "     ", vm_read32(p.obj + kObjFlags),
                                box[0], box[1], box[2], box[3], p.host);
                        if (guest_ok(p.host, 4))
                            fprintf(stderr, " [%08X %08X %08X %08X]", vm_read32(p.host), vm_read32(p.host + 4),
                                    vm_read32(p.host + 8), vm_read32(p.host + 12));
                        fprintf(stderr, "\n");
                    }
                }
            }
        }
        if (s_log >= 2) {   /* the arrows (number pickers, pages), to learn their names */
            static std::string last;
            std::string now;
            char b[200];
            for (size_t i = 0; i < n; i++)
                for (const Piece& p : pcs[i]) {
                    const std::string& nm = name_of(p.obj);
                    std::string low = nm;
                    for (char& ch : low) ch = (char)tolower((unsigned char)ch);
                    float lay[4], box[4];
                    if (!p.drawn || low.find("arrow") == std::string::npos || !rect_of(p.obj, lay)) continue;
                    apply(xf[i].data(), lay, box);
                    snprintf(b, sizeof b, "[mm]   arrow %s in 0x%08X (vt 0x%08X) @%.0f,%.0f,%.0f,%.0f\n", nm.c_str(),
                             ds[i].dlg, ds[i].vt, box[0], box[1], box[2], box[3]);
                    now += b;
                }
            if (now != last) {
                last = now;
                fputs(now.empty() ? "[mm]   no arrows\n" : now.c_str(), stderr);
            }
        }
        /* The menus: each dialog a drawn "...cursor" piece holds (its size is the spot's: the title's is 500x35,
         * most are 32x32), or that sits on a spot of its own name. */
        for (size_t i = 0; i < n; i++) {
            if (!ds[i].vis) continue;
            int o = parent[i];
            std::string host;
            if (o >= 0 && pcs[o][via[i]].drawn) host = name_of(pcs[o][via[i]].obj);
            if (!ends_with(host, "cursor")) {
                if (!cursor_like(ds[i])) continue;
                /* No piece holds it (the inventory's list_cursor), or one hidden: the menu whose spot it sits on, in a
                 * dialog drawn before it, with a drawn "...cursor" piece of the spots' name, or its stay_ one. (A
                 * pause menu made anew, after Use Item, has one cursor dialog, held by the bar's "cursor" piece. With
                 * a page open that piece is hidden and "stay_cursor" shown, so the bar had no menu, and the tabs of
                 * the bar behind it, 31 px off the icons, took the glow and the clicks.) */
                o = -1;
                host.clear();
                for (size_t j = 0; j < i && o < 0; j++)
                    for (const Piece& p : pcs[j]) {
                        std::string prefix;
                        int r, c;
                        float lay[4];
                        if (!p.drawn || !grid_name(name_of(p.obj), prefix, r, c) ||
                            prefix.find("cursor") == std::string::npos || !rect_of(p.obj, lay) ||
                            !same_box(lay, ds[i].cur))
                            continue;
                        for (const Piece& q : pcs[j]) {
                            const std::string& qn = name_of(q.obj);
                            const std::string bare = starts_with(qn, "stay_") ? qn.substr(5) : qn;
                            if (q.drawn && ends_with(qn, "cursor") && (prefix == bare || starts_with(prefix, bare + "_"))) {
                                host = qn;
                                break;
                            }
                        }
                        if (!host.empty()) {
                            o = (int)j;
                            break;
                        }
                    }
                if (o < 0) continue;
            }
            Menu m{};
            m.owner = ds[o].dlg;
            m.key = (uint64_t)ds[o].vt << 32 ^ (uint64_t)std::hash<std::string>{}(host);
            m.stay = starts_with(host, "stay_");
            if (m.stay) host = host.substr(5);
            m.host = host;
            m.tabs = host.find("tab") != std::string::npos;
            m.z = o;
            for (const Piece& p : pcs[o]) {
                if (!p.drawn) continue;
                const std::string& nm = name_of(p.obj);
                std::string prefix;
                int r, c;
                if (!grid_name(nm, prefix, r, c)) continue;
                if (prefix != host && !starts_with(prefix, host + "_")) continue;   /* (cursor_button, not cursorX) */
                float lay[4];
                if (!rect_of(p.obj, lay)) continue;
                Spot s{r, c, {}};
                apply(xf[o].data(), lay, s.box);
                if (same_box(lay, ds[i].cur)) m.cur = (int)m.spots.size();
                m.spots.push_back(s);
            }
            /* Two layouts in one dialog: the online server message (DsMenuDlgTos) has OK in the middle (cursor0_2)
             * and two buttons beside it (cursor0_0, cursor0_1), all drawn, though only one layout shows. The
             * cursor is on the shown one's, so a spot over the cursor's is the other layout's: the cursor never
             * goes there, and a click on it walked to nowhere ("stuck"). */
            if (m.cur >= 0) {
                const Spot at = m.spots[m.cur];
                std::vector<Spot> shown;
                int cur = -1;
                for (size_t k = 0; k < m.spots.size(); k++) {
                    if ((int)k == m.cur) cur = (int)shown.size();
                    else if (overlap(m.spots[k].box, at.box)) continue;
                    shown.push_back(m.spots[k]);
                }
                m.spots.swap(shown);
                m.cur = cur;
            }
            if (m.spots.empty()) continue;
            bool dup = false;   /* a stay cursor beside its own menu's cursor: the active one counts */
            for (Menu& e : menus)
                if (e.owner == m.owner && e.host == m.host) {
                    if (e.stay && !m.stay) e = m;
                    dup = true;
                }
            if (!dup) menus.push_back(std::move(m));
        }
        /* A menu beneath another (the Maiden in Black's tab bar under the pause menu's, both drawn) has spots under
         * the other's: only the top one counts. */
        std::sort(menus.begin(), menus.end(), [](const Menu& a, const Menu& b) { return a.z > b.z; });
        std::vector<Menu> top;
        for (Menu& m : menus) {
            uint32_t under = 0;   /* the owner of the menu over it */
            for (const Menu& t : top)
                for (const Spot& a : m.spots)
                    for (const Spot& b : t.spots)
                        if (!under && overlap(a.box, b.box)) under = t.owner;
            if (!under) top.push_back(std::move(m));
            else if (s_log) left.push_back({std::move(m), "under", under});
        }
        menus.swap(top);
        /* A menu over another that is not part of it (a prompt over the profile, the command menu over the
         * inventory; the inventory's tabs are part of it) has the pad: the one beneath is left out. A stay menu
         * (the pause menu's pages) never is. */
        auto holds = [&](int a, int b) {   /* dialog a holds dialog b, or is it */
            for (int k = b, hops = 0; k >= 0 && hops < 16; k = parent[k], hops++)
                if (k == a) return true;
            return false;
        };
        top.clear();
        for (Menu& m : menus) {
            uint32_t beneath = 0;   /* the owner of the menu over it */
            for (const Menu& t : top)
                if (!beneath && !m.stay && !t.stay && !holds(t.z, m.z) && !holds(m.z, t.z)) beneath = t.owner;
            if (!beneath) top.push_back(std::move(m));
            else if (s_log) left.push_back({std::move(m), "beneath", beneath});
        }
        menus.swap(top);
        /* The title before Start: its line, the one spot. */
        for (size_t i = 0; i < n && menus.empty(); i++) {
            if (!ds[i].vis || ds[i].vt != DES_AT(kTitleVt)) continue;
            for (const Piece& p : pcs[i]) {
                float lay[4];
                if (!p.drawn || name_of(p.obj) != "text_press_start_button" || !rect_of(p.obj, lay)) continue;
                Menu m{};
                m.owner = ds[i].dlg;
                m.start = true;
                m.z = (int)i;
                Spot s{0, 0, {}};
                apply(xf[i].data(), lay, s.box);
                m.spots.push_back(s);
                m.cur = 0;
                menus.push_back(std::move(m));
                break;
            }
        }
        /* The menu whose dialog holds dialog i, or is it. */
        auto menu_holding = [&](size_t i) -> Menu* {
            for (int k = (int)i, hops = 0; k >= 0 && hops < 16; k = parent[k], hops++)
                for (Menu& m : menus)
                    if (m.z == k) return &m;
            return nullptr;
        };
        /* The arrows: of the menu whose dialog holds theirs, at least a finger's width, on screen. */
        for (size_t i = 0; i < n && !menus.empty(); i++) {
            if (!ds[i].vis) continue;
            for (const Piece& p : pcs[i]) {
                const int dir = p.drawn ? arrow_of(name_of(p.obj)) : 0;
                float lay[4];
                if (!dir || !rect_of(p.obj, lay)) continue;
                Menu* owner = menu_holding(i);
                if (!owner) continue;
                Spot s{-1, -1, {}, dir};
                apply(xf[i].data(), lay, s.box);
                const float cx = (s.box[0] + s.box[2]) * 0.5f, cy = (s.box[1] + s.box[3]) * 0.5f;
                const float hw = std::max((s.box[2] - s.box[0]) * 0.5f, 14.0f);
                const float hh = std::max((s.box[3] - s.box[1]) * 0.5f, 16.0f);
                if (cy < 0.0f || cy > kCanvasH) continue;   /* (parked off screen) */
                s.box[0] = cx - hw, s.box[2] = cx + hw, s.box[1] = cy - hh, s.box[3] = cy + hh;
                owner->spots.push_back(s);
            }
        }
        /* The values with arrows: pieces "arrow<n>_<m>" that hold a value and its arrows (the arrows themselves drawn
         * on the cursor's row only, in the options), of the menu whose dialog holds theirs, on screen. */
        for (size_t i = 0; i < n && !menus.empty(); i++) {
            if (!ds[i].vis) continue;
            for (const Piece& p : pcs[i]) {
                std::string prefix;
                int r, c;
                float lay[4];
                if (!p.drawn || !grid_name(name_of(p.obj), prefix, r, c) || prefix != "arrow" || !rect_of(p.obj, lay))
                    continue;
                Menu* owner = menu_holding(i);
                Spot s{-1, -1, {}};
                apply(xf[i].data(), lay, s.box);
                const float cy = (s.box[1] + s.box[3]) * 0.5f, hh = std::max((s.box[3] - s.box[1]) * 0.5f, 16.0f);
                if (!owner || cy < 0.0f || cy > kCanvasH || s.box[2] - s.box[0] < 40.0f) continue;
                s.box[1] = cy - hh, s.box[3] = cy + hh;
                s.field = true;
                owner->spots.push_back(s);
            }
        }
        /* The sliders: a dialog with a track ("move", across) and a thumb ("slider") on it, framed by its "edge",
         * of the menu whose dialog holds it. */
        for (size_t i = 0; i < n && !menus.empty(); i++) {
            if (!ds[i].vis) continue;
            float edge[4], track[4], thumb[4], lay[4];
            int got = 0;
            for (const Piece& p : pcs[i]) {
                if (!p.drawn) continue;
                const std::string& nm = name_of(p.obj);
                const int bit = nm == "move" ? 1 : nm == "slider" ? 2 : nm == "edge" ? 4 : 0;
                if (!bit || !rect_of(p.obj, lay)) continue;
                apply(xf[i].data(), lay, bit == 1 ? track : bit == 2 ? thumb : edge);
                got |= bit;
            }
            if ((got & 3) != 3 || track[2] - track[0] < 2.0f * (track[3] - track[1])) continue;   /* (a scroll bar) */
            Menu* owner = menu_holding(i);
            const float cy = (track[1] + track[3]) * 0.5f;
            if (!owner || cy < 0.0f || cy > kCanvasH) continue;
            Spot s{-1, -1, {}};
            if (got & 4) memcpy(s.box, edge, sizeof s.box);
            else s.box[0] = track[0], s.box[2] = track[2], s.box[1] = cy - 16.0f, s.box[3] = cy + 16.0f;
            s.slider = ds[i].dlg;
            s.thumb = (thumb[0] + thumb[2]) * 0.5f;
            s.track0 = track[0];
            s.track1 = track[2];
            owner->spots.push_back(s);
        }
    }
    if (s_log) log_menus(menus, left, n);
    s_menus.swap(menus);
}

/* ---- What the UI sees (any thread, under s_pub_mu). ---- */

struct PubSpot {
    float box[4];
    int menu, spot;
};
std::mutex s_pub_mu;
std::vector<PubSpot> s_pub;   /* the menus' spots, the top menu's first */
bool s_pub_open;
unsigned s_pub_flip;   /* the flip it was made at: none drawn for long, no menu */
constexpr unsigned kStaleFlips = 30;

void publish()
{
    std::vector<int> order(s_menus.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [](int a, int b) { return s_menus[a].z > s_menus[b].z; });
    std::vector<PubSpot> pub;
    for (int mi : order)
        for (size_t k = 0; k < s_menus[mi].spots.size(); k++) {
            PubSpot p{};
            memcpy(p.box, s_menus[mi].spots[k].box, sizeof p.box);
            p.menu = mi;
            p.spot = (int)k;
            pub.push_back(p);
        }
    std::lock_guard<std::mutex> lk(s_pub_mu);
    s_pub.swap(pub);
    s_pub_open = !s_menus.empty();
    s_pub_flip = des_flip_count();
}

/* The spot under (x, y), the top menu's first: false if none. */
bool spot_at(float x, float y, int& mi, int& si)
{
    std::lock_guard<std::mutex> lk(s_pub_mu);
    for (const PubSpot& p : s_pub)
        if (inside(p.box, x, y)) {
            mi = p.menu;
            si = p.spot;
            return true;
        }
    return false;
}

/* ---- The UI's requests (any thread, under s_ev_mu). ---- */

enum EvKind { kEvClick, kEvRelease, kEvBack, kEvWheel, kEvDrag };
struct Ev {
    EvKind kind;
    float x, y;
    int n;
};
std::mutex s_ev_mu;
std::vector<Ev> s_evs;

void post(const Ev& e)
{
    std::lock_guard<std::mutex> lk(s_ev_mu);
    if (e.kind == kEvDrag && !s_evs.empty() && s_evs.back().kind == kEvDrag) s_evs.back() = e;   /* (the last counts) */
    else if (s_evs.size() < 32) s_evs.push_back(e);
}

/* ---- The presses (the game's thread queues them, cellPad's reads play them; under s_pad_mu). ---- */

struct Press {
    unsigned mask;
    bool started, on;
    unsigned f0, d0;   /* the frame and the packet count its phase began at */
    bool held;         /* an arrow's: held down until the mouse button is let go (let_go), as a held pad button */
};
std::mutex s_pad_mu;
std::deque<Press> s_presses;
int s_press_frames = 1;   /* DES_MENU_MOUSE_PRESS: frames (and packets) a press is held, and let go after */

void press(unsigned mask, int times = 1, bool held = false)
{
    std::lock_guard<std::mutex> lk(s_pad_mu);
    for (int k = 0; k < times && s_presses.size() < 24; k++) s_presses.push_back({mask, false, false, 0, 0, held});
}

void let_go()
{
    std::lock_guard<std::mutex> lk(s_pad_mu);
    for (Press& p : s_presses) p.held = false;
}

bool pressing()
{
    std::lock_guard<std::mutex> lk(s_pad_mu);
    return !s_presses.empty();
}

void drop_presses()
{
    std::lock_guard<std::mutex> lk(s_pad_mu);
    if (!s_presses.empty() && s_presses.front().started) {   /* (the one under way ends as it should) */
        s_presses.resize(1);
        s_presses.front().held = false;
    } else {
        s_presses.clear();
    }
}

/* ---- Walking the game's cursor to a spot. ---- */

struct Learnt {
    int pair = 0;        /* kColPairs (kStayPairs for a stay menu): the column buttons that work */
    bool across = false; /* the spots' names give the column first: down and up step the second number */
};
std::unordered_map<uint64_t, Learnt> s_learnt;

struct Walk {
    bool on = false;
    uint32_t owner = 0;
    uint64_t key = 0;
    bool stay = false, confirm = false;
    int r = 0, c = 0;             /* the spot to reach */
    int rounds = 0;
    bool settling = false;        /* a round's presses are out: waiting for the cursor to stop */
    int axis = 0;                 /* the round's: 0 rows, 1 columns */
    int from_r = 0, from_c = 0;   /* where the round began */
    int seen_r = 0, seen_c = 0, still = 0;
    int lost = 0, off_spot = 0;
    bool rows_dead = false, cols_dead = false;
    unsigned then = 0;            /* there: this button (an arrow's, on the row it was beside) */
    int then_times = 0;
    uint32_t then_slide = 0;      /* there: slide this slider (on the row it was beside) */
    int retries = 0, retries_all = 0;   /* a stay menu's page button pressed again while the page turned */
    int direct = 0;               /* the spot was selected at once (select_now): passes left for the cursor to show it */
};
Walk s_walk;

/* The stay menu on screen (the pause menu's bar with a page open), the page its cursor is on and the game seconds
 * since that last changed: a page turned, or the page opened. */
uint32_t s_stay_owner = 0;
int s_stay_cur = -1;
float s_stay_quiet = 0.0f;
bool s_mouse_down;   /* the left button is down (a click, not yet let go): an arrow's press is held meanwhile */
uint32_t s_held_owner;   /* the menu an arrow is held down in: let go if it goes */

/* Sliding a slider's thumb to the pointer: the column buttons, held while it is far (the game repeats them), one
 * press at a time near it. While the mouse button is down the pointer drags it (des_menu_mouse_drag). */
struct Slide {
    bool on = false;
    uint32_t owner = 0, dlg = 0;
    bool stay = false;
    float target = 0.0f;
    int held = 0;            /* the button held down: 1 next (right), -1 back */
    int held_moves = 0;      /* frames the thumb moved while it was */
    float last = -1.0f;      /* the thumb's place the frame before */
    int still = 0;           /* frames it has not moved */
    float step = 0.0f;       /* how far one press moves it: learnt from the first */
    float tap_from = -1.0f;  /* a press is out: the thumb's place before it, and its direction */
    int tap_dir = 0;
    int end = 0;             /* the direction it did not move in (the track's end) */
    bool no_repeat = false;  /* holding the button moved it once only: one press at a time */
};
Slide s_slide;
constexpr int kHoldStill = 45;   /* frames a held button may leave the thumb still before it counts as not repeating */

const Menu* menu_of(uint32_t owner, bool stay)
{
    for (const Menu& m : s_menus)
        if (m.owner == owner && m.stay == stay) return &m;
    return nullptr;
}

void start_slide(const Menu& m, const Spot& s, float target);

void walk_end(const char* why)
{
    if (s_log) fprintf(stderr, "[mm] walk to %d_%d: %s, %d rounds\n", s_walk.r, s_walk.c, why, s_walk.rounds);
    s_walk.on = false;
}

void walk_step()
{
    Walk& w = s_walk;
    if (!w.on) return;
    const Menu* m = menu_of(w.owner, w.stay);
    if (!m) {
        if (++w.lost > kLostMax) walk_end("the menu went");
        return;
    }
    w.lost = 0;
    if (w.direct > 0) {   /* selected at once: the menu places its cursor in its next update; no presses meanwhile */
        if (m->cur >= 0 && m->spots[m->cur].r == w.r && m->spots[m->cur].c == w.c) {
            w.direct = 0;
        } else {
            if (--w.direct == 0 && s_log) fprintf(stderr, "[mm] walk: the select did not show, walking\n");
            return;
        }
    }
    if (pressing()) return;
    if (m->cur < 0) {
        if (++w.off_spot > kOffSpotMax) walk_end("the cursor is not on a spot");
        return;
    }
    w.off_spot = 0;
    const int r0 = m->spots[m->cur].r, c0 = m->spots[m->cur].c;
    Learnt& l = s_learnt[w.key];
    if (w.settling) {
        if (r0 != w.seen_r || c0 != w.seen_c) {
            w.seen_r = r0, w.seen_c = c0, w.still = 0;
            return;
        }
        if (++w.still < kSettle) return;
        w.settling = false;
        const bool moved_r = r0 != w.from_r, moved_c = c0 != w.from_c;
        if (w.stay && (moved_r || moved_c)) {
            if (s_log) fprintf(stderr, "[mm] walk: the page turned, pressed %d times\n", w.retries + 1);
            w.retries = 0;
        }
        if (!moved_r && !moved_c && w.stay && w.axis == 1 && s_stay_quiet < kTurnWait) {
            w.retries++, w.retries_all++;   /* the page before is still turning: the same button again */
        } else if (!moved_r && !moved_c) {
            if (w.axis == 0) w.rows_dead = true;
            else if (++l.pair >= (w.stay ? 2 : 3)) l.pair = 0, w.cols_dead = true;
            if (s_log) fprintf(stderr, "[mm] walk: the %s did not move it\n", w.axis ? "column buttons" : "up/down");
        } else {
            if (w.axis == 0 && !moved_r && moved_c && !l.across) l.across = true;   /* rows run across: learnt */
            else if (w.axis == 0 && moved_r && !moved_c && l.across) l.across = false;
            if (w.axis == 0) w.cols_dead = false;
            else w.rows_dead = false;
        }
    }
    if (r0 == w.r && c0 == w.c) {
        if (w.confirm) press(kPadCross);
        if (w.then) {   /* an arrow's row reached: its button now, held if the mouse button still is */
            const bool hold = s_mouse_down && w.then_times == 1;
            press(w.then, w.then_times, hold);
            if (hold) s_held_owner = w.owner;
        }
        walk_end("there");
        if (w.then_slide)
            for (const Spot& s : m->spots)
                if (s.slider == w.then_slide) start_slide(*m, s, s_slide.target);
        return;
    }
    if (++w.rounds > kRoundsMax + w.retries_all) {   /* (a press again, for a page still turning, is no new round) */
        walk_end("too many rounds");
        return;
    }
    /* Down and up step the rows (the columns, for a menu whose rows run across); the column buttons the other. */
    const int d_ud = l.across ? w.c - c0 : w.r - r0;
    const int d_lr = l.across ? w.r - r0 : w.c - c0;
    unsigned b = 0;
    int times = 0;
    if (d_ud && !w.rows_dead) {
        w.axis = 0;
        b = d_ud > 0 ? kPadDown : kPadUp;
        times = std::abs(d_ud);
    } else if (d_lr && !w.cols_dead) {
        w.axis = 1;
        const unsigned* pair = w.stay ? kStayPairs[l.pair % 2] : kColPairs[l.pair % 3];
        b = d_lr > 0 ? pair[0] : pair[1];
        times = std::abs(d_lr);
    }
    if (!b) {
        walk_end("stuck");
        return;
    }
    w.from_r = w.seen_r = r0;
    w.from_c = w.seen_c = c0;
    w.still = 0;
    w.settling = true;
    /* A stay menu's pages one at a time: while one turns the game takes no page button, so of several presses it
     * took one, or two at 30 fps, and the walk stopped a page or more short. */
    if (w.stay) times = 1;
    if (s_log && !w.retries) fprintf(stderr, "[mm] walk: at %d_%d, 0x%04X x%d\n", r0, c0, b, times);
    press(b, std::min(times, kPressesMax));
}

void start_walk(const Menu& m, const Spot& s)
{
    s_walk = Walk{};
    s_walk.on = true;
    s_walk.owner = m.owner;
    s_walk.key = m.key;
    s_walk.stay = m.stay;
    s_walk.confirm = !m.tabs && !m.stay;
    s_walk.r = s.r;
    s_walk.c = s.c;
    if (s_log) fprintf(stderr, "[mm] click on %d_%d of 0x%08X (on %d)\n", s.r, s.c, m.owner, m.cur);
}

/* The buttons that step a menu's columns: next, then back. */
const unsigned* col_pair(const Menu& m)
{
    const Learnt& l = s_learnt[m.key];
    return m.stay ? kStayPairs[l.pair % 2] : kColPairs[l.pair % 3];
}

/* The button an arrow presses. */
unsigned arrow_button(const Menu& m, int dir)
{
    return dir == 1 ? col_pair(m)[0] : dir == -1 ? col_pair(m)[1] : dir == 2 ? kPadDown : kPadUp;
}

void start_slide(const Menu& m, const Spot& s, float target)
{
    s_slide = Slide{};
    s_slide.on = true;
    s_slide.owner = m.owner;
    s_slide.dlg = s.slider;
    s_slide.stay = m.stay;
    s_slide.target = target;
    if (s_log) fprintf(stderr, "[mm] slide 0x%08X from %.0f to %.0f\n", s.slider, s.thumb, target);
}

void slide_end(const char* why)
{
    if (s_log) fprintf(stderr, "[mm] slide 0x%08X: %s, step %.1f\n", s_slide.dlg, why, s_slide.step);
    s_slide.on = false;
}

/* A click, a right click or the wheel: the slide stops (its held button let go). */
void stop_slide()
{
    if (s_slide.on && s_slide.held) let_go();
    s_slide.on = false;
}

void slide_step()
{
    Slide& sl = s_slide;
    if (!sl.on) return;
    const Menu* m = menu_of(sl.owner, sl.stay);
    const Spot* sp = nullptr;
    if (m)
        for (const Spot& s : m->spots)
            if (s.slider == sl.dlg) sp = &s;
    if (!sp) {
        if (sl.held) let_go();
        slide_end("the slider went");
        return;
    }
    const float t = std::min(std::max(sl.target, sp->track0), sp->track1);
    const float x = sp->thumb;
    const bool moved = std::fabs(x - sl.last) > 0.25f;
    sl.still = moved ? 0 : sl.still + 1;
    sl.last = x;
    const float err = t - x;
    const float tol = sl.step > 0.0f ? sl.step * 0.5f : 1.5f;
    if (sl.held) {   /* let go near it (the game may step once more), or once it stops: the end, or no repeat */
        sl.held_moves += moved;
        if (!pressing()) {
            sl.held = 0;
        } else if (err * sl.held <= std::max(tol, sl.step)) {
            let_go();
            sl.held = 0;
        } else if (sl.still > kHoldStill) {
            let_go();
            if (sl.held_moves > 1) sl.end = sl.held;
            else sl.no_repeat = true;
            sl.held = 0;
        }
        return;
    }
    if (pressing()) return;
    if (sl.tap_from >= 0.0f) {   /* a press is out: once the thumb is still, how far it went */
        if (sl.still < kSettle) return;
        const float d = std::fabs(x - sl.tap_from);
        if (d < 0.25f) sl.end = sl.tap_dir;
        else sl.step = d;
        sl.tap_from = -1.0f;
    }
    const int want = std::fabs(err) <= tol ? 0 : err > 0.0f ? 1 : -1;
    if (!want || want == sl.end) {
        if (!s_mouse_down) slide_end(want ? "at the end" : "there");
        return;
    }
    sl.end = 0;
    const unsigned b = want > 0 ? col_pair(*m)[0] : col_pair(*m)[1];
    sl.still = 0;
    if (!sl.no_repeat && std::fabs(err) > 3.0f * std::max(sl.step, 4.0f)) {
        press(b, 1, true);
        sl.held = want;
        sl.held_moves = 0;
        return;
    }
    press(b);
    sl.tap_from = x;
    sl.tap_dir = want;
}

/* How far two spots' boxes share their heights: more than a few pixels, the same row. */
float band(const Spot& a, const Spot& b)
{
    return std::min(a.box[3], b.box[3]) - std::max(a.box[1], b.box[1]);
}

/* Spot si is a sideways arrow or a slider, or its row has one beside it (a number to change, pages to turn). */
bool row_has_arrows(const Menu& m, int si)
{
    auto sideways = [](const Spot& a) { return a.arrow == 1 || a.arrow == -1 || a.slider || a.field; };
    const Spot& s = m.spots[si];
    if (sideways(s)) return true;
    for (const Spot& a : m.spots)
        if (sideways(a) && band(a, s) > 4.0f) return true;
    return false;
}

/* The spot of the menu's grid on spot si's row (si itself if it is one); -1 if none. */
int row_spot(const Menu& m, int si)
{
    if (grid_spot(m.spots[si])) return si;
    int best = -1;
    for (size_t k = 0; k < m.spots.size(); k++)
        if (grid_spot(m.spots[k]) && band(m.spots[k], m.spots[si]) > (best < 0 ? 4.0f : band(m.spots[best], m.spots[si])))
            best = (int)k;
    return best;
}

/* The cursor is on spot si's row: its arrows change what the cursor is on. */
bool on_cursor_row(const Menu& m, int si)
{
    return m.cur >= 0 && band(m.spots[m.cur], m.spots[si]) > 4.0f;
}

bool select_now(ppu_context* ctx, const Menu& m, const Spot& s);

/* Goes to spot si's row first (at once where the menu can be, else by walking), then presses b (an arrow's, on
 * another row than the cursor's). */
void walk_then(ppu_context* ctx, const Menu& m, int row, unsigned b, int times)
{
    const bool now = !s_walk_only && m.cur != row && select_now(ctx, m, m.spots[row]);
    start_walk(m, m.spots[row]);
    if (now) s_walk.direct = kDirectWait;
    s_walk.confirm = false;
    s_walk.then = b;
    s_walk.then_times = times;
}

/* The menu a wheel notch goes to: the one under the pointer (si its spot), else the top one that is not tabs (the
 * inventory's list, not its categories) and not a stay menu (si -1). */
const Menu* wheel_menu(float x, float y, int& si)
{
    int mi;
    if (spot_at(x, y, mi, si) && mi < (int)s_menus.size() && si < (int)s_menus[mi].spots.size()) return &s_menus[mi];
    si = -1;
    const Menu* top = nullptr;
    for (const Menu& m : s_menus)
        if (!m.stay && (!top || (top->tabs && !m.tabs) || (top->tabs == m.tabs && m.z > top->z))) top = &m;
    return top;
}

/* Calls a game function on the game's thread with r3.. = a, and returns r3; the registers come back as they were.
 * toc, if not 0, is r2 for it, and CTR is code (ps3_indirect_call goes to the address in CTR). */
uint32_t call_at(ppu_context* ctx, void (*fn)(ppu_context*), uint32_t toc, uint32_t code,
                 std::initializer_list<int64_t> a)
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
    if (toc) ctx->gpr[2] = toc, ctx->ctr = code;
    int i = 0;
    for (int64_t x : a)
        if (i < 8) ctx->gpr[3 + i++] = (uint64_t)x;
    fn(ctx);
    while (g_trampoline_fn) {
        void (*tf)(void*) = g_trampoline_fn;
        g_trampoline_fn = nullptr;
        tf(ctx);
    }
    const uint32_t r3 = (uint32_t)ctx->gpr[3];
    memcpy(ctx->gpr, g, sizeof g);
    memcpy(ctx->fpr, f, sizeof f);
    memcpy(ctx->vr, v, sizeof v);
    ctx->lr = lr, ctx->ctr = ctr, ctx->xer = xer;
    ppu_cr_unpack(ctx, cr);
    return r3;
}

uint32_t call_game(ppu_context* ctx, void (*fn)(ppu_context*), std::initializer_list<int64_t> a)
{
    return call_at(ctx, fn, 0, 0, a);
}

/* The virtual function at off in obj's table: the entry points at a descriptor {code, TOC}. */
uint32_t call_virtual(ppu_context* ctx, uint32_t obj, uint32_t off, std::initializer_list<int64_t> a)
{
    const uint32_t d = vm_read32(vm_read32(obj) + off);
    return call_at(ctx, ps3_indirect_call, vm_read32(d + 4), vm_read32(d), a);
}

/* A game function by its address (the running disc's), with the game's TOC as it is. */
uint32_t call_addr(ppu_context* ctx, uint32_t code, std::initializer_list<int64_t> a)
{
    return call_at(ctx, ps3_indirect_call, (uint32_t)ctx->gpr[2], code, a);
}

/* The code a class's virtual function at off runs; 0 if its table can't be read. */
uint32_t virtual_code(uint32_t vt, uint32_t off)
{
    if (!guest_ok(vt, 4)) return 0;
    const uint32_t d = vm_read32(vt + off);
    return guest_ok(d, 4) ? vm_read32(d) : 0;
}

int menu_var(ppu_context* ctx, int id)
{
    return (int)call_addr(ctx, DES_AT(kGetMenuVarFn), {id});
}

/* Closes the pause page of tab t, as the page switch closes a neighbour of the page it opens. */
void close_page(ppu_context* ctx, int t)
{
    switch (t) {
    case 0: call_addr(ctx, DES_AT(kInvCloseFn), {0}); break;
    case 1: call_addr(ctx, DES_AT(kEquipCloseFn), {0}); break;
    case 2: call_addr(ctx, DES_AT(kStatusCloseFn), {0}); break;
    case 3: call_addr(ctx, DES_AT(kWorldCloseFn), {0}); break;
    default:
        call_addr(ctx, DES_AT(kOptionCloseFn), {0});
        call_addr(ctx, DES_AT(kSystemCloseFn), {0});
        break;
    }
}

/* The pause page of tab t takes R2 and L2 now: it is open and idle, and nothing is over it (every menu shown
 * besides the bar is a part of the page, and the page's own dialog has the pad). */
bool page_ready(ppu_context* ctx, int t)
{
    if (menu_var(ctx, kPages[t].state_var) != kPages[t].idle) return false;
    for (const Menu& o : s_menus) {
        if (o.stay) continue;
        const uint32_t vt = vm_read32(o.owner);
        const bool own = (t == 0 && vt == DES_AT(kInventoryVt)) || (t == 1 && vt == DES_AT(kEquipSlotsVt)) ||
                         (t == 4 && vt == DES_AT(kSystemVt));
        if (!own && !(t == 0 && vt == DES_AT(kInvTabsVt))) return false;   /* a prompt, a list, Options, ... */
        if (own && !(vm_read32(o.owner + kDlgInput) & 0x80000000u)) return false;
    }
    return true;
}

/* A click on another tab of the bar while a page is open: the page switch to that page at once, and the page left
 * closed when the switch doesn't close it (it closes only the new page's two neighbours: from World Tendency to
 * the inventory, World Tendency's statues stayed drawn). Only while the page left would take R2 and L2. The
 * switch plays the tick, and the bar's cursor follows the tab by itself. */
bool turn_page_now(ppu_context* ctx, const Spot& s)
{
    const int from = menu_var(ctx, kBarTabVar), to = s.c;
    if (s.r != 0 || to < 0 || to >= kTabs || from < 0 || from >= kTabs || to == from) return false;
    if (!page_ready(ctx, from)) return false;
    if (!call_addr(ctx, DES_AT(kPageSwitchFn), {kPages[to].open_var})) return false;   /* refused: still closing */
    const int d = (to - from + kTabs) % kTabs;
    if (d != 1 && d != kTabs - 1) close_page(ctx, from);
    if (s_log) fprintf(stderr, "[mm] page %d to %d at once\n", from, to);
    return true;
}

/* Spot s of menu m is a category tab the tabs' step passes over (greyed: a shop's category with nothing in it).
 * The pad never reaches it, and a walk to it went round the others and stopped on one of them. */
bool tab_passed_over(const Menu& m, const Spot& s)
{
    if (vm_read32(m.owner) != DES_AT(kInvTabsVt) || s.r != 0) return false;
    const int first = (int)vm_read32(m.owner + kTabFirst), count = (int)vm_read32(m.owner + kTabCount);
    if (s.c < first || s.c >= first + count) return false;
    const uint32_t skip = vm_read32(m.owner + kTabSkip) + 4u * (uint32_t)s.c;
    return guest_ok(skip, 4) && vm_read32(skip) != 0;
}

/* How a dialog that owns category tabs changes category (its input's left and right), or false if it is none. */
struct CatOwner {
    uint32_t change;   /* its call (this, right, wrap) */
    uint32_t state;    /* where its input needs 2, or 0 */
    bool busy;         /* its input takes nothing while 0x00778AB8 says so */
    bool place;        /* its input sets the list's +0x111 after the call */
    bool ticks;        /* the call plays the tick */
};

bool cat_owner(uint32_t vt, CatOwner& o)
{
    if (vt == DES_AT(kInventoryVt)) o = {DES_AT(kInvCategoryFn), kInvState, true, false, false};
    else if (vt == DES_AT(kDepositVt)) o = {DES_AT(kDepositCategoryFn), 0, false, false, false};
    else if (vt == DES_AT(kTakeVt)) o = {DES_AT(kTakeCategoryFn), kTakeState, false, false, false};
    else if (vt == DES_AT(kShopVt)) o = {DES_AT(kShopCategoryFn), 0, false, true, false};
    else if (vt == DES_AT(kRepairVt)) o = {DES_AT(kRepairCategoryFn), 0, false, true, true};
    else return false;
    return true;
}

/* A click on another category (m the tabs): the category change one press makes, from the tab beside the one
 * clicked. The tabs take one step a frame, so two presses showed the category between for a frame; here the tabs
 * are first set to that neighbour, with the list's place kept for the category left as the step keeps it, and the
 * neighbour's own left as it was. Only while the owner would take left and right. 0: not done; 1: done, the tick
 * still to play; 2: done with the tick. */
int tab_now(ppu_context* ctx, const Menu& m, const Spot& s)
{
    const uint32_t tabs = m.owner;
    const int var = (int)vm_read32(tabs + kTabVar);
    uint32_t dlg = 0;
    CatOwner o{};
    for (const Drawn& d : s_draws) {   /* (drawn: a category with no items gives its list no spots, so no menu) */
        CatOwner c;
        if (d.vis && cat_owner(d.vt, c) && (int)vm_read32(d.dlg + kCatVar) == var) dlg = d.dlg, o = c;
    }
    if (!dlg || s.r != 0) return 0;
    if (!(vm_read32(dlg + kDlgInput) & 0x80000000u) || (o.state && vm_read32(dlg + o.state) != 2)) return 0;
    if (vm_read32(tabs + kTabStepped) != 0 || (o.busy && call_addr(ctx, DES_AT(kMenuBusyFn), {}) != 0)) return 0;
    const int first = (int)vm_read32(tabs + kTabFirst), count = (int)vm_read32(tabs + kTabCount);
    const int from = menu_var(ctx, var), to = s.c, last = first + count - 1;
    if (count < 2 || from < first || from > last || to < first || to > last || to == from) return 0;
    const uint32_t skip = vm_read32(tabs + kTabSkip) + 4u * (uint32_t)to;   /* (by the tab's own number) */
    if (!guest_ok(skip, 4) || vm_read32(skip) != 0) return 0;   /* (a step passes it over) */
    const int right = to > from ? 1 : 0;
    const int beside = right ? (to == first ? last : to - 1) : (to == last ? first : to + 1);
    call_game(ctx, func_00720F50, {kCatPlaceVars + from, (int32_t)vm_read32(dlg + kCatPlace)});
    call_game(ctx, func_00720F50, {var, beside});
    vm_write32(dlg + kCatPlace, (uint32_t)menu_var(ctx, kCatPlaceVars + beside));
    call_addr(ctx, o.change, {dlg, right, 1});
    if (o.place) vm_write8(dlg + kListPlace, 1);
    const int now = menu_var(ctx, var);
    if (s_log) fprintf(stderr, "[mm] category %d to %d of 0x%08X at once (%d)\n", from, to, dlg, now);
    return now != to ? 0 : o.ticks ? 2 : 1;
}

/* Selects spot s of menu m at once, as the menu's own input code does it, with one tick; false where this menu
 * can't be (the walk does it then). Only while the menu's dialog takes input. Read from the game's code (docs/114
 * section 3.1):
 * - The pause menu's bar, while it has the pad: its tab is a menu variable (0x4B). Its step (0x0079B2C0) sets it,
 *   then runs vt+0x80 (the help line), +0x90 and +0x88 (the cursor); the bytes +0x180, +0x181 and +0x182 have its
 *   update run the same three. +0x184 is only the tab it placed last.
 * - System: its row is +0xFC (0 to 2), and its update places the cursor and the help line from it every frame. Its
 *   input keeps to row 0 while +0xF5 is set, and steps over row 1 (Load Profile) while +0xF7 is.
 * - A list (DsMenuDlgListbox's: the inventory, ...; any class that keeps its cursor placement at vt+0x8C): the row
 *   in its window is +0x100, of +0xF4 rows, from item +0x108 of +0x104. Its step (0x00780E80) sets the row and the
 *   byte +0x111 (the inventory's update then places the cursor), then calls vt+0x84(this, up, moved): the class's
 *   "row moved", the tick with it. A row in the window with an item needs no scrolling, so the same does it, and
 *   then places the cursor (vt+0x8C, which clears +0x111), as an NPC's talk menu (DsMenuDlgTalkMsgbox) does after
 *   a step: its update places nothing.
 * - A button group (DsMenuDlgButtonGroup and the classes that keep its vt+0x88 or its input at vt+0x8C: the
 *   inventory's command menu, the equipment's slots): its step (0x00777A18) sets the row +0xF4 (the column +0xF8)
 *   and calls vt+0x84(this, moved), the class's "moved" (the command menu's and the equipment's play the tick); its
 *   update places the cursor only after a step, so 0x00776F68 places it. (The equipment's own vt+0x88 changes only
 *   a step from row 6, column 0, and its grid is 2 by 9; its update shows the slot of the row and column every
 *   frame.)
 * - Options: its row is menu variable 0x51. Its step up (0x007CC598, from the row below) sets it, the help line
 *   for it and every row's arrows; 0x007CA320 then places the cursor. DEFAULT (OK's column 1, row 12) is the step
 *   right from OK (0x007CA7E0), then OK's row redrawn (0x007CAA70), as its input does it.
 * - The category tabs (the inventory's, the storage's, the shops'): tab_now.
 * - The profile list (LOAD GAME's, and System's Load Profile): its row is +0xF8, 0 to 3. Its step goes one row with
 *   no row skipped, and its update places the cursor from it every frame.
 * - The system layer's prompts (yes/no, Drop's count): the button is +0x110, from 1. Its steps (vt+0x8C, +0x90)
 *   move it by one, and its update places the cursor from it every frame while it is open (+0xF8 is 0) and its kind
 *   (menu variable 0x11) takes left and right.
 * - The bar with a page open: turn_page_now. */
bool select_now(ppu_context* ctx, const Menu& m, const Spot& s)
{
    if (m.cur < 0 || !grid_spot(s)) return false;
    const uint32_t vt = vm_read32(m.owner);
    if (vt == DES_AT(kStartTabVt) && m.stay) return turn_page_now(ctx, s);   /* (the page has the pad, not the bar) */
    const bool cats = vt == DES_AT(kInvTabsVt);   /* (their owner has the pad, not the tabs) */
    if (!cats && !(vm_read32(m.owner + kDlgInput) & 0x80000000u)) return false;   /* a dialog over it has the pad */
    bool tick = true;
    if (cats) {
        const int done = tab_now(ctx, m, s);
        if (!done) return false;
        tick = done == 1;
    } else if (vt == DES_AT(kStartTabVt) && !m.stay) {
        if (s.r != 0 || s.c < 0 || s.c >= kTabs) return false;
        call_game(ctx, func_00720F50, {kBarTabVar, s.c});
        for (uint32_t k = 0x180; k <= 0x182; k++) vm_write8(m.owner + k, 1);
    } else if (vt == DES_AT(kSystemVt)) {
        if (s.c != 0 || s.r < 0 || s.r > 2) return false;
        if ((vm_read8(m.owner + 0xF5) && s.r != 0) || (vm_read8(m.owner + 0xF7) && s.r == 1)) return false;
        vm_write32(m.owner + 0xFC, (uint32_t)s.r);
    } else if (virtual_code(vt, 0x8C) == DES_AT(kListPlaceFn)) {
        for (const Spot& t : m.spots)
            if (grid_spot(t) && t.c != 0) return false;   /* (a list shown as a grid of icons: its columns, not here) */
        const int rows = (int)vm_read32(m.owner + kListRows), top = (int)vm_read32(m.owner + kListTop);
        const int items = (int)vm_read32(m.owner + kListItems), from = (int)vm_read32(m.owner + kListRow);
        if (s.c != 0 || s.r < 0 || s.r >= rows || top < 0 || top + s.r >= items) return false;
        vm_write32(m.owner + kListRow, (uint32_t)s.r);
        vm_write8(m.owner + kListPlace, 1);
        call_virtual(ctx, m.owner, 0x84, {m.owner, s.r < from ? 1 : 0, 1});
        call_virtual(ctx, m.owner, 0x8C, {m.owner});   /* the cursor placed now (it clears +0x111) */
        tick = false;
    } else if (virtual_code(vt, 0x88) == DES_AT(kGroupNoHookFn) || virtual_code(vt, 0x8C) == DES_AT(kGroupInputFn)) {
        const int rows = (int)vm_read32(m.owner + kGroupRows), cols = (int)vm_read32(m.owner + kGroupCols);
        if (s.r < 0 || s.r >= rows || s.c < 0 || s.c >= cols) return false;
        vm_write32(m.owner + kGroupRow, (uint32_t)s.r);
        vm_write32(m.owner + kGroupCol, (uint32_t)s.c);
        call_addr(ctx, DES_AT(kGroupPlaceFn), {m.owner});
        call_virtual(ctx, m.owner, 0x84, {m.owner, 1});
        tick = false;
    } else if (vt == DES_AT(kOptionVt)) {
        const bool dflt = s.r == kOptionOk && s.c == 1;
        if ((s.c != 0 && !dflt) || s.r < 0 || s.r > kOptionOk) return false;
        if (!dflt || menu_var(ctx, kOptionRowVar) != kOptionOk) {
            call_addr(ctx, DES_AT(kOptionUpFn), {m.owner, 0, s.r + 1, 0});
            call_addr(ctx, DES_AT(kOptionPlaceFn), {m.owner});
        }
        if (dflt) {   /* (the step right plays the tick) */
            call_addr(ctx, DES_AT(kOptionRightFn), {m.owner, 0, kOptionOk, 0, 0});
            call_addr(ctx, DES_AT(kOptionRowFn), {m.owner, kOptionOk});
            tick = false;
        }
    } else if (vt == DES_AT(kProfileVt)) {
        if (s.c != 0 || s.r < 0 || s.r > 3) return false;
        vm_write32(m.owner + 0xF8, (uint32_t)s.r);
    } else if (vt == DES_AT(kPromptVt)) {
        const int kind = (int)call_addr(ctx, DES_AT(kGetMenuVarFn), {kPromptKindVar});
        if (kind == 0 || kind == 1 || kind == 6 || kind == 9 || vm_read32(m.owner + kPromptState) != 0) return false;
        const Spot& at = m.spots[m.cur];
        if (s.r != 0 || at.r != 0 || s.c < 0 || (int)vm_read32(m.owner + kPromptButton) != at.c + 1) return false;
        vm_write32(m.owner + kPromptButton, (uint32_t)(s.c + 1));
    } else {
        return false;
    }
    if (tick) call_game(ctx, func_0072D970, {0});   /* the menu's tick, as a step plays it */
    if (s_log) fprintf(stderr, "[mm] select %d_%d of 0x%08X at once\n", s.r, s.c, m.owner);
    return true;
}

void handle_events(ppu_context* ctx)
{
    PS3_KNOB(s_walk_only, ps3_env_int("DES_MENU_MOUSE_WALK", 0));
    std::vector<Ev> evs;
    {
        std::lock_guard<std::mutex> lk(s_ev_mu);
        evs.swap(s_evs);
    }
    for (size_t k = 0; k < evs.size(); k++) {
        if (s_walk.on && s_walk.direct > 0) {   /* a select at once not shown yet: the rest wait for it, so that a
                                                   click right after reads the cursor where it is, not where it was */
            std::lock_guard<std::mutex> lk(s_ev_mu);
            s_evs.insert(s_evs.begin(), evs.begin() + (std::ptrdiff_t)k, evs.end());
            break;
        }
        const Ev& e = evs[k];
        switch (e.kind) {
        case kEvClick: {
            if (s_menus.size() == 1 && s_menus[0].start) {   /* the title: anywhere */
                if (!pressing()) press(kPadStart);
                break;
            }
            stop_slide();
            int mi, si;
            if (!spot_at(e.x, e.y, mi, si) || mi >= (int)s_menus.size()) break;
            const Menu& m = s_menus[mi];
            if (si >= (int)m.spots.size()) break;
            const Spot& sp = m.spots[si];
            const int dir = sp.field ? (e.x < (sp.box[0] + sp.box[2]) * 0.5f ? -1 : 1) : sp.arrow;   /* (a value's half) */
            if (dir) {   /* an arrow: its button, held while the mouse button is */
                s_walk.on = false;
                drop_presses();
                s_mouse_down = true;
                const int row = row_spot(m, si);
                if ((dir == 1 || dir == -1) && row >= 0 && !on_cursor_row(m, si)) {   /* another row's: there first */
                    walk_then(ctx, m, row, arrow_button(m, dir), 1);
                    break;
                }
                press(arrow_button(m, dir), 1, true);
                s_held_owner = m.owner;
                if (s_log) fprintf(stderr, "[mm] arrow %+d of 0x%08X held\n", dir, m.owner);
                break;
            }
            if (m.spots[si].slider) {   /* a slider: its thumb to the pointer, dragged while the mouse button is down */
                s_walk.on = false;
                stop_slide();
                drop_presses();
                s_mouse_down = true;
                const int row = row_spot(m, si);
                if (row >= 0 && !on_cursor_row(m, si)) {   /* another row's: there first */
                    walk_then(ctx, m, row, 0, 0);
                    s_walk.then_slide = m.spots[si].slider;
                    s_slide.target = e.x;
                    break;
                }
                start_slide(m, m.spots[si], e.x);
                break;
            }
            if (tab_passed_over(m, m.spots[si])) {   /* a greyed category, as a shop's: no press reaches it */
                if (s_log) fprintf(stderr, "[mm] click on greyed tab %d of 0x%08X: nothing\n", m.spots[si].c, m.owner);
                break;
            }
            /* Where the menu can be, the spot is selected at once; the walk then only confirms (or walks, if the
             * select did not show). */
            const bool now = !s_walk_only && m.cur != si && select_now(ctx, m, m.spots[si]);
            start_walk(m, m.spots[si]);
            if (now) s_walk.direct = kDirectWait;
            break;
        }
        case kEvDrag:   /* a slider's thumb follows (one being slid, or one a walk goes to) */
            if (s_mouse_down) s_slide.target = e.x, s_slide.end = 0;
            break;
        case kEvRelease:
            s_mouse_down = false;
            let_go();
            s_held_owner = 0;
            break;
        case kEvBack:
            s_walk.on = false;
            stop_slide();
            drop_presses();
            press(kPadCircle);
            break;
        case kEvWheel: {
            stop_slide();
            int si;
            const Menu* m = wheel_menu(e.x, e.y, si);
            if (!m) break;
            /* Over a menu of one row (tabs, the pause menu's bar) the wheel steps along it, down to the next, as
             * down a list. Over a row with arrows in a taller menu (a number to change) it turns them, up to the
             * right one (a number goes up). Else it goes up and down. */
            bool one_row = true;
            int grid = 0;
            for (const Spot& s : m->spots)
                if (grid_spot(s)) one_row &= s.r == m->spots[0].r, grid++;
            unsigned b = e.n > 0 ? kPadUp : kPadDown;
            const int times = std::min(std::abs(e.n), 8);
            if (one_row && grid > 1 && !s_learnt[m->key].across) {
                b = e.n > 0 ? col_pair(*m)[1] : col_pair(*m)[0];
            } else if (si >= 0 && row_has_arrows(*m, si)) {
                b = e.n > 0 ? col_pair(*m)[0] : col_pair(*m)[1];
                const int row = row_spot(*m, si);
                if (row >= 0 && !on_cursor_row(*m, si)) {   /* another row's number: there first */
                    const Spot& to = m->spots[row];
                    if (s_walk.on && s_walk.owner == m->owner && s_walk.r == to.r && s_walk.c == to.c &&
                        s_walk.then == b) {   /* (more notches while it walks there) */
                        s_walk.then_times = std::min(s_walk.then_times + times, 8);
                        break;
                    }
                    walk_then(ctx, *m, row, b, times);
                    break;
                }
            }
            s_walk.on = false;
            press(b, times);
            break;
        }
        }
    }
}

/* Keeps s_stay_quiet: the game seconds since the stay menu's page last changed (or it came up). */
void watch_stay()
{
    const Menu* m = nullptr;
    for (const Menu& t : s_menus)
        if (t.stay) m = &t;
    if (!m) {
        s_stay_owner = 0;
        return;
    }
    if (m->owner != s_stay_owner || (m->cur >= 0 && m->cur != s_stay_cur)) {
        s_stay_owner = m->owner;
        s_stay_cur = m->cur;
        s_stay_quiet = 0.0f;
        return;
    }
    s_stay_quiet += des_frame_step();
}

void end_frame(ppu_context* ctx)
{
    build_menus(ctx);
    publish();
    watch_stay();
    if (s_held_owner && !menu_of(s_held_owner, false) && !menu_of(s_held_owner, true)) {   /* its menu went */
        let_go();
        s_held_owner = 0;
    }
    handle_events(ctx);
    walk_step();
    slide_step();
    s_draws.clear();
}

}  // namespace

/* For each dialog the game draws (des_hud.cpp's wrapper of 0x00F60BD0), on the game's thread. */
extern "C" void des_mm_draw_enter(ppu_context* ctx, uint32_t dlg)
{
    PS3_KNOB(s_on, ps3_env_int("DES_MENU_MOUSE", 0));
    PS3_KNOB(s_log, ps3_env_int("DES_MENU_MOUSE_LOG", 0));
    if (!s_on || !guest_ok(dlg, 4)) return;
    /* The first draw after this thread read the pad: the game has begun its next frame, and the pass before is whole.
     * Each frame it reads the pad once, then draws its own layers (the pause menu, the inventory) and then the
     * system's (the prompts, over them). Else, where it read none (a loading screen), a dialog drawn again.
     * (Not the flip: it is counted on another thread, so at a high frame rate it lands in the middle of a pass, and
     * the menus of half a pass came and went. Nor a dialog drawn again alone: that cuts the frame where the oldest
     * dialog still drawn is, the system's layer, so the prompts came first, under the menus they are drawn over, and
     * a click on OK went to the row beneath.) */
    static thread_local unsigned seen = 0;
    bool again = t_pad_reads != seen && !s_draws.empty();
    seen = t_pad_reads;
    for (size_t k = 0; k < s_draws.size() && !again; k++) again = s_draws[k].dlg == dlg;
    if (again) end_frame(ctx);
    if (s_draws.size() >= 512) return;
    Drawn d{};
    d.dlg = dlg;
    d.vt = vm_read32(dlg);
    for (uint32_t k = 0; k < 4; k++) {
        d.cur[k] = rdf(dlg + kDlgRect + 4 * k);
        d.lay[k] = rdf(dlg + kDlgLayout + 4 * k);
    }
    d.vis = vm_read8(dlg + kDlgVisible) != 0;
    const uint32_t rend = (uint32_t)ctx->gpr[4];
    if (guest_ok(rend, 4)) {
        float m[16];
        for (uint32_t k = 0; k < 16; k++) m[k] = rdf(rend + kRendMatrix + 4 * k);
        d.at[0] = m[0], d.at[1] = m[5], d.at[2] = m[12], d.at[3] = m[13];
        d.at_ok = m[0] > 0.0f && m[5] > 0.0f && std::fabs(m[1]) < 1e-4f && std::fabs(m[4]) < 1e-4f &&
                  std::fabs(m[15] - 1.0f) < 1e-4f;
    }
    s_draws.push_back(d);
}

/* The buttons to press on this read of the pad (cellPad's title hook, through des_options.cpp): each press is held
 * for DES_MENU_MOUSE_PRESS frames and as many packets the game took, then let go for as long. */
extern "C" unsigned des_mm_pad_buttons(void)
{
    t_pad_reads++;
    PS3_KNOB(s_press_frames, std::max(1, ps3_env_int("DES_MENU_MOUSE_PRESS", 1)));
    const unsigned flip = des_flip_count();
    const unsigned taken = g_pad0_reads - g_pad0_empty_reads;
    const unsigned n = (unsigned)s_press_frames;
    std::lock_guard<std::mutex> lk(s_pad_mu);
    while (!s_presses.empty()) {
        Press& p = s_presses.front();
        if (!p.started) p = {p.mask, true, true, flip, taken, p.held};
        if (p.on) {
            if (p.held || flip - p.f0 < n || taken - p.d0 < n) return p.mask;
            p.on = false;
            p.f0 = flip;
            p.d0 = taken;
        }
        if (flip - p.f0 < n || taken - p.d0 < n) return 0;
        s_presses.pop_front();
    }
    return 0;
}

/* ---- For the UI (port/ui/src/game_menu.cpp), on its threads. Canvas units: the game's 1280x720, x from the left
 * of the 16:9 middle. ---- */

/* 1 while a menu of the game with spots is on screen (and DES_MENU_MOUSE is on). */
extern "C" int des_menu_mouse_open(void)
{
    std::lock_guard<std::mutex> lk(s_pub_mu);
    return s_pub_open && s_on && des_flip_count() - s_pub_flip < kStaleFlips ? 1 : 0;
}

/* The canvas the picture shows: left, top, right, bottom. */
extern "C" void des_menu_mouse_view(float view[4])
{
    const float side = des_hud_side_width(), top = des_hud_top_height();
    view[0] = -side;
    view[1] = -top;
    view[2] = kCanvasW + side;
    view[3] = kCanvasH + top;
}

/* The spot under (x, y): its box, and 1; 0 if none. */
extern "C" int des_menu_mouse_hit(float x, float y, float box[4])
{
    std::lock_guard<std::mutex> lk(s_pub_mu);
    if (!s_pub_open || des_flip_count() - s_pub_flip >= kStaleFlips) return 0;
    for (const PubSpot& p : s_pub)
        if (inside(p.box, x, y)) {
            memcpy(box, p.box, sizeof p.box);
            return 1;
        }
    return 0;
}

extern "C" void des_menu_mouse_click(float x, float y) { post({kEvClick, x, y, 0}); }
extern "C" void des_menu_mouse_release(void) { post({kEvRelease, 0.0f, 0.0f, 0}); }
extern "C" void des_menu_mouse_back(void) { post({kEvBack, 0.0f, 0.0f, 0}); }

/* The pointer is at (x, y) with the left button down: a slider's thumb follows it. */
extern "C" void des_menu_mouse_drag(float x, float y) { post({kEvDrag, x, y, 0}); }

/* notches > 0: away from the player (up a list), at (x, y). */
extern "C" void des_menu_mouse_wheel(int notches, float x, float y)
{
    if (notches) post({kEvWheel, x, y, notches});
}
