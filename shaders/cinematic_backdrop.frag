#version 330 core
in vec2 vUv;
out vec4 FragColor;

#include "lib/common.glsl"

// The cinematic backdrop: the void a story animation opens in.
//
// It is NOT the menu background. The menu draws a perspective grid and a
// scan line, because a menu is a place with a floor; an opening is a place
// with no floor at all. Reusing the menu shader here would put the game's
// own front room behind the narration.
//
// What this draws instead:
//   - a cold vertical gradient (deep slate at the top, a slightly warmer
//     haze near the horizon line)
//   - two very slow drifting fog banks, so the field is never quite still
//   - a broad, weak glow behind the centre where the words sit
//   - a vignette that closes the frame
//   - very fine grain, so large flat areas do not band
//
// Everything is driven by u_time, so the motion is continuous and cheap: no
// textures, no particles, no extra draw calls.

uniform vec3 u_deep;
uniform vec3 u_haze;
uniform vec3 u_glow;
uniform float u_time;
uniform float u_aspect;
uniform float u_effect_quality;

void main() {
    float quality = clamp(u_effect_quality, 0.0, 1.0);
    vec2 p = vUv * 2.0 - 1.0;
    vec2 centered = vec2(p.x * u_aspect, p.y);

    // The base field: dark above, a touch of haze below the middle.
    float horizon = smoothstep(-0.95, 0.65, p.y);
    vec3 color = mix(u_haze, u_deep, horizon);

    // Two fog banks drifting sideways at different speeds. Value noise is
    // sampled in a stretched space, so the banks are wide and flat rather
    // than puffy.
    vec2 fogA = vec2(centered.x * 0.55 + u_time * 0.012, centered.y * 1.7 - u_time * 0.008);
    vec2 fogB = vec2(centered.x * 0.90 - u_time * 0.019, centered.y * 2.6 + u_time * 0.005);
    float bankA = valueNoise(fogA);
    float bankB = valueNoise(fogB);
    float banks = (bankA * 0.62 + bankB * 0.38);
    // Only the middle band of the screen carries the fog: the top and the
    // bottom stay clear so the letterbox bars read as bars.
    float band = 1.0 - smoothstep(0.55, 1.05, abs(p.y + 0.05));
    color += u_haze * (banks - 0.5) * 0.85 * band * quality;

    // A broad, weak glow behind the words: the words need something to sit
    // against, and a card on a flat field reads as a sticker.
    float halo = exp(-dot(centered * vec2(0.42, 0.62), centered * vec2(0.42, 0.62)) * 1.15);
    color += u_glow * halo * 0.16 * quality;

    // The vignette closes the frame.
    float vignette = 1.0 - smoothstep(0.25, 1.75, dot(centered, centered));
    color *= mix(0.42, 1.0, vignette);

    // Fine grain, so the gradient does not band on 8-bit displays.
    float grain = hash12(gl_FragCoord.xy + u_time * 13.0) - 0.5;
    color += vec3(grain * 0.010 * quality);

    FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
