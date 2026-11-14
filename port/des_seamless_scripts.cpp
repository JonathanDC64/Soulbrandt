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
//     it first watches until the fight ends (the boss dead, or the host dead: then it goes back with the host);
//   - the cutscenes keep their solo mode in a session: each player skips its own;
//   - a boss's extra health counts a third blue phantom (a party of four) as two: x2, the game's most.
// Each area's script:
//   - a boss fight is marked from the boss's health bar to its clearing (for the watch above);
//   - the boss rooms' party walls drop as in solo play;
//   - a guest doesn't play the warp-out pose at a boss's death;
//   - the Archstones: the host may use them in a session (the guests then follow it, des_seamless_game.cpp); a guest
//     still gets "You can't return during multiplayer.", at the Nexus's stones too (they had no session check);
//   - a boss's soul: a blue phantom may take its own, as the host takes its own;
//   - the wandering ghosts' sightings are each player's own (local_wander_loops);
//   - a light wall one player walks through is gone in every player's game (walls_for_watchers);
//   - the room is never locked (LockSession), here or at a kill in global_event.lua, so a friend can always join again;
//   - the Nexus's timers and checks are each player's own (local_timers), and so is the kneel at a level-up.
// And from each area's script, the flags that move forward in every player's world (progress_flags), the soul
// pickups each player makes for itself (own_flags) and the cutscenes each player sees for itself (the Nexus's,
// scene_flags, and an area's opening, opening_flags), for des_seamless_game.cpp.
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
#include <sstream>
#include <string>
#include <vector>

