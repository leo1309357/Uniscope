#version 330 core
in vec2 vLocal;
in vec2 vScreen;
out vec4 FragColor;

#include "lib/common.glsl"

uniform float u_color_r;
uniform float u_color_g;
uniform float u_color_b;
uniform float u_alpha;
uniform float u_time;
uniform float u_effect_quality;

void main() {
    vec3 color = vec3(u_color_r, u_color_g, u_color_b);
    float edge = max(abs(vLocal.x), abs(vLocal.y));
    float bevel = smoothstep(0.58, 1.0, edge);
    float topLight = smoothstep(-1.0, 0.72, vLocal.y);
    float quality = clamp(u_effect_quality, 0.0, 1.0);
    float grain = hash12(gl_FragCoord.xy + u_time * 37.0) - 0.5;

    color *= mix(1.0, mix(0.92, 1.08, topLight), quality);
    color = mix(color, color * 0.72, bevel * 0.30 * quality);
    color += vec3(grain * 0.010 * quality);
    FragColor = vec4(toneMap(color, 0.88, 1.42, 0.92), u_alpha);
}
