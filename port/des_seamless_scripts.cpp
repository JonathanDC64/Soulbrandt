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
//   - no rating screen after a kill: a short timer goes on to the next step instead;
//   - a blue phantom's death keeps it in the session: it comes back where the host last arrived, and in a boss fight
//     it first watches until the fight ends (the boss dead, or the host dead: then it goes back with the host).
// Each area's script:
//   - a boss fight is marked from the boss's health bar to its clearing (for the watch above);
//   - the boss rooms' party walls drop as in solo play;
//   - a guest doesn't play the warp-out pose at a boss's death;
//   - the Archstones: the host may use them in a session (the guests then follow it, des_seamless_game.cpp); a guest
//     still gets "You can't return during multiplayer.";
//   - a boss's soul: a blue phantom may take its own, as the host takes its own.
// And from each area's script, the flags that move forward in every player's world (progress_flags), the soul
// pickups each player makes for itself (own_flags) and the Nexus's cutscenes each player sees for itself
// (scene_flags), for des_seamless_game.cpp.
#include "des_seamless.h"

#include "ps3emu/ppu_lift.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <vector>

extern "C" void ds_malloc(ppu_context* ctx);   // des_heap.cpp: the guest heap
// des_seamless_game.cpp: an area script of world was just read (its Nexus cutscenes, if any), before the area starts
void des_seamless_area_read(uint32_t world, const std::vector<uint32_t>& scenes);

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
    // (with the boss already dead too: the phantom isn't sent home after a kill either)
    if (!replace_func(t, "PartyGhostDeath_1",
                      "function PartyGhostDeath_1(proxy,param)\n"
                      "\t--SEAMLESS: no recall menu and no trip home; PartyGhostDeath_2 brings the phantom back\n"
                      "\tproxy:NotNetMessage_begin();\n"
                      "\t\tproxy:OnKeyTime2(4000,\"PartyGhostDeath_2\",1.0,0,6,once);\n"
                      "\tproxy:NotNetMessage_end();\n"
                      "end\n", nl))
        return false;
    // ... and instead of the trip home, a summoned reload into the host's world (des_seamless_game.cpp puts it where
    // the host last arrived: the area's start). In a boss fight (SeamlessBossFight: from the boss's health bar on,
    // edit_area) the phantom first watches until the fight ends: it looks again each second (two timer slots in turn,
    // so a handler never sets up its own slot) until the boss is dead, or the host has died: the host's respawn
    // then takes everyone back (des_seamless_game.cpp's follow), and this phantom goes with it at full health.
    if (!replace_func(t, "PartyGhostDeath_2",
                      "function PartyGhostDeath_2(proxy,param)\n"
                      "\t--SEAMLESS: the phantom comes back in the host's world and stays in the session\n"
                      "\tif SeamlessHostDown == true then\n"
                      "\t\tproxy:RequestFullRecover();\n"
                      "\t\tproxy:SetEventFlag(4047, false);\n"
                      "\t\tproxy:SetEventFlag(4000, false);\n"
                      "\t\treturn;\n"
                      "\tend\n"
                      "\tif SeamlessBossFight == true and ClearBoss == false then\n"
                      "\t\tif SeamlessWatchSlot == 6 then SeamlessWatchSlot = 8 else SeamlessWatchSlot = 6 end\n"
                      "\t\tproxy:NotNetMessage_begin();\n"
                      "\t\t\tproxy:OnKeyTime2(4000,\"PartyGhostDeath_2\",1.0,0,SeamlessWatchSlot,once);\n"
                      "\t\tproxy:NotNetMessage_end();\n"
                      "\t\treturn;\n"
                      "\tend\n"
                      "\tproxy:RequestFullRecover();\n"
                      "\tproxy:SummonedMapReload();\n"
                      "\tproxy:SetEventFlag(4047, false);\n"
                      "\tproxy:SetEventFlag(4000, false);\n"
                      "end\n", nl))
        return false;
    // The host's death: the host's respawn takes everyone back together (des_seamless_game.cpp's follow), so a blue
    // phantom isn't recalled and sent home (MissionFailed's 8943, HostDead_1's trip). A phantom watching a boss fight
    // goes back with it (PartyGhostDeath_2).
    if (!replace_func(t, "HostDead",
                      "function HostDead(proxy, param)\n"
                      "\t--SEAMLESS: the host's respawn takes the group back together; no recall, no trip home\n"
                      "\tif proxy:IsHost() == true or proxy:IsGreyGhost() == true then\n"
                      "\t\treturn;\n"
                      "\tend\n"
                      "\tSeamlessHostDown = true;\n"
                      "\tprint(\"SEAMLESS HostDead\");\n"
                      "end\n", nl))
        return false;
    // A boss's health bar cleared: its fight is over (BlockClear's kill; see edit_area)
    const std::regex clear("proxy:ClearBossGauge\\s*\\(");
    if (!std::regex_search(t, clear)) return false;
    t = std::regex_replace(t, clear, "SeamlessBossFight = false; proxy:ClearBossGauge(");
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
    // A boss fight is on from the boss's health bar to its clearing (each is a statement of its own in every area's
    // script): a guest who dies in it watches until it ends (global_event.lua's PartyGhostDeath_2)
    sub(std::regex("proxy:SetBossGauge\\s*\\("), "SeamlessBossFight = true; proxy:SetBossGauge(");
    sub(std::regex("proxy:ClearBossGauge\\s*\\("), "SeamlessBossFight = false; proxy:ClearBossGauge(");
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
    // A boss's soul (the prompt 10010180 at the Archstone or the boss's remains): a blue phantom may take its own too,
    // and each player's prompt is its own. A prompt is a network event, so one player's pickup would also fire every
    // other player's (they see the pickup animation) and use up their once-only prompt; registered between
    // NotNetMessage_begin and _end it fires only for the player who took it. (None of these sits in such a block.)
    sub(std::regex("proxy:(OnDistanceActionAttribute\\(\\s*\\d+\\s*,[^\\n]*?,\\s*10010180\\s*,\\s*0\\s*,\\s*180\\s*,\\s*)"
                   "(ATTR_HOST \\+ ATTR_LIVE \\+ ATTR_GREY|possible_chr_type)(\\s*,\\s*once\\s*\\)\\s*;)"),
        "proxy:NotNetMessage_begin(); proxy:$1$2 + ATTR_WHITE$3 proxy:NotNetMessage_end();");
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

