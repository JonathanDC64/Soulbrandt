// Written for Demon's Souls. Seamless co-op's edits to the area scripts (docs/146), made in memory as
// each script is read: no file changes, and only while co-op runs (des_seamless.h). The scripts are Lua source; the
// game reads each through one of two Lua readers, hooked at their first instruction (port/insn_hooks.txt,
// des_coop_hook -> des_seamless_script). The edited text is a copy on the guest heap, kept for the next load of the
// same script.
//
// global_event.lua, the scripts every area shares (all or nothing: if one edit doesn't fit, the file stays as it is):
//   - a boss kill keeps the session: a blue phantom isn't sent home (no load wait, no trip-home animation, no
//     "Objective met. Resurrecting in your own world.", no leaving announcement, no flag rollback), and the
//     multiplayer walls drop as at a session's end;
//   - no rating screen after a kill: a short timer goes on to the next step instead.
// Each area's script:
//   - the boss rooms' party walls drop as in solo play;
//   - a guest doesn't play the warp-out pose at a boss's death;
//   - the Archstones: the host may use them in a session (the guests then follow it, des_seamless_game.cpp); a guest
//     still gets "You can't return during multiplayer."
#include "des_seamless.h"

#include "ps3emu/ppu_lift.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <string>

extern "C" void ds_malloc(ppu_context* ctx);   // des_heap.cpp: the guest heap

