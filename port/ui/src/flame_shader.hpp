// Written for Demon's Souls. A cold flame rising off the start screen's logo (DeS UI): RmlUi's "shader" decorator with
// the value "flame:<name>", drawn by render_gpu.cpp (the Windows menu renderer, D3D12 and Vulkan) as one pixel shader
// into a picture of the logo's element at half its size, padding box included, which is scaled up over it
// (render_effect). It reads one image, the logo's map (soulbrandt-logo-map.png, made by
// tools/ui/make_logo_map.py): the letters softened, and a wide halo round them that the flame rises from, carried up
// and bent by the smoke's noise (fog_shader.hpp's DES_UI_HLSL_NOISE). It gives light only; the letters, an image
// decorator over the content box, are drawn over it. The numbers in kLooks were picked on a preview page that ran the
// same maths in WebGL ("Soul flame").
//
// With DES_UI_NO_ANIM (the tests' pictures) the flame stands still at its first frame. render_d3d12.cpp (frozen, the
// Linux menu) and the D3D11 renderer draw no flame: there the logo shows plain.
#pragma once

#include <cstring>

#include "fog_shader.hpp"

// The pixel shader, after the renderer's HLSL prelude (DES_UI_HLSL_COMMON: PS_IN with the element's uv, 0 to 1; t0
// and s_clamp) and DES_UI_HLSL_NOISE. b0 starts as RmlUi's vertex shader reads it (the transform and the translation).
//   m_time   seconds times the look's speed (the flame's motion)    m_clock  seconds (the flicker)
//   m_color  the light's colour, its strength                       m_shape  reach, width, flicker, the room above
//   m_box    the map's box in dp at 1280 x 720
#define DES_UI_HLSL_FLAME R"(
cbuffer Flame : register(b0) {
    float4x4 m_transform; float2 m_translate; float m_time; float m_clock; float4 m_color; float4 m_shape; float4 m_box;
};

// The map: red the letters, softened; blue a wide halo round them. Nothing outside it.
float3 flame_map(float2 q) {
    if (any(q < 0.0) || any(q > 1.0)) return float3(0.0, 0.0, 0.0);
    return t0.SampleLevel(s_clamp, q, 0.0).rgb;
}
float4 main(PS_IN i) : SV_Target {
    // q: over the map's box, which is the element less the room at its top that the flame rises into.
    float2 q = float2(i.uv.x, (i.uv.y - m_shape.w) / (1.0 - m_shape.w));
    float t = m_time, S = m_color.a, R = m_shape.x, W = m_shape.y;
    // The flame's body: the halo seen from a little above, bent by noise, so it holds no copies of the letters.
    float n = fbm(float3(q.x * 9.0, q.y * 5.0 + t * 1.1, t * 0.25), 3.0, 0.5);
    float2 off = float2(n * 0.05 * (0.4 + W), 0.10 + 0.16 * R);
    float f = 0.0;
    [unroll] for (int k = 0; k < 8; k++) {
        float fk = (float(k) + 0.5) / 8.0;
        f += flame_map(q + off * fk).b * (1.0 - fk) * (1.0 - fk);
    }
    f /= 8.0;
    // Tongues: the body broken up by noise that rises faster; only above the letters, where the halo thins out.
    float tongues = smoothstep(-0.1, 0.45, fbm(float3(q.x * 18.0 + n * 3.0, q.y * 6.0 + t * 2.0, t * 0.4), 3.0, 0.5) + 0.25);
    float3 m = flame_map(q);
    float above = smoothstep(-0.05, 0.15, f - m.b * 0.35);
    float flick = lerp(1.0, 0.55 + 0.9 * (0.5 + 0.5 * gnoise(float3(q * m_box.xy * 0.035, m_clock * 2.3))), m_shape.z);
    float3 light = m_color.rgb * S * flick * (f * tongues * above * 9.0 + m.r * 0.45 + m.b * 0.2 * R);
    // Light only (alpha 0): added to what is behind it. (The dither that keeps the glow from banding on black is the
    // scaling up's, at the window's pixels.)
    return float4(max(light, 0.0), 0.0) * i.color.a;
}
)"

namespace ui::flame {

struct Look {
    const char* name;
    float color[3];  // the light's colour
    float strength;  // how bright, 0 to 2
    float reach;     // how tall the flame rises, 0 to 2
    float width;     // how much its tongues waver, 0 to 2
    float speed;     // how fast it moves (its clock runs this much faster)
    float flicker;   // 0 steady, 1 flickering
};

// "logo", the start screen's (picked 2026-11-17): a soul's pale blue light, tall and slow.
inline constexpr Look kLooks[] = {
    {"logo", {0.70f, 0.84f, 1.00f}, 0.59f, 2.f, 1.f, 0.4f, 0.3f},
};

// The map, and where it sits in the element. des.rcss pads the logo 160 dp above (the map's 60 and 100 more for the
// flame to rise into) and 60 dp on the other sides (the map's), at a 440 x 156 dp logo: the element is 560 x 376 dp,
// and the map's box its lower 560 x 276. A smaller logo scales all of it alike.
inline constexpr char kMap[] = "/res/img/soulbrandt-logo-map.png";
inline constexpr float kMapDp[2] = {560.f, 276.f};
inline constexpr float kTop = 100.f / 376.f;

// The look named by a shader decorator's value ("flame:logo"), or null.
inline const Look* find(const char* value) {
    if (!value || std::strncmp(value, "flame:", 6) != 0) return nullptr;
    for (const Look& l : kLooks)
        if (std::strcmp(value + 6, l.name) == 0) return &l;
    return nullptr;
}

// The pixel shader's b0, after RmlUi's transform and translation.
struct Constants {
    float transform[16];
    float translate[2];
    float time;
    float clock;
    float color[4];
    float shape[4];
    float box[4];
};
static_assert(sizeof(Constants) == 128, "the HLSL cbuffer above");

// Fills all but the transform and the translation: seconds since the flame began (0 holds it still).
inline void fill(const Look& look, double seconds, Constants& c) {
    c.time = float(seconds * look.speed);
    c.clock = float(seconds);
    c.color[0] = look.color[0];
    c.color[1] = look.color[1];
    c.color[2] = look.color[2];
    c.color[3] = look.strength;
    c.shape[0] = look.reach;
    c.shape[1] = look.width;
    c.shape[2] = look.flicker;
    c.shape[3] = kTop;
    c.box[0] = kMapDp[0];
    c.box[1] = kMapDp[1];
    c.box[2] = 0.f;
    c.box[3] = 0.f;
}

}  // namespace ui::flame