// Progress for everyone: the flags of an area's script that a session moves forward in every player's world.
//   - Boss kills: an event whose handler starts the boss clear (CustomLuaCallStart(4050, ...)); every flag its
//     handlers (OnEvent_<id>, OnEvent_<id>_<k>) set.
//   - Doors, levers, elevators, shortcuts, bridges and fog walls: an event whose header comment (--■<id>■<what>■)
//     names one of them, and names no treasure, item, corpse, shop, talk, NPC, checkpoint, host-only wall, cutscene
//     or the like. Its ID.
//   - The Archstones' seal opened (the Nexus, after the Monumental's tale): its ID and every flag its handlers set,
//     a list of their own (seal). Its handlers run between NotNetMessage_begin and _end, so the game never tells the
//     others: whoever hears the tale opens the seal, and des_seamless_game.cpp takes it to the host's world too.
// Flags below 15000 only. The headers are Shift-JIS; the words below are their bytes.
const char* const kInclude[] = {
    "\x94\xE0",                                                   // door
    "\x83\x8C\x83\x6F\x81\x5B",                                   // lever
    "\x83\x47\x83\x8C\x83\x78\x81\x5B\x83\x5E",                   // elevator
    "\x83\x56\x83\x87\x81\x5B\x83\x67\x83\x4A\x83\x62\x83\x67",   // shortcut
    "\x8B\xB4",                                                   // bridge
    "\x96\xB6",                                                   // fog
};
const char* const kExclude[] = {
    "\x95\xF3",                                                           // treasure
    "\x83\x41\x83\x43\x83\x65\x83\x80",                                   // item
    "\x83\x7A\x83\x58\x83\x67",                                           // host
    "\x8E\x80\x91\xCC",                                                   // corpse
    "\x83\x56\x83\x87\x83\x62\x83\x76",                                   // shop
    "\x89\xEF\x98\x62",                                                   // talk
    "NPC",                                                                // NPC
    "\x83\x60\x83\x46\x83\x62\x83\x4E\x83\x7C\x83\x43\x83\x93\x83\x67",   // checkpoint
    "\x83\x4E\x83\x89\x83\x43\x83\x41\x83\x93\x83\x67",                   // client
    "\x83\x56\x83\x93\x83\x4F\x83\x8B",                                   // single player
    "\x89\x89\x8F\x6F",                                                   // staging
    "\x95\x5C\x8E\xA6",                                                   // display
    "\x8F\x89\x8A\xFA\x89\xBB",                                           // initialise
    "\x89\xA4\x8E\x71",                                                   // prince
    "\x83\x7C\x83\x8A\x8C\x80",                                           // cutscene
    "\x8A\x4A\x8E\x6E",                                                   // start
    "\x91\x83",                                                           // nest
    "\x93\x47",                                                           // enemy
    "\x88\xDA\x93\xAE",                                                   // move
    "\x93\x9E\x92\x85",                                                   // arrival
    "\x92\x54\x8D\xF5",                                                   // search
    "\x95\xC2",                                                           // close
    "\x83\x43\x83\x93\x83\x74\x83\x48",                                   // info
};
const char kBox[] = "\x81\xA1";   // the headers' separator
const char kSeal[] = "\x97\x76\x90\xCE\x82\xCC\x95\x95\x88\xF3";   // the Archstones' seal (the Nexus: its opening)
constexpr uint32_t kProgressMax = 15000;