namespace {

// [a, e): the function `name` in t, from its "function" line to the next function's.
bool func_span(const std::string& t, const std::string& name, size_t* a, size_t* e)
{
    const std::regex re("\nfunction " + name + "\\s*\\(");
    std::smatch m;
    if (!std::regex_search(t, m, re)) return false;
    *a = (size_t)m.position(0) + 1;
    const size_t next = t.find("\nfunction ", *a + 5);
    *e = next == std::string::npos ? t.size() : next + 1;
    return true;
}

// Replaces the first `old` inside function `name`; false if the function or the text isn't there.
bool replace_in(std::string& t, const std::string& name, const std::string& old, const std::string& with)
{
    size_t a, e;
    if (!func_span(t, name, &a, &e)) return false;
    const size_t at = t.find(old, a);
    if (at == std::string::npos || at + old.size() > e) return false;
    t.replace(at, old.size(), with);
    return true;
}

bool replace_func(std::string& t, const std::string& name, std::string body, const std::string& nl)
{
    size_t a, e;
    if (!func_span(t, name, &a, &e)) return false;
    if (nl != "\n")
        for (size_t p = 0; (p = body.find('\n', p)) != std::string::npos; p += nl.size()) body.replace(p, 1, nl);
    t.replace(a, e - a, body + nl);
    return true;
}

// global_event.lua's edits; false (t unchanged by the caller) if one doesn't fit.
bool edit_global(std::string& t, const std::string& nl)
{
    // BlockClear2: in the blue phantom's branch only, no load wait (it waits for a trip home that seamless never makes)
    size_t a, e;
    if (!func_span(t, "BlockClear2", &a, &e)) return false;
    const size_t w = t.find("elseif proxy:IsWhiteGhost() == true then", a);
    const size_t b = t.find("elseif proxy:IsBlackGhost() == true then", a);
    if (w == std::string::npos || b == std::string::npos || !(w < b && b < e)) return false;
    const size_t lw = t.find("proxy:SetLoadWait();", w);
    if (lw == std::string::npos || lw > b) return false;
    t.insert(lw, "--SEAMLESS ");
    // BlockClear2: the trip-home animation for black phantoms only
    if (!replace_in(t, "BlockClear2", "if proxy:IsWhiteGhost() == true or proxy:IsBlackGhost() == true then",
                    "if proxy:IsBlackGhost() == true then --SEAMLESS a blue phantom stays"))
        return false;
    // BlockClear2_1: no rating screen; the screen's close was the only way on to BlockClear2_2
    if (!replace_in(t, "BlockClear2_1", "proxy:OnPlayerAssessMenu(4050,ClearBossId,\"BlockClear2_2\",once);",
                    "proxy:OnKeyTime2(4050,\"BlockClear2_2\",0.5,0,7,once); --SEAMLESS no rating screen"))
        return false;
    // BlockClear2_2: no "leaving" announcement
    if (!replace_in(t, "BlockClear2_2", "proxy:CustomLuaCallStart( 4063 , proxy:GetLocalPlayerId() );",
                    "--SEAMLESS proxy:CustomLuaCallStart( 4063 , proxy:GetLocalPlayerId() );"))
        return false;
    // MissionSuccessed: no recall ("Objective met. Resurrecting in your own world.")
    if (!replace_in(t, "MissionSuccessed", "proxy:RecallMenuEvent( 0, 8933 );", "--SEAMLESS proxy:RecallMenuEvent( 0, 8933 );"))
        return false;
    if (!replace_func(t, "BlockClear2_3",
                      "function BlockClear2_3(proxy,param)\n"
                      "\t--SEAMLESS: everyone stays in the world; no flag rollback, no trip home\n"
                      "\tproxy:SetSubMenuBrake( false );\n"
                      "\tproxy:SetEventFlag(4047, false);\n"
                      "\tproxy:SetEventFlag( 4000, false );\n"
                      "\tif proxy:IsAlive(10000) == true then\n"
                      "\t\tproxy:SetEventFlag( 4067, false );\n"
                      "\tend\n"
                      "end\n", nl))
        return false;
    if (!replace_func(t, "BlockClear2_3Leave",
                      "function BlockClear2_3Leave(proxy,param)\n"
                      "\t--SEAMLESS: the room stays; the multiplayer walls go as at a session's end\n"
                      "\tOnEvent_4035(proxy,param);\n"
                      "end\n", nl))
        return false;
    // Deaths: a blue phantom's own death keeps it in the session: no leaving announcement, no flag
    // rollback, no recall (MissionDeadFailed's 8953 hides and freezes it for the trip home; its close was the only way
    // on to PartyGhostDeath_2, so a short timer goes there instead: slot 6, 4000's timers use 0, 1 and 3) ...
    if (!replace_in(t, "PartyGhostDeath", "proxy:CustomLuaCallStart( 4063 , proxy:GetLocalPlayerId() );",
                    "--SEAMLESS proxy:CustomLuaCallStart( 4063 , proxy:GetLocalPlayerId() );"))
        return false;
    if (!replace_in(t, "PartyGhostDeath", "proxy:SetFlagInitState(2);", "--SEAMLESS proxy:SetFlagInitState(2);"))
        return false;
    if (!replace_in(t, "PartyGhostDeath_1", "MissionDeadFailed(proxy,param);", "--SEAMLESS MissionDeadFailed(proxy,param);"))
        return false;
    if (!replace_in(t, "PartyGhostDeath_1", "proxy:OnRequestMenuEnd(4000, \"PartyGhostDeath_2\");",
                    "proxy:OnKeyTime2(4000,\"PartyGhostDeath_2\",1.0,0,6,once); --SEAMLESS no recall menu"))
        return false;
    // ... and instead of the trip home, a summoned reload into the host's world (des_seamless_game.cpp puts it where
    // the host last arrived: the area's start)
    if (!replace_func(t, "PartyGhostDeath_2",
                      "function PartyGhostDeath_2(proxy,param)\n"
                      "\t--SEAMLESS: the phantom comes back in the host's world and stays in the session\n"
                      "\tif ClearBoss == false then\n"
                      "\t\tproxy:RequestFullRecover();\n"
                      "\t\tproxy:SummonedMapReload();\n"
                      "\t\tproxy:SetEventFlag(4047, false);\n"
                      "\t\tproxy:SetEventFlag(4000, false);\n"
                      "\tend\n"
                      "end\n", nl))
        return false;
    // The host's death: the host's respawn takes everyone back together (des_seamless_game.cpp's follow), so a blue
    // phantom isn't recalled and sent home (MissionFailed's 8943, HostDead_1's trip).
    if (!replace_func(t, "HostDead",
                      "function HostDead(proxy, param)\n"
                      "\t--SEAMLESS: the host's respawn takes the group back together; no recall, no trip home\n"
                      "\tif proxy:IsHost() == true or proxy:IsGreyGhost() == true then\n"
                      "\t\treturn;\n"
                      "\tend\n"
                      "\tprint(\"SEAMLESS HostDead\");\n"
                      "end\n", nl))
        return false;
    return true;
}

// An area's script: the party walls, the warp-out pose and the Archstones. Returns the number of edits.
int edit_area(std::string& t, bool nexus)
{
    int n = 0;
    auto sub = [&](const std::regex& re, const char* with) {
        size_t count = 0;
        auto it = std::sregex_iterator(t.begin(), t.end(), re);
        for (auto end = std::sregex_iterator(); it != end; ++it) count++;
        if (!count) return;
        t = std::regex_replace(t, re, with);
        n += (int)count;
    };
    sub(std::regex("(function Check_\\d+_PT\\(proxy\\)\\s*\\r?\\n)(\\s*)if proxy:IsInParty\\(\\) == false then"),
        "$1$2if true then --SEAMLESS: the walls drop as in solo play");
    sub(std::regex("(IsClient\\(\\) == true then\\s*\\r?\\n\\s*)proxy:PlayAnimation\\(\\s*LOCAL_PLAYER\\s*,\\s*8284\\s*\\);"),
        "$1--SEAMLESS proxy:PlayAnimation( LOCAL_PLAYER , 8284 );");
    if (nexus) return n;
    // The Archstones: the OnEvent_<id>[_k] handlers that show "Return to the Nexus?" (10010710) or "You can't
    // return during multiplayer." (10010720).
    std::set<std::string> ids;
    const std::regex handler("\nfunction OnEvent_(\\d+)(?:_\\d+)?\\s*\\(");
    for (auto it = std::sregex_iterator(t.begin(), t.end(), handler); it != std::sregex_iterator(); ++it) {
        const size_t a = (size_t)it->position(0);
        const size_t e = t.find("\nfunction ", a + 5);
        const std::string body = t.substr(a, (e == std::string::npos ? t.size() : e) - a);
        if (body.find("10010710") != std::string::npos || body.find("10010720") != std::string::npos)
            ids.insert((*it)[1].str());
    }
    for (const std::string& id : ids) {
        // in this stone's handlers, the session check passes for the host
        const std::regex fn("\nfunction OnEvent_" + id + "(?:_\\d+)?\\s*\\(");
        std::string out;
        size_t from = 0;
        for (auto it = std::sregex_iterator(t.begin(), t.end(), fn); it != std::sregex_iterator(); ++it) {
            const size_t a = (size_t)it->position(0);
            const size_t e0 = t.find("\nfunction ", a + 5);
            const size_t e = e0 == std::string::npos ? t.size() : e0;
            out += t.substr(from, a - from);
            std::string seg = t.substr(a, e - a);
            const std::regex check("proxy:IsSession\\(\\)\\s*==\\s*false");
            const auto k = std::distance(std::sregex_iterator(seg.begin(), seg.end(), check), std::sregex_iterator());
            seg = std::regex_replace(seg, check, "(proxy:IsSession() == false or proxy:IsClient() == false)");
            n += (int)k;
            out += seg;
            from = e;
        }
        out += t.substr(from);
        t = out;
        // the prompt is offered with or without a session
        sub(std::regex("(OnDistanceActionAttribute\\(\\s*" + id + "\\s*,[^)]*?,\\s*)ATTR_NOSESSION(\\s*,\\s*everytime\\s*\\))"),
            "$1ATTR_SESSION + ATTR_NOSESSION$2");
    }
    return n;
}

// The edited copies, by the original's size and hash; 0: the script is left as it is.
std::mutex sLock;
std::map<std::pair<uint32_t, uint64_t>, std::pair<uint32_t, uint32_t>> sCopies;   // -> guest address, size

uint64_t fnv(const std::string& s)
{
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return h;
}

uint32_t guest_alloc(ppu_context* ctx, uint32_t size)
{
    const uint64_t r3 = ctx->gpr[3];
    ctx->gpr[3] = size;
    ds_malloc(ctx);
    const uint32_t a = (uint32_t)ctx->gpr[3];
    ctx->gpr[3] = r3;
    return a;
}

}  // namespace

