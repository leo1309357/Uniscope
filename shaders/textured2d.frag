#version 330 core
in vec2 vTexCoord;
in vec2 vLocal;
in vec2 vScreen;
out vec4 FragColor;

#include "lib/common.glsl"

uniform sampler2D u_texture;
uniform float u_tint_r;
uniform float u_tint_g;
uniform float u_tint_b;
uniform float u_alpha;
uniform float u_time;
uniform float u_effect_quality;

// This sprite path uses a slightly softer highlight roll-off than the batched
// path, so it passes its own tone-map constants.
const float kToneShoulder = 0.80;
const float kToneGain = 1.34;
const float kToneGamma = 0.90;

vec3 enhanceSprite(vec3 color, vec2 uv, vec2 local) {
    float quality = clamp(u_effect_quality, 0.0, 1.0);
    if (quality <= 0.05) {
        return toneMap(color, kToneShoulder, kToneGain, kToneGamma);
    }

    vec2 texel = 1.0 / vec2(textureSize(u_texture, 0));
    vec3 left = texture(u_texture, uv - vec2(texel.x, 0.0)).rgb;
    vec3 right = texture(u_texture, uv + vec2(texel.x, 0.0)).rgb;
    vec3 down = texture(u_texture, uv - vec2(0.0, texel.y)).rgb;
    vec3 up = texture(u_texture, uv + vec2(0.0, texel.y)).rgb;
    float edge = length((right - left) + (up - down));
    float rim = smoothstep(0.74, 1.0, max(abs(local.x), abs(local.y)));
    float grain = hash12(floor(uv * 512.0) + u_time * 11.0) - 0.5;

    color = mix(color, color + edge * 0.18, 0.30 * quality);
    color *= mix(1.0, mix(1.05, 0.90, rim), quality);
    color += vec3(grain * 0.012 * quality);
    return toneMap(color, kToneShoulder, kToneGain, kToneGamma);
}

void main() {
    vec4 texelColor = texture(u_texture, vTexCoord);
    vec3 color = texelColor.rgb * vec3(u_tint_r, u_tint_g, u_tint_b);
    FragColor = vec4(enhanceSprite(color, vTexCoord, vLocal), texelColor.a * u_alpha);
}
