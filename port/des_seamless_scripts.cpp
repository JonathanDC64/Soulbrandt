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
//     it first watches until the fight ends (the boss dead, or the host dead: then it goes back with the host); out
//     of the session meanwhile, it goes home;
//   - the cutscenes keep their solo mode in a session: each player skips its own;
//   - no multiplayer wall closes the way back to an Archstone (the host may use them in a session);
//   - a boss's extra health counts a third blue phantom (a party of four) as two: x2, the game's most.
// Each area's script:
//   - a boss fight is marked from the boss's health bar to its clearing (for the watch above);
//   - the boss rooms' party walls drop as in solo play;
//   - a guest doesn't play the warp-out pose at a boss's death;
//   - the poses a player's script plays on it (an Archstone's touch and the trip home after it, a sword pulled out)
//     show in the other players' games too (poses_for_watchers);
//   - the Archstones: the host may use them in a session (the guests then follow it, des_seamless_game.cpp); a guest
//     still gets "You can't return during multiplayer.", at the Nexus's stones too (they had no session check);
//   - a boss's soul: a blue phantom may take its own, as the host takes its own;
//   - the wandering ghosts' sightings are each player's own (local_wander_loops);
//   - a light wall one player walks through is gone in every player's game (walls_for_watchers);
//   - a boss that wakes only for the player who walks through its fog wakes for one put in its room too
//     (standby_in_the_room);
//   - the room is never locked (LockSession), here or at a kill in global_event.lua, so a friend can always join again;
//   - a friend in a session doesn't travel alone: an area script's own trip (WarpNextStage) takes only the host or a
//     player out of a session, and the host's trip takes its friends along (no_travel_alone);
//   - the Nexus's timers and checks are each player's own (local_timers), and so is the kneel at a level-up.
// And from each area's script, the flags that move forward in every player's world (progress_flags), the soul
// pickups each player makes for itself (own_flags) and the cutscenes each player sees for itself (the Nexus's,
// scene_flags, and an area's opening, opening_flags), for des_seamless_game.cpp.
//
// One edit is a game option, made with or without co-op: DES_LIZARD_RETURN=1 (Settings, Items; off by default) has a
// crystal lizard that gets away come back at the next load without using up one of its returns (edit_lizards).
#include "des_seamless.h"

#include "ps3emu/ppu_lift.h"
#include "ps3emu/ps3_knob.h"

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
#include <tuple>
#include <vector>

extern "C" void ds_malloc(ppu_context* ctx);   // des_heap.cpp: the guest heap
// des_seamless_game.cpp: an area script of world was just read (its cutscenes each player sees, of them its opening's,
// and its soul, key and item pickups), before the area starts
void des_seamless_area_read(uint32_t world, const std::vector<uint32_t>& scenes, const std::vector<uint32_t>& chain,
                            const std::vector<uint32_t>& own);

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

// Crystal lizards (global_event.lua's RegistTokage, called by each area's script for each of its lizards). A lizard
// has an event flag and a count of returns (GetEventCount), and two watches that call the area's OnEvent_<flag>:
// its death and its EzState message 1000, its escape. That handler sets the flag and deletes both watches. At each
// load, a lizard whose flag is set comes back if it has a return left (the flag cleared, the count down one; the
// host's game only), else it stays gone. Each boss kill in a world gives each of its lizards a return
// (ResetTokage). So in the original game an escape uses up a return as a kill does.
// DES_LIZARD_RETURN=1: the escape's watch calls a handler of its own that only deletes the watches, so the flag stays
// clear and the lizard is there at the next load with its returns as they were. It is registered between
// NotNetMessage_begin and _end, so each game handles an escape it sees itself: the other players' games never run
// a handler they may not have. A kill works as before.
bool lizard_return()
{
    static int on = 0;
    PS3_KNOB(on, ps3_env_int("DES_LIZARD_RETURN", 0) != 0);
    return on != 0;
}