extern "C" void ds_malloc(ppu_context* ctx);   // des_heap.cpp: the guest heap
// des_seamless_game.cpp: an area script of world was just read (its cutscenes each player sees, and its soul, key
// and item pickups), before the area starts
void des_seamless_area_read(uint32_t world, const std::vector<uint32_t>& scenes, const std::vector<uint32_t>& own);

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
    // BlockClear2: no room lock at a kill (see edit_area)
    if (!replace_in(t, "BlockClear2", "proxy:LockSession();", "--[[SEAMLESS proxy:LockSession();]]"))
        return false;
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
    // OnEvent_4034 (the session connected): the cutscenes keep their solo mode. REMO_FLAG is the mode every
    // RequestRemo passes. A map's scripts start with 0, 4034 makes it 2 (multiplayer: a guest can't skip one, and the
    // host's skip ends only its own), and 4035 (the session's end, and BlockClear2_3Leave above) makes it 0 again. In
    // 0, as the ones asked for before 4034 (an area's opening) already were in a session, each player skips its own.
    if (!replace_in(t, "OnEvent_4034", "REMO_FLAG = 2;",
                    "REMO_FLAG = 0; --SEAMLESS each player's cutscenes are its own"))
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
    // Its souls stay where it died, in a bloodstain of its own, as a host's do (SoloPlayDeath_2's UpDateBloodMark):
    // the stain is this player's (its save's), so in the host's world only it sees and touches it, and if it goes home
    // first the stain is at the same place in its own world; a death before it gets back there takes its place. The
    // touch (g_Initialize's OnSelfBloodMark) is for blue phantoms too.
    if (!replace_in(t, "g_Initialize", "proxy:OnSelfBloodMark( 4032, \"SelfBloodMark\", ATTR_LIVE + ATTR_GREY, everytime );",
                    "proxy:OnSelfBloodMark( 4032, \"SelfBloodMark\", ATTR_LIVE + ATTR_GREY + ATTR_WHITE, everytime ); --SEAMLESS"))
        return false;
    if (!replace_func(t, "PartyGhostDeath_1",
                      "function PartyGhostDeath_1(proxy,param)\n"
                      "\t--SEAMLESS: no recall menu and no trip home; PartyGhostDeath_2 brings the phantom back\n"
                      "\tproxy:UpDateBloodMark();\n"
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
    // A boss's extra health with blue phantoms (Lua_MultiDoping, at the boss room's first entry, per boss part): one
    // phantom gets effect 7500 (health x1.5), two 7501 (x2). The count is the session's blue phantoms (the game's
    // GetWhiteGhostCount: members of type 1), so three, a party of four, matched neither and got none. Three take the
    // most there is, 7501. Lua_MultiDopingm04_02 is the same for 4-2.
    if (!replace_in(t, "Lua_MultiDoping", "if whiteCount == 2 then", "if whiteCount >= 2 then --SEAMLESS three too"))
        return false;
    if (!replace_in(t, "Lua_MultiDopingm04_02", "if whiteCount == 2 then",
                    "if whiteCount >= 2 then --SEAMLESS three too"))
        return false;
    // A boss's health bar cleared: its fight is over (BlockClear's kill; see edit_area)
    const std::regex clear("proxy:ClearBossGauge\\s*\\(");
    if (!std::regex_search(t, clear)) return false;
    t = std::regex_replace(t, clear, "SeamlessBossFight = false; proxy:ClearBossGauge(");
    return true;
}

// Whether pos in a script is between NotNetMessage_begin and _end.
bool in_not_net(const std::string& t, size_t pos)
{
    const size_t b = t.rfind("NotNetMessage_begin", pos);
    const size_t e = t.rfind("NotNetMessage_end", pos);
    return b != std::string::npos && (e == std::string::npos || b > e);
}

// The wandering ghosts' sightings: a scripted phantom that walks by and fades, over and over (OnWanderFade). The
// tutorial's (m08_00's 14400, m08_01's 14411) loop through timers registered between RepeatMessage_begin and _end
// with no NotNetMessage, as the tutorial has no co-op. In a session each pass is a network event, so the other
// games run it too and send theirs back: the timers multiply until the event heap (INGAME, 1 MB) runs out, and
// the game stops with "Transaction failed" a few minutes in. The areas made for sessions run theirs between
// NotNetMessage_begin and _end (m06_00's 124), each player seeing its own; so do these now. The functions are each
// one with OnWanderFade, the timers' handlers and the functions they call, and the ones whose timers start them;
// their RepeatMessage blocks and the region events that start them are made local. Returns the number of edits.
int local_wander_loops(std::string& t)
{
    struct Span { size_t a, e; };
    std::map<std::string, Span> fns;
    const std::regex head("\nfunction ([A-Za-z_][A-Za-z0-9_]*)\\s*\\(");
    for (auto it = std::sregex_iterator(t.begin(), t.end(), head); it != std::sregex_iterator(); ++it) {
        const size_t a = (size_t)it->position(0) + 1;
        const size_t next = t.find("\nfunction ", a + 5);
        fns.emplace((*it)[1].str(), Span{a, next == std::string::npos ? t.size() : next + 1});
    }
    const auto body = [&](const std::string& f) { return t.substr(fns.at(f).a, fns.at(f).e - fns.at(f).a); };
    std::set<std::string> loop;
    for (const auto& [f, s] : fns)
        if (body(f).find("OnWanderFade(") != std::string::npos) loop.insert(f);
    if (loop.empty()) return 0;
    const std::regex timer("OnKeyTime2\\s*\\([^,\\n]*,\\s*\"([A-Za-z0-9_]+)\"");
    const std::regex call("(?:^|[^:.\\w])([A-Za-z_][A-Za-z0-9_]*)\\s*\\(\\s*proxy\\b");
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& [f, s] : fns) {
            const std::string b = body(f);
            const bool in = loop.count(f) != 0;
            for (auto it = std::sregex_iterator(b.begin(), b.end(), timer); it != std::sregex_iterator(); ++it) {
                const std::string h = (*it)[1].str();
                if (!fns.count(h)) continue;
                if (in && loop.insert(h).second) grew = true;
                if (!in && loop.count(h) && loop.insert(f).second) grew = true;
            }
            if (!in) continue;
            for (auto it = std::sregex_iterator(b.begin(), b.end(), call); it != std::sregex_iterator(); ++it) {
                const std::string c = (*it)[1].str();
                if (c != f && fns.count(c) && loop.insert(c).second) grew = true;
            }
        }
    }
    // The places to wrap, from the end so the earlier ones keep their positions: {begin, end} of each statement or
    // block that isn't local yet.
    std::vector<std::pair<size_t, size_t>> wrap;
    const std::regex block("proxy:RepeatMessage_begin\\(\\);([\\s\\S]*?)proxy:RepeatMessage_end\\(\\);");
    const std::regex fade("proxy:OnWanderFade\\s*\\([^;\\n]*\\)\\s*;");   // an event too: the fade's end
    for (const std::string& f : loop) {
        const Span s = fns.at(f);
        for (const std::regex* re : {&block, &fade})
            for (auto it = std::sregex_iterator(t.begin() + (long)s.a, t.begin() + (long)s.e, *re);
                 it != std::sregex_iterator(); ++it) {
                const size_t a = s.a + (size_t)it->position(0);
                if (it->str().find("NotNetMessage_begin") != std::string::npos || in_not_net(t, a)) continue;
                wrap.push_back({a, a + (size_t)it->length(0)});
            }
        const std::regex trigger("proxy:OnRegion\\w*\\s*\\([^;\\n]*\"" + f + "\"[^;\\n]*\\)\\s*;");
        for (auto it = std::sregex_iterator(t.begin(), t.end(), trigger); it != std::sregex_iterator(); ++it) {
            const size_t a = (size_t)it->position(0);
            if (!in_not_net(t, a)) wrap.push_back({a, a + (size_t)it->length(0)});
        }
    }
    std::sort(wrap.begin(), wrap.end());
    wrap.erase(std::unique(wrap.begin(), wrap.end()), wrap.end());
    for (auto w = wrap.rbegin(); w != wrap.rend(); ++w) {
        t.insert(w->second, " proxy:NotNetMessage_end();");
        t.insert(w->first, "proxy:NotNetMessage_begin(); ");
    }
    return (int)wrap.size();
}

