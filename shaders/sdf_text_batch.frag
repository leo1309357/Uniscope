#version 330 core
in vec2 vTexCoord;
in vec4 vColor;
in vec2 vScreen;
out vec4 FragColor;

#include "lib/common.glsl"

uniform sampler2D u_texture;
uniform float u_sdfEdge;
uniform float u_sdfSmoothing;
uniform float u_time;
uniform float u_effect_quality;

void main() {
    float distance = texture(u_texture, vTexCoord).r;
    float smoothing = u_sdfSmoothing;
    float alpha = smoothstep(u_sdfEdge - smoothing, u_sdfEdge + smoothing, distance);
    float quality = clamp(u_effect_quality, 0.0, 1.0);
    float glow = smoothstep(u_sdfEdge - smoothing * 4.5, u_sdfEdge + smoothing * 2.0, distance);
    float inner = smoothstep(u_sdfEdge + smoothing * 0.6, u_sdfEdge + smoothing * 2.4, distance);
    float grain = hash12(gl_FragCoord.xy + u_time * 17.0) - 0.5;
    vec3 glowColor = mix(vColor.rgb, vec3(0.85, 0.96, 1.0), 0.22);
    vec3 color = mix(mix(vColor.rgb, glowColor * 0.72, quality), vColor.rgb, alpha);
    color = mix(color, color * 1.12, inner * 0.35 * quality);
    color += vec3(grain * 0.006 * quality);
    float outAlpha = max(alpha, glow * 0.20 * quality) * vColor.a;
    FragColor = vec4(color, outAlpha);
}
