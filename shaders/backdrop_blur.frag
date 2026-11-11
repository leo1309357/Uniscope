#version 330 core
out vec4 FragColor;

#include "lib/common.glsl"

uniform sampler2D u_texture;
uniform float u_framebuffer_width;
uniform float u_framebuffer_height;
uniform float u_radius_pixels;
uniform float u_tint_r;
uniform float u_tint_g;
uniform float u_tint_b;
uniform float u_alpha;
uniform float u_time;
uniform float u_effect_quality;

void main() {
    vec2 texel = 1.0 / max(vec2(u_framebuffer_width, u_framebuffer_height), vec2(1.0));
    vec2 uv = gl_FragCoord.xy * texel;
    float radius = max(u_radius_pixels, 0.0);
    float quality = clamp(u_effect_quality, 0.0, 1.0);

    if (quality <= 0.05 || radius <= 0.5) {
        vec3 base = texture(u_texture, uv).rgb;
        vec3 tintedBase = mix(base, vec3(u_tint_r, u_tint_g, u_tint_b), 0.26);
        FragColor = vec4(tintedBase, u_alpha);
        return;
    }

    // Mipmapped frosted glass: the capture texture carries a full pyramid
    // (built once per capture in captureBackbufferForBlur), so one trilinear
    // mip fetch replaces the old 9x9 tap convolution. The LOD tracks the
    // requested pixel radius and the quality tier; the bloom reads two mips
    // higher for its soft threshold.
    float lod = clamp(log2(1.0 + radius * mix(0.1, 0.6, quality)), 0.0, 4.0);
    vec3 accum = textureLod(u_texture, uv, lod).rgb;
    vec3 bloomSrc = textureLod(u_texture, uv, min(lod + 2.0, 6.0)).rgb;
    vec3 bloom = max(bloomSrc - vec3(0.58), vec3(0.0));

    float luminance = dot(accum, vec3(0.299, 0.587, 0.114));
    vec3 frosted = mix(accum, vec3(luminance), 0.18);
    frosted = frosted * 1.06 + bloom * (0.16 + 0.12 * quality) + vec3(0.018);
    frosted = toneMap(frosted, 0.86, 1.36, 0.92);

    vec2 panel = abs(fract(uv * 2.0) - 0.5);
    float satin = 1.0 - smoothstep(0.34, 0.52, length(panel));
    float grain = hash12(gl_FragCoord.xy + u_time * 41.0) - 0.5;
    vec3 tinted = mix(frosted, vec3(u_tint_r, u_tint_g, u_tint_b), 0.34);
    tinted += vec3(grain * 0.018 * quality + satin * 0.018 * quality);
    FragColor = vec4(tinted, u_alpha);
}