// The Nexus's timers and checks (OnKeyTime2, OnRegistFunc). No phantom reaches the Nexus in the original game, so its
// script registers them outside NotNetMessage_begin and _end, and in a session each one is a network event: a step
// that fires in one game runs in the others too, where it registers the next step again. Each game runs its own
// chains (the first visit's messages, the Maiden's revival, the clear items), so when two ran at once each step
// multiplied: a friend got three Nexial Bindings at the end of the first visit's messages (OnEvent_30_RemoFinish, then
// 32 to 37's GetRateItem(10860)). Each game's steps now stay in that game. (Not the calls in a comment.) Returns the
// number of edits.
int local_timers(std::string& t)
{
    std::vector<std::pair<size_t, size_t>> wrap;
    const std::regex reg("proxy:(?:OnKeyTime2|OnRegistFunc)\\s*\\([^;\\n]*\\)\\s*;");
    for (auto it = std::sregex_iterator(t.begin(), t.end(), reg); it != std::sregex_iterator(); ++it) {
        const size_t a = (size_t)it->position(0);
        const size_t bol = t.rfind('\n', a);
        const size_t ls = bol == std::string::npos ? 0 : bol + 1;
        if (t.substr(ls, a - ls).find("--") != std::string::npos) continue;   // a line comment
        const size_t open = t.rfind("--[[", a), close = t.rfind("]]", a);
        if (open != std::string::npos && (close == std::string::npos || close < open)) continue;   // a block comment
        if (in_not_net(t, a)) continue;
        wrap.push_back({a, a + (size_t)it->length(0)});
    }
    for (auto w = wrap.rbegin(); w != wrap.rend(); ++w) {
        t.insert(w->second, " proxy:NotNetMessage_end();");
        t.insert(w->first, "proxy:NotNetMessage_begin(); ");
    }
    return (int)wrap.size();
}

// A line's code without its Lua comment (from "--" on; the scripts have no "--" inside strings).
std::string code_of(const std::string& text)
{
    std::string out;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) out += line.substr(0, line.find("--")) + "\n";
    return out;
}

// The light walls a player passes by pressing Cross (a walk through, then the wall is gone). In the other players'
// games the handler plays that walk on the walker's character and marks the wall passed
// (Luafunc_PlaySynchroAnimation's true branch, then return). The areas made for sessions drop the wall for them
// through the game's wall-delete sync (LuaCallStart). The tutorial's (m08_01's 14291) drops it with a direct call in
// the walker's game alone, as the tutorial has no co-op: the others lost the fog, the prompt and the event, and kept
// the wall itself, so they couldn't pass. Each watcher's game now makes the same call. Returns the number of edits.
int walls_for_watchers(std::string& t)
{
    struct Span { size_t a, e; };
    std::map<std::string, Span> fns;
    const std::regex head("\nfunction ([A-Za-z_][A-Za-z0-9_]*)\\s*\\(");
    for (auto it = std::sregex_iterator(t.begin(), t.end(), head); it != std::sregex_iterator(); ++it) {
        const size_t a = (size_t)it->position(0) + 1;
        const size_t next = t.find("\nfunction ", a + 5);
        fns.emplace((*it)[1].str(), Span{a, next == std::string::npos ? t.size() : next + 1});
    }
    const auto code = [&](const std::string& f) { return code_of(t.substr(fns.at(f).a, fns.at(f).e - fns.at(f).a)); };
    const std::regex sync("Luafunc_PlaySynchroAnimation\\(\\s*proxy\\s*,\\s*param\\s*,\\s*ANIMEID_WALK\\s*\\)\\s*==\\s*true\\s+then");
    const std::regex call("(?:^|[^:.\\w])([A-Za-z_][A-Za-z0-9_]*)\\s*\\(\\s*proxy\\s*,\\s*param\\s*\\)");
    const std::regex drop("SetColiEnable\\(\\s*\\d+\\s*,\\s*false\\s*\\)");
    std::vector<std::pair<size_t, std::string>> adds;   // {after "then", the call}
    for (const auto& [f, s] : fns) {
        const std::string c = code(f);
        std::smatch m;
        if (!std::regex_search(c, m, sync) || c.find("LuaCallStart") != std::string::npos) continue;
        std::string wall;   // the function the walker's game drops the wall with
        for (auto it = std::sregex_iterator(c.begin(), c.end(), call); it != std::sregex_iterator(); ++it) {
            const std::string g = (*it)[1].str();
            if (g != f && g != "Luafunc_PlaySynchroAnimation" && fns.count(g) && std::regex_search(code(g), drop))
                wall = g;
        }
        if (wall.empty() || c.find(wall + "(", (size_t)(m.position(0) + m.length(0))) == std::string::npos) continue;
        // where the same text sits in t (comments kept there): the first sync check in f's span outside a comment
        const std::string seg = t.substr(s.a, s.e - s.a);
        for (auto it = std::sregex_iterator(seg.begin(), seg.end(), sync); it != std::sregex_iterator(); ++it) {
            const size_t at = (size_t)it->position(0);
            const size_t bol = seg.rfind('\n', at);
            if (seg.substr(bol == std::string::npos ? 0 : bol + 1, at - (bol == std::string::npos ? 0 : bol + 1))
                    .find("--") != std::string::npos)
                continue;
            adds.push_back({s.a + at + (size_t)it->length(0),
                            " proxy:NotNetMessage_begin(); " + wall + "(proxy , param); proxy:NotNetMessage_end();"});
            break;
        }
    }
    std::sort(adds.begin(), adds.end());
    for (auto a = adds.rbegin(); a != adds.rend(); ++a) t.insert(a->first, a->second);
    return (int)adds.size();
}

