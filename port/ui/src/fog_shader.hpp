// Written for Demon's Souls. Smoke drawn live behind the start screen (DeS UI): RmlUi's "shader" decorator with the
// value "fog:<name>", drawn by render_gpu.cpp (the Windows menu renderer, D3D12 and Vulkan) as one pixel shader over
// the element. The smoke is 3D gradient noise with time as its third axis, so its shapes churn and dissolve instead of
// sliding, and its coordinates are bent by two more noises so it curls. The numbers in kLooks were tuned on a preview
// page that ran the same maths in WebGL (layer by layer, the preview's sliders).
//
// With DES_UI_NO_ANIM (the tests' pictures) the smoke stands still at its first frame. render_d3d12.cpp (frozen, the
// Linux menu) and the D3D11 renderer draw no smoke: there the element shows only its black background.
#pragma once

#include <cmath>
#include <cstring>

// The noise, shared with the logo's flame (flame_shader.hpp): 3D gradient noise and its octaves.
#define DES_UI_HLSL_NOISE R"(
// The lattice's random gradients: PCG3D (Jarzynski and Olano), in integers. A float hash is not safe here: the
// compiler may reorder its float maths, so a corner shared by two cells can hash differently from each side, and the
// smoke shows straight seams along the cells' edges.
uint3 pcg3d(uint3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v;
}
float3 hash33(float3 i) { return float3(pcg3d(uint3(int3(i)))) * (2.0 / 4294967295.0) - 1.0; }
float gnoise(float3 p) {
    float3 i = floor(p), f = p - i;
    float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float n000 = dot(hash33(i), f);
    float n100 = dot(hash33(i + float3(1.0, 0.0, 0.0)), f - float3(1.0, 0.0, 0.0));
    float n010 = dot(hash33(i + float3(0.0, 1.0, 0.0)), f - float3(0.0, 1.0, 0.0));
    float n110 = dot(hash33(i + float3(1.0, 1.0, 0.0)), f - float3(1.0, 1.0, 0.0));
    float n001 = dot(hash33(i + float3(0.0, 0.0, 1.0)), f - float3(0.0, 0.0, 1.0));
    float n101 = dot(hash33(i + float3(1.0, 0.0, 1.0)), f - float3(1.0, 0.0, 1.0));
    float n011 = dot(hash33(i + float3(0.0, 1.0, 1.0)), f - float3(0.0, 1.0, 1.0));
    float n111 = dot(hash33(i + float3(1.0, 1.0, 1.0)), f - float3(1.0, 1.0, 1.0));
    return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y),
                lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y), u.z);
}
// Octaves: each one smaller and changing twice as fast, so the small curls churn quicker than the big shapes.
float fbm(float3 p, float oct, float gain) {
    float s = 0.0, a = 1.0, n = 0.0;
    [loop] for (int i = 0; i < 6; i++) {
        if (float(i) >= oct) break;
        s += a * gnoise(p);
        n += a;
        p = p * 2.02 + float3(17.3, 9.1, 4.7);
        a *= gain;
    }
    return s / n;
}
)"

// The pixel shader, after the renderer's HLSL prelude (DES_UI_HLSL_COMMON: PS_IN with the element's uv, 0 to 1) and
// DES_UI_HLSL_NOISE. b0 starts as RmlUi's vertex shader reads it (the transform and the translation). The preview ran
// the same maths in WebGL 2. One layer is five float4s:
//   A size, stretch x, stretch y, detail   B roughness, swirl, churn, curl   C drift right, drift up, coverage, softness
//   D strands, strand width, brightness, on   E top, fade, seed, -
#define DES_UI_HLSL_FOG R"(
cbuffer Fog : register(b0) { float4x4 m_transform; float2 m_translate; float m_time; float m_aspect; float4 m_p[10]; };