// A Shift-JIS lead byte: the character takes two bytes.
bool sjis_lead(unsigned char c) { return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC); }

// Whether text holds word, matched at character boundaries only.
bool sjis_has(const std::string& text, const char* word)
{
    const size_t n = std::strlen(word);
    for (size_t i = 0; i + n <= text.size(); i += sjis_lead((unsigned char)text[i]) ? 2 : 1)
        if (text.compare(i, n, word) == 0) return true;
    return false;
}

size_t skip_space(const std::string& t, size_t i)
{
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) i++;
    return i;
}

// A number at t[i], or -1; i moves past it.
long read_number(const std::string& t, size_t* i)
{
    long v = -1;
    while (*i < t.size() && t[*i] >= '0' && t[*i] <= '9' && v < 1000000) v = (v < 0 ? 0 : v * 10) + (t[(*i)++] - '0');
    return v;
}

std::vector<uint32_t> progress_flags(const std::string& t, std::vector<uint32_t>* boss, std::vector<uint32_t>* seal)
{
    std::set<uint32_t> out, kills, sealed;
    // the functions, by name
    std::map<std::string, std::pair<size_t, size_t>> funcs;
    for (size_t a = t.find("\nfunction "); a != std::string::npos; a = t.find("\nfunction ", a + 1)) {
        size_t i = a + 10, j = i;
        while (j < t.size() && (std::isalnum((unsigned char)t[j]) || t[j] == '_')) j++;
        const size_t e = t.find("\nfunction ", j);
        funcs[t.substr(i, j - i)] = {a, e == std::string::npos ? t.size() : e};
    }
    auto body_has_clear = [&](size_t a, size_t e) {   // CustomLuaCallStart( 4050 ,
        for (size_t p = t.find("CustomLuaCallStart(", a); p != std::string::npos && p < e;
             p = t.find("CustomLuaCallStart(", p + 1)) {
            size_t i = skip_space(t, p + 19);
            if (read_number(t, &i) == 4050 && skip_space(t, i) < t.size() && t[skip_space(t, i)] == ',') return true;
        }
        return false;
    };
    auto set_true = [&](size_t a, size_t e, std::set<uint32_t>* to) {   // every SetEventFlag(<n>, true) in t[a, e)
        for (size_t p = t.find("SetEventFlag(", a); p != std::string::npos && p < e; p = t.find("SetEventFlag(", p + 1)) {
            size_t k = skip_space(t, p + 13);
            const long flag = read_number(t, &k);
            k = skip_space(t, k);
            if (flag < 0 || flag >= (long)kProgressMax || k >= t.size() || t[k] != ',') continue;
            k = skip_space(t, k + 1);
            if (t.compare(k, 4, "true") == 0 || t.compare(k, 1, "1") == 0) to->insert((uint32_t)flag);
        }
    };
    auto handlers = [&](long id) {   // OnEvent_<id> and OnEvent_<id>_<k>
        std::vector<std::pair<size_t, size_t>> hs;
        const std::string base = "OnEvent_" + std::to_string(id);
        for (const auto& h : funcs)
            if (h.first == base || h.first.compare(0, base.size() + 1, base + "_") == 0) hs.push_back(h.second);
        return hs;
    };
    for (const auto& f : funcs) {
        if (f.first.compare(0, 8, "OnEvent_") != 0 || !body_has_clear(f.second.first, f.second.second)) continue;
        size_t i = 8;
        const long id = read_number(f.first, &i);
        if (id < 0) continue;
        for (const auto& h : handlers(id)) set_true(h.first, h.second, &kills);   // every flag they set to true
    }
    out = kills;
    if (boss) boss->assign(kills.begin(), kills.end());
    // The Archstones' seal (the Nexus): every flag its handlers set, and the handlers they register by name (a
    // function named in quotes, as OnKeyTime2 and OnRegistFunc take it).
    auto seal_flags = [&](long id) {
        std::vector<std::pair<size_t, size_t>> hs = handlers(id);
        const size_t direct = hs.size();
        for (size_t n = 0; n < direct; n++)
            for (size_t q = t.find('"', hs[n].first); q != std::string::npos && q < hs[n].second; q = t.find('"', q + 1)) {
                const size_t e = t.find('"', q + 1);
                if (e == std::string::npos || e > hs[n].second) break;
                auto it = funcs.find(t.substr(q + 1, e - q - 1));
                if (it != funcs.end()) hs.push_back(it->second);
                q = e;
            }
        for (const auto& h : hs) set_true(h.first, h.second, &sealed);
        sealed.insert((uint32_t)id);
    };
    // the headers: --■<id>■<what>■
    std::set<long> seen;
    for (size_t a = 0; a < t.size();) {
        size_t e = t.find('\n', a);
        if (e == std::string::npos) e = t.size();
        size_t i = skip_space(t, a);
        if (t.compare(i, 2, "--") == 0) {
            i = skip_space(t, i + 2);
            if (t.compare(i, 2, kBox) == 0) {
                i = skip_space(t, i + 2);
                const long id = read_number(t, &i);
                i = skip_space(t, i);
                if (id >= 0 && t.compare(i, 2, kBox) == 0 && seen.insert(id).second && id < (long)kProgressMax) {
                    size_t w = i + 2, we = w;
                    while (we < e && t.compare(we, 2, kBox) != 0) we += sjis_lead((unsigned char)t[we]) ? 2 : 1;
                    const std::string what = t.substr(w, std::min(we, e) - w);
                    bool in = false, ex = false;
                    for (const char* k : kInclude) in = in || sjis_has(what, k);
                    for (const char* k : kExclude) ex = ex || sjis_has(what, k);
                    if (in && !ex) out.insert((uint32_t)id);
                    if (sjis_has(what, kSeal)) seal_flags(id);
                }
            }
        }
        a = e + 1;
    }
    if (seal) seal->assign(sealed.begin(), sealed.end());
    return std::vector<uint32_t>(out.begin(), out.end());
}