// A pickup's "another player did it" branch in its handler: Luafunc_PlaySynchroAnimation(proxy, param, ANIMEID_PICK).
const std::regex& synchro_branch()
{
    static const std::regex re("\\n([ \\t]*)if\\s+Luafunc_PlaySynchroAnimation\\(\\s*proxy\\s*,\\s*param\\s*,\\s*ANIMEID_PICK\\s*\\)"
                               "\\s*==\\s*true\\s+then[^\\n]*\\n(?:[^\\n]*\\n)*?\\1end[^\\n]*\\n");
    return re;
}

// The keys the players pick up at a prompt (3-1's prison keys): OnDistanceActionPlusAttribute(<id>, ...,
// "OnEvent_<id>", ..., ATTR_LIVE + ATTR_GREY, once), whose handler has the pickup's branch and gives an item
// (GetRateItem). The event's ID is the flag its handler sets once the key is taken. Their doors are other events.
std::set<std::string> key_pickups(const std::string& t)
{
    std::set<std::string> ids;
    const std::regex prompt("OnDistanceActionPlusAttribute\\(\\s*(\\d+)\\s*,[^\\n]*?\"OnEvent_(\\d+)\"[^\\n]*?,\\s*"
                            "ATTR_LIVE\\s*\\+\\s*ATTR_GREY\\s*,\\s*once\\s*\\)");
    for (auto it = std::sregex_iterator(t.begin(), t.end(), prompt); it != std::sregex_iterator(); ++it) {
        if ((*it)[1].str() != (*it)[2].str()) continue;
        size_t a, e;
        if (!func_span(t, "OnEvent_" + (*it)[1].str(), &a, &e)) continue;
        const std::string body = t.substr(a, e - a);
        if (body.find("GetRateItem(") != std::string::npos && std::regex_search(body, synchro_branch()))
            ids.insert((*it)[1].str());
    }
    return ids;
}