bool edit_lizards(std::string& t, const std::string& nl)
{
    const std::string old = "proxy:OnCheckEzStateMessage( eventId,TokageId,\"OnEvent_\"..eventId,1000);";
    size_t a, e;
    if (!func_span(t, "RegistTokage", &a, &e)) return false;
    const size_t at = t.find(old, a);
    if (at == std::string::npos || at + old.size() > e) return false;
    const size_t line = t.rfind('\n', at) + 1;
    const std::string indent = t.substr(line, at - line);
    if (indent.find_first_not_of(" \t") != std::string::npos) return false;
    t.replace(at, old.size(),
              "_G[\"OnTokageEscape_\"..eventId] = function(proxy,param) print(\"OnTokageEscape_\"..eventId); "
              "proxy:DeleteEvent(eventId); end; "
              "--DES_LIZARD_RETURN: an escape leaves the flag clear" + nl +
              indent + "proxy:NotNetMessage_begin();" + nl +
              indent + "proxy:OnCheckEzStateMessage( eventId,TokageId,\"OnTokageEscape_\"..eventId,1000);" + nl +
              indent + "proxy:NotNetMessage_end();");
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
    // The boss's souls (SetClearBonus; everyone's in full, des_seamless_game.cpp's hook in it) paid at the kill, as
    // a player alone gets them (SoloBlockClear's GetSoloClearBonus): in BlockClear2_2, right after a soul's revival
    // (BlockClear2_1). Without the rating screen they waited for the player's next load (g_Initialize's
    // AddBlockClearBonus). Once per boss: on a friend the boss clear starts twice (des_seamless_game.cpp's text hook),
    // and a SetClearBonus after the payment would have them paid again at the next load. The load clears both values.
    if (!replace_in(t, "BlockClear2", "proxy:SetClearBonus(ClearBossId);",
                    "if SeamlessBonusBoss ~= ClearBossId then SeamlessBonusBoss = ClearBossId; SeamlessBonusPaid = false; "
                    "proxy:SetClearBonus(ClearBossId); end --SEAMLESS: once per boss"))
        return false;
    if (!replace_in(t, "BlockClear2_2", "MissionSuccessed(proxy,param);",
                    "if SeamlessBonusPaid == false then SeamlessBonusPaid = true; proxy:GetSoloClearBonus(ClearBossId); end "
                    "MissionSuccessed(proxy,param); --SEAMLESS: the boss's souls now"))
        return false;
    if (!replace_in(t, "g_Initialize", "proxy:AddBlockClearBonus();",
                    "proxy:AddBlockClearBonus(); SeamlessBonusBoss = nil; SeamlessBonusPaid = nil; --SEAMLESS"))
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
    // Lua_MultiWall (OnEvent_4034's too): no multiplayer wall. When a session connects past an area's first part, the
    // host's game closes the way back to that part's Archstone, which stands in the last boss's room, with that room's
    // exit wall (1996 minus the block: 1-2 is block 1, wall 1995, the Phalanx room's way to 1-2), and only the
    // session's end (4035) opens it. But the host may use the Archstones in a session (edit_area), and the session
    // stays across a load: after a death, or a trip to that Archstone, it connected again with the host at the
    // Archstone, and the wall shut the host in the boss room. A guest's wall comes from its own flags 3001-3003, which
    // a guest's world doesn't set in seamless co-op.
    if (!replace_func(t, "Lua_MultiWall",
                      "function Lua_MultiWall(proxy,id)\n"
                      "\t--SEAMLESS: no multiplayer wall; the host may go back to the Archstone, and the group follows it\n"
                      "end\n", nl))
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
                      "\tSeamlessFightDeath = SeamlessBossFight == true;\n"
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
    // Once the boss is dead, a phantom that died in the fight (SeamlessFightDeath, noted at its death) comes back at
    // its own bloodstain, in the boss's room, not at the area's start: flag 19993 (past the game's last flag; only
    // des_seamless_game.cpp's flag hook reads it) just before the reload says so.
    // The session can end meanwhile (the host's game gone or out of reach). The game's own handlers for that
    // (OnRoomDisappeared, OnIrregularLeaveSession_1) start the trip home, but skip it while flag 4047 (a reload under
    // way) is set, and the phantom holds it from its death until it is back. So the watch never ended (no boss fight
    // to end, no host), no trip home came, and the phantom stayed a dead watcher out of the session, which
    // des_seamless_game.cpp doesn't join again until 4047 is clear. Out of the session, it now goes home as
    // OnRoomDisappeared would take it: the same message, and OnLeave_Limit's trip 5 s later.
    if (!replace_func(t, "PartyGhostDeath_2",
                      "function PartyGhostDeath_2(proxy,param)\n"
                      "\t--SEAMLESS: the phantom comes back in the host's world and stays in the session\n"
                      "\tif proxy:IsSession() == false then\n"
                      "\t\tproxy:InfomationMenu( INFOMENU_TYPE_LIST, -1, 0, -1, 1 );\n"
                      "\t\tproxy:AddInfomationTosBuffer( 20001000 );\n"
                      "\t\tproxy:NotNetMessage_begin();\n"
                      "\t\t\tproxy:OnKeyTime2( 4046, \"OnLeave_Limit\", 5.0, 0, 1, once );\n"
                      "\t\t\tproxy:SetLoadWait();\n"
                      "\t\t\tproxy:SetFlagInitState(2);\n"
                      "\t\tproxy:NotNetMessage_end();\n"
                      "\t\treturn;\n"
                      "\tend\n"
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
                      "\tif SeamlessFightDeath == true then\n"
                      "\t\tproxy:SetEventFlag(19993, true);\n"
                      "\tend\n"
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

// A boss that wakes only in the game of the player who walks through its fog. Phalanx (1-1) is drawn and can be hit
// only after PhalanxStandBy, which the fog's handler (OnEvent_110) runs only for the player who walked through (its
// IsNetMessage() == false branch). In the game's own co-op every player walks in. A friend that joins (or joins
// again) during the fight arrives beside the leader of the session instead, and stood in the room without Phalanx
// in its game: its blows did nothing. The fight's start (OnRegionJustIn(<id>, LOCAL_PLAYER or 10000, <room>,
// "OnEvent_<id>", once), whose handler shows the boss's health bar) fires in the game of each player that arrives in
// the room, one put there included; it now runs the same stand-by first. A player who walked in runs it twice, which
// sets the same values again. Today Phalanx's is the only such stand-by (the other bosses' fog handlers have none,
// and their StandBy_* run for every player). Returns the number of edits.
int standby_in_the_room(std::string& t)
{
    const std::string nl = t.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::regex fog("OnPlayerActionInRegion(?:Attribute)?\\(\\s*\\d+\\s*,\\s*\\d+\\s*,\\s*\"([A-Za-z_][A-Za-z0-9_]*)\"\\s*,"
                         "\\s*HELPID_GO_TO_IN");
    const std::regex walker("if\\s+param:IsNetMessage\\(\\)\\s*==\\s*false\\s+then\\s+([A-Za-z_][A-Za-z0-9_]*)\\(\\s*proxy\\s*,"
                            "\\s*param\\s*\\)");
    const std::regex start("OnRegionJustIn\\(\\s*\\d+\\s*,\\s*(?:LOCAL_PLAYER|10000)\\s*,\\s*\\d+\\s*,\\s*\"([A-Za-z_][A-Za-z0-9_]*)\""
                           "\\s*,\\s*once\\s*\\)");
    const std::string c = code_of(t);
    std::set<std::string> fogs, standbys, starts;
    for (auto it = std::sregex_iterator(c.begin(), c.end(), fog); it != std::sregex_iterator(); ++it)
        fogs.insert((*it)[1].str());
    for (const auto& f : fogs) {
        size_t a, e;
        if (!func_span(t, f, &a, &e)) continue;
        const std::string body = code_of(t.substr(a, e - a));
        for (auto it = std::sregex_iterator(body.begin(), body.end(), walker); it != std::sregex_iterator(); ++it)
            standbys.insert((*it)[1].str());
    }
    if (standbys.empty()) return 0;
    for (auto it = std::sregex_iterator(c.begin(), c.end(), start); it != std::sregex_iterator(); ++it)
        starts.insert((*it)[1].str());
    int n = 0;
    for (const auto& s : starts) {
        size_t a, e;
        if (!func_span(t, s, &a, &e) || code_of(t.substr(a, e - a)).find("SetBossGauge") == std::string::npos) continue;
        const size_t line = t.find('\n', a);   // the handler's first line
        if (line == std::string::npos || line >= e) continue;
        std::string add;
        for (const auto& sb : standbys)
            add += "\tif " + sb + " then " + sb + "(proxy, param) end --SEAMLESS: a player in the room without its own walk"
                   " through the fog (a friend that joined beside its host) gets the boss's stand-by too" + nl;
        t.insert(line + 1, add);
        n++;
    }
    return n;
}

// A friend in a session doesn't travel alone. An area script's trip (WarpNextStage) ends a friend's session in the
// game's own co-op: at the area's teardown a client that didn't ask for a summoned reload leaves (docs/145). The
// tutorial's trips are each player's own:
//   - at each part's end (m08_00's, m08_01's) the player who got there goes on;
//   - at its end (m08_02's dragon god) every game plays the cutscene and runs the same trip to the Nexus, whoever
//     stepped in;
//   - a death in m08_01 (the Vanguard's part) takes that player to the Nexus (m08_01Death, then OnEvent_14300_3).
// A friend whose game got there first left the session, went to its own Nexus, and its rejoin then stalled on the
// host (11-15). Each such call now runs only for the host or a player out of a session; the host's trip takes its
// friends along, as everywhere else. A friend's death there brings it back where it fell (the script revives it
// first). The levels' other trips (the boss's Archstone back to the Nexus) are the host's alone already: a friend gets
// "You can't return during multiplayer." there. Returns the number of edits.
int no_travel_alone(std::string& t)
{
    // (No comment added: a call can share its line with more code, as in "if playerId ==0 then proxy:WarpNextStage(
    // 8,02,00,00, 30); end".)
    const std::regex call("([^-])proxy:WarpNextStage\\(([^()\\n]*)\\)\\s*;");
    const auto n = std::distance(std::sregex_iterator(t.begin(), t.end(), call), std::sregex_iterator());
    if (n) t = std::regex_replace(t, call, "$1if proxy:IsSession() == false or proxy:IsClient() == false then "
                                           "proxy:WarpNextStage($2); end");
    return (int)n;
}

// A treasure the host alone turns on in the moment: `if proxy:IsClient() == false then` with only
// TreasureDispModeChange / EnableObjTreasure in it (a hanging corpse that falls in 1-1, 3-1's iron maidens that open).
// The original's phantom never took those items, so its game left them off. A friend saw the object open and couldn't
// take what was in it until the area loaded again (the area's start turns them on once their flag is set). Every game
// turns them on now; each player's items are its own (des_seamless_game.cpp), so nobody gets one twice. Returns the
// number of edits.
int treasures_for_all(std::string& t)
{
    const std::regex block("(\\n[ \\t]*if\\s+)proxy:IsClient\\(\\)\\s*==\\s*false(\\s+then[ \\t]*\\r?\\n"
                           "(?:[ \\t]*(?:--[^\\n]*|proxy:(?:TreasureDispModeChange|EnableObjTreasure)\\([^\\n]*)?\\r?\\n)+"
                           "[ \\t]*end)");
    std::string out;
    size_t from = 0;
    int n = 0;
    for (auto it = std::sregex_iterator(t.begin(), t.end(), block); it != std::sregex_iterator(); ++it) {
        const std::string body = (*it)[2].str();
        if (body.find("proxy:") == std::string::npos) continue;   // (comments only)
        out += t.substr(from, (size_t)it->position(0) - from);
        out += (*it)[1].str() + "true" + body;
        from = (size_t)it->position(0) + (size_t)it->length(0);
        n++;
    }
    if (!n) return 0;
    out += t.substr(from);
    t = out;
    return n;
}

// A handler that does nothing in a party: `if proxy:IsInParty() == true then return; end` as its first statement (3-1's
// OnEvent_410, the gargoyle's flight from the Fool's Idol's room to the next block). In the original the kill ends the
// session, so the host is alone again when it gets there; a seamless session stays, so nobody, the host included, was
// ever carried on. The check is commented out. Returns the number of edits.
int party_gates(std::string& t)
{
    const std::regex gate("(\\nfunction \\w+\\s*\\(\\s*proxy\\s*,\\s*param\\s*\\)[^\\n]*\\n)([ \\t]*)if\\s+proxy:IsInParty\\(\\)"
                          "\\s*==\\s*true\\s+then\\s*\\r?\\n\\s*return;?\\s*\\r?\\n\\s*end");
    const auto n = std::distance(std::sregex_iterator(t.begin(), t.end(), gate), std::sregex_iterator());
    if (n) t = std::regex_replace(t, gate, "$1$2--SEAMLESS (a session stays after a kill): if IsInParty() then return; end");
    return (int)n;
}

// A watch the host alone starts after a kill: `if proxy:IsClient() == false then` with only a local timer in it
// (NotNetMessage_begin, OnKeyTime2(<id>, "<handler>", ...), NotNetMessage_end), whose handler registers a region watch
// (3-1's OnEvent_5123_1 -> OnEvent_410_1, the gargoyle's flight). The original's phantom went home at the kill. Every
// game starts it now: each player's watch is its own (it is registered between NotNetMessage_begin and _end). The
// area's start registers the same watch once the kill's flag is set, so this is for the visit of the kill. Returns the
// number of edits.
int kill_watches_for_all(std::string& t)
{
    const std::regex block("(\\n[ \\t]*if\\s+)proxy:IsClient\\(\\)\\s*==\\s*false(\\s+then[ \\t]*\\r?\\n"
                           "[ \\t]*proxy:NotNetMessage_begin\\(\\);[^\\n]*\\n"
                           "[ \\t]*proxy:OnKeyTime2\\(\\s*\\d+\\s*,\\s*\"(\\w+)\"[^\\n]*\\n"
                           "[ \\t]*proxy:NotNetMessage_end\\(\\);[^\\n]*\\n[ \\t]*end)");
    const std::regex watch("proxy:OnRegion\\w*\\(");
    std::string out;
    size_t from = 0;
    int n = 0;
    for (auto it = std::sregex_iterator(t.begin(), t.end(), block); it != std::sregex_iterator(); ++it) {
        size_t a, e;
        if (!func_span(t, (*it)[3].str(), &a, &e) || !std::regex_search(code_of(t.substr(a, e - a)), watch)) continue;
        out += t.substr(from, (size_t)it->position(0) - from);
        out += (*it)[1].str() + "true" + (*it)[2].str();
        from = (size_t)it->position(0) + (size_t)it->length(0);
        n++;
    }
    if (!n) return 0;
    out += t.substr(from);
    t = out;
    return n;
}

// A cutscene that takes its player on: 3-1's flight (410). The walker's own region watch (OnEvent_410) asks for the
// cutscene (40001), whose start (REMO_START) puts the player at the landing (Warp(LOCAL_PLAYER, 2031)) and whose end
// (REMO_FINISH) removes the watch. Those two handlers are registered once each, as network events, so the walker's
// start ran in every game: the others were put at the landing without the cutscene, and their once-only handlers were
// used up, so a player who later played the cutscene itself stayed below (11-16). The game's own cutscene for a party
// (4-2's Patches, 1165) does it this way: the start tells the others (LuaCallStart), each game plays the cutscene, and
// the start and end handlers are each game's own. So now, for a REMO_START handler (outside NotNetMessage) that warps
// LOCAL_PLAYER, with a LOCAL_PLAYER region watch of the same ID whose handler asks for that ID's cutscene:
//   - its REMO_START and REMO_FINISH handlers are registered between NotNetMessage_begin and _end;
//   - the watch's handler runs once per visit (SeamlessFlight<id>: the script starts anew at each load) and tells the
//     others (LuaCallStart(<id>, 79)) before it asks for the cutscene;
//   - SEAMLESS_Flight_<id> (LuaCall(<id>, 79), registered with the REMO_START handler) plays the same cutscene in each
//     other game and sets the flag the walker's sets, unless that game played it already;
//   - the landing is one point, on the edge of a narrow walkway, and two players put there pushed each other off: both
//     at once fell (11-16), and a friend landing 2 s after the host still pushed the host off. For a landing
//     des_seamless_game.cpp has spots for (kBesideLandings: 3-1's 2031), a friend's start then tells co-op its spot
//     right after the warp (SEAMLESS_FlightBeside_<id>: SetEventFlag(19990 + spot - 1), by its player number, past the
//     game's last flag, so the game ignores it), and co-op moves the waiting warp there (beside_landing).
// So every player sees the cutscene, and each player's own start takes that player to the landing. Returns the number
// of edits.
int flights_for_all(std::string& t)
{
    const std::string nl = t.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    // pos's line, up to pos, has no comment
    const auto live = [&](size_t pos) {
        const size_t line = t.rfind('\n', pos);
        return t.substr(line == std::string::npos ? 0 : line + 1, pos - (line == std::string::npos ? 0 : line + 1))
                   .find("--") == std::string::npos;
    };
    // the indentation of pos's line
    const auto indent = [&](size_t pos) {
        const size_t line = t.rfind('\n', pos) + 1;
        return t.substr(line, t.find_first_not_of(" \t", line) - line);
    };
    // [a, e) of the first live registration `proxy:LuaCall(<id>, <kind>, ...);` outside NotNetMessage; false if none
    const auto reg = [&](const std::string& id, const char* kind, size_t* a, size_t* e) {
        const std::regex re(std::string("proxy:LuaCall\\(\\s*") + id + "\\s*,\\s*" + kind +
                            "\\s*,\\s*\"\\w+\"\\s*,\\s*once\\s*\\)\\s*;");
        for (auto it = std::sregex_iterator(t.begin(), t.end(), re); it != std::sregex_iterator(); ++it) {
            const size_t p = (size_t)it->position(0);
            if (!live(p) || in_not_net(t, p)) continue;
            *a = p;
            *e = p + (size_t)it->length(0);
            return true;
        }
        return false;
    };
    const std::regex start("proxy:LuaCall\\(\\s*(\\d+)\\s*,\\s*REMO_START\\s*,\\s*\"(\\w+)\"\\s*,\\s*once\\s*\\)\\s*;");
    const std::regex warp("proxy:Warp\\(\\s*LOCAL_PLAYER\\s*,");
    std::map<std::string, std::string> ids;   // the ID -> its REMO_START handler
    for (auto it = std::sregex_iterator(t.begin(), t.end(), start); it != std::sregex_iterator(); ++it) {
        size_t a, e;
        if (!live((size_t)it->position(0)) || in_not_net(t, (size_t)it->position(0)) ||
            !func_span(t, (*it)[2].str(), &a, &e) || !std::regex_search(code_of(t.substr(a, e - a)), warp))
            continue;
        ids.emplace((*it)[1].str(), (*it)[2].str());
    }
    // the landings des_seamless_game.cpp has spots for beside the point (kBeside): the ID and the warp's point
    static const std::set<std::pair<std::string, std::string>> kBesideLandings = {{"410", "2031"}};
    int n = 0;
    for (const auto& [id, on_start] : ids) {
        // the walker's own watch, and its handler's request for the cutscene (and the flag it sets)
        const std::regex watch("proxy:OnRegion\\w*\\(\\s*" + id + "\\s*,\\s*LOCAL_PLAYER\\s*,\\s*\\d+\\s*,\\s*\"(\\w+)\"");
        std::smatch w;
        if (!std::regex_search(t, w, watch) || !live((size_t)w.position(0)) || !in_not_net(t, (size_t)w.position(0)))
            continue;
        const std::string walk = w[1].str();
        size_t a, e;
        if (!func_span(t, walk, &a, &e)) continue;
        const std::string body = t.substr(a, e - a);
        std::smatch r, f;
        const std::regex ask("proxy:RequestRemo\\(\\s*\\d+\\s*,\\s*REMO_FLAG\\s*,\\s*" + id + "\\s*,\\s*1\\s*\\)\\s*;");
        const std::regex flag("proxy:SetEventFlag\\(\\s*" + id + "\\s*,\\s*(1|true)\\s*\\)\\s*;");
        if (!std::regex_search(body, r, ask) || !live(a + (size_t)r.position(0))) continue;
        const std::string remo = r[0].str();
        const std::string set = std::regex_search(body, f, flag) && live(a + (size_t)f.position(0)) ? f[0].str() : "";
        size_t sa, se;
        if (!reg(id, "REMO_START", &sa, &se)) continue;
        // the start's warp, for a landing with spots beside it
        size_t wa, we;
        std::smatch wp;
        std::string beside_at;   // where the friend's call goes: after the warp's statement
        size_t beside_pos = std::string::npos;
        if (func_span(t, on_start, &wa, &we)) {
            const std::string sb = t.substr(wa, we - wa);
            const std::regex to("proxy:Warp\\(\\s*LOCAL_PLAYER\\s*,\\s*(\\d+)\\s*\\)\\s*;");
            if (std::regex_search(sb, wp, to) && live(wa + (size_t)wp.position(0)) &&
                kBesideLandings.count({id, wp[1].str()})) {
                beside_pos = wa + (size_t)wp.position(0) + (size_t)wp.length(0);
                beside_at = wp[1].str();
            }
        }
        // every insert at its place in the text as it is now, made from the end backwards
        std::vector<std::pair<size_t, std::string>> ins;
        // the walker's handler: once per visit, and the others are told before it asks for the cutscene
        const size_t at = a + (size_t)r.position(0);
        ins.emplace_back(at, "proxy:LuaCallStart( " + id + " , 79 ); --SEAMLESS: every player's game plays it" + nl +
                                 indent(at));
        // the start's warp: a friend goes to its own spot beside the point
        if (beside_pos != std::string::npos)
            ins.emplace_back(beside_pos, " SEAMLESS_FlightBeside_" + id +
                                             "(proxy); --SEAMLESS: a friend beside point " + beside_at +
                                             " (des_seamless_game.cpp)");
        ins.emplace_back(t.find('\n', a) + 1, "\tif SeamlessFlight" + id + " == true then return; end SeamlessFlight" +
                                                  id + " = true; --SEAMLESS: once per visit (its own walk, or another "
                                                  "player's)" + nl);
        // the cutscene's start and end handlers: each game's own; the start's line is followed by the others' call
        ins.emplace_back(sa, "proxy:NotNetMessage_begin(); ");
        ins.emplace_back(se, " proxy:NotNetMessage_end(); --SEAMLESS: each game's own");
        const size_t line_end = t.find('\n', se);
        ins.emplace_back(line_end == std::string::npos ? t.size() : line_end + 1,
                         indent(sa) + "proxy:LuaCall( " + id + " , 79 , \"SEAMLESS_Flight_" + id +
                             "\" , everytime ); --SEAMLESS: another player's walk" + nl);
        size_t fa, fe;
        if (reg(id, "REMO_FINISH", &fa, &fe)) {
            ins.emplace_back(fa, "proxy:NotNetMessage_begin(); ");
            ins.emplace_back(fe, " proxy:NotNetMessage_end(); --SEAMLESS: each game's own");
        }
        std::stable_sort(ins.begin(), ins.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
        for (const auto& [pos, text] : ins) t.insert(pos, text);
        // the other players' games: the same cutscene and flag, once
        t += nl + "--SEAMLESS: a cutscene that takes its player on, in every player's game (flights_for_all)" + nl +
             "function SEAMLESS_Flight_" + id + "(proxy, param)" + nl +
             "\tif param:IsNetMessage() == false or SeamlessFlight" + id + " == true then return; end" + nl +
             "\tSeamlessFlight" + id + " = true;" + nl + "\t" + remo + nl + (set.empty() ? "" : "\t" + set + nl) +
             "end" + nl;
        // a friend's spot to co-op, past the game's last flag (the game ignores it): the first friend's player number
        // is 2 (11-16), so 2, 3 and 4 take spots 1, 2 and 3 (any other number spot 3)
        if (beside_pos != std::string::npos)
            t += "function SEAMLESS_FlightBeside_" + id + "(proxy)" + nl +
                 "\tif proxy:IsClient() == false then return; end" + nl +
                 "\tlocal k = proxy:GetLocalPlayerId();" + nl +
                 "\tlocal spot = 3;" + nl +
                 "\tif k == 2 then spot = 1; elseif k == 3 then spot = 2; end" + nl +
                 "\tproxy:SetEventFlag( 19990 + spot - 1 , true );" + nl + "end" + nl;
        n++;
    }
    return n;
}

// A boss's soul taken: what its handlers (OnEvent_<id>, OnEvent_<id>_<k>) do for the host alone (a block under
// "if proxy:IsClient() == false then") they now do for a friend too. In the original a phantom goes home after the
// boss, so only the host's world goes on from there; a friend in a seamless session stays, and takes its own soul
// (each player's prompt is its own, see edit_area). The one such block is 1-2's: the Tower Knight's soul (4252) starts
// the watch on the castle gate (OnRegionJustIn 570, in the player's game alone: at the gate, its cutscene and the gate
// open, once flag 8035 says a demon of another world is dead). A friend's game never watched it, so its gate stayed
// shut while the host's opened (11-15), until a load (the area's start watches it for every player). Each player now
// sees the cutscene as it walks to the gate. Returns the number of edits.
int soul_steps_for_all(std::string& t)
{
    std::set<std::string> ids;
    const std::regex prompt("OnDistanceActionAttribute\\(\\s*(\\d+)\\s*,[^\\n]*?\"OnEvent_(\\d+)\"[^\\n]*?,\\s*10010180\\s*,");
    for (auto it = std::sregex_iterator(t.begin(), t.end(), prompt); it != std::sregex_iterator(); ++it)
        if ((*it)[1].str() == (*it)[2].str()) ids.insert((*it)[1].str());
    const std::regex host_only("(\\n[ \\t]*if\\s+)proxy:IsClient\\(\\)\\s*==\\s*false(\\s+then)");
    int n = 0;
    for (const std::string& id : ids) {
        const std::regex fn("\\nfunction OnEvent_" + id + "(?:_\\d+)?\\s*\\(");
        std::vector<std::pair<size_t, size_t>> spans;
        for (auto it = std::sregex_iterator(t.begin(), t.end(), fn); it != std::sregex_iterator(); ++it) {
            const size_t a = (size_t)it->position(0), e = t.find("\nfunction ", a + 5);
            spans.emplace_back(a, e == std::string::npos ? t.size() : e);
        }
        for (auto s = spans.rbegin(); s != spans.rend(); ++s) {   // from the end, so the earlier spans stay put
            std::string body = t.substr(s->first, s->second - s->first);
            const auto k = std::distance(std::sregex_iterator(body.begin(), body.end(), host_only), std::sregex_iterator());
            if (!k) continue;
            body = std::regex_replace(body, host_only, "$1true$2");
            t.replace(s->first, s->second - s->first, body);
            n += (int)k;
        }
    }
    return n;
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

// A statement at the top of function `name`, after its first line; false if the function isn't there.
bool insert_first(std::string& t, const std::string& name, const std::string& line, const std::string& nl)
{
    size_t a, e;
    if (!func_span(t, name, &a, &e)) return false;
    const size_t at = t.find('\n', a);
    if (at == std::string::npos || at >= e) return false;
    t.insert(at + 1, line + nl);
    return true;
}

// The final area under the Nexus (King Allant in the Old One) and the endings, for every player in the session. The
// original lets only the host (and players in their own world) in: the boss's fog has no phantom's entry, the kill's
// last steps and the endings run in the host's game alone, and a phantom goes home at the kill. A seamless session
// stays, so the Nexus's script now:
//   - offers the fog (61) to a friend too; each player's prompt is its own (it is registered between
//     NotNetMessage_begin and _end), as at every other boss;
//   - runs the King's death (OnEvent_70) once per game. Each game ran it for its own copy of the King and again for
//     the other games' message (a kill's message reaches its handler whatever the game's own registration did), and
//     everything it set up came twice: the sword's pickup steps (OnEvent_141_WAIT), so one pull gave the host two
//     swords (11-16). The first call runs, whichever it is (a script value, SeamlessKingDead: flag 70 can come from
//     the host's world before a friend's own call). The pickup's last step (OnEvent_141_1) runs once too. The King's
//     death also ends the boss fight (SeamlessBossFight), so a friend who died in it comes back;
//   - shows a friend "The Demon Was Destroyed" after the King's last words, as the host gets it, clears its boss
//     health bar, and sets 450 at the text's end (OnEvent_450; the cutscene after the kill, 71, waits for 450). The
//     handler is registered once (CustomLuaCall(450, ..., once)), and each game's own call comes after the King's
//     last words (9054) in that game. The host's call, a message in the friend's game, came first, used up the
//     friend's registration and returned (messages were the others' business), so the friend's own call found none
//     (11-16: no text, the King's bar stayed). A friend's game now runs the first of the two, once
//     (SeamlessDemon450). Its next step (OnEvent_450_1), which did nothing in a party, pays the King's souls and
//     revives a host in soul form, as for a player alone. A player dead in the fight gets the souls at the King's
//     death instead (OnEvent_70), paid by the load that brings it back;
//   - plays that cutscene (71, the Maiden's prayer) in every game, whoever's game started it: its start tells the
//     others (LuaCallStart(71, 79)), and each game's SEAMLESS_Scene_71 plays its own. Its end (the Maiden praying,
//     the endings' watches, the player put beside her) is then each game's too. A friend's game never played it
//     before (its own talk with the King, 9054, and 450 weren't set there): the Maiden didn't pray there, and no
//     ending could start;
//   - lets a friend start the good ending too (81, its prompt beside the Maiden), and runs the ending a player starts
//     in every game, the first one started (80, the Maiden killed; 81, the prompt). Its step (the clear state, the
//     clear count, the items the clear takes, and the end flow, which resets every flag for NG+) was for the host's
//     game alone (IsClient() == false). It now runs in every game, once (SeamlessEnding), and tells the others
//     (LuaCallStart(<id>, 79)), whose SEAMLESS_End_<id> runs theirs. Each game then plays the ending's cutscene once
//     (SeamlessEndScene: the end flow's step may come twice, as the kill's did), and every player goes on to NG+ in
//     its own world: the step sets flag 19999 too, past the game's last flag, which the game ignores and the flag
//     hook reads as "this game's ending started" (des_seamless_game.cpp's ending_started).
// All or nothing: false (t unchanged by the caller) if one edit doesn't fit.
bool final_area_for_all(std::string& t, const std::string& nl)
{
    static const char kInit[] = "Initialize_m01_00_00_00";
    // the fog, for a friend too
    if (!replace_in(t, kInit,
                    "proxy:OnPlayerActionInRegionAttribute( 61, 2895, \"OnEvent_61\", HELPID_GO_TO_IN, possible_chr_type, once );",
                    "proxy:OnPlayerActionInRegionAttribute( 61, 2895, \"OnEvent_61\", HELPID_GO_TO_IN, possible_chr_type + ATTR_WHITE, "
                    "once ); --SEAMLESS: a friend too"))
        return false;
    // the King's death, once per game; the sword's last step too
    if (!insert_first(t, "OnEvent_70",
                      "\tif SeamlessKingDead == true then return; end SeamlessKingDead = true; --SEAMLESS: once per game "
                      "(its own copy's death, then the others' message)", nl))
        return false;
    if (!insert_first(t, "OnEvent_141_1", "\tif proxy:IsCompleteEvent( 141 ) == true then return; end --SEAMLESS: once", nl))
        return false;
    // the fight over at the King's death, in every game: a friend who died in it stops watching and comes back (at
    // the other bosses, BlockClear's ClearBoss ends the watch at the kill). The bar's clearing, OnEvent_450, comes
    // only after the King's last words and skips a game whose player is dead (4047), so a dead friend watched forever
    if (!insert_first(t, "OnEvent_70", "\tSeamlessBossFight = false; --SEAMLESS: the fight is over", nl))
        return false;
    // "The Demon Was Destroyed", the boss's bar cleared and 450, for a friend too: from the host's message, once
    if (!replace_in(t, "OnEvent_450", "if param:IsNetMessage() == true then",
                    "if SeamlessDemon450 == true or (param:IsNetMessage() == true and proxy:IsClient() == false) then "
                    "--SEAMLESS: a friend's game runs the host's, once"))
        return false;
    if (!replace_in(t, "OnEvent_450", "print(\"OnEvent_450 begin\");",
                    "SeamlessDemon450 = true; print(\"OnEvent_450 begin\"); --SEAMLESS"))
        return false;
    if (!replace_in(t, "OnEvent_450", "if proxy:IsLivePlayer() == true then",
                    "if proxy:IsLivePlayer() == true or proxy:IsWhiteGhost() == true then --SEAMLESS: a friend too"))
        return false;
    // the King's souls at the kill in a party too, as a player alone gets them (Clear_450), a friend's as the host's;
    // a host in soul form is revived there too (the next step's party branch only logged an error)
    if (!replace_in(t, "OnEvent_450_1", "if proxy:IsInParty() == true then", "if false then --SEAMLESS: in a party too"))
        return false;
    if (!replace_in(t, "OnEvent_450_1", "if proxy:IsLivePlayer() == true then",
                    "if proxy:IsLivePlayer() == true or proxy:IsWhiteGhost() == true then --SEAMLESS: a friend too"))
        return false;
    // and for a player dead in the fight: its clear (450) returns while it is dead (4047), and once it is back the
    // area's start sees the King dead (70) and sets 450 itself, so neither 450 nor 70 runs there again. The King's
    // death sets its souls then, and the load that brings it back pays them. (The King's id, from OnEvent_451's call.)
    size_t a451, e451;
    std::smatch king;
    const std::regex call450("CustomLuaCallStart\\(\\s*450\\s*,\\s*(\\d+)\\s*\\)");
    if (!func_span(t, "OnEvent_451", &a451, &e451)) return false;
    const std::string body451 = t.substr(a451, e451 - a451);
    if (!std::regex_search(body451, king, call450)) return false;
    if (!replace_in(t, "OnEvent_70", "proxy:SetEventFlag( 70, true );",
                    "proxy:SetEventFlag( 70, true ); if proxy:IsCompleteEvent( 4047 ) == true then proxy:SetClearBonus( " +
                        king[1].str() + " ); end --SEAMLESS: a player dead in the fight: the souls at its respawn's load"))
        return false;
    // the cutscene after the kill, in every game
    static const char kScene71[] = "proxy:RequestRemo(3,REMO_FLAG,71,1);";
    if (!replace_in(t, "OnEvent_71", kScene71,
                    "proxy:LuaCallStart( 71 , 79 ); --SEAMLESS: every player's game plays it" + nl + "\t" + kScene71))
        return false;
    // the good ending's prompt, for a friend too (the area's start registers it once 71 has played, and 71's end)
    static const char kPrompt81[] = "proxy:OnPlayerActionInRegionAttribute( 81, 2010, \"OnEvent_81\", 10010405, possible_chr_type, once );";
    static const char kPrompt81w[] =
        "proxy:OnPlayerActionInRegionAttribute( 81, 2010, \"OnEvent_81\", 10010405, possible_chr_type + ATTR_WHITE, once ); "
        "--SEAMLESS: a friend too";
    if (!replace_in(t, kInit, kPrompt81, kPrompt81w) || !replace_in(t, "OnEvent_71_RemoFinish", kPrompt81, kPrompt81w))
        return false;
    // the endings, in every game: the first one started
    static const struct { const char* id; const char* fn; } kEnds[] = {{"80", "OnEvent_80"}, {"81", "OnEvent_81_2"}};
    for (const auto& end : kEnds) {
        const std::string id = end.id;
        if (!replace_in(t, end.fn, "if proxy:IsClient() == false then",
                        "if SeamlessEnding == nil then --SEAMLESS: every player's game, the first ending started" + nl +
                            "\t\tSeamlessEnding = " + id + ";" + nl + "\t\tproxy:LuaCallStart( " + id + " , 79 );" + nl +
                            "\t\tproxy:SetEventFlag( 19999 , true ); --SEAMLESS: tells co-op this game's ending started "
                            "(past the last flag: the game ignores it)"))
            return false;
    }
    for (const char* fn : {"OnEvent_80_2", "OnEvent_81_3"})
        if (!insert_first(t, fn,
                          "\tif SeamlessEndScene == true then return; end SeamlessEndScene = true; --SEAMLESS: the "
                          "ending's cutscene once",
                          nl))
            return false;
    // the handlers the others' calls reach, registered as the area starts
    const std::regex init("(\\nfunction Initialize_m01_00_00_00\\s*\\([^)]*\\)[^\\n]*\\n)");
    std::smatch m;
    if (!std::regex_search(t, m, init)) return false;
    t.insert((size_t)m.position(0) + (size_t)m.length(0),
             "\tproxy:LuaCall( 71 , 79 , \"SEAMLESS_Scene_71\" , everytime ); --SEAMLESS" + nl +
                 "\tproxy:LuaCall( 80 , 79 , \"SEAMLESS_End_80\" , everytime ); --SEAMLESS" + nl +
                 "\tproxy:LuaCall( 81 , 79 , \"SEAMLESS_End_81\" , everytime ); --SEAMLESS" + nl);
    t += nl + "--SEAMLESS: the final area for every player in the session (final_area_for_all)" + nl +
         "function SEAMLESS_Scene_71(proxy, param)" + nl +
         "\tif param:IsNetMessage() == false or proxy:IsCompleteEvent( 71 ) == true then return; end" + nl +
         "\tproxy:StopPointSE( 1 );" + nl + "\t" + kScene71 + nl + "\tproxy:SetEventFlag( 71, true );" + nl + "end" + nl +
         "function SEAMLESS_End_80(proxy, param)" + nl +
         "\tif param:IsNetMessage() == false or SeamlessEnding ~= nil then return; end" + nl +
         "\tOnEvent_80(proxy, param);" + nl + "end" + nl +
         "function SEAMLESS_End_81(proxy, param)" + nl +
         "\tif param:IsNetMessage() == false or SeamlessEnding ~= nil then return; end" + nl +
         "\tOnEvent_81_2(proxy, param);" + nl + "end" + nl;
    return true;
}

// The poses a player's own script plays on that player (PlayAnimation(10000 or LOCAL_PLAYER, <pose>)): the touch of an
// Archstone (8283), the trip home after it (8284) and a sword pulled out (8290: the Nexus's last one, Stormbringer, and
// three more). The other players' games never played them on that player's character: no phantom was there when a
// player used an Archstone in the original, and a pull runs in the puller's game only (its handler's
// IsNetMessage() == false branch). The game's own pose sync (Luafunc_PlaySynchroAnimation, as through
// LuaCall(4013, 1)) plays the pose in the sender's game too, a second time, under the script's own OnChrAnimEnd watch
// of it, so the edit sends one of its own after the call: LuaCallStartPlus(4013, <area's key>, <pose>). It isn't sent
// when the handler runs for another player's message. The call's network send doesn't depend on NotNetMessage_begin
// (that is proxy+0x64; the send checks proxy+0x65), so it goes out from inside those blocks too. Each area's
// SEAMLESS_Pose (LuaCall(4013, <area's key>), registered as the area starts) then plays the pose on the sender's
// character in the other games (GetPlayID() + 10001, the game's own pose sync's) and does nothing in the sender's.
// The key is the area's (7600 + 10 x world + block; the game's own call uses 1), as a pose sent just before a trip
// reaches a friend after its follow has loaded the next area: the trip home's (8284) came 2 s after the friend's game
// had loaded 1-1, where its handler played it on the host's character as the host arrived (two copies, 11-20). There
// no handler takes the old area's key. Not the calls in a comment, such as the warp-out pose edit_area takes away.
// Returns the number of edits.
int poses_for_watchers(std::string& t)
{
    const std::regex init("(\\nfunction Initialize_m(\\d\\d)_(\\d\\d)_\\d\\d_\\d\\d\\s*\\([^)]*\\)[^\\n]*\\n)");
    const std::regex pose("proxy:PlayAnimation\\(\\s*(?:10000|LOCAL_PLAYER)\\s*,\\s*(8283|8284|8290)\\s*\\)\\s*;");
    const std::regex sig("^function \\w+\\s*\\(\\s*proxy\\s*,\\s*param\\s*\\)");
    std::smatch m;
    if (!std::regex_search(t, m, init)) return 0;
    const std::string key = std::to_string(7600 + 10 * std::stoi(m[2].str()) + std::stoi(m[3].str()));
    const std::string nl = t.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::vector<std::pair<size_t, std::string>> at;   // after each call: its pose
    for (auto it = std::sregex_iterator(t.begin(), t.end(), pose); it != std::sregex_iterator(); ++it) {
        const size_t p = (size_t)it->position(0);
        const size_t line = t.rfind('\n', p);
        const size_t ls = line == std::string::npos ? 0 : line + 1;
        if (t.substr(ls, p - ls).find("--") != std::string::npos) continue;   // a line comment
        const size_t bo = t.rfind("--[[", p), bc = t.rfind("]]", p);
        if (bo != std::string::npos && (bc == std::string::npos || bc < bo)) continue;   // a block comment
        const size_t fn = t.rfind("\nfunction ", p);
        if (fn == std::string::npos || !std::regex_search(t.substr(fn + 1, t.find('\n', fn + 1) - fn - 1), sig))
            continue;   // not in a handler with the event's param
        at.emplace_back(p + (size_t)it->length(0), (*it)[1].str());
    }
    if (at.empty()) return 0;
    for (auto r = at.rbegin(); r != at.rend(); ++r)
        t.insert(r->first, " if param:IsNetMessage() == false then proxy:LuaCallStartPlus( 4013 , " + key + " , " +
                               r->second + " ); end --SEAMLESS: the others see it too ");
    std::regex_search(t, m, init);
    t.insert((size_t)m.position(0) + (size_t)m.length(0), "\tproxy:LuaCall( 4013 , " + key +
                                                               " , \"SEAMLESS_Pose\" , everytime ); --SEAMLESS: "
                                                               "another player's pose in this area" + nl);
    t += nl + "--SEAMLESS: a pose another player's script played on that player (poses_for_watchers)" + nl +
         "function SEAMLESS_Pose(proxy, param)" + nl + "\tif param:IsNetMessage() == false then return; end" + nl +
         "\tproxy:PlayAnimation( param:GetPlayID() + 10001 , param:GetParam3() );" + nl + "end" + nl;
    return (int)at.size() + 2;
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
    // The warp-out pose a phantom plays at a boss kill, on its way home (some areas name the player 10000)
    sub(std::regex("(IsClient\\(\\) == true then\\s*\\r?\\n\\s*)proxy:PlayAnimation\\(\\s*(LOCAL_PLAYER|10000)\\s*,\\s*8284\\s*\\);"),
        "$1--SEAMLESS proxy:PlayAnimation( $2 , 8284 );");
    // The poses a player's script plays on it (an Archstone's touch and trip home, a sword pulled out), in the other
    // players' games too (poses_for_watchers; after the warp-out pose's edit, which takes that one away)
    n += poses_for_watchers(t);
    // No room lock. LockSession (here as a player walks into a boss room, and in global_event.lua's BlockClear2 at a
    // kill) sets the session's lock (sm+0x2CA), which stays until the host's game leaves its room (UnLockSession is
    // empty): the original ends the session after a boss. A locked host kicks every new join (the member handler at
    // 0x0053D8C0, then KickoutRoomMember). A friend that follows keeps its place in the room and never noticed; one
    // back from a trip of its own (each player's at the tutorial's end) was kicked. (Not the calls already in a
    // comment.)
    sub(std::regex("([^-])proxy:LockSession\\(\\);"), "$1--[[SEAMLESS proxy:LockSession();]]");
    // A boss fight is on from the boss's health bar to its clearing (each is a statement of its own in every area's
    // script): a guest who dies in it watches until it ends (global_event.lua's PartyGhostDeath_2). The Nexus's script
    // holds the final area and its boss too, so it gets this as well (its other edits first).
    auto boss_fight = [&] {
        sub(std::regex("proxy:SetBossGauge\\s*\\("), "SeamlessBossFight = true; proxy:SetBossGauge(");
        sub(std::regex("proxy:ClearBossGauge\\s*\\("), "SeamlessBossFight = false; proxy:ClearBossGauge(");
    };
    if (nexus) {
        // The Nexus's Archstones (OnDistanceAction(<id>, 10000, <stone>, "OnEvent_<id>", WarpGimmickDist_A, ...)) have
        // no session check: no phantom reaches the Nexus in the original game. A guest's own touch took it out of the
        // session and to the area alone, where its rejoin then stalled. A guest in a session gets the levels' answer
        // at the stone instead ("You can't return during multiplayer.", 10010720); the host's travel takes it along.
        // A touch is a network event, so its handler ran in every player's game: when the host touched a stone, a
        // friend's game showed that message too (two copies, 11-20), and a friend's touch ran the host's handler. A
        // handler now ignores another player's touch (param:IsNetMessage(), as the levels' stones do).
        const std::regex stone("OnDistanceAction\\(\\s*(\\d+)\\s*,\\s*10000\\s*,\\s*(\\d+)\\s*,\\s*\"OnEvent_(\\d+)\"\\s*,"
                               "\\s*WarpGimmickDist_A");
        std::map<std::string, std::string> stones;   // its handler's event ID -> the stone (where the dialog shows)
        for (auto it = std::sregex_iterator(t.begin(), t.end(), stone); it != std::sregex_iterator(); ++it)
            if ((*it)[1].str() == (*it)[3].str()) stones[(*it)[1].str()] = (*it)[2].str();
        for (const auto& [id, obj] : stones) {
            const std::string guard =
                "\tif param:IsNetMessage() == true then return; end --SEAMLESS: another player's touch\n"
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
        // The sword the host alone pulled out after the final boss (141, item lot 10730): for every player, as the
        // levels' item prompts (gifts_for_all)
        n += gifts_for_all(t);
        // The final area and the endings, for every player (final_area_for_all; after gifts_for_all, whose step it
        // runs once). All or nothing: without them it stays the host's, as in the original.
        {
            const std::string nl = t.find("\r\n") != std::string::npos ? "\r\n" : "\n";
            std::string fa = t;
            if (final_area_for_all(fa, nl)) {
                t.swap(fa);
                n += 16;
            } else {
                std::fprintf(stderr, "[coop] the final area: an edit didn't fit; it stays the host's alone\n");
            }
        }
        // Each game's timers and checks stay its own (local_timers)
        n += local_timers(t);
        boss_fight();
        return n;
    }
    boss_fight();
    // The wandering ghosts' sightings: each player's own (local_wander_loops)
    n += local_wander_loops(t);
    // The light walls: one a player walks through is gone in every player's game (walls_for_watchers)
    n += walls_for_watchers(t);
    // A boss that wakes only for the player who walked through its fog wakes for a player put in its room too
    n += standby_in_the_room(t);
    // A friend in a session goes with its host, not on an area script's trip of its own (no_travel_alone)
    n += no_travel_alone(t);
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
    // ... what its handlers do for the host alone, a friend's game does too (soul_steps_for_all; before the copies
    // pickups_for_all makes of them)
    n += soul_steps_for_all(t);
    // ... and when one player takes it, the others get it too; the same for the keys (pickups_for_all)
    n += pickups_for_all(t);
    // ... and the items the host alone took at an item prompt
    n += gifts_for_all(t);
    // The treasures the host alone turned on in the moment (treasures_for_all)
    n += treasures_for_all(t);
    // A handler that does nothing in a party (party_gates), and a watch the host alone starts after a kill
    // (kill_watches_for_all): 3-1's flight to the next block
    n += party_gates(t);
    n += kill_watches_for_all(t);
    // ... and its cutscene in every player's game (flights_for_all; after party_gates, whose check is the handler's
    // first line)
    n += flights_for_all(t);
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
        for (const auto& h : handlers(id)) {
            set_true(h.first, h.second, &kills);   // every flag they set to true
            // ... and the kill's next step when it has a name of its own (1-4's OnEvent_4481, a second after King
            // Allant's kill, where the host alone sets Boletaria's clear, 8010): a function a handler names in quotes
            // (as OnKeyTime2 takes it) that counts the bosses (GetBossFlagCount). Not every named function: those
            // include NPCs' states, the warp-out pose's and the music's.
            for (size_t q = t.find('"', h.first); q != std::string::npos && q < h.second; q = t.find('"', q + 1)) {
                const size_t e = t.find('"', q + 1);
                if (e == std::string::npos || e > h.second) break;
                const auto it = funcs.find(t.substr(q + 1, e - q - 1));
                if (it != funcs.end() && t.find("GetBossFlagCount(", it->second.first) < it->second.second)
                    set_true(it->second.first, it->second.second, &kills);
                q = e;
            }
        }
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
    std::vector<uint32_t> chain;      // of those, the opening's (the area's start plays one: opening_flags)
    std::vector<uint32_t> seal;       // and the Archstones' seal (the Nexus)
};

// The final area's cutscenes in the Nexus's script: the trip down (40, and 60, which its end runs), the one after King
// Allant (71) and the endings (80, 81). Each plays in every player's game when it happens (40 through the talk's
// call, the others through final_area_for_all), and their flags set up the way down, the Old One and the Maiden at the
// area's start. So a friend's game keeps them as the host has them: one cleared to be seen again took the way down,
// the King and the Maiden's place from a friend's Nexus while its host's had them.
constexpr uint32_t kFinalScenes[] = {40, 60, 71, 80, 81};
std::mutex sLock;
// (by the original's size and hash, and the edits made: 1 co-op's, 2 the lizards')
std::map<std::tuple<uint32_t, uint64_t, int>, Copy> sCopies;
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
    const bool coop = des::seamless::active() != des::seamless::Mode::Off, lizards = lizard_return();
    if ((!coop && !lizards) || size < 64 || size > (4u << 20)) return false;
    // only the area scripts: global_event.lua and m0X_YY_ZZ_WW.lua, recognised by what they define
    static const char kGlobal[] = "function BlockClear2_3Leave(";
    static const char kArea[] = "function Initialize_m0";
    std::string t(size, '\0');
    for (uint32_t i = 0; i < size; i++) t[i] = (char)vm_read8(buf + i);
    const bool global = t.find(kGlobal) != std::string::npos;
    const size_t ar = t.find(kArea);
    if (!global && ar == std::string::npos) return false;
    if (!coop && !global) return false;   // (without co-op only the lizards' edit, in global_event.lua)
    const auto key = std::make_tuple(size, fnv(t), (coop ? 1 : 0) | (lizards ? 2 : 0));
    {
        std::unique_lock<std::mutex> g(sLock);
        auto it = sCopies.find(key);
        if (it != sCopies.end()) {
            learn(it->second);
            const Copy c = it->second;
            g.unlock();
            if (!global) des_seamless_area_read(c.world, c.scenes, c.chain, c.own);
            if (!c.addr) return false;
            *out = c.addr;
            *out_size = c.size;
            return true;
        }
    }
    const std::string nl = t.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string ed = t;
    int edits = 0;   // co-op's
    bool lizard_edit = false;
    if (global) {
        if (coop) {
            if (edit_global(ed, nl)) edits = 19;
            else ed = t;
        }
        if (lizards) {
            lizard_edit = edit_lizards(ed, nl);
            std::fprintf(stderr, "[des] crystal lizards: %s\n",
                         lizard_edit ? "one that gets away comes back at the next load (DES_LIZARD_RETURN)"
                                     : "the script's lizards aren't as expected, so they work as in the original game");
        }
    } else {
        static const char kNexus[] = "function Initialize_m01_00_00_00(";   // the Nexus: no Archstone edits
        edits = edit_area(ed, t.compare(ar, sizeof kNexus - 1, kNexus) == 0);
    }
    uint32_t addr = 0, n = 0;
    if ((edits || lizard_edit) && ed != t) {
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
        // The Nexus: all its cutscenes but the final area's. Elsewhere only an area's opening: the others start fights
        // or follow what a player did there, and they play in every player's game.
        c.chain = opening_flags(t);
        c.scenes = c.world == 0 ? scene_flags(t) : c.chain;
        if (c.world == 0)
            c.scenes.erase(std::remove_if(c.scenes.begin(), c.scenes.end(),
                                          [](uint32_t f) { return std::find(std::begin(kFinalScenes), std::end(kFinalScenes), f) !=
                                                                  std::end(kFinalScenes); }),
                           c.scenes.end());
    }
    if (coop)
        std::fprintf(stderr, "[coop] script %s: %s%s\n", global ? "global_event.lua" : t.substr(ar + 20, 12).c_str(),
                 addr && edits ? (std::to_string(edits) + " edits for co-op").c_str() : global ? "left as it is (an edit didn't fit)"
                                                                                      : "nothing to edit",
                 global ? "" : ("; " + std::to_string(c.progress.size()) + " progress flags, " +
                                std::to_string(c.own.size()) + " soul and key pickups, " + std::to_string(c.scenes.size()) +
                                " cutscenes, " + std::to_string(c.seal.size()) + " for the Archstones' seal").c_str());
    {
        std::lock_guard<std::mutex> g(sLock);
        learn(c);
        sCopies[key] = c;
    }
    if (!global) des_seamless_area_read(c.world, c.scenes, c.chain, c.own);
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