// The soul pickups (the prompt 10010180): the event's ID is the flag its handler sets once the soul is taken.
std::vector<uint32_t> own_flags(const std::string& t)
{
    std::set<uint32_t> out;
    const std::regex re("OnDistanceActionAttribute\\(\\s*(\\d+)\\s*,[^\\n]*?,\\s*10010180\\s*,");
    for (auto it = std::sregex_iterator(t.begin(), t.end(), re); it != std::sregex_iterator(); ++it) {
        const unsigned long id = std::stoul((*it)[1].str());
        if (id < kProgressMax) out.insert((uint32_t)id);
    }
    return std::vector<uint32_t>(out.begin(), out.end());
}

// The cutscenes (RequestRemo / RequestRemoPlus(scene, REMO_FLAG, event, ...)): the event's ID is the flag its handler
// sets once the cutscene has played, and the flag the area's start checks before playing it. And the movies an event
// handler plays (RequestPlayMovie, the Monumental's tale): that event's ID, the flag it sets once the movie has played.
std::vector<uint32_t> scene_flags(const std::string& t)
{
    std::set<uint32_t> out;
    const std::regex re("RequestRemo(?:Plus)?\\(\\s*\\d+\\s*,\\s*REMO_FLAG\\s*,\\s*(\\d+)\\s*,");
    for (auto it = std::sregex_iterator(t.begin(), t.end(), re); it != std::sregex_iterator(); ++it) {
        const unsigned long id = std::stoul((*it)[1].str());
        if (id < kProgressMax) out.insert((uint32_t)id);
    }
    for (size_t a = t.find("\nfunction OnEvent_"); a != std::string::npos; a = t.find("\nfunction OnEvent_", a + 1)) {
        size_t i = a + 18;
        const long id = read_number(t, &i);
        if (id < 0 || id >= (long)kProgressMax || i >= t.size() || t[i] != '(') continue;
        const size_t e = t.find("\nfunction ", i);
        const size_t m = t.find("RequestPlayMovie", i);
        if (m != std::string::npos && (e == std::string::npos || m < e)) out.insert((uint32_t)id);
    }
    return std::vector<uint32_t>(out.begin(), out.end());
}