// A boss's soul, and a key, for every player in the session. Each player's prompt is its own (edit_area for the
// souls, here for the keys); when one player takes the soul or key, every other player there gets it too, as if it
// had taken it, unless its own world has it already (des_seamless_game.cpp makes a friend's flag in play its own
// world's as the area starts). The prompt's handler (OnEvent_<id>) plays the pickup and gives the item (in its own
// lines, or a moment later in OnEvent_<id>_1); after its "another player did it" branch
// (Luafunc_PlaySynchroAnimation) it now tells the others: LuaCallStart(<id>, 77), a network event, as the boss rooms'
// notices are sent (4045). Each game's SEAMLESS_Grant_<id> (LuaCall(<id>, 77), registered as the area starts) takes
// it from another player (param:IsNetMessage()) when its flag is clear: the prompt goes (DeleteEvent) and the
// handler's own path runs without the pickup (SEAMLESS_Give_<id>, the handler without that branch): the same items,
// flag and steps after. Returns the number of edits.
int pickups_for_all(std::string& t)
{
    const std::string nl = t.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::set<std::string> ids;
    const std::regex prompt("OnDistanceActionAttribute\\(\\s*(\\d+)\\s*,[^\\n]*?\"OnEvent_(\\d+)\"[^\\n]*?,\\s*10010180\\s*,");
    for (auto it = std::sregex_iterator(t.begin(), t.end(), prompt); it != std::sregex_iterator(); ++it)
        if ((*it)[1].str() == (*it)[2].str()) ids.insert((*it)[1].str());
    const std::regex& synchro = synchro_branch();
    const std::regex init("(\\nfunction Initialize_m\\d\\d_\\d\\d_\\d\\d_\\d\\d\\s*\\([^)]*\\)[^\\n]*\\n)");
    if (!std::regex_search(t, init)) return 0;
    int n = 0;
    // a key's prompt: each player's own (local, not sent), and a friend's (white phantom) too
    for (const std::string& id : key_pickups(t)) {
        const std::regex reg("proxy:(OnDistanceActionPlusAttribute\\(\\s*" + id + "\\s*,[^\\n]*?,\\s*)ATTR_LIVE\\s*\\+\\s*ATTR_GREY"
                             "(\\s*,\\s*once\\s*\\)\\s*;)");
        const std::string was = t;
        t = std::regex_replace(t, reg, "proxy:NotNetMessage_begin(); proxy:$1ATTR_LIVE + ATTR_GREY + ATTR_WHITE$2 proxy:NotNetMessage_end();");
        if (t != was) { ids.insert(id); ++n; }
    }
    if (ids.empty()) return 0;
    std::string tail, regs;
    const int edits = n;
    for (const std::string& id : ids) {
        size_t a, e;
        if (!func_span(t, "OnEvent_" + id, &a, &e)) continue;
        const std::string body = t.substr(a, e - a);
        std::smatch m;
        if (!std::regex_search(body, m, synchro)) continue;
        const size_t s = (size_t)m.position(0), s_end = s + (size_t)m.length(0);
        // the others' copy: the handler without the pickup's branch
        std::string give = body.substr(0, s + 1) + body.substr(s_end);
        give.replace(give.find("OnEvent_" + id), ("OnEvent_" + id).size(), "SEAMLESS_Give_" + id);
        // the taker tells the others, after that branch
        const std::string tell = m[1].str() + "proxy:LuaCallStart( " + id + " , 77 ); --SEAMLESS: the others get it too" + nl;
        t.replace(a, e - a, body.substr(0, s_end) + tell + body.substr(s_end));
        tail += nl + "--SEAMLESS: " + id + "'s item for every player in the session (pickups_for_all)" + nl + give;
        if (tail.size() < nl.size() || tail.compare(tail.size() - nl.size(), nl.size(), nl) != 0) tail += nl;
        tail += "function SEAMLESS_Grant_" + id + "(proxy, param)" + nl +
                "\tif param:IsNetMessage() == false or proxy:IsCompleteEvent( " + id + " ) == true then return; end" + nl +
                "\tproxy:DeleteEvent( " + id + " );" + nl +
                "\tSEAMLESS_Give_" + id + "(proxy, param);" + nl + "end" + nl;
        regs += "\tproxy:LuaCall( " + id + " , 77 , \"SEAMLESS_Grant_" + id + "\" , everytime ); --SEAMLESS" + nl;
        n += 3;
    }
    if (n == edits) return n;
    std::smatch m;
    std::regex_search(t, m, init);   // (found above; the handlers' edits come after it or don't touch it)
    t.insert((size_t)m.position(0) + (size_t)m.length(0), regs);
    t += tail;
    return n;
}

// The items the host alone takes at an item prompt with its own pickup steps (a sword pulled out, a glowing corpse):
// OnDistanceActionAttribute(<id>, LOCAL_PLAYER or 10000, <object>, "OnEvent_<id>", ItemBoxDist_A, ...,
// possible_chr_type, ...), whose steps end in OnEvent_<id>_1, which gives the items (GetRateItem) and sets flag <id>.
// The ID -> its lots, in order.
std::map<std::string, std::vector<std::string>> gift_pickups(const std::string& t)
{
    std::map<std::string, std::vector<std::string>> out;
    const std::regex prompt("\\n([^\\n]*?)OnDistanceActionAttribute\\(\\s*(\\d+)\\s*,\\s*(?:LOCAL_PLAYER|10000)\\s*,\\s*\\d+\\s*,\\s*"
                            "\"OnEvent_(\\d+)\"\\s*,\\s*ItemBoxDist_A\\s*,[^\\n]*?,\\s*possible_chr_type\\s*,\\s*(?:once|everytime)\\s*\\)");
    const std::regex lot("GetRateItem\\(\\s*(\\d+)\\s*\\)");
    for (auto it = std::sregex_iterator(t.begin(), t.end(), prompt); it != std::sregex_iterator(); ++it) {
        const std::string id = (*it)[2].str();
        if (id != (*it)[3].str() || (*it)[1].str().find("--") != std::string::npos || out.count(id)) continue;
        size_t a, e;
        if (!func_span(t, "OnEvent_" + id + "_1", &a, &e)) continue;
        const std::string body = t.substr(a, e - a);
        if (!std::regex_search(body, std::regex("SetEventFlag\\(\\s*" + id + "\\s*,\\s*(?:true|1)\\s*\\)"))) continue;
        std::vector<std::string> lots;
        for (auto l = std::sregex_iterator(body.begin(), body.end(), lot); l != std::sregex_iterator(); ++l) lots.push_back((*l)[1].str());
        if (!lots.empty()) out[id] = lots;
    }
    return out;
}

