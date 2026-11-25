/*
 * des_pso_predict.cpp: the pipelines Demon's Souls can ask for, predicted
 * from the game's own files, as pipeline index rows (docs/97).
 *
 * The pipeline index (port/data/pso_index) lists the pipelines players'
 * games have asked for. It only knows what someone has played, and only for
 * the US disc's programs. The game's files say much more: every material
 * the game has (mtd/mtd.mtdbnd) names its shader and its blend mode, and the
 * shader archives (shader/*.shaderbnd) hold every program by name. The names
 * follow one scheme, so each material gives the programs a draw of it can
 * use, and the engine's rules give their draw state:
 *
 *   DS_<Phn|Gst>_Dif<Spc|___><Bmp|___><Mul|___><Lit|___><Csd|Sdw|___>_<Non|HemDir3|HemEnv><|PntS|PntSS|PntSSSS>
 *     the fragment program: the material's maps, then what the draw picks
 *     at run time (shadow receiving, the light type, point lights), and the
 *     ghost twin (Gst) the game draws phantoms with;
 *   DS_<Phn|Gst>_PI[W]N[T|TT]_D[D][L]_<Non|Sdw>
 *     the vertex program: W for a skinned material, T for a bump map, TT
 *     for bump and multi-texture; the UV sets; Sdw for shadow receiving;
 *   DS_Phn_<layout>_Dep / _DepAlp
 *     the shadow map pass, with the engine's depth program, the alpha-tested
 *     one, or the material's own plain program.
 *
 * The light type is the draw's, not the material's: every material is seen
 * with all three types in play (the material's own first in the order). Its
 * blend mode gives the blend and depth states; both cull modes and both
 * vertex input forms (the GPU fetch and the CPU path) are made, as either can
 * happen for any material. The engine's own passes (post filters, menus,
 * particles, debug text, the executable's programs) have fixed rules.
 *
 * Against the pipelines recorded in play over many sessions (3,202 rows),
 * this makes 99.7% of them. The rest are post-filter draws with a vertex
 * program left bound from another pass, and two programs the game builds at
 * run time. The rows are many more than any session asks for (about 44,000
 * for one HDR setting; the rules cannot tell which combinations a level has).
 * That is no matter: a driver's cost is a shader's, the first time a pipeline
 * in the process has it (30-70 ms), and another pipeline with shaders it has
 * seen takes about 0.5 ms. So the start's pass keeps one row per shader the
 * pipeline index does not use (shaders_of, des_prework.cpp's add_predicted):
 * about 1,600, built once and loaded at every start with the index's.
 *
 * A row's fingerprints are the runtime's (rsx_d3d12_program_hash64), of the
 * programs in this disc's files, so the rows fit any version of the game.
 * The rows are in tiers, the likeliest first: the material's own states,
 * then the fade blends, the other light types, the ghost twin.
 */
#include "des_pso_predict.h"

#include "rsx_fp_decompiler.h"   /* rsx_fp_texture_units */
#include "des_rsx_route.h"       /* rsx_d3d12_program_hash64, through rsx_out in a PS3RECOMP_RSX_HAL build */

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <filesystem>
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

extern "C" uint64_t rsx_d3d12_program_hash64(int fragment, const uint8_t* ucode, uint32_t ucode_len);

namespace {

#ifdef _WIN32
constexpr char kSep = '\\';
#else
constexpr char kSep = '/';
#endif

constexpr uint32_t RGBA8 = 0x1C, RGBA16F = 0x0A, R32F = 0x29;
constexpr uint32_t OPQ = 0, ALPHA = 0x126298B, ADD = 0x122288B, ADD2 = 0x162288B;   /* the blend words */
constexpr uint32_t D_ON = 0xF, D_TRN = 0xD, D_OFF = 0x0, D_SHD = 0x7;               /* the depth words */

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint32_t le32(const uint8_t* p) { return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]; }