// The edited copies, by the original's size and hash; 0: the script is left as it is.
struct Copy {
    uint32_t addr = 0, size = 0;
    uint32_t world = 0xFFFFFFFFu;     // an area script's world (its area's top byte - 1)
    std::vector<uint32_t> progress;   // an area script's progress flags
    std::vector<uint32_t> boss;       // the ones a boss kill sets
    std::vector<uint32_t> own;        // its soul pickups
    std::vector<uint32_t> scenes;     // the Nexus's cutscenes
    std::vector<uint32_t> seal;       // and the Archstones' seal (the Nexus)
};
std::mutex sLock;
std::map<std::pair<uint32_t, uint64_t>, Copy> sCopies;
// Each world's flags, from its area scripts (a load reads every area script of its world): sorted, no repeats.
std::map<uint32_t, std::vector<uint32_t>> sProgress, sBoss, sOwn, sScenes, sSeal;

void merge(std::vector<uint32_t>& into, const std::vector<uint32_t>& from)
{
    std::vector<uint32_t> out;
    std::set_union(into.begin(), into.end(), from.begin(), from.end(), std::back_inserter(out));
    into.swap(out);
}

void learn(const Copy& c)   // under sLock
{
    if (c.world >= 8) return;
    merge(sProgress[c.world], c.progress);
    merge(sBoss[c.world], c.boss);
    merge(sOwn[c.world], c.own);
    merge(sScenes[c.world], c.scenes);
    merge(sSeal[c.world], c.seal);
}

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
        std::unique_lock<std::mutex> g(sLock);
        auto it = sCopies.find(key);
        if (it != sCopies.end()) {
            learn(it->second);
            const Copy c = it->second;
            g.unlock();
            if (!global) des_seamless_area_read(c.world, c.scenes);
            if (!c.addr) return false;
            *out = c.addr;
            *out_size = c.size;
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
    Copy c;
    c.addr = addr;
    c.size = n;
    if (!global) {
        c.progress = progress_flags(t, &c.boss, &c.seal);
        c.own = own_flags(t);
        // "function Initialize_mXX_": the world is XX - 1
        const char d0 = t[ar + 21], d1 = t[ar + 22];
        if (d0 >= '0' && d0 <= '9' && d1 >= '1' && d1 <= '9') c.world = (uint32_t)((d0 - '0') * 10 + (d1 - '0') - 1);
        if (c.world == 0) c.scenes = scene_flags(t);   // the Nexus only: an area's cutscenes start its fights
    }
    std::fprintf(stderr, "[coop] script %s: %s%s\n", global ? "global_event.lua" : t.substr(ar + 20, 12).c_str(),
                 addr ? (std::to_string(edits) + " edits for co-op").c_str() : global ? "left as it is (an edit didn't fit)"
                                                                                      : "nothing to edit",
                 global ? "" : ("; " + std::to_string(c.progress.size()) + " progress flags, " +
                                std::to_string(c.own.size()) + " soul pickups, " + std::to_string(c.scenes.size()) +
                                " cutscenes, " + std::to_string(c.seal.size()) + " for the Archstones' seal").c_str());
    {
        std::lock_guard<std::mutex> g(sLock);
        learn(c);
        sCopies[key] = c;
    }
    if (!global) des_seamless_area_read(c.world, c.scenes);
    if (!addr) return false;
    *out = addr;
    *out_size = n;
    return true;
}

// What flag is in world (an area's top byte - 1), from that world's area scripts: 1 a progress flag (every player's
// world keeps it), 3 one a boss kill sets (a progress flag too), 2 a soul pickup (the player who took it keeps it),
// 4 a Nexus cutscene played (the player who saw it keeps it), 5 the Archstones' seal opened (progress that a guest's
// own game makes too: the host's world takes it), 0 none of these.
int des_seamless_flag_kind(uint32_t world, uint32_t flag)
{
    std::lock_guard<std::mutex> g(sLock);
    auto has = [&](const std::map<uint32_t, std::vector<uint32_t>>& m) {
        auto it = m.find(world);
        return it != m.end() && std::binary_search(it->second.begin(), it->second.end(), flag);
    };
    return has(sBoss) ? 3 : has(sSeal) ? 5 : has(sProgress) ? 1 : has(sOwn) ? 2 : has(sScenes) ? 4 : 0;
}