// Those items for every player in the session, the way pickups_for_all gives the souls and keys: a friend (white
// phantom) may take one too (its prompt allows ATTR_WHITE), and when one player takes it, its OnEvent_<id>_1 first
// tells the others (LuaCallStart(<id>, 78), before the pickup's own messages, as one of them takes the others'
// prompt away); each game's SEAMLESS_Gift_<id> (LuaCall(<id>, 78)) then gives the same items and sets the flag,
// unless its flag is set already (des_seamless_game.cpp makes a friend's flag in play its own world's). Returns the
// number of edits.
int gifts_for_all(std::string& t)
{
    const std::map<std::string, std::vector<std::string>> gifts = gift_pickups(t);
    const std::regex init("(\\nfunction Initialize_m\\d\\d_\\d\\d_\\d\\d_\\d\\d\\s*\\([^)]*\\)[^\\n]*\\n)");
    if (gifts.empty() || !std::regex_search(t, init)) return 0;
    const std::string nl = t.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string tail, regs;
    int n = 0;
    for (const auto& g : gifts) {
        const std::string& id = g.first;
        const std::regex reg("(OnDistanceActionAttribute\\(\\s*" + id + "\\s*,[^\\n]*?,\\s*)possible_chr_type(\\s*,\\s*(?:once|everytime)\\s*\\))");
        t = std::regex_replace(t, reg, "$1possible_chr_type + ATTR_WHITE$2");
        size_t a, e;
        if (!func_span(t, "OnEvent_" + id + "_1", &a, &e)) continue;
        const size_t line = t.find('\n', a + 1);
        if (line == std::string::npos || line >= e) continue;
        t.insert(line + 1, "\tproxy:LuaCallStart( " + id + " , 78 ); --SEAMLESS: the others get it too" + nl);
        tail += nl + "--SEAMLESS: " + id + "'s item for every player in the session (gifts_for_all)" + nl +
                "function SEAMLESS_Gift_" + id + "(proxy, param)" + nl +
                "\tif param:IsNetMessage() == false or proxy:IsCompleteEvent( " + id + " ) == true then return; end" + nl;
        for (const std::string& l : g.second) tail += "\tproxy:GetRateItem( " + l + " );" + nl;
        tail += "\tproxy:SetEventFlag( " + id + " , true );" + nl + "end" + nl;
        regs += "\tproxy:LuaCall( " + id + " , 78 , \"SEAMLESS_Gift_" + id + "\" , everytime ); --SEAMLESS" + nl;
        n += 4;
    }
    if (!n) return 0;
    std::smatch m;
    std::regex_search(t, m, init);
    t.insert((size_t)m.position(0) + (size_t)m.length(0), regs);
    t += tail;
    return n;
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
    // No room lock. LockSession (here as a player walks into a boss room, and in global_event.lua's BlockClear2 at a
    // kill) sets the session's lock (sm+0x2CA), which stays until the host's game leaves its room (UnLockSession is
    // empty): the original ends the session after a boss. A locked host kicks every new join (the member handler at
    // 0x0053D8C0, then KickoutRoomMember). A friend that follows keeps its place in the room and never noticed; one
    // back from a trip of its own (each player's at the tutorial's end) was kicked. (Not the calls already in a
    // comment.)
    sub(std::regex("([^-])proxy:LockSession\\(\\);"), "$1--[[SEAMLESS proxy:LockSession();]]");
    if (nexus) {
        // The Nexus's Archstones (OnDistanceAction(<id>, 10000, <stone>, "OnEvent_<id>", WarpGimmickDist_A, ...)) have
        // no session check: no phantom reaches the Nexus in the original game. A guest's own touch took it out of the
        // session and to the area alone, where its rejoin then stalled. A guest in a session gets the levels' answer
        // at the stone instead ("You can't return during multiplayer.", 10010720); the host's travel takes it along.
        const std::regex stone("OnDistanceAction\\(\\s*(\\d+)\\s*,\\s*10000\\s*,\\s*(\\d+)\\s*,\\s*\"OnEvent_(\\d+)\"\\s*,"
                               "\\s*WarpGimmickDist_A");
        std::map<std::string, std::string> stones;   // its handler's event ID -> the stone (where the dialog shows)
        for (auto it = std::sregex_iterator(t.begin(), t.end(), stone); it != std::sregex_iterator(); ++it)
            if ((*it)[1].str() == (*it)[3].str()) stones[(*it)[1].str()] = (*it)[2].str();
        for (const auto& [id, obj] : stones) {
            const std::string guard =
                "\tif proxy:IsSession() == true and proxy:IsClient() == true then --SEAMLESS: a guest doesn't travel alone\n"
                "\t\tproxy:RepeatMessage_begin(); proxy:NotNetMessage_begin();\n"
                "\t\tproxy:OnSelectMenu( SYSTEM_GENDIALOG, \"MenuClose\", 10010720, 0, 6, " + obj +
                ", GEN_DIALOG_DIST, once );\n"
                "\t\tproxy:NotNetMessage_end(); proxy:RepeatMessage_end();\n"
                "\t\treturn;\n"
                "\tend\n";
            sub(std::regex("(\\nfunction OnEvent_" + id + "\\s*\\(\\s*proxy\\s*,\\s*param\\s*\\)[^\\n]*\\n)"),
                ("$1" + guard).c_str());
        }
        // The kneel before the Maiden at a level-up: OnEvent_90 turns the player to her, takes its control
        // (EnableLogic(10000, false)) and kneels; OnEvent_91 stands it up and gives the control back. The talk system
        // starts them (LuaCall(<id>, 0, ...)), and in a session that call reaches every game, where 10000 is that
        // game's own player: a friend knelt and couldn't move while the host leveled up. A handler of that kind (a
        // LuaCall's that takes or gives back the player's control) now ignores a call from another player's game.
        const std::regex talk("proxy:LuaCall\\(\\s*\\d+\\s*,\\s*\\d+\\s*,\\s*\"(\\w+)\"\\s*,\\s*everytime\\s*\\)");
        const std::regex control("EnableLogic\\(\\s*10000\\s*,");
        std::set<std::string> own;
        for (auto it = std::sregex_iterator(t.begin(), t.end(), talk); it != std::sregex_iterator(); ++it) {
            size_t a, e;
            if (!func_span(t, (*it)[1].str(), &a, &e)) continue;
            const std::string c = code_of(t.substr(a, e - a));
            if (std::regex_search(c, control) && c.find("IsNetMessage") == std::string::npos) own.insert((*it)[1].str());
        }
        for (const std::string& h : own)
            sub(std::regex("(\\nfunction " + h + "\\s*\\(\\s*proxy\\s*,\\s*param\\s*\\)[^\\n]*\\n)"),
                "$1\tif param:IsNetMessage() == true then return; end --SEAMLESS: another player's, not this one's\n");
        // Each game's timers and checks stay its own (local_timers)
        n += local_timers(t);
        return n;
    }
    // A boss fight is on from the boss's health bar to its clearing (each is a statement of its own in every area's
    // script): a guest who dies in it watches until it ends (global_event.lua's PartyGhostDeath_2)
    sub(std::regex("proxy:SetBossGauge\\s*\\("), "SeamlessBossFight = true; proxy:SetBossGauge(");
    sub(std::regex("proxy:ClearBossGauge\\s*\\("), "SeamlessBossFight = false; proxy:ClearBossGauge(");
    // The wandering ghosts' sightings: each player's own (local_wander_loops)
    n += local_wander_loops(t);
    // The light walls: one a player walks through is gone in every player's game (walls_for_watchers)
    n += walls_for_watchers(t);
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
    // The tutorial's demon (m08_01's 14301) offers its soul only with no session, as the original has no co-op there,
    // so nobody in a seamless session got it: offered with or without one, to each player, in the same way.
    sub(std::regex("proxy:(OnDistanceActionAttribute\\(\\s*\\d+\\s*,[^\\n]*?,\\s*10010180\\s*,\\s*0\\s*,\\s*180\\s*,\\s*)"
                   "ATTR_NOSESSION(\\s*,\\s*once\\s*\\)\\s*;)"),
        "proxy:NotNetMessage_begin(); proxy:$1ATTR_SESSION + ATTR_NOSESSION$2 proxy:NotNetMessage_end();");
    // ... and when one player takes it, the others get it too; the same for the keys (pickups_for_all)
    n += pickups_for_all(t);
    // ... and the items the host alone took at an item prompt
    n += gifts_for_all(t);
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

// The soul pickups (the prompt 10010180), the keys (key_pickups) and the host's item prompts (gift_pickups): the
// event's ID is the flag its handler sets once the item is taken.
std::vector<uint32_t> own_flags(const std::string& t)
{
    std::set<uint32_t> out;
    std::set<std::string> ids = key_pickups(t);
    for (const auto& g : gift_pickups(t)) ids.insert(g.first);
    for (const std::string& id : ids) {
        const unsigned long v = std::stoul(id);
        if (v < kProgressMax) out.insert((uint32_t)v);
    }
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

// An area's opening cutscenes: the ones its start plays (Initialize calls the event's handler itself, as 1-1's
// "if proxy:IsCompleteEvent( 2 ) == false then ... OnEvent_2(proxy,param); end" does for the dragon's flight). The
// others start from something a player does there (a fog walked through, a region entered), in every player's game.
std::vector<uint32_t> opening_flags(const std::string& t)
{
    std::set<uint32_t> out;
    const size_t ar = t.find("\nfunction Initialize_m0");
    if (ar == std::string::npos) return {};
    const size_t e = t.find("\nfunction ", ar + 1);
    const std::string init = code_of(t.substr(ar, e == std::string::npos ? std::string::npos : e - ar));
    const std::vector<uint32_t> scenes = scene_flags(code_of(t));
    const std::regex call("[^\\w\"]OnEvent_(\\d+)\\s*\\(\\s*proxy");
    for (auto it = std::sregex_iterator(init.begin(), init.end(), call); it != std::sregex_iterator(); ++it) {
        const unsigned long id = std::stoul((*it)[1].str());
        if (std::binary_search(scenes.begin(), scenes.end(), (uint32_t)id)) out.insert((uint32_t)id);
    }
    return std::vector<uint32_t>(out.begin(), out.end());
}

// The edited copies, by the original's size and hash; 0: the script is left as it is.
struct Copy {
    uint32_t addr = 0, size = 0;
    uint32_t world = 0xFFFFFFFFu;     // an area script's world (its area's top byte - 1)
    std::vector<uint32_t> progress;   // an area script's progress flags
    std::vector<uint32_t> boss;       // the ones a boss kill sets
    std::vector<uint32_t> own;        // its soul and key pickups
    std::vector<uint32_t> scenes;     // the cutscenes each player sees: the Nexus's, an area's opening
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
            if (!global) des_seamless_area_read(c.world, c.scenes, c.own);
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
        if (edit_global(ed, nl)) edits = 16;
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
        // The Nexus: all its cutscenes. Elsewhere only an area's opening: the others start fights or follow what a
        // player did there, and they play in every player's game.
        c.scenes = c.world == 0 ? scene_flags(t) : opening_flags(t);
    }
    std::fprintf(stderr, "[coop] script %s: %s%s\n", global ? "global_event.lua" : t.substr(ar + 20, 12).c_str(),
                 addr ? (std::to_string(edits) + " edits for co-op").c_str() : global ? "left as it is (an edit didn't fit)"
                                                                                      : "nothing to edit",
                 global ? "" : ("; " + std::to_string(c.progress.size()) + " progress flags, " +
                                std::to_string(c.own.size()) + " soul and key pickups, " + std::to_string(c.scenes.size()) +
                                " cutscenes, " + std::to_string(c.seal.size()) + " for the Archstones' seal").c_str());
    {
        std::lock_guard<std::mutex> g(sLock);
        learn(c);
        sCopies[key] = c;
    }
    if (!global) des_seamless_area_read(c.world, c.scenes, c.own);
    if (!addr) return false;
    *out = addr;
    *out_size = n;
    return true;
}

// What flag is in world (an area's top byte - 1), from that world's area scripts: 1 a progress flag (every player's
// world keeps it), 3 one a boss kill sets (a progress flag too), 2 a soul, key or item pickup (the player who took
// it keeps it), 4 a cutscene played (the Nexus's or an area's opening: the player who saw it keeps it), 5 the
// Archstones' seal opened (progress that a guest's own game makes too: the host's world takes it), 0 none of these.
int des_seamless_flag_kind(uint32_t world, uint32_t flag)
{
    std::lock_guard<std::mutex> g(sLock);
    auto has = [&](const std::map<uint32_t, std::vector<uint32_t>>& m) {
        auto it = m.find(world);
        return it != m.end() && std::binary_search(it->second.begin(), it->second.end(), flag);
    };
    return has(sBoss) ? 3 : has(sSeal) ? 5 : has(sProgress) ? 1 : has(sOwn) ? 2 : has(sScenes) ? 4 : 0;
}
