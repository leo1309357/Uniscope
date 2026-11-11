#version 330 core
in vec4 vColor;
in vec2 vScreen;
out vec4 FragColor;

#include "lib/common.glsl"

uniform float u_time;
uniform float u_effect_quality;

void main() {
    float quality = clamp(u_effect_quality, 0.0, 1.0);
    float screenDepth = smoothstep(-1.0, 1.0, vScreen.y);
    float grain = hash12(gl_FragCoord.xy + u_time * 29.0) - 0.5;
    vec3 color = vColor.rgb * mix(1.0, mix(0.96, 1.035, screenDepth), quality);
    color += vec3(grain * 0.008 * quality);
    FragColor = vec4(toneMap(color, 0.92, 1.40, 0.94), vColor.a);
}