float fog_layer(float2 uv, float t, float4 A, float4 B, float4 C, float4 D, float4 E) {
    if (D.w < 0.5) return 0.0;
    float2 q = (uv - float2(C.x, -C.y) * t) * A.x / A.yz;
    float2 w = float2(0.0, 0.0);
    if (B.y > 0.0) {
        float zw = t * B.w + E.z * 1.7;
        w = float2(fbm(float3(q + float2(1.7, 9.2), zw), 3.0, 0.5), fbm(float3(q + float2(8.3, 2.8), zw + 3.1), 3.0, 0.5));
    }
    float n = fbm(float3(q + B.y * w, t * B.z + E.z), A.w, B.x);
    float lo = lerp(0.35, -0.35, C.z);
    float a = smoothstep(lo, lo + C.w * 0.6 + 0.01, n);
    if (D.x > 0.0) {
        // Strands: the long middle line of the noise, a thin core in a soft halo, shown in patches (the coverage).
        float d = abs(n);
        float core = 1.0 - smoothstep(0.0, D.y, d);
        float halo = 1.0 - smoothstep(0.0, D.y * 4.0, d);
        float env = smoothstep(lo - 0.15, lo + 0.25, fbm(float3(q * 0.5 + 3.7, t * B.z * 0.5 + E.z + 5.0), 2.0, 0.5));
        a = lerp(a, (core * core * 0.75 + halo * 0.35) * env, D.x);
    }
    return a * D.z * smoothstep(E.x, E.x + E.y + 0.001, uv.y);
}
float4 main(PS_IN i) : SV_Target {
    float2 uv = float2(i.uv.x * m_aspect, i.uv.y);
    float a1 = fog_layer(uv, m_time, m_p[0], m_p[1], m_p[2], m_p[3], m_p[4]);
    float a2 = fog_layer(uv, m_time, m_p[5], m_p[6], m_p[7], m_p[8], m_p[9]);
    float a = 1.0 - (1.0 - a1) * (1.0 - a2);
    a = saturate(a + hash33(floor(float3(i.position.xy, 7.0))).x * (0.5 / 255.0));  // a dither: no bands on black
    return float4(float3(0.894, 0.894, 0.886) * a, a) * i.color.a;
}
)"

namespace ui::fog {

// One layer of smoke, in the preview's terms.
struct Layer {
    float size;       // shapes across the screen's height
    float stretch;    // -1 tall to 1 wide
    float detail;     // octaves, 1 to 6
    float rough;      // each octave's share of the one before
    float swirl;      // how far the curl noise bends the shapes
    float churn;      // how fast the shapes change
    float curl;       // how fast the curl noise itself changes
    float drift[2];   // right and up, in 720p pixels a second
    float cover;      // the share of the screen with any smoke
    float soft;       // how soft its edges are
    float strands;    // 0 soft fog, up to 1 thin curling lines
    float width;      // the lines' width
    float bright;     // its brightness
    float top, fade;  // nothing above top (a share of the height), full strength below top + fade
    float seed;
};

struct Look {
    const char* name;
    float speed;  // the preview's speed: its clock runs this much faster
    int count;
    Layer layers[2];
};

// "start", the start screen's (tuned 2026-11-17): one layer of soft smoke rising and churning, over black.
inline constexpr Look kLooks[] = {
    {"start", 3.5f, 1, {{3.f, 0.f, 4.f, 0.5f, 1.f, 0.035f, 0.1f, {0.f, 15.f}, 0.79f, 0.9f, 0.f, 0.06f, 0.63f, 0.f, 0.f, 7.f}}},
};

// The look named by a shader decorator's value ("fog:start"), or null.
inline const Look* find(const char* value) {
    if (!value || std::strncmp(value, "fog:", 4) != 0) return nullptr;
    for (const Look& l : kLooks)
        if (std::strcmp(value + 4, l.name) == 0) return &l;
    return nullptr;
}

// The pixel shader's b0, after RmlUi's transform and translation.
struct Constants {
    float transform[16];
    float translate[2];
    float time;
    float aspect;
    float p[10][4];
};
static_assert(sizeof(Constants) == 240, "the HLSL cbuffer above");

// Fills all but the transform and the translation: seconds since the smoke began (0 holds it still), the element's
// width over its height.
inline void fill(const Look& look, double seconds, float aspect, Constants& c) {
    c.time = float(seconds * look.speed);
    c.aspect = aspect;
    std::memset(c.p, 0, sizeof c.p);
    for (int i = 0; i < look.count && i < 2; i++) {
        const Layer& l = look.layers[i];
        float(*p)[4] = c.p + i * 5;
        const float s = std::exp2(1.5f * std::fabs(l.stretch));  // (the preview's: 2 to the 1.5 x stretch)
        p[0][0] = l.size;
        p[0][1] = l.stretch > 0.f ? s : 1.f;
        p[0][2] = l.stretch < 0.f ? s : 1.f;
        p[0][3] = l.detail;
        p[1][0] = l.rough;
        p[1][1] = l.swirl;
        p[1][2] = l.churn;
        p[1][3] = l.curl;
        p[2][0] = l.drift[0] / 720.f;
        p[2][1] = l.drift[1] / 720.f;
        p[2][2] = l.cover;
        p[2][3] = l.soft;
        p[3][0] = l.strands;
        p[3][1] = l.width;
        p[3][2] = l.bright;
        p[3][3] = 1.f;
        p[4][0] = l.top;
        p[4][1] = l.fade;
        p[4][2] = l.seed * 13.37f;
    }
}

}  // namespace ui::fog