bool read_file(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    const bool ok = n > 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

/* A BND3 archive's entries (big-endian, as the PS3's): name and bytes. */
struct Entry {
    std::string name;   /* after the last backslash */
    const uint8_t* data;
    size_t size;
};
std::vector<Entry> bnd3(const std::vector<uint8_t>& b)
{
    std::vector<Entry> out;
    if (b.size() < 0x20 || std::memcmp(b.data(), "BND3", 4) != 0) return out;
    const uint32_t count = be32(&b[0x10]);
    for (uint32_t i = 0; i < count; i++) {
        const size_t o = 0x20 + size_t(i) * 0x18;
        if (o + 0x18 > b.size()) break;
        const uint32_t size = be32(&b[o + 4]), off = be32(&b[o + 8]), name_off = be32(&b[o + 16]);
        if (off > b.size() || size > b.size() - off || name_off >= b.size()) continue;
        const char* n = reinterpret_cast<const char*>(&b[name_off]);
        std::string name(n, strnlen(n, b.size() - name_off));
        const size_t cut = name.find_last_of('\\');
        if (cut != std::string::npos) name = name.substr(cut + 1);
        out.push_back({name, &b[off], size});
    }
    return out;
}

/* A Cg binary's microcode (sce_vp_rsx 0x1B5B, sce_fp_rsx 0x1B5C). */
bool cg_ucode(const uint8_t* cg, size_t n, const uint8_t** uc, uint32_t* usz, bool* fragment)
{
    if (n < 32) return false;
    const uint32_t profile = be32(cg), total = be32(cg + 8), prog = be32(cg + 20), sz = be32(cg + 24),
                   uoff = be32(cg + 28);
    if ((profile != 0x1B5Bu && profile != 0x1B5Cu) || total > n || uoff > total || sz > total - uoff ||
        uint64_t(prog) + 24 > total || !sz)
        return false;
    *fragment = profile == 0x1B5Cu;
    *uc = cg + uoff;
    *usz = sz;
    return true;
}

bool starts(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

struct Row {
    uint64_t vp, fp;
    uint32_t vfetch, exp32, cube, d2c, clamp, blend, nrt, rtfmt, cmask, cull, depth, hdrv;
    auto key() const
    {
        return std::tie(vp, vfetch, fp, exp32, cube, d2c, clamp, blend, nrt, rtfmt, cmask, cull, depth, hdrv);
    }
    bool operator<(const Row& o) const { return key() < o.key(); }
};

/* A rule's forms beyond its state (Predict::add): each list is every value a row takes; vps/fps: these
 * programs instead of the rule's names. */
struct Opt {
    std::vector<uint32_t> vfetch{0, 1}, cube{0}, clamp{0}, d2c{0}, hdrv{0};
    const std::set<uint64_t>* vps = nullptr;
    const std::set<uint64_t>* fps = nullptr;
};

struct Predict {
    std::map<std::string, std::set<uint64_t>> v, f;   /* a program's name -> its fingerprints */
    std::unordered_map<uint64_t, uint32_t> units;     /* a fragment program -> the texture units it samples */
    std::unordered_map<uint64_t, bool> exp32;          /* a fragment program -> its archive's draws export 32-bit */
    std::set<uint64_t> eboot_v, eboot_f;               /* the executable's programs */
    std::map<Row, int> rows;                           /* row -> its tier (the lowest that made it) */
    std::set<std::string> missing;
    bool hdr = false;

    void program(const std::string& archive, const std::string& name, const uint8_t* cg, size_t n)
    {
        const uint8_t* uc;
        uint32_t usz;
        bool frag;
        if (!cg_ucode(cg, n, &uc, &usz, &frag)) return;
        const uint64_t h = rsx_d3d12_program_hash64(frag ? 1 : 0, uc, usz);
        (frag ? f : v)[name].insert(h);
        if (frag) {
            units[h] = rsx_fp_texture_units(uc, usz);
            /* (the last archive to hold the code wins, as the draws' own rule has it) */
            exp32[h] = archive == "ds_sfx" || archive == "ds_filter" || archive == "dbgfont" || archive == "EBOOT";
        }
        if (archive == "EBOOT") (frag ? eboot_f : eboot_v).insert(h);
    }

    const std::set<uint64_t>* find(const std::map<std::string, std::set<uint64_t>>& m, const std::string& n,
                                   const char* kind)
    {
        const auto it = m.find(n);
        if (it == m.end() || it->second.empty()) {
            missing.insert(std::string(kind) + " " + n);
            return nullptr;
        }
        return &it->second;
    }

    /* One rule's rows: every vertex program of vpn with every fragment program of fpn, in each state given.
     * A texture kind (cube, d2c) or HDR variant counts only for units the fragment program samples. Scene
     * rows come in the SDR and HDR target formats; only the one HDR has is made. */
    void add(int tier, const std::string& vpn, const std::string& fpn, uint32_t blend, uint32_t nrt,
             std::vector<uint32_t> rtfmts, uint32_t cmask, const std::vector<uint32_t>& culls, uint32_t depth,
             const Opt& o = Opt())
    {
        const std::set<uint64_t>* vs = o.vps ? o.vps : find(v, vpn, "VP");
        const std::set<uint64_t>* fs = o.fps ? o.fps : (vs ? find(f, fpn, "FP") : nullptr);
        if (!vs || !fs || vs->empty() || fs->empty()) return;
        /* the scene's format: SDR's RGBA8 or HDR's RGBA16F, whichever this setting draws with */
        if (std::find(rtfmts.begin(), rtfmts.end(), RGBA8) != rtfmts.end() &&
            std::find(rtfmts.begin(), rtfmts.end(), RGBA16F) != rtfmts.end())
            rtfmts.erase(std::find(rtfmts.begin(), rtfmts.end(), hdr ? RGBA8 : RGBA16F));
        for (const uint64_t vh : *vs)
            for (const uint64_t fh : *fs) {
                const uint32_t used = units.count(fh) ? units[fh] : 0;
                const uint32_t e32 = exp32.count(fh) && exp32[fh] ? 1 : 0;
                for (uint32_t cube : o.cube) {
                    cube &= used | (used << 16);
                    for (uint32_t d2c : o.d2c) {
                        d2c &= used;
                        for (const uint32_t clamp : o.clamp)
                            for (const uint32_t hdrv : o.hdrv) {
                                if (hdrv && !(hdrv & 0x3FFFFFFFu & used) && hdrv != 0x40000000u) continue;
                                for (const uint32_t vf : o.vfetch)
                                    for (const uint32_t rt : rtfmts)
                                        for (const uint32_t cu : culls) {
                                            const Row r{vh, fh, vf, e32, cube, d2c, clamp, blend, nrt, rt, cmask,
                                                        cu, depth, hdrv};
                                            auto it = rows.find(r);
                                            if (it == rows.end()) rows.emplace(r, tier);
                                            else if (tier < it->second) it->second = tier;
                                        }
                            }
                    }
                }
            }
    }

    void material(const std::string& spx, int bm, int lt);
    void engine();
};

const char* const kLight[4] = {"Non", "HemDir3", "HemEnvLerp", "HemEnv"};
const std::vector<uint32_t> kCulls{5, 0};

/* One material's rows: its shader's programs in every form a draw picks at run time, with its blend mode's
 * states. */
void Predict::material(const std::string& spx, int bm, int lt)
{
    std::string base = spx.substr(0, spx.rfind('.'));
    std::string low = base;
    for (char& c : low) c = char(std::tolower((unsigned char)c));
    const bool skin = low.find("skin") != std::string::npos;
    if (starts(base, "DS_Phn_ColDif")) {
        std::string tok = base.substr(std::strlen("DS_Phn_ColDif"));
        if (tok.size() >= 5 && (tok.compare(tok.size() - 5, 5, "_Skin") == 0 || tok.compare(tok.size() - 5, 5, "_skin") == 0))
            tok.resize(tok.size() - 5);
        const bool spc = tok.find("Spc") != std::string::npos, bmp = tok.find("Bmp") != std::string::npos,
                   mul = tok.find("Mul") != std::string::npos, lit = tok.find("Lit") != std::string::npos;
        const std::string uv = std::string("D") + (mul ? "D" : "") + (lit ? "L" : "");
        const std::string lc = std::string("PI") + (skin ? "W" : "") + "N" + (bmp && mul ? "TT" : bmp ? "T" : "");
        std::vector<int> lts;
        if (lt >= 0 && lt <= 3) lts.push_back(lt);
        for (const int x : {1, 3, 0})
            if (x != lt) lts.push_back(x);
        typedef std::pair<uint32_t, uint32_t> St;   /* blend, depth */
        std::vector<St> main, extra, fade;
        if (bm == 0 || bm == 1) {
            main = {{OPQ, D_ON}};
            extra = {{OPQ, D_OFF}};
            fade = {{ALPHA, D_TRN}, {ADD2, D_TRN}};
        } else if (bm == 4) {
            main = {{ADD, D_TRN}, {ADD2, D_TRN}};
            extra = {{ALPHA, D_TRN}};
        } else {
            main = {{ALPHA, D_TRN}};
            extra = {{ADD2, D_TRN}, {ADD, D_TRN}, {OPQ, D_ON}};
        }
        const std::string flags = std::string(spc ? "Spc" : "___") + (bmp ? "Bmp" : "___") + (mul ? "Mul" : "___") +
                                  (lit ? "Lit" : "___");
        struct Sdw { const char* fp; const char* vp; uint32_t bit; };
        const Sdw sdws[3] = {{"Csd", "Non", 0x800000}, {"Sdw", "Sdw", 0x800000}, {"___", "Non", 0}};
        const char* const pnts[4] = {"", "PntS", "PntSS", "PntSSSS"};
        for (size_t li = 0; li < lts.size(); li++) {
            const std::string light = kLight[lts[li]];
            const bool env = starts(light, "HemEnv");
            const std::vector<std::pair<uint32_t, int>> envs =
                env ? std::vector<std::pair<uint32_t, int>>{{0x800, 0}, {0x1800, 0}, {0, 1}}
                    : std::vector<std::pair<uint32_t, int>>{{0, 0}};
            for (const Sdw& s : sdws)
                for (const char* pnt : pnts)
                    for (const char* fam : {"Phn", "Gst"}) {
                        const bool gst = fam[0] == 'G';
                        const std::string fpn = std::string("DS_") + fam + "_Dif" + flags + s.fp + "_" + light + pnt;
                        const std::string vpn = std::string("DS_") + fam + "_" + lc + "_" + uv + "_" + s.vp;
                        std::vector<std::pair<St, int>> states;
                        for (const St& x : main) states.push_back({x, 0});
                        for (const St& x : fade) states.push_back({x, 1});
                        for (const St& x : extra) states.push_back({x, 2});
                        if (gst) states.push_back({{ALPHA, D_TRN}, 0});
                        for (const auto& st : states)
                            for (const auto& e : envs) {
                                const int t = 1 + std::max({st.second, e.second, int(li)}) + (gst ? 1 : 0);
                                Opt o;
                                o.cube = {s.bit | e.first};
                                add(t, vpn, fpn, st.first.first, 1, {RGBA8, RGBA16F}, 7, kCulls, st.first.second, o);
                            }
                    }
        }
        /* The shadow map: through the engine's depth program, or DepAlp for alpha-tested materials ... */
        for (const char* suf : {"Dep", "DepAlp"}) {
            const bool dep = suf[3] == 0;
            const std::string vpn = "DS_Phn_" + lc + "_" + uv + "_" + suf;
            const std::string fpn = dep ? "shadow" : "DS_Phn_Dif________________DepAlp";
            Opt o;
            if (dep) o.fps = &eboot_f;
            const int t = 1 + (dep == (bm == 0) ? 0 : 1);
            Opt oh = o;
            oh.hdrv = {0, 0x80000001u};
            add(t, vpn, fpn, OPQ, 0, {RGBA8}, 0, kCulls, D_SHD, oh);
            add(t + 1, vpn, fpn, OPQ, 1, {RGBA8}, 0, kCulls, D_SHD, o);
            add(t + 1, vpn, fpn, OPQ, 1, {RGBA8}, 0, kCulls, D_OFF, o);
            add(t + 1, vpn, fpn, ALPHA, 1, {RGBA8}, 7, kCulls, D_OFF, o);
            add(t + 1, vpn, fpn, OPQ, 1, {RGBA8}, 7, kCulls, D_TRN, o);
        }
        /* ... or the material's own plain program (no shadow receiving, no point lights), with its layout's
         * vertex program or the two general ones. */
        const std::pair<std::string, int> lays[3] = {{lc + "_" + uv, 0}, {"PINTT_DDL", 1}, {"PIWNTT_DDL", 1}};
        for (const auto& lay : lays)
            for (size_t li = 0; li < lts.size(); li++) {
                const std::string light = kLight[lts[li]];
                Opt o;
                o.cube = starts(light, "HemEnv") ? std::vector<uint32_t>{0, 0x800, 0x1800} : std::vector<uint32_t>{0};
                add(2 + lay.second + std::min<int>(int(li), 1), "DS_Phn_" + lay.first + "_Dep",
                    "DS_Phn_Dif" + flags + "____" + light, OPQ, 0, {RGBA8}, 0, kCulls, D_SHD, o);
            }
    } else if (starts(base, "DS_Water_Env")) {
        const std::string sk = skin ? "_Skin" : "";
        Opt o;
        o.cube = {0, 0x100};
        add(1, "DS_Water" + sk, "DS_Water_Env", ALPHA, 1, {RGBA8, RGBA16F}, 7, kCulls, D_TRN, o);
        for (const uint32_t dp : {D_ON, D_OFF}) {
            add(1, "DS_Water_Mask" + sk, "DS_Water_Mask", OPQ, 1, {RGBA8, RGBA16F}, 8, kCulls, dp);
            add(2, "DS_Phn_PINTT_DDL_Dep", "DS_Water_Mask", OPQ, 1, {RGBA8, RGBA16F}, 8, kCulls, dp);
        }
    } else if (starts(base, "DS_Ghost")) {
        for (const char* vpn : {"DS_Ghost_Skin", "DS_Ghost_Tod"})
            for (const uint32_t bl : {ADD, ALPHA}) add(1, vpn, "DS_Ghost", bl, 1, {RGBA8, RGBA16F}, 7, kCulls, D_TRN);
        for (const char* lay : {"PINTT_DDL", "PIWNTT_DDL", "PIN_D", "PIWN_D", "PINT_D", "PIWNT_D"})
            add(2, std::string("DS_Phn_") + lay + "_Dep", "DS_Ghost", OPQ, 0, {RGBA8}, 0, kCulls, D_SHD);
    }
}

/* The engine's own passes: post filters, menus, debug drawing, particles, and the executable's programs. */
void Predict::engine()
{
    const std::vector<uint32_t> none{0};
    for (const auto& fp : f) {
        if (!starts(fp.first, "DS_Fil_")) continue;
        std::vector<std::string> vpns{"DS_Fil_CameraBlur"};
        if (v.count(fp.first)) vpns.push_back(fp.first);
        for (const std::string& vpn : vpns)
            for (const uint32_t cm : {7u, 0xFu, 8u}) {
                Opt o;
                o.vfetch = {0};
                o.clamp = {0, 1};
                o.d2c = {0, 2};
                o.hdrv = {0, 0x80000001u, 0x40000000u};
                add(1, vpn, fp.first, OPQ, 1, {RGBA8, RGBA16F, R32F}, cm, none, D_OFF, o);
                Opt o2;
                o2.vfetch = {0};
                o2.clamp = {0, 1};
                add(2, vpn, fp.first, OPQ, 0, {RGBA8}, cm, none, D_OFF, o2);
            }
    }
    for (const char* vpn : {"DS_Menu_Common", "DS_Menu_Font"})
        for (const auto& fp : f) {
            if (!starts(fp.first, "DS_Menu_")) continue;
            for (const uint32_t bl : {ALPHA, ADD})
                for (const uint32_t nrt : {1u, 0u}) {
                    Opt o;
                    o.clamp = {0, 1};
                    add(1, vpn, fp.first, bl, nrt, {RGBA8, RGBA16F}, 7, none, D_OFF, o);
                }
        }
    for (const char* fpn : {"DbgPrim_Col", "DbgPrim_ColTex", "DbgFont_"})
        for (const uint32_t bl : {ADD, ALPHA})
            for (const uint32_t dp : {D_OFF, D_TRN}) {
                Opt o;
                o.vfetch = {0};
                o.clamp = {0, 1};
                add(2, "DbgPrim_Common", fpn, bl, 1, {RGBA8, RGBA16F}, 7, none, dp, o);
            }
    /* particles: each vertex program with the fragment programs of its family (the name without "TypeN") */
    const auto family = [](const std::string& n) {
        size_t e = n.size();
        while (e > 0 && std::isdigit((unsigned char)n[e - 1])) e--;
        return e < n.size() && e >= 4 && n.compare(e - 4, 4, "Type") == 0 ? n.substr(0, e - 4) : n;
    };
    for (const auto& vp : v) {
        if (!starts(vp.first, "DS_Sfx_")) continue;
        for (const auto& fp : f) {
            if (!starts(fp.first, "DS_Sfx_") || family(vp.first) != family(fp.first)) continue;
            const std::pair<uint32_t, uint32_t> sts[4] = {{ADD, D_TRN}, {ALPHA, D_TRN}, {ADD2, D_TRN}, {OPQ, D_ON}};
            for (const auto& st : sts) {
                Opt o;
                o.d2c = {0, 4};
                add(st.first == ADD || st.first == ALPHA ? 1 : 2, vp.first, fp.first, st.first, 1, {RGBA8, RGBA16F},
                    7, {0, 7, 5}, st.second, o);
            }
        }
    }
    /* the executable's: every vertex program with every fragment program, the 2D and shadow passes' states */
    for (const uint32_t cm : {7u, 0xFu, 0u})
        for (const uint32_t bl : {OPQ, ALPHA, ADD}) {
            Opt o;
            o.vfetch = {0};
            o.clamp = {0, 1};
            o.hdrv = {0, 0x80000001u};
            o.vps = &eboot_v;
            o.fps = &eboot_f;
            add(2, "eboot", "eboot", bl, cm ? 1 : 0, {RGBA8, RGBA16F}, cm, {0, 5}, D_OFF, o);
        }
}

/* An MTD's shader (its .spx) and one int parameter (g_BlendMode, g_LightingType): the name, then a 0xA3
 * byte, the type's name at the next 4-byte boundary, and the value 16 bytes after the "10 00 00" that
 * follows. -1 when it is not there. */
int mtd_int(const uint8_t* d, size_t n, const char* param)
{
    std::vector<uint8_t> key(param, param + std::strlen(param));
    key.push_back(0xA3);
    const uint8_t* end = d + n;
    const uint8_t* p = std::search(d, end, key.begin(), key.end());
    if (p == end) return -1;
    size_t k = size_t(p - d) + key.size();
    while (k % 4) k++;
    if (k + 4 > n) return -1;
    const uint32_t tl = le32(d + k);
    if (tl > 16 || k + 4 + tl > n) return -1;
    if (std::string(reinterpret_cast<const char*>(d + k + 4), tl) != "int") return -1;
    const size_t q = k + 4 + tl;
    static const uint8_t mark[3] = {0x10, 0, 0};
    const uint8_t* h = std::search(d + q, d + std::min(n, q + 40), mark, mark + 3);
    if (h == d + std::min(n, q + 40)) return -1;
    const size_t at = size_t(h - d) - 1 + 16;
    return at + 4 <= n ? int(le32(d + at)) : -1;
}
std::string mtd_spx(const uint8_t* d, size_t n)
{
    static const char ext[] = ".spx";
    const uint8_t* end = d + n;
    const uint8_t* p = std::search(d, end, ext, ext + 4);
    if (p == end) return std::string();
    const uint8_t* s = p;
    while (s > d && s[-1] != '\\' && s[-1] >= 0x20) s--;
    return std::string(reinterpret_cast<const char*>(s), size_t(p + 4 - s));
}

}  // namespace

std::string des_pso_predict(const std::string& usrdir, const std::string& elf, bool hdr, DesPsoPredictCounts* counts,
                            const std::string* shaders_of)
{
    Predict pr;
    pr.hdr = hdr;
    if (counts) *counts = DesPsoPredictCounts{};
    /* the programs: the archives in name order, then the executable's */
    const std::string sd = usrdir + kSep + "shader";
    std::vector<std::string> names;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE fh = FindFirstFileA((sd + "\\*.shaderbnd").c_str(), &fd);
    if (fh != INVALID_HANDLE_VALUE) {
        do names.emplace_back(fd.cFileName);
        while (FindNextFileA(fh, &fd));
        FindClose(fh);
    }
#else
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(sd, ec))
        if (e.path().extension() == ".shaderbnd") names.emplace_back(e.path().filename().string());
#endif
    std::sort(names.begin(), names.end());
    std::vector<std::vector<uint8_t>> keep;   /* (the entries point into these) */
    for (const std::string& an : names) {
        keep.emplace_back();
        if (!read_file(sd + kSep + an, keep.back())) continue;
        const std::string arch = an.substr(0, an.size() - std::strlen(".shaderbnd"));
        for (const Entry& e : bnd3(keep.back())) {
            const size_t nl = e.name.size();
            if (nl < 4 || e.size < 8) continue;
            std::string ext = e.name.substr(nl - 4);
            for (char& c : ext) c = char(std::tolower((unsigned char)c));
            if (ext != ".vpo" && ext != ".fpo") continue;
            const uint32_t hdr_size = be32(e.data + 4);
            if (hdr_size >= e.size) continue;
            pr.program(arch, e.name.substr(0, nl - 4), e.data + hdr_size, e.size - hdr_size);
        }
    }
    std::vector<uint8_t> eb;
    if (!elf.empty() && read_file(elf, eb))
        for (size_t o = 0; o + 32 <= eb.size(); o += 4) {
            const uint32_t profile = be32(&eb[o]);
            if ((profile != 0x1B5Bu && profile != 0x1B5Cu) || be32(&eb[o + 4]) != 6) continue;
            const uint32_t total = be32(&eb[o + 8]), params = be32(&eb[o + 16]), prog = be32(&eb[o + 20]);
            const uint32_t usz = be32(&eb[o + 24]), uoff = be32(&eb[o + 28]);
            if (total < 32 || total > eb.size() - o || params > total || uint64_t(prog) + 24 > total ||
                uoff > total || !usz || usz > total - uoff || usz % 16)
                continue;
            char nm[32];
            std::snprintf(nm, sizeof nm, "eboot_%08zx", o);
            pr.program("EBOOT", nm, &eb[o], total);
            o += (total - 4) & ~size_t(3);
        }
    if (pr.v.empty() || pr.f.empty()) return std::string();
    /* the materials: each shader and blend mode once (the light type only orders the tiers) */
    std::vector<uint8_t> mb;
    std::set<std::tuple<std::string, int, int>> mats;
    if (read_file(usrdir + kSep + "mtd" + kSep + "mtd.mtdbnd", mb))
        for (const Entry& e : bnd3(mb)) {
            const std::string spx = mtd_spx(e.data, e.size);
            if (spx.empty()) continue;
            const int bm = mtd_int(e.data, e.size, "g_BlendMode");
            mats.emplace(spx, bm < 0 ? 0 : bm, mtd_int(e.data, e.size, "g_LightingType"));
        }
    for (const auto& m : mats) pr.material(std::get<0>(m), std::get<1>(m), std::get<2>(m) < 0 ? 0 : std::get<2>(m));
    pr.engine();
    /* the text: by tier, then by row */
    std::vector<std::pair<int, const Row*>> order;
    order.reserve(pr.rows.size());
    for (const auto& r : pr.rows) order.push_back({r.second, &r.first});
    std::stable_sort(order.begin(), order.end(),
                     [](const std::pair<int, const Row*>& a, const std::pair<int, const Row*>& b) {
                         return a.first < b.first;
                     });
    /* One row per shader: the first that uses a vertex or pixel shader that shaders_of's rows and the rows
     * before it do not. A shader is a program in one form (vertex: fetch or not; pixel: its texture kinds,
     * exports, clamp and HDR variant); the clamp's vertex shaders count apart, as a run draws one form. */
    typedef std::tuple<uint64_t, uint32_t, uint32_t> VsKey;
    typedef std::tuple<uint64_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t> PsKey;
    std::set<VsKey> have_vs;
    std::set<PsKey> have_ps;
    const auto vs_key = [](const Row& r) { return VsKey(r.vp, r.vfetch, r.clamp); };
    const auto ps_key = [](const Row& r) { return PsKey(r.fp, r.exp32, r.cube, r.d2c, r.clamp, r.hdrv); };
    if (shaders_of)
        for (size_t i = 0; i < shaders_of->size();) {
            size_t e = shaders_of->find('\n', i);
            if (e == std::string::npos) e = shaders_of->size();
            if (e > i && (*shaders_of)[i] != '#') {
                unsigned long long vp = 0, fp = 0;
                unsigned x[13] = {};
                if (std::sscanf(shaders_of->c_str() + i, "%llx %x %x %llx %x %x %x %x %x %x %x %x %x %x %x", &vp,
                                &x[0], &x[1], &fp, &x[2], &x[3], &x[4], &x[5], &x[6], &x[7], &x[8], &x[9], &x[10],
                                &x[11], &x[12]) >= 14) {
                    Row r{vp, fp, x[1], x[2], x[3], x[4], x[5], 0, 0, 0, 0, 0, 0, x[12]};
                    have_vs.insert(vs_key(r));
                    have_ps.insert(ps_key(r));
                }
            }
            i = e + 1;
        }
    std::string out;
    out.reserve(order.size() * 80);
    char line[256];
    unsigned kept = 0;
    for (const auto& o : order) {
        const Row& r = *o.second;
        if (shaders_of) {
            const bool nv = have_vs.insert(vs_key(r)).second, np = have_ps.insert(ps_key(r)).second;
            if (!nv && !np) continue;
        }
        kept++;
        int n = std::snprintf(line, sizeof line, "%llx\t0\t%x\t%llx\t%x\t%x\t%x\t%x\t%x\t%x\t%x\t%x\t%x\t%x",
                              (unsigned long long)r.vp, r.vfetch, (unsigned long long)r.fp, r.exp32, r.cube, r.d2c,
                              r.clamp, r.blend, r.nrt, r.rtfmt, r.cmask, r.cull, r.depth);
        if (r.hdrv) n += std::snprintf(line + n, sizeof line - n, "\t%x", r.hdrv);
        out.append(line, size_t(n));
        out += '\n';
    }
    if (counts) {
        counts->rows = kept;
        counts->predicted = unsigned(pr.rows.size());
        counts->materials = unsigned(mats.size());
        counts->programs = 0;
        for (const auto& x : pr.v) counts->programs += unsigned(x.second.size());
        for (const auto& x : pr.f) counts->programs += unsigned(x.second.size());
        counts->missing = unsigned(pr.missing.size());
    }
    return out;
}