// A Lua reader is about to hand the parser the script at buf (size bytes): the address and size of the edited copy,
// or false to leave it.
bool des_seamless_script(ppu_context* ctx, uint32_t buf, uint32_t size, uint32_t* out, uint32_t* out_size)
{
    if (des::seamless::active() == des::seamless::Mode::Off || size < 64 || size > (4u << 20)) return false;
    // only the area scripts: global_event.lua and m0X_YY_ZZ_WW.lua, recognised by what they define
    static const char kGlobal[] = "function BlockClear2_3Leave(";
    static const char kArea[] = "function Initialize_m0";
    std::string t(size, '\0');
    for (uint32_t i = 0; i < size; i++) t[i] = (char)vm_read8(buf + i);
    const bool global = t.find(kGlobal) != std::string::npos;
    const size_t ar = t.find(kArea);
    if (!global && ar == std::string::npos) return false;
    const auto key = std::make_pair(size, fnv(t));
    {
        std::lock_guard<std::mutex> g(sLock);
        auto it = sCopies.find(key);
        if (it != sCopies.end()) {
            if (!it->second.first) return false;
            *out = it->second.first;
            *out_size = it->second.second;
            return true;
        }
    }
    const std::string nl = t.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string ed = t;
    int edits = 0;
    if (global) {
        if (edit_global(ed, nl)) edits = 13;
        else ed = t;
    } else {
        static const char kNexus[] = "function Initialize_m01_00_00_00(";   // the Nexus: no Archstone edits
        edits = edit_area(ed, t.compare(ar, sizeof kNexus - 1, kNexus) == 0);
    }
    uint32_t addr = 0, n = 0;
    if (edits && ed != t) {
        n = (uint32_t)ed.size();
        addr = guest_alloc(ctx, n + 16);
        if (addr) for (uint32_t i = 0; i < n; i++) vm_write8(addr + i, (uint8_t)ed[i]);
    }
    std::fprintf(stderr, "[coop] script %s: %s\n", global ? "global_event.lua" : t.substr(ar + 20, 12).c_str(),
                 addr ? (std::to_string(edits) + " edits for co-op").c_str() : global ? "left as it is (an edit didn't fit)"
                                                                                      : "nothing to edit");
    std::lock_guard<std::mutex> g(sLock);
    sCopies[key] = {addr, n};
    if (!addr) return false;
    *out = addr;
    *out_size = n;
    return true;
}
